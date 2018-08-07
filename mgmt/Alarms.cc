/** @file

  Function defs for the Alarms keeper.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "tscore/ink_platform.h"
#include "tscore/ink_string.h"
#include "tscore/ink_file.h"
#include "tscore/ink_time.h"
#include "MgmtUtils.h"
#include "Alarms.h"

#include "records/P_RecCore.h"

const char *alarmText[] = {
  "Unknown Alarm",
  "[TrafficManager] Traffic Server process was reset.",
  "[TrafficManager] Traffic Server process established.",
  "New Peer",
  "Peer Died",
  "Invalid Configuration",
  "System Error",
  "Log Space Crisis",
  "Cache Error",
  "Cache Warning",
  "Logging Error",
  "Logging Warning",
  "Mgmt Debugging Alarm",
  "Configuration File Update Failed",
  "Unable to Establish Manager User-Interface Services",
  "Ping Failure",
  "",
  "Add OEM Alarm",
  "",
};

const int alarmTextNum = sizeof(alarmText) / sizeof(char *);

// Return the alarm script directory. Use proxy.config.alarm.abs_path if it is
// set, falling back to proxy.config.bin_path otherwise.
static char *
alarm_script_dir()
{
  char *path;

  path = REC_readString("proxy.config.alarm.abs_path", nullptr);
  if (path && *path) {
    return path;
  }

  return ats_stringdup(RecConfigReadBinDir());
}

Alarms::Alarms()
{
  cur_cb = 0;
  ink_mutex_init(&mutex);
  alarmOEMcount = minOEMkey;
} /* End Alarms::Alarms */

Alarms::~Alarms()
{
  for (auto &&it : local_alarms) {
    ats_free(it.second);
  }
  for (auto &&it : remote_alarms) {
    ats_free(it.second);
  }
  ink_mutex_destroy(&mutex);
} /* End Alarms::Alarms */

void
Alarms::registerCallback(AlarmCallbackFunc func)
{
  char cb_buf[80];

  ink_mutex_acquire(&mutex);
  snprintf(cb_buf, sizeof(cb_buf), "%d", cur_cb++);
  Debug("alarm", "[Alarms::registerCallback] Registering Alarms callback");
  cblist.emplace(cb_buf, func);
  ink_mutex_release(&mutex);
} /* End Alarms::registerCallback */

bool
Alarms::isCurrentAlarm(alarm_t a, char *ip)
{
  bool ret = false;
  char buf[80];

  ink_mutex_acquire(&mutex);
  if (!ip) {
    snprintf(buf, sizeof(buf), "%d", a);
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
  }

  if (!ip && local_alarms.find(buf) != local_alarms.end()) {
    ret = true;
  } else if (ip && remote_alarms.find(buf) != remote_alarms.end()) {
    ret = true;
  }
  ink_mutex_release(&mutex);
  return ret;
} /* End Alarms::isCurrentAlarm */

void
Alarms::resolveAlarm(alarm_t a, char *ip)
{
  char buf[80];

  ink_mutex_acquire(&mutex);
  if (!ip) {
    snprintf(buf, sizeof(buf), "%d", a);
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
  }

  if (!ip && local_alarms.find(buf) != local_alarms.end()) {
    Alarm *hash_value = local_alarms[buf];
    local_alarms.erase(buf);
    ats_free(hash_value->description);
    ats_free(hash_value);
  } else if (ip && remote_alarms.find(buf) != remote_alarms.end()) {
    Alarm *hash_value = remote_alarms[buf];
    remote_alarms.erase(buf);
    ats_free(hash_value->description);
    ats_free(hash_value);
  }
  ink_mutex_release(&mutex);

  return;
} /* End Alarms::resolveAlarm */

void
Alarms::signalAlarm(alarm_t a, const char *desc, const char *ip)
{
  static time_t last_sent           = 0;
  static char prev_alarm_text[2048] = "";

  int priority;
  char buf[80];
  Alarm *atmp;

  /* Assign correct priorities */
  switch (a) {
  case MGMT_ALARM_PROXY_CACHE_ERROR:
    priority = 1; // INKqa07595
    break;
  case MGMT_ALARM_PROXY_CACHE_WARNING:
    return;
  case MGMT_ALARM_PROXY_PEER_BORN:
    priority = 3;
    break;
  case MGMT_ALARM_PROXY_PEER_DIED:
    priority = 3;
    break;
  case MGMT_ALARM_PING_FAILURE:
    priority = 1;
    break;
  case MGMT_ALARM_PROXY_PROCESS_DIED:
    priority = 1;
    break;
  case MGMT_ALARM_PROXY_PROCESS_BORN:
    mgmt_log("[Alarms::signalAlarm] Server Process born\n");
    return;
  case MGMT_ALARM_ADD_ALARM:
    priority = 2;
    break;
  default:
    priority = 2;
    break;
  }

  /* Quick hack to buffer repeat alarms and only send every 15 min */
  if (desc && (priority == 1 || priority == 2) && !ip) {
    if (strcmp(prev_alarm_text, desc) == 0) { /* a repeated alarm */
      time_t time_delta = time(nullptr) - last_sent;
      if (time_delta < 900) {
        mgmt_log("[Alarms::signalAlarm] Skipping Alarm: '%s'\n", desc);
        return;
      } else {
        last_sent = time(nullptr);
      }
    } else {
      ink_strlcpy(prev_alarm_text, desc, sizeof(prev_alarm_text));
      last_sent = time(nullptr);
    }
  }

  Debug("alarm", "[Alarms::signalAlarm] Sending Alarm: '%s'", desc);

  if (!desc) {
    desc = (char *)getAlarmText(a);
  }

  /*
   * Exec alarm bin for priority alarms every time, regardless if they are
   * potentially duplicates. However, only exec this for you own alarms,
   * don't want every node in the cluster reporting the same alarm.
   */
  if (priority == 1 && !ip) {
    execAlarmBin(desc);
  }

  ink_mutex_acquire(&mutex);
  if (!ip) {
    // if an OEM alarm, then must create the unique key alarm type;
    // this key is used to hash the new OEM alarm description in the hash table
    if (a == MGMT_ALARM_ADD_ALARM) {
      a = (alarmOEMcount - minOEMkey) % (maxOEMkey - minOEMkey) + minOEMkey;
      alarmOEMcount++;
    }
    snprintf(buf, sizeof(buf), "%d", a);
    if (local_alarms.find(buf) != local_alarms.end()) {
      ink_mutex_release(&mutex);
      return;
    }
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
    if (auto it = remote_alarms.find(buf); it != remote_alarms.end()) {
      // Reset the seen flag so that we know the remote alarm is
      //   still active
      atmp       = it->second;
      atmp->seen = true;
      ink_mutex_release(&mutex);
      return;
    }
  }

  atmp              = (Alarm *)ats_malloc(sizeof(Alarm));
  atmp->type        = a;
  atmp->linger      = true;
  atmp->seen        = true;
  atmp->priority    = priority;
  atmp->description = nullptr;

  if (!ip) {
    atmp->local        = true;
    atmp->inet_address = 0;
    local_alarms.emplace(buf, atmp);
  } else {
    atmp->local        = false;
    atmp->inet_address = inet_addr(ip);
    local_alarms.emplace(buf, atmp);
  }

  // Swap desc with time-stamped description.  Kinda hackish
  // Temporary until we get a new
  // alarm system in place.  TS 5.0.0, 02/08/2001
  time_t my_time_t;
  char my_ctime_str[32];
  time(&my_time_t);
  ink_ctime_r(&my_time_t, my_ctime_str);
  char *p = my_ctime_str;
  while (*p != '\n' && *p != '\0') {
    p++;
  }
  if (*p == '\n') {
    *p = '\0';
  }
  const size_t sz = sizeof(char) * (strlen(desc) + strlen(my_ctime_str) + 4);
  ats_free(atmp->description);
  atmp->description = (char *)ats_malloc(sz);
  snprintf(atmp->description, sz, "[%s] %s", my_ctime_str, desc);

  ink_mutex_release(&mutex);

  for (auto &&it : cblist) {
    AlarmCallbackFunc func = it.second;
    Debug("alarm", "[Alarms::signalAlarm] invoke callback for %d", a);
    (*(func))(a, ip, desc);
  }

  /* Priority 2 alarms get signaled if they are the first unsolved occurrence. */
  if (priority == 2 && !ip) {
    execAlarmBin(desc);
  }

} /* End Alarms::signalAlarm */

/*
 * resetSeenFlag(...)
 *   Function resets the "seen" flag for a given peer's alarms. This allows
 * us to flush alarms that may have expired naturally or were dealt.
 */
void
Alarms::resetSeenFlag(char *ip)
{
  ink_mutex_acquire(&mutex);
  for (auto &&it : remote_alarms) {
    std::string const &key = it.first;
    Alarm *tmp             = it.second;
    if (key.find(ip) != std::string::npos) {
      tmp->seen = false;
    }
  }
  ink_mutex_release(&mutex);
  return;
} /* End Alarms::resetSeenFlag */

/*
 * clearUnSeen(...)
 *   This function is a sweeper function to clean up those alarms that have
 * been taken care of through other local managers or at the peer itself.
 */
void
Alarms::clearUnSeen(char *ip)
{
  ink_mutex_acquire(&mutex);
  for (auto &&it : remote_alarms) {
    std::string const &key = it.first;
    Alarm *tmp             = it.second;

    if (key.find(ip) != std::string::npos) { /* Make sure alarm is for correct ip */
      if (!tmp->seen) {                      /* Make sure we did not see it in peer's report */
        remote_alarms.erase(key);
        ats_free(tmp->description);
        ats_free(tmp);
      }
    }
  }
  ink_mutex_release(&mutex);
  return;
} /* End Alarms::clearUnSeen */

/*
 * checkSystemNAlert(...)
 *   This function should test the system and signal local alarms. Sending
 * out remote notification commands if necessary.
 */
void
Alarms::checkSystemNAlert()
{
  return;
} /* End Alarms::checkSystemNAlert */

void
Alarms::execAlarmBin(const char *desc)
{
  ats_scoped_str bindir(alarm_script_dir());
  char cmd_line[MAXPATHLEN];

  ats_scoped_str alarm_bin(REC_readString("proxy.config.alarm.bin", nullptr));
  ats_scoped_str alarm_email_from_name;
  ats_scoped_str alarm_email_from_addr;
  ats_scoped_str alarm_email_to_addr;

  pid_t pid;

  // If there's no alarm script configured, don't even bother.
  if (!alarm_bin || *alarm_bin == '\0') {
    return;
  }

  // get email info
  alarm_email_from_name = REC_readString("proxy.config.product_name", nullptr);
  alarm_email_from_addr = REC_readString("proxy.config.admin.admin_user", nullptr);
  alarm_email_to_addr   = REC_readString("proxy.config.alarm_email", nullptr);

  ink_filepath_make(cmd_line, sizeof(cmd_line), bindir, alarm_bin);

#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(errno, "[Alarms::execAlarmBin] Unable to fork1 process\n");
  } else if (pid > 0) { /* Parent */
    int status;
    bool script_done = false;
    time_t timeout   = (time_t)REC_readInteger("proxy.config.alarm.script_runtime", nullptr);
    if (!timeout) {
      timeout = 5; // default time = 5 secs
    }
    time_t time_delta = 0;
    time_t first_time = time(nullptr);
    while (time_delta <= timeout) {
      // waitpid will return child's pid if status is available
      // or -1 if there is some problem; returns 0 if child status
      // is not available
      if (waitpid(pid, &status, WNOHANG) != 0) {
        Debug("alarm", "[Alarms::execAlarmBin] child pid %" PRId64 " has status", (int64_t)pid);
        script_done = true;
        break;
      }
      time_delta = time(nullptr) - first_time;
    }
    // need to kill the child script process if it's not complete
    if (!script_done) {
      Debug("alarm", "[Alarms::execAlarmBin] kill child pid %" PRId64 "", (int64_t)pid);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // to reap the thread
    }
  } else {
    int res;
    if (alarm_email_from_name && alarm_email_from_addr && alarm_email_to_addr) {
      res = execl(cmd_line, (const char *)alarm_bin, desc, (const char *)alarm_email_from_name, (const char *)alarm_email_from_addr,
                  (const char *)alarm_email_to_addr, (char *)nullptr);
    } else {
      res = execl(cmd_line, (const char *)alarm_bin, desc, (char *)nullptr);
    }
    _exit(res);
  }
}

//
// getAlarmText
//
// returns the corresponding text for the alarm id
//
const char *
Alarms::getAlarmText(alarm_t id)
{
  if (id < alarmTextNum) {
    return alarmText[id];
  } else {
    return alarmText[0]; // "Unknown Alarm";
  }
}
