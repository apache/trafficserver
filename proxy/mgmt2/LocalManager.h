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
 * LocalManager.h
 *   Definitions for the LocalManager class.
 *
 * $Date: 2007-10-05 16:56:44 $
 *
 *
 */

#ifndef _LOCAL_MANAGER_H
#define _LOCAL_MANAGER_H

#include "Main.h"
#include "Alarms.h"
#include "LMRecords.h"
#include "BaseManager.h"
#include "ClusterCom.h"
#include "VMap.h"
#include "WebPluginList.h"
#include "../wccp/Wccp.h"

#if !defined(WIN32)
#define ink_get_hrtime ink_get_hrtime_internal
#define ink_get_based_hrtime ink_get_based_hrtime_internal
#endif

extern LocalManager *lmgmt;

class LocalManager:public BaseManager
{

public:

  LocalManager(char *mpath, LMRecords * rd, bool proxy_on);
   ~LocalManager()
  {
    delete record_data;
    delete alarm_keeper;
    delete virt_map;
    delete ccom;
    if (config_path)
    {
      xfree(config_path);
    }
    if (bin_path)
    {
      xfree(bin_path);
    }
    if (absolute_proxy_binary) {
      xfree(absolute_proxy_binary);
    }
    if (proxy_name) {
      xfree(proxy_name);
    }
    if (proxy_binary) {
      xfree(proxy_binary);
    }
    if (proxy_options) {
      xfree(proxy_options);
    }
    if (env_prep) {
      xfree(env_prep);
    }
  };

  void initAlarm();
  void initCCom(int port, char *addr, int sport);
  void initMgmtProcessServer();
  void pollMgmtProcessServer();
  void handleMgmtMsgFromProcesses(MgmtMessageHdr * mh);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_str);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_raw, int data_len);
  void sendMgmtMsgToProcesses(MgmtMessageHdr * mh);

  void signalFileChange(const char *var_name);
  void signalEvent(int msg_id, const char *data_str);
  void signalEvent(int msg_id, const char *data_raw, int data_len);
  void signalAlarm(int alarm_id, const char *desc = NULL, const char *ip = NULL);

  void processEventQueue();
  bool startProxy();
  void listenForProxy();

  void mgmtCleanup();
  void mgmtShutdown(int status, bool mainThread = false);
  void processShutdown(bool mainThread = false);
  void processRestart();
  void processBounce();
  void rollLogFiles();
  void clearStats();

  bool processRunning();
  bool clusterOk();
  bool SetForDup(void *hIOCPort, long lTProcId, void *hTh);

  void tick()
  {
    ++internal_ticker;
  };
  void resetTicker()
  {
    internal_ticker = 0;
  }

  void syslogThrInit();

  volatile bool run_proxy;
#ifdef __alpha
  static bool clean_up;
#endif

  volatile time_t manager_started_at;
  volatile time_t proxy_started_at;
  volatile int proxy_launch_count;
  volatile bool proxy_launch_outstanding;
  volatile bool mgmt_shutdown_outstanding;
  volatile int proxy_running;
  volatile int proxy_server_port[MAX_PROXY_SERVER_PORTS];
  volatile char proxy_server_port_attributes[MAX_PROXY_SERVER_PORTS][MAX_ATTR_LEN];
  volatile int proxy_server_fd[MAX_PROXY_SERVER_PORTS];
  in_addr_t proxy_server_incoming_ip_to_bind;

  int process_server_timeout_secs;
  int process_server_timeout_msecs;

  char pserver_path[PATH_NAME_MAX];
  char *config_path;
  char *bin_path;
  char *absolute_proxy_binary;
  char *proxy_name;
  char *proxy_binary;
  char *proxy_options;
  char *env_prep;

#ifndef _WIN32
  int process_server_sockfd;
  volatile int watched_process_fd;
  volatile pid_t proxy_launch_pid;
#else
  bool process_server_connected;
  int proxy_valid_server_ports;
  HANDLE process_server_hpipe;
  HANDLE process_connect_hevent;
  HANDLE proxy_launch_hproc;
  HANDLE proxy_IOCPort;
#endif

  int mgmt_sync_key;

  Alarms *alarm_keeper;
  VMap *virt_map;

  ClusterCom *ccom;

  volatile int internal_ticker;
  volatile pid_t watched_process_pid;

  LMRecords *record_data;

#ifdef MGMT_USE_SYSLOG
  int syslog_facility;
#endif

  WebPluginList plugin_list;
  wccp::Cache wccp_cache;

private:

};                              /* End class LocalManager */

#if TS_USE_POSIX_CAP
bool elevateFileAccess(bool);
#else
bool restoreRootPriv(uid_t *old_euid = NULL);
bool removeRootPriv(uid_t euid);
#endif

#endif /* _LOCAL_MANAGER_H */
