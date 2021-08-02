/** @file

  Process Manager Class, derived from BaseManager. Class provides callback
  registration for management events as well as the interface to the outside
  world.

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

#include <functional>
#include <string_view>

#include <ts/apidefs.h>

#include "MgmtUtils.h"
#include "BaseManager.h"
#include "tscore/ink_sock.h"
#include "tscore/ink_llqueue.h"
#include "tscore/ink_apidefs.h"

#if HAVE_EVENTFD
#include <sys/eventfd.h>
#endif

class ConfigUpdateCbTable;

class ProcessManager : public BaseManager
{
public:
  ProcessManager(bool rlm);
  ~ProcessManager();

  // Start a thread for the process manager. If @a cb is set then it
  // is called after the thread is started and before any messages are
  // processed.
  void start(std::function<TSThread()> const &cb_init        = std::function<TSThread()>(),
             std::function<void(TSThread)> const &cb_destroy = std::function<void(TSThread)>());

  // Stop the process manager, dropping any unprocessed messages.
  void stop();

  void signalConfigFileChild(const char *parent, const char *child);
  void signalManager(int msg_id, const char *data_str);
  void signalManager(int msg_id, const char *data_raw, int data_len);

  /** Send a management message of type @a msg_id with @a text.
   *
   * @param msg_id ID for the message.
   * @param text Content for the message.
   *
   * A terminating null character is added automatically.
   */
  void signalManager(int msg_id, std::string_view text);

  void signalManager(MgmtMessageHdr *mh);

  void reconfigure();
  void initLMConnection();
  void handleMgmtMsgFromLM(MgmtMessageHdr *mh);

  void
  registerPluginCallbacks(ConfigUpdateCbTable *_cbtable)
  {
    cbtable = _cbtable;
  }

private:
  int pollLMConnection();
  int processSignalQueue();
  bool processEventQueue();

  bool require_lm;
  RecInt timeout;
  LLQ *mgmt_signal_queue;
  pid_t pid;

  ink_thread poll_thread = ink_thread_null();
  int running            = 0;

  /// Thread initialization callback.
  /// This allows @c traffic_server and @c traffic_manager to perform different initialization in the thread.
  std::function<TSThread()> init;
  std::function<void(TSThread)> destroy;
  TSThread managerThread = nullptr;

  int local_manager_sockfd;
#if HAVE_EVENTFD
  int wakeup_fd; // external trigger to stop polling
#endif
  ConfigUpdateCbTable *cbtable;
  int max_msgs_in_a_row;

  static const int MAX_MSGS_IN_A_ROW = 10000;
  static void *processManagerThread(void *arg);
};

extern ProcessManager *pmgmt;
