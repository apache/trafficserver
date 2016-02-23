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
 * ProcessManager.h
 *   Process Manager Class, derived from BaseManager. Class provides callback
 * registration for management events as well as the interface to the outside
 * world.
 *
 * $Date: 2003-06-01 18:37:18 $
 *
 *
 */

#ifndef _PROCESS_MANAGER_H
#define _PROCESS_MANAGER_H

#include "MgmtUtils.h"
#include "BaseManager.h"
#include "ts/ink_sock.h"

#include "ts/ink_apidefs.h"

class ConfigUpdateCbTable;

void *startProcessManager(void *arg);
class ProcessManager : public BaseManager
{
public:
  ProcessManager(bool rlm);
  ~ProcessManager()
  {
    close_socket(local_manager_sockfd);
    while (!queue_is_empty(mgmt_signal_queue)) {
      char *sig = (char *)dequeue(mgmt_signal_queue);
      ats_free(sig);
    }
    ats_free(mgmt_signal_queue);
  }

  void
  start()
  {
    ink_thread_create(startProcessManager, 0);
  }

  void
  stop()
  {
    mgmt_log(stderr, "[ProcessManager::stop] Bringing down connection\n");
    close_socket(local_manager_sockfd);
  }

  inkcoreapi void signalConfigFileChild(const char *parent, const char *child, unsigned int options);
  inkcoreapi void signalManager(int msg_id, const char *data_str);
  inkcoreapi void signalManager(int msg_id, const char *data_raw, int data_len);

  void reconfigure();
  void initLMConnection();
  void pollLMConnection();
  void handleMgmtMsgFromLM(MgmtMessageHdr *mh);

  bool processEventQueue();
  bool processSignalQueue();

  void
  registerPluginCallbacks(ConfigUpdateCbTable *_cbtable)
  {
    cbtable = _cbtable;
  }

  bool require_lm;
  time_t timeout;

  LLQ *mgmt_signal_queue;

  pid_t pid;

  int local_manager_sockfd;

private:
  static const int MAX_MSGS_IN_A_ROW = 10000;

  ConfigUpdateCbTable *cbtable;
}; /* End class ProcessManager */

inkcoreapi extern ProcessManager *pmgmt;

#endif /* _PROCESS_MANAGER_H */
