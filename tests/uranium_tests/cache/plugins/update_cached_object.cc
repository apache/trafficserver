/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Exercises TSHttpTxnUpdateCachedObject(). On a fresh cache hit whose request
// carries X-Update-Cached-Object, copy that header's value into the cached
// response as X-Cached-Update and ask for the cached object to be updated.

#include <ts/ts.h>

#include <string_view>

namespace
{
constexpr char             PLUGIN_NAME[] = "update_cached_object";
constexpr std::string_view TRIGGER_HEADER{"X-Update-Cached-Object"};
constexpr std::string_view UPDATED_HEADER{"X-Cached-Update"};

DbgCtl dbg_ctl{PLUGIN_NAME};

std::string_view
header_value(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view name)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name.data(), static_cast<int>(name.size()));

  if (field_loc == TS_NULL_MLOC) {
    return {};
  }

  int         len   = 0;
  char const *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return value != nullptr ? std::string_view{value, static_cast<size_t>(len)} : std::string_view{};
}

bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string_view name, std::string_view value)
{
  TSMLoc field_loc = TS_NULL_MLOC;

  if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, name.data(), static_cast<int>(name.size()), &field_loc) != TS_SUCCESS) {
    return false;
  }

  bool const ok =
    TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, value.data(), static_cast<int>(value.size())) == TS_SUCCESS &&
    TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc) == TS_SUCCESS;

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return ok;
}

void
update_cached_object(TSHttpTxn txnp)
{
  int lookup_status = 0;

  if (TSHttpTxnCacheLookupStatusGet(txnp, &lookup_status) != TS_SUCCESS || lookup_status != TS_CACHE_LOOKUP_HIT_FRESH) {
    return;
  }

  TSMBuffer req_bufp = nullptr;
  TSMLoc    req_hdr  = TS_NULL_MLOC;

  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr) != TS_SUCCESS) {
    return;
  }

  std::string_view const value = header_value(req_bufp, req_hdr, TRIGGER_HEADER);

  if (!value.empty()) {
    TSMBuffer resp_bufp = nullptr;
    TSMLoc    resp_hdr  = TS_NULL_MLOC;

    if (TSHttpTxnCachedRespModifiableGet(txnp, &resp_bufp, &resp_hdr) != TS_SUCCESS) {
      TSError("[%s] could not get the modifiable cached response", PLUGIN_NAME);
    } else {
      if (!set_header(resp_bufp, resp_hdr, UPDATED_HEADER, value)) {
        TSError("[%s] could not set %.*s", PLUGIN_NAME, static_cast<int>(UPDATED_HEADER.size()), UPDATED_HEADER.data());
      } else if (TSHttpTxnUpdateCachedObject(txnp) != TS_SUCCESS) {
        TSError("[%s] TSHttpTxnUpdateCachedObject failed", PLUGIN_NAME);
      } else {
        Dbg(dbg_ctl, "requested cached object update with %.*s", static_cast<int>(value.size()), value.data());
      }
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_hdr);
    }
  }

  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr);
}

int
handle_event(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  auto txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE) {
    update_cached_object(txnp);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, char const * /* argv ATS_UNUSED */[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, TSContCreate(handle_event, nullptr));
}
