/**
  @file
  @brief Plugin that holds its continuation's mutex in a transaction hook while
  a plugin message is delivered to the same continuation.

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
#include <cstdlib>
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
// message thread goes straight from the watcher to the shared continuation, so
// this only has to outlast that step.
static constexpr auto HOLD_AFTER_MESSAGE = std::chrono::seconds(2);

static std::atomic<bool> hook_holds_mutex{false};
static std::atomic<bool> message_during_hold{false};

static int stat_delivered = -1;

static bool
is_our_message(void *edata)
{
  auto const *msg = static_cast<TSPluginMsg const *>(edata);
  return msg->tag != nullptr && strcmp(msg->tag, PLUGIN_NAME) == 0;
}

// This continuation has its own mutex and its message hook is added first, so
// it sees each message before the shared continuation does, even while the
// transaction hook holds the shared mutex.
static int
watcher_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_MSG && is_our_message(edata) && hook_holds_mutex.load()) {
    TSDebug(PLUGIN_NAME, "message arrived while the transaction hook holds the shared mutex");
    message_during_hold = true;
  }
  return 0;
}

// One continuation, one mutex, for both an event thread hook and the message
// hook, the way a plugin that keeps shared state behind its mutex is written.
static int
shared_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

    hook_holds_mutex = true;
    TSDebug(PLUGIN_NAME, "holding the shared mutex until a message arrives");
    auto const deadline = std::chrono::steady_clock::now() + MESSAGE_WAIT;
    while (!message_during_hold && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (message_during_hold) {
      std::this_thread::sleep_for(HOLD_AFTER_MESSAGE);
    }
    hook_holds_mutex = false;
    TSDebug(PLUGIN_NAME, "releasing the shared mutex");

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  } break;

  case TS_EVENT_LIFECYCLE_MSG:
    if (is_our_message(edata) && message_during_hold.exchange(false)) {
      TSDebug(PLUGIN_NAME, "message delivered after the hold");
      TSStatIntIncrement(stat_delivered, 1);
    }
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

  TSCont watcher = TSContCreate(watcher_handler, TSMutexCreate());
  TSCont shared  = TSContCreate(shared_handler, TSMutexCreate());
  if (watcher == nullptr || shared == nullptr) {
    TSError("[%s] could not create continuations.", PLUGIN_NAME);
    std::abort();
  }

  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, watcher);
  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, shared);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, shared);
}
