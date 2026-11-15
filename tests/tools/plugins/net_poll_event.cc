/** @file

  Put a poll event on every ET_NET thread, the way a QUIC build does.

  A thread with a poll event runs its immediate events a second time before it
  does network I/O. Tests load this plugin to get that ordering on a build
  without QUIC.

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

#include <ts/ts.h>

namespace
{
char const PLUGIN_NAME[] = "net_poll_event";

DbgCtl dbg_ctl{PLUGIN_NAME};

int
poll_handler(TSCont /* contp ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  return 0;
}

int
lifecycle_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void * /* edata ATS_UNUSED */)
{
  if (event == TS_EVENT_LIFECYCLE_TASK_THREADS_READY) {
    // A negative period makes these poll events. The entire-pool schedule
    // requires a continuation without a mutex.
    auto const actions = TSContScheduleEveryOnEntirePool(TSContCreate(poll_handler, nullptr), -1, TS_THREAD_POOL_NET);
    Dbg(dbg_ctl, "scheduled a poll event on %zu ET_NET threads", actions.size());
  }
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }
  TSLifecycleHookAdd(TS_LIFECYCLE_TASK_THREADS_READY_HOOK, TSContCreate(lifecycle_handler, TSMutexCreate()));
}
