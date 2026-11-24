/** @file

  Hold a transaction's mutex on a task thread for a while after its request is sent to the origin.

  Plugins commonly create a continuation with the transaction's mutex and schedule it on another
  thread. While such a continuation runs, every try-lock of that mutex on the transaction's own
  thread fails, so events for the transaction are retried later instead of delivered at once. This
  plugin makes that happen on demand, for the requests whose path starts with a given prefix.

  Usage in plugin.config: hold_txn_mutex.so <path prefix, no leading slash> <delay ms> <hold ms>

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

#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace
{
char const PLUGIN_NAME[] = "hold_txn_mutex";

DbgCtl dbg_ctl{PLUGIN_NAME};

std::string prefix;
int         delay_ms = 0;
int         hold_ms  = 0;

/** Runs on a task thread with the transaction's mutex held, and keeps it for hold_ms. */
int
hold_handler(TSCont contp, TSEvent /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  auto *txn_id = static_cast<uint64_t *>(TSContDataGet(contp));
  Dbg(dbg_ctl, "[%" PRIu64 "] holding the transaction mutex for %d ms", *txn_id, hold_ms);
  std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
  Dbg(dbg_ctl, "[%" PRIu64 "] releasing the transaction mutex", *txn_id);
  delete txn_id;
  // The continuation keeps its own reference to the mutex, so this is safe even if the
  // transaction has finished in the meantime.
  TSContDestroy(contp);
  return 0;
}

bool
path_matches(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    return false;
  }
  bool   matches = false;
  TSMLoc url_loc;
  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) == TS_SUCCESS) {
    int         length = 0;
    char const *path   = TSUrlPathGet(bufp, url_loc, &length);
    matches            = path != nullptr && std::string_view{path, static_cast<size_t>(length)}.starts_with(prefix);
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return matches;
}

int
global_handler(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  auto txnp = static_cast<TSHttpTxn>(edata);
  if (event == TS_EVENT_HTTP_SEND_REQUEST_HDR && path_matches(txnp)) {
    // A transaction is a continuation, so this is its mutex.
    TSCont hold = TSContCreate(hold_handler, TSContMutexGet(reinterpret_cast<TSCont>(txnp)));
    TSContDataSet(hold, new uint64_t{TSHttpTxnIdGet(txnp)});
    Dbg(dbg_ctl, "[%" PRIu64 "] will hold the transaction mutex in %d ms", TSHttpTxnIdGet(txnp), delay_ms);
    TSContScheduleOnPool(hold, delay_ms, TS_THREAD_POOL_TASK);
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
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

  if (argc != 4) {
    TSError("[%s] usage: %s <path prefix> <delay ms> <hold ms>", PLUGIN_NAME, PLUGIN_NAME);
    return;
  }
  prefix   = argv[1];
  delay_ms = std::atoi(argv[2]);
  hold_ms  = std::atoi(argv[3]);

  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, TSContCreate(global_handler, nullptr));
}
