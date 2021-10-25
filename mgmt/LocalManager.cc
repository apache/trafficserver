/** @file

  The Local Manager process of the management system.

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
#include "tscore/ink_sock.h"
#include "tscore/ink_file.h"
#include "tscore/ink_error.h"
#include "Alarms.h"
#include "MgmtUtils.h"
#include "tscore/I_Layout.h"
#include "tscore/runroot.h"
#include "LocalManager.h"
#include "MgmtSocket.h"
#include "tscore/ink_cap.h"
#include "FileManager.h"
#include <string_view>
#include <algorithm>
#include "tscpp/util/TextView.h"
#include "tscore/BufferWriter.h"
#include "tscore/bwf_std_format.h"
#include "tscore/Filenames.h"

#if TS_USE_POSIX_CAP
#include <sys/capability.h>
#endif

using namespace std::literals;
static const std::string_view MGMT_OPT{"-M"};
static const std::string_view RUNROOT_OPT{"--run-root="};

void
LocalManager::mgmtCleanup()
{
  close_socket(process_server_sockfd);
  process_server_sockfd = ts::NO_FD;

#if HAVE_EVENTFD
  if (wakeup_fd != ts::NO_FD) {
    close_socket(wakeup_fd);
    wakeup_fd = ts::NO_FD;
  }
#endif

  // fix me for librecords

  closelog();
  return;
}

void
LocalManager::mgmtShutdown()
{
  mgmt_log("[LocalManager::mgmtShutdown] Executing shutdown request.\n");
  processShutdown(true);
  // WCCP TBD: Send a shutdown message to routers.

  if (processRunning()) {
    waitpid(watched_process_pid, nullptr, 0);
#if defined(linux)
    /* Avert race condition, wait for the thread to complete,
       before getting one more restart process */
    /* Workaround for bugid INKqa10060 */
    mgmt_sleep_msec(1);
#endif
  }
  mgmtCleanup();
}

void
LocalManager::processShutdown(bool mainThread)
{
  mgmt_log("[LocalManager::processShutdown] Executing process shutdown request.\n");
  if (mainThread) {
    sendMgmtMsgToProcesses(MGMT_EVENT_SHUTDOWN, "processShutdown[main]");
  } else {
    signalEvent(MGMT_EVENT_SHUTDOWN, "processShutdown");
  }
  return;
}

void
LocalManager::processRestart()
{
  mgmt_log("[LocalManager::processRestart] Executing process restart request.\n");
  signalEvent(MGMT_EVENT_RESTART, "processRestart");
  return;
}

void
LocalManager::processBounce()
{
  mgmt_log("[LocalManager::processBounce] Executing process bounce request.\n");
  signalEvent(MGMT_EVENT_BOUNCE, "processBounce");
  return;
}

void
LocalManager::processDrain(int to_drain)
{
  mgmt_log("[LocalManager::processDrain] Executing process drain request.\n");
  signalEvent(MGMT_EVENT_DRAIN, to_drain ? "1" : "0");
  return;
}

void
LocalManager::rollLogFiles()
{
  mgmt_log("[LocalManager::rollLogFiles] Log files are being rolled.\n");
  signalEvent(MGMT_EVENT_ROLL_LOG_FILES, "rollLogs");
  return;
}

void
LocalManager::hostStatusSetDown(const char *marshalled_req, int len)
{
  signalEvent(MGMT_EVENT_HOST_STATUS_DOWN, marshalled_req, len);
  return;
}

void
LocalManager::hostStatusSetUp(const char *marshalled_req, int len)
{
  signalEvent(MGMT_EVENT_HOST_STATUS_UP, marshalled_req, len);
  return;
}

void
LocalManager::clearStats(const char *name)
{
  // Clear our records and then send the signal.  There is a race condition
  //  here where our stats could get re-updated from the proxy
  //  before the proxy clears them, but this should be rare.
  //
  //  Doing things in the opposite order prevents that race
  //   but exacerbates the race between the node and cluster
  //   stats getting cleared by propagation of clearing the
  //   cluster stats
  //
  if (name && *name) {
    RecResetStatRecord(name);
  } else {
    RecResetStatRecord(RECT_NULL, true);
  }

  // If the proxy is not running, sending the signal does
  //   not do anything.  Remove the stats file to make sure
  //   that operation works even when the proxy is off
  //
  if (this->proxy_running == 0) {
    ats_scoped_str statsPath(RecConfigReadPersistentStatsPath());
    if (unlink(statsPath) < 0) {
      if (errno != ENOENT) {
        mgmt_log("[LocalManager::clearStats] Unlink of %s failed : %s\n", (const char *)statsPath, strerror(errno));
      }
    }
  }
}

bool
LocalManager::processRunning()
{
  if (watched_process_fd != ts::NO_FD && watched_process_pid != -1) {
    return true;
  } else {
    return false;
  }
}

LocalManager::LocalManager(bool proxy_on, bool listen) : BaseManager(), run_proxy(proxy_on), listen_for_proxy(listen)
{
  bool found;
  std::string bindir(RecConfigReadBinDir());
  std::string sysconfdir(RecConfigReadConfigDir());

  manager_started_at = time(nullptr);

  RecRegisterStatInt(RECT_NODE, "proxy.node.proxy_running", 0, RECP_NON_PERSISTENT);

  RecInt http_enabled = REC_readInteger("proxy.config.http.enabled", &found);
  ink_assert(found);
  if (http_enabled && found) {
    HttpProxyPort::loadConfig(m_proxy_ports);
  }
  HttpProxyPort::loadDefaultIfEmpty(m_proxy_ports);

  // Get the default IP binding values.
  RecHttpLoadIp("proxy.local.incoming_ip_to_bind", m_inbound_ip4, m_inbound_ip6);

  if (access(sysconfdir.c_str(), R_OK) == -1) {
    mgmt_log("[LocalManager::LocalManager] unable to access() directory '%s': %d, %s\n", sysconfdir.c_str(), errno,
             strerror(errno));
    mgmt_fatal(0, "[LocalManager::LocalManager] please set the 'TS_ROOT' environment variable\n");
  }

#if TS_HAS_WCCP
  // Bind the WCCP address if present.
  ats_scoped_str wccp_addr_str(REC_readString("proxy.config.wccp.addr", &found));
  if (found && wccp_addr_str && *wccp_addr_str) {
    wccp_cache.setAddr(inet_addr(wccp_addr_str));
    mgmt_log("[LocalManager::LocalManager] WCCP identifying address set to %s.\n", static_cast<char *>(wccp_addr_str));
  }

  ats_scoped_str wccp_config_str(RecConfigReadConfigPath("proxy.config.wccp.services"));
  if (wccp_config_str && strlen(wccp_config_str) > 0) {
    bool located = true;
    if (access(wccp_config_str, R_OK) == -1) {
      located = false;
    }

    if (located) {
      wccp_cache.loadServicesFromFile(wccp_config_str);
    } else { // not located
      mgmt_log("[LocalManager::LocalManager] WCCP service configuration file '%s' was specified but could not be found in the file "
               "system.\n",
               static_cast<char *>(wccp_config_str));
    }
  }
#endif

  process_server_timeout_secs  = REC_readInteger("proxy.config.lm.pserver_timeout_secs", &found);
  process_server_timeout_msecs = REC_readInteger("proxy.config.lm.pserver_timeout_msecs", &found);
  proxy_name                   = REC_readString("proxy.config.proxy_name", &found);
  proxy_binary                 = REC_readString("proxy.config.proxy_binary", &found);
  env_prep                     = REC_readString("proxy.config.env_prep", &found);

  // Calculate proxy_binary from the absolute bin_path
  absolute_proxy_binary = ats_stringdup(Layout::relative_to(bindir, proxy_binary));

  // coverity[fs_check_call]
  if (access(absolute_proxy_binary, R_OK | X_OK) == -1) {
    mgmt_log("[LocalManager::LocalManager] Unable to access() '%s': %d, %s\n", absolute_proxy_binary, errno, strerror(errno));
    mgmt_fatal(0, "[LocalManager::LocalManager] please set bin path 'proxy.config.bin_path' \n");
  }

  return;
}

LocalManager::~LocalManager()
{
  delete alarm_keeper;
  ats_free(absolute_proxy_binary);
  ats_free(proxy_name);
  ats_free(proxy_binary);
  ats_free(env_prep);
}

void
LocalManager::initAlarm()
{
  alarm_keeper = new Alarms();
}

/*
 * initMgmtProcessServer()
 *   sets up the server socket that proxy processes connect to.
 */
void
LocalManager::initMgmtProcessServer()
{
  std::string rundir(RecConfigReadRuntimeDir());
  std::string sockpath(Layout::relative_to(rundir, LM_CONNECTION_SERVER));
  mode_t oldmask = umask(0);

#if TS_HAS_WCCP
  if (wccp_cache.isConfigured()) {
    if (0 > wccp_cache.open())
      mgmt_log("Failed to open WCCP socket\n");
  }
#endif

  process_server_sockfd = bind_unix_domain_socket(sockpath.c_str(), 00700);
  if (process_server_sockfd == -1) {
    mgmt_fatal(errno, "[LocalManager::initMgmtProcessServer] failed to bind socket at %s\n", sockpath.c_str());
  }

#if HAVE_EVENTFD
  wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (wakeup_fd < 0) {
    mgmt_fatal(errno, "[LocalManager::initMgmtProcessServer] failed to create eventfd. errno : %s\n", strerror(errno));
  }
#endif

  umask(oldmask);
  RecSetRecordInt("proxy.node.restarts.manager.start_time", manager_started_at, REC_SOURCE_DEFAULT);
}

/*
 * pollMgmtProcessServer()
 * -  Function checks the mgmt process server for new processes
 *    and any requests sent from processes. It handles processes sent.
 */
void
LocalManager::pollMgmtProcessServer()
{
  struct timeval timeout;
  fd_set fdlist;

  while (true) {
#if TS_HAS_WCCP
    int wccp_fd = wccp_cache.getSocket();
#endif

    timeout.tv_sec  = process_server_timeout_secs;
    timeout.tv_usec = process_server_timeout_msecs * 1000;

    FD_ZERO(&fdlist);

    if (process_server_sockfd != ts::NO_FD) {
      FD_SET(process_server_sockfd, &fdlist);
    }

    if (watched_process_fd != ts::NO_FD) {
      FD_SET(watched_process_fd, &fdlist);
    }

#if TS_HAS_WCCP
    // Only run WCCP housekeeping while we have a server process.
    // Note: The WCCP socket is opened iff WCCP is configured.
    if (wccp_fd != ts::NO_FD && watched_process_fd != ts::NO_FD) {
      wccp_cache.housekeeping();
      time_t wccp_wait = wccp_cache.waitTime();
      if (wccp_wait < process_server_timeout_secs)
        timeout.tv_sec = wccp_wait;

      if (wccp_fd != ts::NO_FD) {
        FD_SET(wccp_fd, &fdlist);
      }
    }
#endif

#if HAVE_EVENTFD
    if (wakeup_fd != ts::NO_FD) {
      FD_SET(wakeup_fd, &fdlist);
    }
#endif

    int num = mgmt_select(FD_SETSIZE, &fdlist, nullptr, nullptr, &timeout);

    switch (num) {
    case 0:
      // Timed out, nothing to do.
      return;
    case -1:
      if (mgmt_transient_error()) {
        continue;
      }

      mgmt_log("[LocalManager::pollMgmtProcessServer] select failed: %s (%d)\n", ::strerror(errno), errno);
      return;

    default:
      // if we get a wakeup_fd event, we may not want to follow it
      // because there may be more data to be read on the socket.
      bool keep_polling = false;
#if TS_HAS_WCCP
      if (wccp_fd != ts::NO_FD && FD_ISSET(wccp_fd, &fdlist)) {
        wccp_cache.handleMessage();
        --num;
        keep_polling = true;
      }
#endif

      if (process_server_sockfd != ts::NO_FD && FD_ISSET(process_server_sockfd, &fdlist)) { /* New connection */
        struct sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int new_sockfd      = mgmt_accept(process_server_sockfd, reinterpret_cast<struct sockaddr *>(&clientAddr), &clientLen);

        mgmt_log("[LocalManager::pollMgmtProcessServer] New process connecting fd '%d'\n", new_sockfd);

        if (new_sockfd < 0) {
          mgmt_elog(errno, "[LocalManager::pollMgmtProcessServer] ==> ");
        } else if (!processRunning()) {
          watched_process_fd = new_sockfd;
        } else {
          close_socket(new_sockfd);
        }
        --num;
        keep_polling = true;
      }

      if (ts::NO_FD != watched_process_fd && FD_ISSET(watched_process_fd, &fdlist)) {
        int res;
        MgmtMessageHdr mh_hdr;

        keep_polling = true;

        // read the message
        if ((res = mgmt_read_pipe(watched_process_fd, reinterpret_cast<char *>(&mh_hdr), sizeof(MgmtMessageHdr))) > 0) {
          MgmtMessageHdr *mh_full = static_cast<MgmtMessageHdr *>(malloc(sizeof(MgmtMessageHdr) + mh_hdr.data_len));
          memcpy(mh_full, &mh_hdr, sizeof(MgmtMessageHdr));
          char *data_raw = reinterpret_cast<char *>(mh_full) + sizeof(MgmtMessageHdr);
          if ((res = mgmt_read_pipe(watched_process_fd, data_raw, mh_hdr.data_len)) > 0) {
            handleMgmtMsgFromProcesses(mh_full);
          } else if (res < 0) {
            mgmt_fatal(0, "[LocalManager::pollMgmtProcessServer] Error in read (errno: %d)\n", -res);
          }
          free(mh_full);
        } else if (res < 0) {
          mgmt_fatal(0, "[LocalManager::pollMgmtProcessServer] Error in read (errno: %d)\n", -res);
        }

        // handle EOF
        if (res == 0) {
          int estatus;
          pid_t tmp_pid = watched_process_pid;

          Debug("lm", "[LocalManager::pollMgmtProcessServer] Lost process EOF!");

          close_socket(watched_process_fd);

          waitpid(watched_process_pid, &estatus, 0); /* Reap child */
          if (WIFSIGNALED(estatus)) {
            int sig = WTERMSIG(estatus);
            mgmt_log("[LocalManager::pollMgmtProcessServer] Server Process terminated due to Sig %d: %s\n", sig, strsignal(sig));
          } else if (WIFEXITED(estatus)) {
            int return_code = WEXITSTATUS(estatus);

            // traffic_server's exit code will be UNRECOVERABLE_EXIT if it calls
            // ink_emergency() or ink_emergency_va(). The call signals that traffic_server
            // cannot be recovered with a reboot. In other words, catastrophic failure.
            if (return_code == UNRECOVERABLE_EXIT) {
              proxy_recoverable = false;
            }
          }

          if (lmgmt->run_proxy) {
            mgmt_log("[Alarms::signalAlarm] Server Process was reset\n");
            lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_DIED, nullptr);
          } else {
            mgmt_log("[TrafficManager] Server process shutdown\n");
          }

          watched_process_fd = watched_process_pid = -1;
          if (tmp_pid != -1) { /* Incremented after a pid: message is sent */
            proxy_running--;
          }
          proxy_started_at = -1;
          RecSetRecordInt("proxy.node.proxy_running", 0, REC_SOURCE_DEFAULT);
        }

        --num;
      }

#if HAVE_EVENTFD
      if (wakeup_fd != ts::NO_FD && FD_ISSET(wakeup_fd, &fdlist)) {
        if (!keep_polling) {
          // read or else fd will always be set.
          uint64_t ignore;
          ATS_UNUSED_RETURN(read(wakeup_fd, &ignore, sizeof(uint64_t)));
          return;
        }
        --num;
      }
#else
      (void)keep_polling; // suppress compiler warning
#endif

      ink_assert(num == 0); /* Invariant */
    }
  }
}

void
LocalManager::handleMgmtMsgFromProcesses(MgmtMessageHdr *mh)
{
  char *data_raw = reinterpret_cast<char *>(mh) + sizeof(MgmtMessageHdr);
  switch (mh->msg_id) {
  case MGMT_SIGNAL_PID:
    watched_process_pid = *(reinterpret_cast<pid_t *>(data_raw));
    lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_BORN, nullptr);
    proxy_running++;
    proxy_launch_pid         = -1;
    proxy_launch_outstanding = false;
    RecSetRecordInt("proxy.node.proxy_running", 1, REC_SOURCE_DEFAULT);
    break;

  // FIX: This is very messy need to correlate mgmt signals and
  // alarms better
  case MGMT_SIGNAL_CONFIG_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CONFIG_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_SYSTEM_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_CACHE_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CACHE_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_CACHE_WARNING:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_CACHE_WARNING, data_raw);
    break;
  case MGMT_SIGNAL_LOGGING_ERROR:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_LOGGING_ERROR, data_raw);
    break;
  case MGMT_SIGNAL_LOGGING_WARNING:
    alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_LOGGING_WARNING, data_raw);
    break;
  case MGMT_SIGNAL_PLUGIN_SET_CONFIG: {
    char var_name[256];
    char var_value[256];
    MgmtType stype;
    // stype is an enum type, so cast to an int* to avoid warnings. /leif
    int tokens = sscanf(data_raw, "%255s %d %255s", var_name, reinterpret_cast<int *>(&stype), var_value);
    if (tokens != 3) {
      stype = MGMT_INVALID;
    }
    switch (stype) {
    case MGMT_INT:
      RecSetRecordInt(var_name, ink_atoi64(var_value), REC_SOURCE_EXPLICIT);
      break;
    case MGMT_COUNTER:
    case MGMT_FLOAT:
    case MGMT_STRING:
    case MGMT_INVALID:
    default:
      mgmt_log("[LocalManager::handleMgmtMsgFromProcesses] "
               "Invalid plugin set-config msg '%s'\n",
               data_raw);
      break;
    }
  } break;
  case MGMT_SIGNAL_LIBRECORDS:
    if (mh->data_len > 0) {
      executeMgmtCallback(MGMT_SIGNAL_LIBRECORDS, {data_raw, static_cast<size_t>(mh->data_len)});
    } else {
      executeMgmtCallback(MGMT_SIGNAL_LIBRECORDS, {});
    }
    break;
  case MGMT_SIGNAL_CONFIG_FILE_CHILD: {
    static const MgmtMarshallType fields[] = {MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING};
    char *parent                           = nullptr;
    char *child                            = nullptr;
    if (mgmt_message_parse(data_raw, mh->data_len, fields, countof(fields), &parent, &child) != -1) {
      configFiles->configFileChild(parent, child);
    } else {
      mgmt_log("[LocalManager::handleMgmtMsgFromProcesses] "
               "MGMT_SIGNAL_CONFIG_FILE_CHILD mgmt_message_parse error\n");
    }
    // Output pointers are guaranteed to be NULL or valid.
    ats_free_null(parent);
    ats_free_null(child);
  } break;

  default:
    break;
  }
}

void
LocalManager::sendMgmtMsgToProcesses(int msg_id, const char *data_str)
{
  sendMgmtMsgToProcesses(msg_id, data_str, strlen(data_str) + 1);
  return;
}

void
LocalManager::sendMgmtMsgToProcesses(int msg_id, const char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;

  mh           = static_cast<MgmtMessageHdr *>(alloca(sizeof(MgmtMessageHdr) + data_len));
  mh->msg_id   = msg_id;
  mh->data_len = data_len;
  memcpy(reinterpret_cast<char *>(mh) + sizeof(MgmtMessageHdr), data_raw, data_len);
  sendMgmtMsgToProcesses(mh);
  return;
}

void
LocalManager::sendMgmtMsgToProcesses(MgmtMessageHdr *mh)
{
  switch (mh->msg_id) {
  case MGMT_EVENT_SHUTDOWN: {
    run_proxy = false;
    this->closeProxyPorts();
    break;
  }
  case MGMT_EVENT_RESTART:
    run_proxy = true;
    listenForProxy();
    return;
  case MGMT_EVENT_BOUNCE: /* Just bouncing the cluster, have it exit well restart */
    mh->msg_id = MGMT_EVENT_SHUTDOWN;
    break;
  case MGMT_EVENT_ROLL_LOG_FILES:
    mgmt_log("[LocalManager::SendMgmtMsgsToProcesses]Event is being constructed .\n");
    break;
  case MGMT_EVENT_CONFIG_FILE_UPDATE:
    bool found;
    char *fname = nullptr;
    ConfigManager *rb;
    char *data_raw;

    data_raw = reinterpret_cast<char *>(mh) + sizeof(MgmtMessageHdr);
    fname    = REC_readString(data_raw, &found);

    RecT rec_type;
    if (RecGetRecordType(data_raw, &rec_type) == REC_ERR_OKAY && rec_type == RECT_CONFIG) {
      RecSetSyncRequired(data_raw);
    } else {
      mgmt_log("[LocalManager:sendMgmtMsgToProcesses] Unknown file change: '%s'\n", data_raw);
    }
    ink_assert(found);
    if (!(fname && configFiles && configFiles->getConfigObj(fname, &rb)) &&
        (strcmp(data_raw, "proxy.config.body_factory.template_sets_dir") != 0) &&
        (strcmp(data_raw, "proxy.config.ssl.server.ticket_key.filename") != 0)) {
      mgmt_fatal(0, "[LocalManager::sendMgmtMsgToProcesses] "
                    "Invalid 'data_raw' for MGMT_EVENT_CONFIG_FILE_UPDATE\n");
    }
    ats_free(fname);
    break;
  }

  if (watched_process_fd != -1) {
    if (mgmt_write_pipe(watched_process_fd, reinterpret_cast<char *>(mh), sizeof(MgmtMessageHdr) + mh->data_len) <= 0) {
      // In case of Linux, sometimes when the TS dies, the connection between TS and TM
      // is not closed properly. the socket does not receive an EOF. So, the TM does
      // not detect that the connection and hence TS has gone down. Hence it still
      // tries to send a message to TS, but encounters an error and enters here
      // Also, ensure that this whole thing is done only once because there will be a
      // deluge of message in the traffic.log otherwise

      static pid_t check_prev_pid    = watched_process_pid;
      static pid_t check_current_pid = watched_process_pid;
      if (check_prev_pid != watched_process_pid) {
        check_prev_pid    = watched_process_pid;
        check_current_pid = watched_process_pid;
      }

      if (check_prev_pid == check_current_pid) {
        check_current_pid = -1;
        int lerrno        = errno;
        mgmt_elog(errno, "[LocalManager::sendMgmtMsgToProcesses] Error writing message\n");
        if (lerrno == ECONNRESET || lerrno == EPIPE) { // Connection closed by peer or Broken pipe
          if ((kill(watched_process_pid, 0) < 0) && (errno == ESRCH)) {
            // TS is down
            pid_t tmp_pid = watched_process_pid;
            close_socket(watched_process_fd);
            mgmt_log("[LocalManager::pollMgmtProcessServer] "
                     "Server Process has been terminated\n");
            if (lmgmt->run_proxy) {
              mgmt_log("[Alarms::signalAlarm] Server Process was reset\n");
              lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_PROCESS_DIED, nullptr);
            } else {
              mgmt_log("[TrafficManager] Server process shutdown\n");
            }
            watched_process_fd = watched_process_pid = -1;
            if (tmp_pid != -1) { /* Incremented after a pid: message is sent */
              proxy_running--;
            }
            proxy_started_at = -1;
            RecSetRecordInt("proxy.node.proxy_running", 0, REC_SOURCE_DEFAULT);
            // End of TS down
          } else {
            // TS is still up, but the connection is lost
            const char *err_msg = "The TS-TM connection is broken for some reason. Either restart TS and TM or correct this error "
                                  "for TM to display TS statistics correctly";
            lmgmt->alarm_keeper->signalAlarm(MGMT_ALARM_PROXY_SYSTEM_ERROR, err_msg);
          }

          // check if the TS is down, by checking the pid
          // if TS is down, then,
          //     raise an alarm
          //     set the variables so that TS is restarted later
          // else (TS is still up)
          //     raise an alarm stating the problem
        }
      }
    }
  }
}

void
LocalManager::signalFileChange(const char *var_name)
{
  signalEvent(MGMT_EVENT_CONFIG_FILE_UPDATE, var_name);

  return;
}

void
LocalManager::signalEvent(int msg_id, const char *data_str)
{
  signalEvent(msg_id, data_str, strlen(data_str) + 1);
  return;
}

void
LocalManager::signalEvent(int msg_id, const char *data_raw, int data_len)
{
  MgmtMessageHdr *mh;
  size_t n = sizeof(MgmtMessageHdr) + data_len;

  mh           = static_cast<MgmtMessageHdr *>(ats_malloc(n));
  mh->msg_id   = msg_id;
  mh->data_len = data_len;
  auto payload = mh->payload();
  memcpy(payload.data(), data_raw, data_len);
  this->enqueue(mh);
  //  ink_assert(enqueue(mgmt_event_queue, mh));

#if HAVE_EVENTFD
  // we don't care about the actual value of wakeup_fd, so just keep adding 1. just need to
  // wakeup the fd. also, note that wakeup_fd was initialized to non-blocking so we can
  // directly write to it without any timeout checking.
  //
  // don't trigger if MGMT_EVENT_LIBRECORD because they happen all the time
  // and don't require a quick response. for MGMT_EVENT_LIBRECORD, rely on timeouts so
  // traffic_server can spend more time doing other things
  uint64_t one = 1;
  if (wakeup_fd != ts::NO_FD && mh->msg_id != MGMT_EVENT_LIBRECORDS) {
    ATS_UNUSED_RETURN(write(wakeup_fd, &one, sizeof(uint64_t))); // trigger to stop polling
  }
#endif
}

/*
 * processEventQueue()
 *   Function drains and processes the mgmt event queue
 * notifying any registered callback functions and performing
 * any mgmt tasks for each event.
 */
void
LocalManager::processEventQueue()
{
  while (!this->queue_empty()) {
    bool handled_by_mgmt = false;

    MgmtMessageHdr *mh = this->dequeue();
    auto payload       = mh->payload().rebind<char>();

    // check if we have a local file update
    if (mh->msg_id == MGMT_EVENT_CONFIG_FILE_UPDATE) {
      // records.config
      if (!(strcmp(payload.begin(), ts::filename::RECORDS))) {
        if (RecReadConfigFile() != REC_ERR_OKAY) {
          mgmt_elog(errno, "[fileUpdated] Config update failed for %s\n", ts::filename::RECORDS);
        } else {
          RecConfigWarnIfUnregistered();
        }
        handled_by_mgmt = true;
      }
    }

    if (!handled_by_mgmt) {
      if (processRunning() == false) {
        // Fix INKqa04984
        // If traffic server hasn't completely come up yet,
        // we will hold off until next round.
        this->enqueue(mh);
        return;
      }
      Debug("lm", "[TrafficManager] ==> Sending signal event '%d' %s payload=%d", mh->msg_id, payload.begin(), int(payload.size()));
      lmgmt->sendMgmtMsgToProcesses(mh);
    }
    ats_free(mh);
  }
}

/*
 * startProxy()
 *   Function fires up a proxy process.
 *
 * Args:
 *   onetime_options: one time options that traffic_server should be started with (ie
 *                    these options do not persist across reboots)
 */
static const size_t OPTIONS_SIZE = 16384; // Arbitrary max size for command line option string

bool
LocalManager::startProxy(const char *onetime_options)
{
  if (proxy_launch_outstanding) {
    return false;
  }
  mgmt_log("[LocalManager::startProxy] Launching ts process\n");

  pid_t pid;

  // Before we do anything lets check for the existence of
  // the traffic server binary along with it's execute permissions
  if (access(absolute_proxy_binary, F_OK) < 0) {
    // Error can't find traffic_server
    mgmt_elog(errno, "[LocalManager::startProxy] Unable to find traffic server at %s\n", absolute_proxy_binary);
    return false;
  }
  // traffic server binary exists, check permissions
  else if (access(absolute_proxy_binary, R_OK | X_OK) < 0) {
    // Error don't have proper permissions
    mgmt_elog(errno, "[LocalManager::startProxy] Unable to access %s due to bad permissions \n", absolute_proxy_binary);
    return false;
  }

  if (env_prep) {
#ifdef POSIX_THREAD
    if ((pid = fork()) < 0)
#else
    if ((pid = fork1()) < 0)
#endif
    {
      mgmt_elog(errno, "[LocalManager::startProxy] Unable to fork1 prep process\n");
      return false;
    } else if (pid > 0) {
      int estatus;
      waitpid(pid, &estatus, 0);
    } else {
      int res;

      char env_prep_bin[MAXPATHLEN];
      std::string bindir(RecConfigReadBinDir());

      ink_filepath_make(env_prep_bin, sizeof(env_prep_bin), bindir.c_str(), env_prep);
      res = execl(env_prep_bin, env_prep_bin, (char *)nullptr);
      _exit(res);
    }
  }
#ifdef POSIX_THREAD
  if ((pid = fork()) < 0)
#else
  if ((pid = fork1()) < 0)
#endif
  {
    mgmt_elog(errno, "[LocalManager::startProxy] Unable to fork1 process\n");
    return false;
  } else if (pid > 0) { /* Parent */
    proxy_launch_pid         = pid;
    proxy_launch_outstanding = true;
    proxy_started_at         = time(nullptr);
    ++proxy_launch_count;
    RecSetRecordInt("proxy.node.restarts.proxy.start_time", proxy_started_at, REC_SOURCE_DEFAULT);
    RecSetRecordInt("proxy.node.restarts.proxy.restart_count", proxy_launch_count, REC_SOURCE_DEFAULT);
  } else {
    int i = 0;
    char *options[32], *last, *tok;
    char options_buffer[OPTIONS_SIZE];
    ts::FixedBufferWriter w{options_buffer, OPTIONS_SIZE};

    w.clip(1);
    w.print("{}{}", ts::bwf::OptionalAffix(proxy_options), ts::bwf::OptionalAffix(onetime_options));

    // Make sure we're starting the proxy in mgmt mode
    if (w.view().find(MGMT_OPT) == std::string_view::npos) {
      w.write(MGMT_OPT);
      w.write(' ');
    }

    // pass the runroot option to traffic_server
    std::string_view runroot_arg = get_runroot();
    if (!runroot_arg.empty()) {
      w.write(RUNROOT_OPT);
      w.write(runroot_arg);
      w.write(' ');
    }

    // Pass down port/fd information to traffic_server if there are any open ports.
    if (std::any_of(m_proxy_ports.begin(), m_proxy_ports.end(), [](HttpProxyPort &p) { return ts::NO_FD != p.m_fd; })) {
      char portbuf[128];
      bool need_comma_p = false;

      w.write("--httpport "sv);
      for (auto &p : m_proxy_ports) {
        if (ts::NO_FD != p.m_fd) {
          if (need_comma_p) {
            w.write(',');
          }
          need_comma_p = true;
          p.print(portbuf, sizeof(portbuf));
          w.write(portbuf);
        }
      }
    }

    w.extend(1);
    w.write('\0'); // null terminate.

    Debug("lm", "[LocalManager::startProxy] Launching %s '%s'", absolute_proxy_binary, w.data());

    // Unfortunately the normally obnoxious null writing of strtok is in this case a required
    // side effect and other alternatives are noticeably more clunky.
    ink_zero(options);
    options[0] = absolute_proxy_binary;
    i          = 1;
    tok        = strtok_r(options_buffer, " ", &last);
    Debug("lm", "opt %d = '%s'", i, tok);
    options[i++] = tok;
    while (i < 32 && (tok = strtok_r(nullptr, " ", &last))) {
      Debug("lm", "opt %d = '%s'", i, tok);
      options[i++] = tok;
    }

    EnableDeathSignal(SIGTERM);

    execv(absolute_proxy_binary, options);
    mgmt_fatal(errno, "[LocalManager::startProxy] Exec of %s failed\n", absolute_proxy_binary);
  }
  return true;
}

/** Close all open ports.
 */
void
LocalManager::closeProxyPorts()
{
  for (auto &p : lmgmt->m_proxy_ports) {
    if (ts::NO_FD != p.m_fd) {
      close_socket(p.m_fd);
      p.m_fd = ts::NO_FD;
    }
  }
}
/*
 * listenForProxy()
 *  Function listens on the accept port of the proxy, so users aren't dropped.
 */
void
LocalManager::listenForProxy()
{
  if (!run_proxy || !listen_for_proxy) {
    return;
  }

  // We are not already bound, bind the port
  for (auto &p : lmgmt->m_proxy_ports) {
    if (ts::NO_FD == p.m_fd) {
      // Check the protocol (TCP or UDP) and create an appropriate socket
      if (p.isQUIC()) {
        this->bindUdpProxyPort(p);
      } else {
        this->bindTcpProxyPort(p);
      }
    }

    std::string_view fam{ats_ip_family_name(p.m_family)};
    if (p.isQUIC()) {
      // Can we do something like listen backlog for QUIC(UDP) ??
      // Do nothing for now
    } else {
      // read backlog configuration value and overwrite the default value if found
      bool found;
      RecInt backlog = REC_readInteger("proxy.config.net.listen_backlog", &found);
      backlog        = (found && backlog >= 0) ? backlog : ats_tcp_somaxconn();

      if ((listen(p.m_fd, backlog)) < 0) {
        mgmt_fatal(errno, "[LocalManager::listenForProxy] Unable to listen on port: %d (%.*s)\n", p.m_port, fam.size(), fam.data());
      }
    }

    mgmt_log("[LocalManager::listenForProxy] Listening on port: %d (%.*s)\n", p.m_port, fam.size(), fam.data());
  }
  return;
}

/*
 * bindUdpProxyPort()
 *  Function binds the accept port of the proxy
 */
void
LocalManager::bindUdpProxyPort(HttpProxyPort &port)
{
  int one  = 1;
  int priv = (port.m_port < 1024 && 0 != geteuid()) ? ElevateAccess::LOW_PORT_PRIVILEGE : 0;

  ElevateAccess access(priv);

  if ((port.m_fd = socket(port.m_family, SOCK_DGRAM, 0)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to create socket : %s\n", strerror(errno));
  }

  if (port.m_family == AF_INET6) {
    if (setsockopt(port.m_fd, IPPROTO_IPV6, IPV6_V6ONLY, SOCKOPT_ON, sizeof(int)) < 0) {
      mgmt_log("[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
    }
  }
  if (setsockopt(port.m_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&one), sizeof(int)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
  }

  IpEndpoint ip;
  if (port.m_inbound_ip.isValid()) {
    ip.assign(port.m_inbound_ip);
  } else if (AF_INET6 == port.m_family) {
    if (m_inbound_ip6.isValid()) {
      ip.assign(m_inbound_ip6);
    } else {
      ip.setToAnyAddr(AF_INET6);
    }
  } else if (AF_INET == port.m_family) {
    if (m_inbound_ip4.isValid()) {
      ip.assign(m_inbound_ip4);
    } else {
      ip.setToAnyAddr(AF_INET);
    }
  } else {
    mgmt_fatal(0, "[bindProxyPort] Proxy port with invalid address type %d\n", port.m_family);
  }
  ip.network_order_port() = htons(port.m_port);
  if (bind(port.m_fd, &ip.sa, ats_ip_size(&ip)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to bind socket: %d : %s\n", port.m_port, strerror(errno));
  }

  Debug("lm", "[bindProxyPort] Successfully bound proxy port %d", port.m_port);
}

/*
 * bindTcpProxyPort()
 *  Function binds the accept port of the proxy
 */
void
LocalManager::bindTcpProxyPort(HttpProxyPort &port)
{
  int one  = 1;
  int priv = (port.m_port < 1024 && 0 != geteuid()) ? ElevateAccess::LOW_PORT_PRIVILEGE : 0;

  ElevateAccess access(priv);

  /* Setup reliable connection, for large config changes */
  if ((port.m_fd = socket(port.m_family, SOCK_STREAM, 0)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to create socket : %s\n", strerror(errno));
  }

  if (port.m_type == HttpProxyPort::TRANSPORT_DEFAULT) {
    int should_filter_int = 0;
    bool found;
    should_filter_int = REC_readInteger("proxy.config.net.defer_accept", &found);
    if (found && should_filter_int > 0) {
#if defined(SOL_FILTER) && defined(FIL_ATTACH)
      (void)setsockopt(port.m_fd, SOL_FILTER, FIL_ATTACH, "httpfilt", 9);
#endif
    }
  }

  if (port.m_mptcp) {
#if MPTCP_ENABLED
    int err;

    err = setsockopt(port.m_fd, IPPROTO_TCP, MPTCP_ENABLED, &one, sizeof(one));
    if (err < 0) {
      mgmt_log("[bindProxyPort] Unable to enable MPTCP: %s\n", strerror(errno));
      Debug("lm_mptcp", "[bindProxyPort] Unable to enable MPTCP: %s", strerror(errno));
    } else {
      mgmt_log("[bindProxyPort] Successfully enabled MPTCP on %d\n", port.m_port);
      Debug("lm_mptcp", "[bindProxyPort] Successfully enabled MPTCP on %d\n", port.m_port);
    }
#else
    Debug("lm_mptcp", "[bindProxyPort] Multipath TCP requested but not configured on this host");
#endif
  }

  if (port.m_family == AF_INET6) {
    if (setsockopt(port.m_fd, IPPROTO_IPV6, IPV6_V6ONLY, SOCKOPT_ON, sizeof(int)) < 0) {
      mgmt_log("[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
    }
  }
  if (setsockopt(port.m_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&one), sizeof(int)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to set socket options: %d : %s\n", port.m_port, strerror(errno));
  }

  if (port.m_proxy_protocol) {
    Debug("lm", "[bindProxyPort] Proxy Protocol enabled");
  }

  if (port.m_inbound_transparent_p) {
#if TS_USE_TPROXY
    Debug("http_tproxy", "Listen port %d inbound transparency enabled.", port.m_port);
    if (setsockopt(port.m_fd, SOL_IP, TS_IP_TRANSPARENT, &one, sizeof(one)) == -1) {
      mgmt_fatal(0, "[bindProxyPort] Unable to set transparent socket option [%d] %s\n", errno, strerror(errno));
    }
#else
    Debug("lm", "[bindProxyPort] Transparency requested but TPROXY not configured");
#endif
  }

  IpEndpoint ip;
  if (port.m_inbound_ip.isValid()) {
    ip.assign(port.m_inbound_ip);
  } else if (AF_INET6 == port.m_family) {
    if (m_inbound_ip6.isValid()) {
      ip.assign(m_inbound_ip6);
    } else {
      ip.setToAnyAddr(AF_INET6);
    }
  } else if (AF_INET == port.m_family) {
    if (m_inbound_ip4.isValid()) {
      ip.assign(m_inbound_ip4);
    } else {
      ip.setToAnyAddr(AF_INET);
    }
  } else {
    mgmt_fatal(0, "[bindProxyPort] Proxy port with invalid address type %d\n", port.m_family);
  }
  ip.network_order_port() = htons(port.m_port);
  if (bind(port.m_fd, &ip.sa, ats_ip_size(&ip)) < 0) {
    mgmt_fatal(0, "[bindProxyPort] Unable to bind socket: %d : %s\n", port.m_port, strerror(errno));
  }

  Debug("lm", "[bindProxyPort] Successfully bound proxy port %d", port.m_port);
}

void
LocalManager::signalAlarm(int alarm_id, const char *desc, const char *ip)
{
  if (alarm_keeper) {
    alarm_keeper->signalAlarm(static_cast<alarm_t>(alarm_id), desc, ip);
  }
}
