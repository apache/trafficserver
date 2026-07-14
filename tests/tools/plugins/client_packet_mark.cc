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

#include <ts/ts.h>

extern "C" {
#include <sys/socket.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view PLUGIN_NAME = "client_packet_mark";
constexpr std::string_view MARK_HEADER = "X-Set-Mark";
constexpr std::string_view ECHO_HEADER = "X-Client-Packet-Mark";

DbgCtl dbg_ctl{PLUGIN_NAME.data()};

/** Read a header field and interpret its value as a 32-bit unsigned quantity.

    Values are parsed with strtoul (base 0), so "0x0000000A" and "10" are both
    accepted. Returns std::nullopt if the header is absent. */
std::optional<uint32_t>
get_uint_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view header)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header.data(), static_cast<int>(header.length()));
  if (field_loc == TS_NULL_MLOC) {
    return std::nullopt;
  }

  int         value_len = 0;
  const char *value_str = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &value_len);
  uint32_t    result    = 0;
  if (value_str != nullptr && value_len > 0) {
    std::string value(value_str, value_len);
    result = static_cast<uint32_t>(strtoul(value.c_str(), nullptr, 0));
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return result;
}

/** Create the echo header on the response with the value formatted as 0x%08x. */
void
set_echo_header(TSMBuffer bufp, TSMLoc hdr_loc, uint32_t value)
{
  // 0x + 8 hex digits for a uint32_t + NUL = 11 bytes; 16 is comfortably enough.
  char formatted[16];
  std::snprintf(formatted, sizeof(formatted), "0x%08x", value);

  TSMLoc field_loc = TS_NULL_MLOC;
  if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, ECHO_HEADER.data(), static_cast<int>(ECHO_HEADER.length()), &field_loc) ==
      TS_SUCCESS) {
    // -1 length lets the API strlen the null-terminated buffer, so we do not
    // rely on snprintf's return value (which is the would-be length, not the
    // truncated length) as a byte count.
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, formatted, -1);
    TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
}

int
handle_send_response(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event != TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMBuffer req_bufp = nullptr;
  TSMLoc    req_loc  = TS_NULL_MLOC;
  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) != TS_SUCCESS) {
    TSError("[%s] Failed to get client request headers", PLUGIN_NAME.data());
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  std::optional<uint32_t> mark = get_uint_header(req_bufp, req_loc, MARK_HEADER);
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);

  if (mark.has_value()) {
    Dbg(dbg_ctl, "Setting client packet mark to 0x%08x", *mark);
    TSHttpTxnClientPacketMarkSet(txnp, static_cast<int>(*mark));
  }

  uint32_t observed = 0;
#if defined(SO_MARK)
  int client_fd = -1;
  if (TSHttpTxnClientFdGet(txnp, &client_fd) == TS_SUCCESS && client_fd >= 0) {
    socklen_t optlen = sizeof(observed);
    if (getsockopt(client_fd, SOL_SOCKET, SO_MARK, &observed, &optlen) != 0) {
      TSError("[%s] getsockopt(SO_MARK) failed on fd %d", PLUGIN_NAME.data(), client_fd);
    }
  } else {
    TSError("[%s] Failed to obtain client fd", PLUGIN_NAME.data());
  }
#else
  // SO_MARK is Linux-only. On other platforms the accompanying AuTest is skipped
  // via Test.SkipUnless, so this readback path is never exercised; keep it
  // compilable so the plugin still builds everywhere.
  TSError("[%s] SO_MARK is not supported on this platform", PLUGIN_NAME.data());
#endif

  TSMBuffer resp_bufp = nullptr;
  TSMLoc    resp_loc  = TS_NULL_MLOC;
  if (TSHttpTxnClientRespGet(txnp, &resp_bufp, &resp_loc) == TS_SUCCESS) {
    set_echo_header(resp_bufp, resp_loc, observed);
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);
  } else {
    TSError("[%s] Failed to get client response headers", PLUGIN_NAME.data());
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // anonymous namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME.data();
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME.data());
    return;
  }

  TSCont contp = TSContCreate(handle_send_response, nullptr);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}
