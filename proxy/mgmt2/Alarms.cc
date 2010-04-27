/** @file

  A brief file description

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

/*
 *
 * Alarms.cc
 *   Function defs for the Alarms keeper.
 *
 * $Date: 2007-10-05 16:56:44 $
 *
 * 
 */

#include "inktomi++.h"
#include "Main.h"
#include "Alarms.h"
#include "Diags.h"

#if defined(MGMT_API)
#include "TSControlMain.h"
#include "Message.h"
#include "Defs.h"
#endif

#include "P_RecCore.h"

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
  "NNTP Error",
  "Mgmt Debugging Alarm",
  "Configuration File Update Failed",
  "Unable to Establish Manager User-Interface Services",
  "Ping Failure",
  "",
  "Add OEM Alarm",
  "",
  "HTTP Origin Server is Congested",
  "Congested HTTP Origin Server is now Alleviated",
  "",                           /* congested server */
  ""                            /* alleviated server */
};

const int alarmTextNum = sizeof(alarmText) / sizeof(char *);

Alarms::Alarms()
{
  bool found;

  cur_cb = 0;
  cblist = ink_hash_table_create(InkHashTableKeyType_String);
  local_alarms = ink_hash_table_create(InkHashTableKeyType_String);
  remote_alarms = ink_hash_table_create(InkHashTableKeyType_String);
  ink_mutex_init(&mutex, "alarms-mutex");
  alarm_bin = REC_readString("proxy.config.alarm.bin", &found);
  ink_assert(found);
  alarm_bin_path = REC_readString("proxy.config.alarm.abs_path", &found);
  ink_assert(found);
  if (!alarm_bin_path) {
    alarm_bin_path = REC_readString("proxy.config.bin_path", &found);
    ink_assert(found);
  }
  alarmOEMcount = minOEMkey;

  return;
}                               /* End Alarms::Alarms */


Alarms::~Alarms()
{
  ink_hash_table_destroy(cblist);
  ink_hash_table_destroy_and_xfree_values(local_alarms);
  ink_hash_table_destroy_and_xfree_values(remote_alarms);
  ink_mutex_destroy(&mutex);
  return;
}                               /* End Alarms::Alarms */


void
Alarms::registerCallback(AlarmCallbackFunc func)
{
  char cb_buf[80];

  ink_mutex_acquire(&mutex);
  snprintf(cb_buf, sizeof(cb_buf), "%d", cur_cb++);
  Debug("alarm", "[Alarms::registerCallback] Registering Alarms callback\n");
  ink_hash_table_insert(cblist, cb_buf, (void *) func);
  ink_mutex_release(&mutex);
  return;
}                               /* End Alarms::registerCallback */


bool
Alarms::isCurrentAlarm(alarm_t a, char *ip)
{
  bool ret = false;
  char buf[80];
  InkHashTableValue hash_value;

  ink_mutex_acquire(&mutex);
  if (!ip) {
    snprintf(buf, sizeof(buf), "%d", a);
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
  }

  if (!ip && ink_hash_table_lookup(local_alarms, buf, &hash_value) != 0) {
    ret = true;
  } else if (ip && ink_hash_table_lookup(remote_alarms, buf, &hash_value) != 0) {
    ret = true;
  }
  ink_mutex_release(&mutex);
  return ret;
}                               /* End Alarms::isCurrentAlarm */


void
Alarms::resolveAlarm(alarm_t a, char *ip)
{
  char buf[80];
  InkHashTableValue hash_value;

  ink_mutex_acquire(&mutex);
  if (!ip) {
    snprintf(buf, sizeof(buf), "%d", a);
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
  }

  if (!ip && ink_hash_table_lookup(local_alarms, buf, &hash_value) != 0) {
    ink_hash_table_delete(local_alarms, buf);
    if (((Alarm *) hash_value)->description) {
      xfree(((Alarm *) hash_value)->description);
    }
    xfree(hash_value);
  } else if (ip && ink_hash_table_lookup(remote_alarms, buf, &hash_value) != 0) {
    char buf2[1024];

    snprintf(buf2, sizeof(buf2), "aresolv: %d\n", a);
    if (!lmgmt->ccom->sendReliableMessage(inet_addr(ip), buf2, strlen(buf2))) {
      ink_mutex_release(&mutex);
      return;
    }
    ink_hash_table_delete(remote_alarms, buf);
    xfree(hash_value);
  }
  ink_mutex_release(&mutex);

  return;
}                               /* End Alarms::resolveAlarm */


void
Alarms::signalAlarm(alarm_t a, const char *desc, const char *ip)
{
  static time_t last_sent = 0;
  static char prev_alarm_text[2048] = "";

  int priority;
  char buf[80];
  Alarm *atmp;
  InkHashTableValue hash_value;
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  /* Assign correct priorities */
  switch (a) {
  case MGMT_ALARM_PROXY_CACHE_ERROR:
    priority = 1;               // INKqa07595
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
    mgmt_log(stderr, "[Alarms::signalAlarm] Server Process born\n");
    priority = 2;
#ifdef USE_SNMP
    snmpAlarmCallback(a, NULL, desc);
#endif
    return;
  case MGMT_ALARM_ADD_ALARM:
    priority = 2;
    break;
  case MGMT_ALARM_PROXY_HTTP_CONGESTED_SERVER:
  case MGMT_ALARM_PROXY_HTTP_ALLEVIATED_SERVER:
#ifdef USE_SNMP
    snmpAlarmCallback(a, NULL, desc);
#endif // USE_SNMP
    return;
  case MGMT_ALARM_WDA_BILLING_CONNECTION_DIED:
  case MGMT_ALARM_WDA_BILLING_CORRUPTED_DATA:
  case MGMT_ALARM_WDA_XF_ENGINE_DOWN:
  case MGMT_ALARM_WDA_RADIUS_CORRUPTED_PACKETS:
    priority = 2;
    break;
  default:
    priority = 2;
    break;
  }

  /* Quick hack to buffer repeat alarms and only send every 15 min */
  if (desc && (priority == 1 || priority == 2) && !ip) {

    if (strcmp(prev_alarm_text, desc) == 0) {   /* a repeated alarm */

      /* INKqa11884: repeated wireless alarms always signalled */
      if (a != MGMT_ALARM_WDA_BILLING_CONNECTION_DIED &&
          a != MGMT_ALARM_WDA_BILLING_CORRUPTED_DATA &&
          a != MGMT_ALARM_WDA_XF_ENGINE_DOWN && a != MGMT_ALARM_WDA_RADIUS_CORRUPTED_PACKETS) {

        time_t time_delta = time(0) - last_sent;
        if (time_delta < 900) {
          mgmt_log("[Alarms::signalAlarm] Skipping Alarm: '%s'\n", desc);
          return;
        } else {
          last_sent = time(0);
        }
      }
    } else {
      ink_strncpy(prev_alarm_text, desc, sizeof(prev_alarm_text));
      last_sent = time(0);
    }
  }

  Debug("alarm", "[Alarms::signalAlarm] Sending Alarm: '%s'", desc);

  if (!desc)
    desc = (char *) getAlarmText(a);

  /*
   * Exec alarm bin for priority alarms everytime, regardless if they are
   * potentially duplicates. However, only exec this for you own alarms, 
   * don't want every node in the cluster reporting the same alarm.
   */
  if (priority == 1 && alarm_bin && alarm_bin_path && !ip) {
    execAlarmBin(desc);
  }



  ink_mutex_acquire(&mutex);
  if (!ip) {
    // if an OEM alarm, then must create the unique key alarm type;
    // this key is used to hash the new OEM alarm descritption in the hash table
    if (a == MGMT_ALARM_ADD_ALARM) {
      a = (alarmOEMcount - minOEMkey) % (maxOEMkey - minOEMkey) + minOEMkey;
      alarmOEMcount++;
    }
    snprintf(buf, sizeof(buf), "%d", a);
    if (ink_hash_table_lookup(local_alarms, buf, &hash_value) != 0) {
      // INKqa11884: if wireless alarm already active, just 
      // update desc with new timestamp and skip to actions part
      if (a == MGMT_ALARM_WDA_BILLING_CONNECTION_DIED ||
          a == MGMT_ALARM_WDA_BILLING_CORRUPTED_DATA ||
          a == MGMT_ALARM_WDA_XF_ENGINE_DOWN || a == MGMT_ALARM_WDA_RADIUS_CORRUPTED_PACKETS) {
        Debug("alarm", "[signalAlarm] wireless alarm already active");
        atmp = (Alarm *) hash_value;
        goto ALARM_REPEAT;
      } else {
        ink_mutex_release(&mutex);
        return;
      }
    }
  } else {
    snprintf(buf, sizeof(buf), "%d-%s", a, ip);
    if (ink_hash_table_lookup(remote_alarms, buf, &hash_value) != 0) {
      // Reset the seen flag so that we know the remote alarm is
      //   still active
      atmp = (Alarm *) hash_value;
      atmp->seen = true;

      // INKqa11884: if wireless alarm already active, just 
      // update desc with new timstamp and skip to actions part
      if (a == MGMT_ALARM_WDA_BILLING_CONNECTION_DIED ||
          a == MGMT_ALARM_WDA_BILLING_CORRUPTED_DATA ||
          a == MGMT_ALARM_WDA_XF_ENGINE_DOWN || a == MGMT_ALARM_WDA_RADIUS_CORRUPTED_PACKETS) {
        Debug("alarm", "[Alarms::signalAlarm] wireless alarm already active");
        goto ALARM_REPEAT;
      } else {
        ink_mutex_release(&mutex);
        return;
      }
    }
  }

  ink_assert((atmp = (Alarm *) xmalloc(sizeof(Alarm))));
  atmp->type = a;
  atmp->linger = true;
  atmp->seen = true;
  atmp->priority = priority;
  atmp->description = NULL;

  if (!ip) {
    atmp->local = true;
    atmp->inet_address = 0;
    ink_hash_table_insert(local_alarms, (InkHashTableKey) (buf), (atmp));
  } else {
    atmp->local = false;
    atmp->inet_address = inet_addr(ip);
    ink_hash_table_insert(remote_alarms, (InkHashTableKey) (buf), (atmp));
  }

ALARM_REPEAT:
  // Swap desc with time-stamped description.  Kinda hackish
  // Temporary until we get a new
  // alarm system in place.  TS 5.0.0, 02/08/2001

  time_t my_time_t;
  char my_ctime_str[32];
  time(&my_time_t);
  ink_ctime_r(&my_time_t, my_ctime_str);
  char *p = my_ctime_str;
  while (*p != '\n' && *p != '\0')
    p++;
  if (*p == '\n')
    *p = '\0';
  char *new_desc;
  const size_t new_desc_size = sizeof(char) * (strlen(desc) + strlen(my_ctime_str) + 4);
  ink_assert(new_desc = (char *) alloca(new_desc_size));
  snprintf(new_desc, new_desc_size, "[%s] %s", my_ctime_str, desc);
  desc = new_desc;
  if (atmp->description)
    xfree(atmp->description);
  const size_t atmp_desc_size = sizeof(char) * (strlen(desc) + 1);
  ink_assert(atmp->description = (char *) xmalloc(atmp_desc_size));
  ink_strncpy(atmp->description, desc, atmp_desc_size);

  ink_mutex_release(&mutex);

#if defined(MGMT_API)
  if (mgmt_alarm_event_q) {
    // ADDED CODE here is where we Add to the queue of alarms one more
    EventNoticeForm *new_alarm;

    new_alarm = (EventNoticeForm *) xmalloc(sizeof(EventNoticeForm));
    if (!new_alarm) {
      Debug("alarm", "can't xmalloc so can't create new alarm struct.\n");
      return;
    }
    // allocated space copy over values
    // remember AlarmID start from 0 exactly 1 off but everything else
    // matches
    new_alarm->alarm_t = (AlarmID) (atmp->type - 1);
    new_alarm->priority = atmp->priority;
    new_alarm->linger = atmp->linger;
    new_alarm->local = atmp->local;
    new_alarm->seen = atmp->seen;
    if (!atmp->local)
      new_alarm->inet_address = atmp->inet_address;
    if (!atmp->description)
      new_alarm->description = NULL;
    else {
      new_alarm->description = (char *) xmalloc(sizeof(char) * (strlen(atmp->description) + 1));
      if (!new_alarm->description)
        new_alarm->description = NULL;  // rather have alarm without description than drop it completely
      else
        strcpy(new_alarm->description, atmp->description);
    }

    // new alarm is complete now add it
    ink_mutex_acquire(&mgmt_alarm_event_q->mgmt_alarm_lock);

    // enqueue
    enqueue(mgmt_alarm_event_q->mgmt_alarm_q, new_alarm);

    ink_mutex_release(&mgmt_alarm_event_q->mgmt_alarm_lock);
  }
#endif

  for (entry = ink_hash_table_iterator_first(cblist, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(cblist, &iterator_state)) {
    char *tmp, *tmp2;
    AlarmCallbackFunc func = (AlarmCallbackFunc) ink_hash_table_entry_value(remote_alarms, entry);
    if (ip) {
      const size_t tmp_size = sizeof(char) * (strlen(ip) + 1);
      ink_assert((tmp = (char *) xmalloc(tmp_size)));
      ink_strncpy(tmp, ip, tmp_size);
    } else {
      tmp = NULL;
    }

    if (desc) {
      const size_t tmp2_size = sizeof(char) * (strlen(desc) + 1);
      ink_assert((tmp2 = (char *) xmalloc(tmp2_size)));
      ink_strncpy(tmp2, desc, tmp2_size);
    } else {
      tmp2 = NULL;
    }
    Debug("alarm", "[Alarms::signalAlarm] invoke callback for %d", a);
    (*(func)) (a, tmp, tmp2);
  }
  /* Priority 2 alarms get signalled if they are the first unsolved occurence. */
  if (priority == 2 && alarm_bin && alarm_bin_path && !ip) {
    execAlarmBin(desc);
  }

  return;
}                               /* End Alarms::signalAlarm */


/*
 * resetSeenFlag(...)
 *   Function resets the "seen" flag for a given peer's alarms. This allows
 * us to flush alarms that may have expired naturally or were dealt.
 */
void
Alarms::resetSeenFlag(char *ip)
{
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  ink_mutex_acquire(&mutex);
  for (entry = ink_hash_table_iterator_first(remote_alarms, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(remote_alarms, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(remote_alarms, entry);
    Alarm *tmp = (Alarm *) ink_hash_table_entry_value(remote_alarms, entry);

    if (strstr(key, ip)) {
      tmp->seen = false;
    }
  }
  ink_mutex_release(&mutex);
  return;
}                               /* End Alarms::resetSeenFlag */


/*
 * clearUnSeen(...)
 *   This function is a sweeper functionto clean up those alarms that have
 * been taken care of through otehr local managers or at the peer itself.
 */
void
Alarms::clearUnSeen(char *ip)
{
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  ink_mutex_acquire(&mutex);
  for (entry = ink_hash_table_iterator_first(remote_alarms, &iterator_state);
       entry != NULL; entry = ink_hash_table_iterator_next(remote_alarms, &iterator_state)) {

    char *key = (char *) ink_hash_table_entry_key(remote_alarms, entry);
    Alarm *tmp = (Alarm *) ink_hash_table_entry_value(remote_alarms, entry);

    if (strstr(key, ip)) {      /* Make sure alarm is for correct ip */
      if (!tmp->seen) {         /* Make sure we did not see it in peer's report */
        ink_hash_table_delete(remote_alarms, key);      /* Safe in iterator? */
        xfree(tmp->description);
        xfree(tmp);
      }
    }
  }
  ink_mutex_release(&mutex);
  return;
}                               /* End Alarms::clearUnSeen */


/*
 * constructAlarmMessage(...)
 *   This functions builds a message buffer for passing to peers. It basically
 * takes the current list of local alarms and builds an alarm message.
 */
void
Alarms::constructAlarmMessage(char *ip, char *message, int max)
{
  int n = 0, bsum = 0;
  char buf[4096];
  InkHashTableEntry *entry;
  InkHashTableIteratorState iterator_state;

  if (!ip) {
    return;
  }
  // Insert the standard mcast packet header
  n = ClusterCom::constructSharedPacketHeader(message, ip, max);

  ink_mutex_acquire(&mutex);
  if (!((n + (int) strlen("type: alarm\n")) < max)) {
    if (max >= 1) {
      message[0] = '\0';
    }
    return;
  }

  ink_strncpy(&message[n], "type: alarm\n", max - n);
  n += strlen("type: alarm\n");
  bsum = n;
  for (entry = ink_hash_table_iterator_first(local_alarms, &iterator_state);
       (entry != NULL && n < max); entry = ink_hash_table_iterator_next(local_alarms, &iterator_state)) {

    Alarm *tmp = (Alarm *) ink_hash_table_entry_value(remote_alarms, entry);

    if (tmp->description) {
      snprintf(buf, sizeof(buf), "alarm: %d %s\n", tmp->type, tmp->description);
    } else {
      snprintf(buf, sizeof(buf), "alarm: %d No details available\n", tmp->type);
    }

    if (!((n + (int) strlen(buf)) < max)) {
      break;
    }
    ink_strncpy(&message[n], buf, max - n);
    n += strlen(buf);
  }

  if (n == bsum) {              /* No alarms */
    if (!((n + (int) strlen("alarm: none\n")) < max)) {
      if (max >= 1) {
        message[0] = '\0';
      }
      return;
    }
    ink_strncpy(&message[n], "alarm: none\n", max - n);
    n += strlen("alarm: none\n");
  }
  ink_mutex_release(&mutex);
  return;
}                               /* End Alarms::constructAlarmMessage */


/*
 * checkSystemNAlert(...)
 *   This function should test the system and signal local alarms. Sending
 * out remote notification commands if necessary.
 */
void
Alarms::checkSystemNAlert()
{
  return;
}                               /* End Alarms::checkSystenNAlert */

void
Alarms::execAlarmBin(const char *desc)
{
  char cmd_line[1024];
  char *alarm_email_from_name = 0;
  char *alarm_email_from_addr = 0;
  char *alarm_email_to_addr = 0;
  bool found;

  // get email info
  alarm_email_from_name = REC_readString("proxy.config.product_name", &found);
  if (!found)
    alarm_email_from_name = 0;
  alarm_email_from_addr = REC_readString("proxy.config.admin.admin_user", &found);
  if (!found)
    alarm_email_from_addr = 0;
  alarm_email_to_addr = REC_readString("proxy.config.alarm_email", &found);
  if (!found)
    alarm_email_to_addr = 0;

#ifndef _WIN32

  int status;
  pid_t pid;

  snprintf(cmd_line, sizeof(cmd_line), "%s%s%s", alarm_bin_path, DIR_SEP, alarm_bin);

#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(stderr, "[Alarms::execAlarmBin] Unable to fork1 process\n");
  } else if (pid > 0) {         /* Parent */
    // INKqa11769
    bool script_done = false;
    time_t timeout = (time_t) REC_readInteger("proxy.config.alarm.script_runtime", &found);
    if (!found)
      timeout = 5;              // default time = 5 secs
    time_t time_delta = 0;
    time_t first_time = time(0);
    while (time_delta <= timeout) {
      // waitpid will return child's pid if status is available
      // or -1 if there is some problem; returns 0 if child status
      // is not available
      if (waitpid(pid, &status, WNOHANG) != 0) {
        Debug("alarm", "[Alarms::execAlarmBin] child pid %d has status", pid);
        script_done = true;
        break;
      }
      time_delta = time(0) - first_time;
    }
    // need to kill the child script process if it's not complete
    if (!script_done) {
      Debug("alarm", "[Alarms::execAlarmBin] kill child pid %d", pid);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0); // to reap the thread
    }
  } else {
    int res;
    if (alarm_email_from_name && alarm_email_from_addr && alarm_email_to_addr) {
      res = execl(cmd_line, alarm_bin, desc, alarm_email_from_name, alarm_email_from_addr, alarm_email_to_addr, (char*)NULL);
    } else {
      res = execl(cmd_line, alarm_bin, desc, (char*)NULL);
    }
    _exit(res);
  }

#else

  bool is_exe = true;
  char *fileExt = NULL;

  if ((fileExt = strchr(alarm_bin, '.')) != NULL) {
    if (ink_strcasecmp(fileExt, ".CMD") == 0 || ink_strcasecmp(fileExt, ".BAT") == 0) {
      is_exe = false;
    }
  }

  if (is_exe) {
    sprintf(cmd_line, "%s%s%s", alarm_bin_path, DIR_SEP, alarm_bin);
  } else {
    sprintf(cmd_line, "CMD.EXE /C \"%s%s%s\"", alarm_bin_path, DIR_SEP, alarm_bin);
  }

  SetEnvironmentVariable("TRAFFIC_SERVER_ALARM_MSG", desc);
  SetEnvironmentVariable("ADMIN_EMAIL", alarm_email_to_addr);

  STARTUPINFO suInfo;
  PROCESS_INFORMATION procInfo;
  ZeroMemory((PVOID) & suInfo, sizeof(suInfo));

  // hide the new console window from the user
  suInfo.cb = sizeof(STARTUPINFO);
  suInfo.dwFlags = STARTF_USESHOWWINDOW;
  suInfo.wShowWindow = SW_HIDE;

  if (CreateProcess(NULL, cmd_line, NULL,       // FIX THIS: process security attributes
                    NULL,       // FIX THIS: thread security attributes
                    FALSE,      // no need to make handles inheritable
                    0,          // FIX THIS: specify a priority
                    NULL,       // FIX THIS: specify environment variables
                    ts_base_dir,        // make script run from TSBase
                    &suInfo, &procInfo) == FALSE) {
    mgmt_elog(stderr, "[Alarm::execAlarmBin] CreateProcess error: %s\n", ink_last_err());
  } else {
    CloseHandle(procInfo.hThread);
    CloseHandle(procInfo.hProcess);
  }

#endif // !_WIN32

  // free memory
  if (alarm_email_from_name)
    xfree(alarm_email_from_name);
  if (alarm_email_from_addr)
    xfree(alarm_email_from_addr);
  if (alarm_email_to_addr)
    xfree(alarm_email_to_addr);

}

//
// getAlarmText
//
// returns the corresponding text for the alarm id
// 
const char *
Alarms::getAlarmText(alarm_t id)
{
  const char *wda_conn_died = "The connection to the billing system is broken. Unable to retrieve user profile.";
  const char *wda_corr_data =
    "Could not read user profile or URL list from the billing system. The data received doesn't have the expected format.";
  const char *wda_xf_down = "The XF engine heartbeat could not be properly detected. It appears dead.";
  const char *wda_corr_packets =
    "Could not find the expected data in the radius packet. Happened multi-times (configurable) consecutively.";

  switch (id) {
  case MGMT_ALARM_WDA_BILLING_CONNECTION_DIED:
    return wda_conn_died;
  case MGMT_ALARM_WDA_BILLING_CORRUPTED_DATA:
    return wda_corr_data;
  case MGMT_ALARM_WDA_XF_ENGINE_DOWN:
    return wda_xf_down;
  case MGMT_ALARM_WDA_RADIUS_CORRUPTED_PACKETS:
    return wda_corr_packets;
  default:
    if (id < alarmTextNum)
      return alarmText[id];
    else
      return alarmText[0];      // "Unknown Alarm";
  }
}
