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

#include "Alarms.h"
#include "BaseManager.h"
#include <records/I_RecHttp.h>
#if TS_HAS_WCCP
#include <wccp/Wccp.h>
#endif

class FileManager;
class ClusterCom;
class VMap;

class LocalManager: public BaseManager
{
public:
  explicit LocalManager(bool proxy_on);
  ~LocalManager();

  void initAlarm();
  void initCCom(const AppVersionInfo& version, FileManager * files, int mcport, char *addr, int rsport);
  void initMgmtProcessServer();
  void pollMgmtProcessServer();
  void handleMgmtMsgFromProcesses(MgmtMessageHdr * mh);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_str);
  void sendMgmtMsgToProcesses(int msg_id, const char *data_raw, int data_len);
  void sendMgmtMsgToProcesses(MgmtMessageHdr * mh);

  void signalFileChange(const char *var_name, bool incVersion = true);
  void signalEvent(int msg_id, const char *data_str);
  void signalEvent(int msg_id, const char *data_raw, int data_len);
  void signalAlarm(int alarm_id, const char *desc = NULL, const char *ip = NULL);

  void processEventQueue();
  bool startProxy();
  void listenForProxy();
  void bindProxyPort(HttpProxyPort&);
  void closeProxyPorts();

  void mgmtCleanup();
  void mgmtShutdown(bool mainThread = false);
  void processShutdown(bool mainThread = false);
  void processRestart();
  void processBounce();
  void rollLogFiles();
  void clearStats(const char *name = NULL);

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
  volatile time_t manager_started_at;
  volatile time_t proxy_started_at;
  volatile int proxy_launch_count;
  volatile bool proxy_launch_outstanding;
  volatile bool mgmt_shutdown_outstanding;
  volatile int proxy_running;
  HttpProxyPort::Group m_proxy_ports;
  // Local inbound addresses to bind, if set.
  IpAddr m_inbound_ip4;
  IpAddr m_inbound_ip6;

  int process_server_timeout_secs;
  int process_server_timeout_msecs;

  char *absolute_proxy_binary;
  char *proxy_name;
  char *proxy_binary;
  char *proxy_options;
  char *env_prep;

  int process_server_sockfd;
  volatile int watched_process_fd;
  volatile pid_t proxy_launch_pid;

  int mgmt_sync_key;

  Alarms *alarm_keeper;
  VMap *virt_map;
  FileManager *configFiles;

  ClusterCom *ccom;

  volatile int internal_ticker;
  volatile pid_t watched_process_pid;

  int syslog_facility;

#if TS_HAS_WCCP
  wccp::Cache wccp_cache;
# endif
private:
};                              /* End class LocalManager */

extern LocalManager *lmgmt;

#endif /* _LOCAL_MANAGER_H */
