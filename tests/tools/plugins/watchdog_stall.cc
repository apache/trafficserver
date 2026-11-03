/** @file

  Test plugin that blocks the event thread handling a request, then answers it locally.

  A request carrying `X-Stall-Ms: <n>` blocks its ET_NET thread for n milliseconds, which is what makes
  that thread's awake span grow for the watchdog. Every request is answered with a local 200, so no
  origin is needed.

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
#include <cstdlib>
#include <string>
#include <thread>

namespace
{
char const PLUGIN_NAME[]  = "watchdog_stall";
char const STALL_HEADER[] = "X-Stall-Ms";
char const BODY[]         = "ok\n";

// The error path discards a plugin-set status below 400 and answers 500, so the reply must be a 4xx to survive. 418 is
// used because no real response from the proxy would carry it, which lets tests tell this reply from an actual error.
constexpr auto SERVED_STATUS = static_cast<TSHttpStatus>(418);

DbgCtl dbg_ctl{PLUGIN_NAME};

int
stall_ms(TSMBuffer bufp, TSMLoc hdr_loc)
{
  TSMLoc field = TSMimeHdrFieldFind(bufp, hdr_loc, STALL_HEADER, sizeof(STALL_HEADER) - 1);
  if (field == TS_NULL_MLOC) {
    return 0;
  }

  int         length = 0;
  char const *value  = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field, 0, &length);
  int         ms     = value != nullptr ? std::atoi(std::string(value, length).c_str()) : 0;

  TSHandleMLocRelease(bufp, hdr_loc, field);
  return ms;
}

int
handle_read_request(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_READ_REQUEST_HDR) {
    return 0;
  }

  auto      txnp    = static_cast<TSHttpTxn>(edata);
  TSMBuffer bufp    = nullptr;
  TSMLoc    hdr_loc = TS_NULL_MLOC;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) == TS_SUCCESS) {
    int const ms = stall_ms(bufp, hdr_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    if (ms > 0) {
      Dbg(dbg_ctl, "blocking this event thread for %d ms", ms);
      std::this_thread::sleep_for(std::chrono::milliseconds{ms});
    }
  }

  TSHttpTxnStatusSet(txnp, SERVED_STATUS);
  TSHttpTxnErrorBodySet(txnp, TSstrdup(BODY), sizeof(BODY) - 1, TSstrdup("text/plain"));
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, char const * /* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache Software Foundation");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(handle_read_request, nullptr));
}
