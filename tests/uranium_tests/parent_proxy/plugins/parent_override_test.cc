/** @file

  A test plugin that routes a transaction to a plugin-chosen parent.

  The X-Parent-Override request header selects the API:
    proxy-set       - TSHttpTxnParentProxySet()
    response-action - TSHttpTxnResponseActionSet()

  Either way the parent is 127.0.0.1:1, where nothing listens, so a request
  routed to it fails with a 502.

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

#include <string_view>

namespace
{
constexpr char             PLUGIN_NAME[]   = "parent_override_test";
constexpr char             DEAD_PARENT[]   = "127.0.0.1";
constexpr int              DEAD_PORT       = 1;
constexpr std::string_view OVERRIDE_HEADER = "X-Parent-Override";

DbgCtl dbg_ctl{PLUGIN_NAME};

enum class OverrideMode { NONE, PROXY_SET, RESPONSE_ACTION };

OverrideMode
override_mode(TSHttpTxn txnp)
{
  TSMBuffer    bufp    = nullptr;
  TSMLoc       hdr_loc = TS_NULL_MLOC;
  OverrideMode mode    = OverrideMode::NONE;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    return mode;
  }

  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, OVERRIDE_HEADER.data(), static_cast<int>(OVERRIDE_HEADER.size()));
  if (field_loc != TS_NULL_MLOC) {
    int         len   = 0;
    char const *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);

    if (value != nullptr) {
      std::string_view const header_value{value, static_cast<size_t>(len)};

      if (header_value == "proxy-set") {
        mode = OverrideMode::PROXY_SET;
      } else if (header_value == "response-action") {
        mode = OverrideMode::RESPONSE_ACTION;
      }
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  return mode;
}

int
handle_read_request(TSCont /* contp ATS_UNUSED */, TSEvent /* event ATS_UNUSED */, void *edata)
{
  TSHttpTxn    txnp = static_cast<TSHttpTxn>(edata);
  OverrideMode mode = override_mode(txnp);

  if (mode == OverrideMode::PROXY_SET) {
    Dbg(dbg_ctl, "setting parent %s:%d with TSHttpTxnParentProxySet", DEAD_PARENT, DEAD_PORT);
    TSHttpTxnParentProxySet(txnp, DEAD_PARENT, DEAD_PORT);
  } else if (mode == OverrideMode::RESPONSE_ACTION) {
    TSResponseAction action{};

    action.hostname      = DEAD_PARENT;
    action.hostname_len  = sizeof(DEAD_PARENT) - 1;
    action.port          = DEAD_PORT;
    action.nextHopExists = true;
    action.parentIsProxy = true;
    Dbg(dbg_ctl, "setting parent %s:%d with TSHttpTxnResponseActionSet", DEAD_PARENT, DEAD_PORT);
    TSHttpTxnResponseActionSet(txnp, &action);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, char const * /* argv ATS_UNUSED */[])
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
}
