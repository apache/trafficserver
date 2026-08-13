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

#include <ts/ts.h>
#include <ts/remap.h>

#include <cstring>
#include <string>

namespace
{
constexpr char PLUGIN_NAME[]                = "at_header_probe";
constexpr char REQUEST_ADDED_VALUE[]        = "request-added";
constexpr char RESPONSE_ADDED_VALUE[]       = "response-added";
constexpr char REQUEST_ERROR_PREFIX[]       = "saw unexpected request header";
constexpr char RESPONSE_ERROR_PREFIX[]      = "saw unexpected response header";
constexpr char REMAP_REQUEST_ERROR_PREFIX[] = "saw unexpected remap request header";

std::string request_probe_header;
std::string response_probe_header;
std::string request_added_header;
std::string response_added_header;

struct RemapConfig {
  std::string request_probe_header;
};

bool
set_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string const &name, char const *value)
{
  int const name_len  = static_cast<int>(name.size());
  int const value_len = static_cast<int>(std::strlen(value));
  TSMLoc    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name.data(), name_len);
  bool      created   = false;
  bool      ok        = false;

  if (field_loc == TS_NULL_MLOC) {
    if (TSMimeHdrFieldCreateNamed(bufp, hdr_loc, name.data(), name_len, &field_loc) != TS_SUCCESS) {
      return false;
    }
    created = true;
  }

  if (TSMimeHdrFieldValuesClear(bufp, hdr_loc, field_loc) == TS_SUCCESS &&
      TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, value, value_len) == TS_SUCCESS) {
    if (!created || TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc) == TS_SUCCESS) {
      ok = true;
    }
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return ok;
}

bool
has_header(TSMBuffer bufp, TSMLoc hdr_loc, std::string const &name)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, name.data(), static_cast<int>(name.size()));
  if (field_loc == TS_NULL_MLOC) {
    return false;
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  return true;
}

void
log_unexpected(char const *prefix, std::string const &name)
{
  TSError("[%s] %s %s", PLUGIN_NAME, prefix, name.c_str());
}

bool
parse_arguments(int argc, char const *argv[])
{
  if (argc != 5) {
    TSError("[%s] Expected request-probe, response-probe, request-added, and response-added header names", PLUGIN_NAME);
    return false;
  }

  request_probe_header  = argv[1];
  response_probe_header = argv[2];
  request_added_header  = argv[3];
  response_added_header = argv[4];

  return true;
}

int
handle_event(TSCont /* contp ATS_UNUSED */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSMBuffer req_bufp = nullptr;
    TSMLoc    req_hdr  = TS_NULL_MLOC;

    if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr) == TS_SUCCESS) {
      if (has_header(req_bufp, req_hdr, request_probe_header)) {
        log_unexpected(REQUEST_ERROR_PREFIX, request_probe_header);
      }
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr);
    }
  } else if (event == TS_EVENT_HTTP_SEND_REQUEST_HDR) {
    TSMBuffer req_bufp = nullptr;
    TSMLoc    req_hdr  = TS_NULL_MLOC;

    if (TSHttpTxnServerReqGet(txnp, &req_bufp, &req_hdr) == TS_SUCCESS) {
      set_header(req_bufp, req_hdr, request_added_header, REQUEST_ADDED_VALUE);
      TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr);
    }
  } else if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    TSMBuffer resp_bufp = nullptr;
    TSMLoc    resp_hdr  = TS_NULL_MLOC;

    if (TSHttpTxnServerRespGet(txnp, &resp_bufp, &resp_hdr) == TS_SUCCESS) {
      if (has_header(resp_bufp, resp_hdr, response_probe_header)) {
        log_unexpected(RESPONSE_ERROR_PREFIX, response_probe_header);
      }
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_hdr);
    }
  } else if (event == TS_EVENT_HTTP_SEND_RESPONSE_HDR) {
    TSMBuffer resp_bufp = nullptr;
    TSMLoc    resp_hdr  = TS_NULL_MLOC;

    if (TSHttpTxnClientRespGet(txnp, &resp_bufp, &resp_hdr) == TS_SUCCESS) {
      set_header(resp_bufp, resp_hdr, response_added_header, RESPONSE_ADDED_VALUE);
      TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_hdr);
    }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // namespace

void
TSPluginInit(int argc, char const *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  if (!parse_arguments(argc, argv)) {
    return;
  }

  TSCont contp = TSContCreate(handle_event, nullptr);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info ATS_UNUSED */, char * /* errbuf ATS_UNUSED */, int /* errbuf_size ATS_UNUSED */)
{
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuf, int errbuf_size)
{
  if (argc != 3) {
    TSstrlcpy(errbuf, "expected from, to, and request-probe header arguments", errbuf_size);
    return TS_ERROR;
  }

  auto *config                 = new RemapConfig;
  config->request_probe_header = argv[2];
  *ih                          = config;

  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  delete static_cast<RemapConfig *>(ih);
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri ATS_UNUSED */)
{
  auto const *config   = static_cast<RemapConfig *>(ih);
  TSMBuffer   req_bufp = nullptr;
  TSMLoc      req_hdr  = TS_NULL_MLOC;

  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_hdr) == TS_SUCCESS) {
    if (has_header(req_bufp, req_hdr, config->request_probe_header)) {
      log_unexpected(REMAP_REQUEST_ERROR_PREFIX, config->request_probe_header);
    }
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_hdr);
  }

  return TSREMAP_NO_REMAP;
}
