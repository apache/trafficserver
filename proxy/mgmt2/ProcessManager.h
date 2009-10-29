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

#include "ProcessRecords.h"
#include "BaseManager.h"
#include "ink_sock.h"

#include "ink_apidefs.h"

void *startProcessManager(void *arg);
class ProcessManager:public BaseManager
{

public:

  ProcessManager(bool rlm, char *mpath, ProcessRecords * rd);
   ~ProcessManager()
  {
    delete record_data;
#ifndef _WIN32
      ink_close_socket(local_manager_sockfd);
#else
      CloseHandle(local_manager_hpipe);
#endif
    while (!queue_is_empty(mgmt_signal_queue))
    {
      char *sig = (char *) dequeue(mgmt_signal_queue);
        xfree(sig);
    }
    xfree(mgmt_signal_queue);
  };

  void start()
  {
    ink_thread_create(startProcessManager, 0);
  };

  void stop()
  {
    mgmt_log(stderr, "[ProcessManager::stop] Bringing down connection\n");
#ifndef _WIN32
    ink_close_socket(local_manager_sockfd);
#else
    CloseHandle(local_manager_hpipe);
#endif
  };

  inkcoreapi void signalManager(int msg_id, const char *data_str);
  inkcoreapi void signalManager(int msg_id, const char *data_raw, int data_len);

  void reconfigure();
  void initLMConnection();
  void pollLMConnection();
  void handleMgmtMsgFromLM(MgmtMessageHdr * mh);

  bool processEventQueue();
  bool processSignalQueue();

  /*
   * addPlugin*(...)
   *   Functions for adding plugin defined variables.
   *
   * Returns:    true if sucessful
   *             false otherwise
   */
  bool addPluginCounter(const char *name, MgmtIntCounter value);
  bool addPluginInteger(const char *name, MgmtInt value);
  bool addPluginFloat(const char *name, MgmtFloat value);
  bool addPluginString(const char *name, MgmtString value);

  bool require_lm;
  time_t timeout;
  char pserver_path[1024];
  int mgmt_sync_key;
  ProcessRecords *record_data;

  LLQ *mgmt_signal_queue;

#ifndef _WIN32
  int local_manager_sockfd;
#else
  HANDLE local_manager_hpipe;
#endif

private:

  /*
   * You should not be concerned what is under the covers.
   */

};                              /* End class ProcessManager */

#ifndef _PROCESS_MANAGER
#define _PROCESS_MANAGER
inkcoreapi extern ProcessManager *pmgmt;
#endif /* _PROCESS_MANAGER */

#endif /* _PROCESS_MANAGER_H */
