/** @file

  Test plugin for the TSHttpTxnServerPacketMarkSet API.

  The plugin exercises both halves of the TSHttpTxnServerPacketMarkSet contract,
  selected by request header:

    - X-Set-Mark: applied at TS_HTTP_READ_RESPONSE_HDR_HOOK, when the origin
      connection is already live. This tests the "apply to the live server
      connection immediately" path.

    - X-Set-Mark-Preconnect: applied at TS_HTTP_READ_REQUEST_HDR_HOOK, before an
      origin connection exists. This tests the server-only "record the mark so a
      future origin connection is opened with it" path -- there is no live vc to
      apply to at that point, so the mark reaches the socket only via the
      transaction config seed.

  Regardless of which header drove the set, the readback happens at
  TS_HTTP_READ_RESPONSE_HDR_HOOK: the origin fd is valid there and the server
  response headers -- which propagate to the client response -- carry the
  observed value (echoed into X-Server-Packet-Mark) back to curl.

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

#include "packet_mark_common.h"

#include <ts/ts.h>

#include <string_view>

namespace
{
constexpr char PLUGIN_NAME[]            = "server_packet_mark";
constexpr char MARK_HEADER[]            = "X-Set-Mark";
constexpr char PRECONNECT_MARK_HEADER[] = "X-Set-Mark-Preconnect";
constexpr char ECHO_HEADER[]            = "X-Server-Packet-Mark";

DbgCtl dbg_ctl{PLUGIN_NAME};

int
handle_read_request(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    // No origin connection exists yet; this exercises the "seed the mark for a
    // future server connection" half of the contract.
    packet_mark::LogContext log{PLUGIN_NAME, dbg_ctl};
    packet_mark::apply_server_mark(log, txnp, PRECONNECT_MARK_HEADER);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

int
handle_read_response(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    // The origin connection is live here; this applies the mark to it and reads
    // it back off the server socket.
    packet_mark::LogContext log{PLUGIN_NAME, dbg_ctl};
    packet_mark::apply_server_mark(log, txnp, MARK_HEADER);
    packet_mark::echo_server_mark(log, txnp, ECHO_HEADER);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // anonymous namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(handle_read_request, nullptr));
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(handle_read_response, nullptr));
}
