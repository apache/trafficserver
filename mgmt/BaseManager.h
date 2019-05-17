/** @file

  Base Manager Class, base class for all managers.

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

#include <list>
#include <queue>
#include <mutex>
#include <unordered_map>

#include "tscore/ink_thread.h"
#include "tscore/ink_mutex.h"
#include "tscpp/util/MemSpan.h"

#include "MgmtDefs.h"
#include "MgmtMarshall.h"

/*
 * MgmtEvent defines.
 */

// Event flows: traffic manager -> traffic server
#define MGMT_EVENT_SYNC_KEY 10000
#define MGMT_EVENT_SHUTDOWN 10001
#define MGMT_EVENT_RESTART 10002
#define MGMT_EVENT_BOUNCE 10003
#define MGMT_EVENT_CLEAR_STATS 10004
#define MGMT_EVENT_CONFIG_FILE_UPDATE 10005
#define MGMT_EVENT_PLUGIN_CONFIG_UPDATE 10006
#define MGMT_EVENT_ROLL_LOG_FILES 10008
#define MGMT_EVENT_LIBRECORDS 10009
#define MGMT_EVENT_CONFIG_FILE_UPDATE_NO_INC_VERSION 10010
// cache storage operations - each is a distinct event.
// this is done because the code paths share nothing but boilerplate logic
// so it's easier to do this than to try to encode an opcode and yet another
// case statement.
#define MGMT_EVENT_STORAGE_DEVICE_CMD_OFFLINE 10011
#define MGMT_EVENT_LIFECYCLE_MESSAGE 10012
#define MGMT_EVENT_DRAIN 10013
#define MGMT_EVENT_HOST_STATUS_UP 10014
#define MGMT_EVENT_HOST_STATUS_DOWN 10015

/***********************************************************************
 *
 * MODULARIZATION: if you are adding new signals, please ensure to add
 *                 the corresponding signals in librecords/I_RecSignals.h
 *
 *
 ***********************************************************************/

// Signal flows: traffic server -> traffic manager
#define MGMT_SIGNAL_PID 0

#define MGMT_SIGNAL_PROXY_PROCESS_DIED 1
#define MGMT_SIGNAL_PROXY_PROCESS_BORN 2
#define MGMT_SIGNAL_CONFIG_ERROR 3
#define MGMT_SIGNAL_SYSTEM_ERROR 4
#define MGMT_SIGNAL_CACHE_ERROR 5
#define MGMT_SIGNAL_CACHE_WARNING 6
#define MGMT_SIGNAL_LOGGING_ERROR 7
#define MGMT_SIGNAL_LOGGING_WARNING 8
#define MGMT_SIGNAL_PLUGIN_SET_CONFIG 9

// This are additional on top of the ones defined in Alarms.h. Que?
#define MGMT_SIGNAL_LIBRECORDS 10
#define MGMT_SIGNAL_CONFIG_FILE_CHILD 11

struct MgmtMessageHdr {
  int msg_id;
  int data_len;
  ts::MemSpan<void>
  payload()
  {
    return {this + 1, static_cast<size_t>(data_len)};
  }
};

class BaseManager
{
  using MgmtCallbackList = std::list<MgmtCallback>;

public:
  BaseManager();

  ~BaseManager();

  /** Associate a callback function @a func with message identifier @a msg_id.
   *
   * @param msg_id Message identifier for the callback.
   * @param func The callback function.
   * @return @a msg_id on success, -1 on failure.
   *
   * @a msg_id should be one of the @c MGMT_EVENT_... values.
   *
   * If a management message with @a msg is received, the callbacks for that message id
   * are invoked and passed the message payload (not including the header).
   */
  int registerMgmtCallback(int msg_id, MgmtCallback const &func);

  /// Add a @a msg to the queue.
  /// This must be the entire message as read off the wire including the header.
  void enqueue(MgmtMessageHdr *msg);

  /// Current size of the queue.
  /// @note This does not block on the semaphore.
  bool queue_empty();

  /// Dequeue a msg.
  /// This waits on the semaphore for a message to arrive.
  MgmtMessageHdr *dequeue();

protected:
  void executeMgmtCallback(int msg_id, ts::MemSpan<void> span);

  /// The mapping from an event type to a list of callbacks to invoke.
  std::unordered_map<int, MgmtCallbackList> mgmt_callback_table;

  /// Message queue.
  // These holds the entire message object, including the header.
  std::queue<MgmtMessageHdr *> queue;
  /// Locked access to the queue.
  std::mutex q_mutex;
  /// Semaphore to signal queue state.
  ink_semaphore q_sem;
};
