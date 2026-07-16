/** @file

  Test plugin for the TSHttpTxnClientPacketMarkSet API.

  On each request it reads a target mark from request headers, applies it to the
  client-side connection via TSHttpTxnClientPacketMarkSet, then reads the mark
  back off the client socket with getsockopt(SO_MARK) and echoes the observed
  value into the X-Client-Packet-Mark response header for the AuTest to assert
  on.

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
constexpr char PLUGIN_NAME[] = "client_packet_mark";
constexpr char MARK_HEADER[] = "X-Set-Mark";
constexpr char ECHO_HEADER[] = "X-Client-Packet-Mark";

DbgCtl dbg_ctl{PLUGIN_NAME};

int
handle_send_response(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    // The client connection is live here; this applies the mark to it and reads
    // it back off the client socket.
    packet_mark::LogContext log{PLUGIN_NAME, dbg_ctl};
    packet_mark::apply_client_mark(log, txnp, MARK_HEADER);
    packet_mark::echo_client_mark(log, txnp, ECHO_HEADER);
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

  TSCont contp = TSContCreate(handle_send_response, nullptr);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
