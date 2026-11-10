/**
  @file
  @brief Test plugin whose transaction hook holds a continuation's mutex while a
  plugin message for that continuation arrives on the process manager thread.

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
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>
#include <ts/ts.h>

#define PLUGIN_NAME "msg_held_mutex"

static char plugin_name[]   = PLUGIN_NAME;
static char vendor_name[]   = "Apache";
static char support_email[] = "dev@trafficserver.apache.org";

// How long the transaction hook waits for a message before giving up, so a
// test that never sends one still finishes.
static constexpr auto MESSAGE_WAIT = std::chrono::seconds(20);

// How long the hook keeps the mutex after the watcher sees a message. The
// process manager thread goes straight from the watcher to the shared
// continuation, so this only has to outlast that step, with room for a loaded
// host.
static constexpr auto HOLD_AFTER_MESSAGE = std::chrono::seconds(5);

// The state is built for one held transaction per traffic_server process.
// Written by the transaction hook while it holds the shared mutex.
static std::atomic<bool> hook_holds_mutex{false};
// Set by the watcher on the process manager thread, read by the transaction
// hook.
static std::atomic<bool> message_during_hold{false};
// Set by the transaction hook once it has kept the mutex for
// HOLD_AFTER_MESSAGE, and consumed by the shared continuation's message
// handler, so only a message that met the held mutex is counted.
static std::atomic<bool> held_for_message{false};

static int stat_delivered = -1;

static bool
is_our_message(void *edata)
{
  auto const *msg = static_cast<TSPluginMsg const *>(edata);
  return msg->tag != nullptr && strcmp(msg->tag, PLUGIN_NAME) == 0;
}

// This continuation has its own mutex and its message hook is added first, so
// it sees each message before the shared continuation does, even while the
// transaction hook holds the shared mutex. Its flag tells the hook that the
// process manager thread is walking the hook list and the shared continuation
// is next.
static int
watcher_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_MSG && is_our_message(edata) && hook_holds_mutex.load()) {
    TSDebug(PLUGIN_NAME, "message arrived while the transaction hook holds the shared mutex");
    message_during_hold.store(true);
  }
  return 0;
}

// One continuation, one mutex, for both an event thread hook and the message
// hook, the way a plugin that keeps shared state behind its mutex is written.
static int
shared_handler(TSCont contp, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

    hook_holds_mutex.store(true);
    TSDebug(PLUGIN_NAME, "holding the shared mutex until a message arrives");
    auto const deadline = std::chrono::steady_clock::now() + MESSAGE_WAIT;
    while (!message_during_hold.load() && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (message_during_hold.load()) {
      std::this_thread::sleep_for(HOLD_AFTER_MESSAGE);
      held_for_message.store(true);
    } else {
      TSError("[%s] no message arrived while the transaction hook held the shared mutex", PLUGIN_NAME);
    }
    hook_holds_mutex.store(false);
    TSDebug(PLUGIN_NAME, "releasing the shared mutex");

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_LIFECYCLE_MSG:
    if (is_our_message(edata) && held_for_message.exchange(false)) {
      TSDebug(PLUGIN_NAME, "message delivered after the hold");
      // Raw stats are per thread and summed only over the event processor
      // threads, so an increment on the process manager thread is never
      // counted.
      TSContScheduleOnPool(contp, 0, TS_THREAD_POOL_NET);
    }
    break;

  case TS_EVENT_IMMEDIATE:
    TSStatIntIncrement(stat_delivered, 1);
    break;

  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = plugin_name;
  info.vendor_name   = vendor_name;
  info.support_email = support_email;

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
    return;
  }

  stat_delivered = TSStatCreate(PLUGIN_NAME ".msg.delivered", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  if (stat_delivered == TS_ERROR) {
    TSError("[%s] could not create the delivery stat.", PLUGIN_NAME);
    return;
  }

  TSCont watcher = TSContCreate(watcher_handler, TSMutexCreate());
  TSCont shared  = TSContCreate(shared_handler, TSMutexCreate());

  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, watcher);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, shared);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, shared);
}
