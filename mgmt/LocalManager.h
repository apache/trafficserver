/** @file

  Definitions for the LocalManager class.

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

#pragma once

#include <string>

#include "BaseManager.h"
#include "records/I_RecHttp.h"
#include "tscore/I_Version.h"

#include <syslog.h>
#if TS_HAS_WCCP
#include <wccp/Wccp.h>
#endif
#if HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

class Alarms;
class FileManager;

enum ManagementPendingOperation {
  MGMT_PENDING_NONE,         // Do nothing
  MGMT_PENDING_RESTART,      // Restart TS and TM
  MGMT_PENDING_BOUNCE,       // Restart TS
  MGMT_PENDING_STOP,         // Stop TS
  MGMT_PENDING_DRAIN,        // Drain TS
  MGMT_PENDING_IDLE_RESTART, // Restart TS and TM when TS is idle
  MGMT_PENDING_IDLE_BOUNCE,  // Restart TS when TS is idle
  MGMT_PENDING_IDLE_STOP,    // Stop TS when TS is idle
  MGMT_PENDING_IDLE_DRAIN,   // Drain TS when TS is idle from new connections
  MGMT_PENDING_UNDO_DRAIN,   // Recover TS from drain
};

class LocalManager : public BaseManager
{
public:
  explicit LocalManager(bool proxy_on, bool listen);
  ~LocalManager();

  void initAlarm();
  void initCCom(const AppVersionInfo &version, FileManager *files, int mcport, char *addr, int rsport);
  void initMgmtProcessServer();
  void pollMgmtProcessServer();
  void handleMgmtMsgFromProcesses(MgmtMessageHdr *mh);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_str);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_raw, int data_len);
  void sendMgmtMsgToProcesses(MgmtMessageHdr *mh);

  void signalFileChange(const char *var_name, bool incVersion = true);
  void signalEvent(int msg_id, const char *data_str);
  void signalEvent(int msg_id, const char *data_raw, int data_len);
  void signalAlarm(int alarm_id, const char *desc = nullptr, const char *ip = nullptr);

  void processEventQueue();
  bool startProxy(const char *onetime_options);
  void listenForProxy();
  void bindProxyPort(HttpProxyPort &);
  void closeProxyPorts();

  void mgmtCleanup();
  void mgmtShutdown();
  void processShutdown(bool mainThread = false);
  void processRestart();
  void processBounce();
  void processDrain(int to_drain = 1);
  void rollLogFiles();
  void clearStats(const char *name = nullptr);
  void hostStatusSetDown(const char *marshalled_req, int len);
  void hostStatusSetUp(const char *marshalled_req, int len);

  bool processRunning();

  bool run_proxy;
  bool listen_for_proxy;
  bool proxy_recoverable = true; // false if traffic_server cannot recover with a reboot
  time_t manager_started_at;
  time_t proxy_started_at                              = -1;
  int proxy_launch_count                               = 0;
  bool proxy_launch_outstanding                        = false;
  ManagementPendingOperation mgmt_shutdown_outstanding = MGMT_PENDING_NONE;
  time_t mgmt_shutdown_triggered_at;
  time_t mgmt_drain_triggered_at;
  int proxy_running = 0;
  HttpProxyPort::Group m_proxy_ports;
  // Local inbound addresses to bind, if set.
  IpAddr m_inbound_ip4;
  IpAddr m_inbound_ip6;

  int process_server_timeout_secs;
  int process_server_timeout_msecs;

  char *absolute_proxy_binary;
  char *proxy_name;
  char *proxy_binary;
  std::string proxy_options; // These options should persist across proxy reboots
  char *env_prep;

  int process_server_sockfd = ts::NO_FD;
  int watched_process_fd    = ts::NO_FD;
#if HAVE_EVENTFD
  int wakeup_fd = ts::NO_FD; // external trigger to stop polling
#endif
  pid_t proxy_launch_pid = -1;

  Alarms *alarm_keeper     = nullptr;
  FileManager *configFiles = nullptr;

  pid_t watched_process_pid = -1;

  int syslog_facility = LOG_DAEMON;

#if TS_HAS_WCCP
  wccp::Cache wccp_cache;
#endif
private:
}; /* End class LocalManager */

extern LocalManager *lmgmt;
