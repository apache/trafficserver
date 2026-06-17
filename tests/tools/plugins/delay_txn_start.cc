/** @file

  Delay transaction start hook completion for HTTP/2 read gating tests.

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

#include <cstdlib>

namespace
{
char const PLUGIN_NAME[] = "delay_txn_start";

int delay_ms = 500;

struct TxnState {
  TSHttpTxn txn   = nullptr;
  TSAction action = nullptr;
  bool reenabled  = false;
};

int
txn_handler(TSCont contp, TSEvent event, void *edata)
{
  auto *state = static_cast<TxnState *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_TIMEOUT:
    state->action    = nullptr;
    state->reenabled = true;
    TSDebug(PLUGIN_NAME, "delayed TXN_START reenable");
    TSHttpTxnReenable(state->txn, TS_EVENT_HTTP_CONTINUE);
    break;
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    if (!state->reenabled) {
      TSError("[%s] READ_REQUEST_HDR before delayed TXN_START reenable", PLUGIN_NAME);
      TSReleaseAssert(state->reenabled);
    }
    TSDebug(PLUGIN_NAME, "READ_REQUEST_HDR after delayed TXN_START reenable");
    auto txnp = static_cast<TSHttpTxn>(edata);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }
  case TS_EVENT_HTTP_TXN_CLOSE: {
    auto txnp = static_cast<TSHttpTxn>(edata);

    if (state->action != nullptr && !TSActionDone(state->action)) {
      TSActionCancel(state->action);
    }
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    TSContDataSet(contp, nullptr);
    delete state;
    TSContDestroy(contp);
    break;
  }
  default:
    TSError("[%s] unexpected event: %d", PLUGIN_NAME, event);
    break;
  }

  return 0;
}

int
global_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (event == TS_EVENT_HTTP_TXN_START) {
    auto txnp  = static_cast<TSHttpTxn>(edata);
    auto contp = TSContCreate(txn_handler, TSMutexCreate());
    auto state = new TxnState;

    state->txn = txnp;
    TSContDataSet(contp, state);
    TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);
    state->action = TSContScheduleOnPool(contp, delay_ms, TS_THREAD_POOL_TASK);
  }

  return 0;
}
} // namespace

void
TSPluginInit(int argc, const char **argv)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (argc > 1) {
    delay_ms = std::atoi(argv[1]);
  }

  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, TSContCreate(global_handler, TSMutexCreate()));
}
