/*
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

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <iostream>
#include <vector>
#include <string_view>
#include <sstream>
#include <unistd.h>
#include <getopt.h>

#include "ts/ts.h"

#define PLUGIN_NAME "otel_tracer"

constexpr std::string_view ua_key     = {"User-Agent"};
constexpr std::string_view host_key   = {"Host"};
constexpr std::string_view l_host_key = {"host"};
constexpr std::string_view b3_key     = {"b3"};
constexpr std::string_view b3_tid_key = {"X-B3-TraceId"};
constexpr std::string_view b3_sid_key = {"X-B3-SpanId"};
constexpr std::string_view b3_s_key   = {"X-B3-Sampled"};

#include "tracer_common.h"

static int
close_txn(TSCont contp, TSEvent event, void *edata)
{
  int retval = 0;
  TSMBuffer buf;
  TSMLoc hdr_loc;

  TSDebug(PLUGIN_NAME, "[%s] Retrieving status code to add to span attributes", __FUNCTION__);
  auto req_data = static_cast<ExtraRequestData *>(TSContDataGet(contp));

  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event != TS_EVENT_HTTP_TXN_CLOSE) {
    TSError("[otel_tracer][%s] Unexpected event (%d)", __FUNCTION__, event);
    goto lReturn;
  }

  if (TSHttpTxnClientRespGet(txnp, &buf, &hdr_loc) == TS_SUCCESS) {
    int status = TSHttpHdrStatusGet(buf, hdr_loc);
    req_data->SetSpanStatus(req_data, status);
    if (status > 499) {
      req_data->SetSpanError(req_data);
    }

    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
  }

  retval = 1;

lReturn:
  TSDebug(PLUGIN_NAME, "[%s] Cleaning up after close hook handler", __FUNCTION__);
  req_data->Destruct(req_data);
  delete req_data;

  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return retval;
}

static void
set_request_header(TSMBuffer buf, TSMLoc hdr_loc, const char *key, int key_len, const char *val, int val_len)
{
  TSMLoc field_loc = TSMimeHdrFieldFind(buf, hdr_loc, key, key_len);

  if (field_loc != TS_NULL_MLOC) {
    int first = 1;
    while (field_loc != TS_NULL_MLOC) {
      TSMLoc tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field_loc);
      if (first) {
        first = 0;
        TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len);
      } else {
        TSMimeHdrFieldDestroy(buf, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(buf, hdr_loc, key, key_len, &field_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] TSMimeHdrFieldCreateNamed error", __FUNCTION__);
    return;
  } else {
    TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len);
    TSMimeHdrFieldAppend(buf, hdr_loc, field_loc);
  }

  if (field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, field_loc);
  }

  return;
}

static void
read_request(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer buf;
  TSMLoc hdr_loc;

  TSDebug(PLUGIN_NAME, "[%s] Reading information from request", __FUNCTION__);
  if (TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] cannot retrieve client request", __FUNCTION__);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  // path, host, port, scheme
  TSMLoc url_loc = nullptr;
  if (TSHttpHdrUrlGet(buf, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] cannot retrieve client request url", __FUNCTION__);
    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  std::string path_str = "/";
  const char *path     = nullptr;
  int path_len         = 0;
  path                 = TSUrlPathGet(buf, url_loc, &path_len);
  path_str.append(std::string_view(path, path_len));

  TSMLoc host_field_loc   = TS_NULL_MLOC;
  TSMLoc l_host_field_loc = TS_NULL_MLOC;
  const char *host        = nullptr;
  int host_len            = 0;
  host                    = TSUrlHostGet(buf, url_loc, &host_len);
  if (host_len == 0) {
    host_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, host_key.data(), host_key.length());
    if (host_field_loc != TS_NULL_MLOC) {
      host = TSMimeHdrFieldValueStringGet(buf, hdr_loc, host_field_loc, -1, &host_len);
    }

    if (host_len == 0) {
      l_host_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, l_host_key.data(), l_host_key.length());
      if (l_host_field_loc != TS_NULL_MLOC) {
        host = TSMimeHdrFieldValueStringGet(buf, hdr_loc, l_host_field_loc, -1, &host_len);
      }
    }
  }
  std::string_view host_str(host, host_len);

  const char *scheme = nullptr;
  int scheme_len     = 0;
  scheme             = TSUrlSchemeGet(buf, url_loc, &scheme_len);
  std::string_view scheme_str(scheme, scheme_len);

  int port;
  port = TSUrlPortGet(buf, url_loc);

  // method
  const char *method = nullptr;
  int method_len     = 0;
  method             = TSHttpHdrMethodGet(buf, hdr_loc, &method_len);
  std::string_view method_str(method, method_len);

  // TO-DO: add http flavor as attribute
  // const char *h2_tag = TSHttpTxnClientProtocolStackContains(txnp, "h2");
  // const char *h1_tag = TSHttpTxnClientProtocolStackContains(txnp, "http/1.0");
  // const char *h11_tag = TSHttpTxnClientProtocolStackContains(txnp, "http/1.1");

  // target
  char *target   = nullptr;
  int target_len = 0;
  target         = TSHttpTxnEffectiveUrlStringGet(txnp, &target_len);
  std::string_view target_str(target, target_len);

  // user-agent
  TSMLoc ua_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, ua_key.data(), ua_key.length());
  const char *ua      = nullptr;
  int ua_len          = 0;
  if (ua_field_loc) {
    ua = TSMimeHdrFieldValueStringGet(buf, hdr_loc, ua_field_loc, -1, &ua_len);
  }
  std::string_view ua_str(ua, ua_len);

  // B3 headers
  TSMLoc b3_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, b3_key.data(), b3_key.length());
  const char *b3      = nullptr;
  int b3_len          = 0;
  if (b3_field_loc) {
    b3 = TSMimeHdrFieldValueStringGet(buf, hdr_loc, b3_field_loc, -1, &b3_len);
  }
  std::string_view b3_str(b3, b3_len);

  TSMLoc b3_tid_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, b3_tid_key.data(), b3_tid_key.length());
  const char *b3_tid      = nullptr;
  int b3_tid_len          = 0;
  if (b3_tid_field_loc) {
    b3_tid = TSMimeHdrFieldValueStringGet(buf, hdr_loc, b3_tid_field_loc, -1, &b3_tid_len);
  }
  std::string_view b3_tid_str(b3_tid, b3_tid_len);

  TSMLoc b3_sid_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, b3_sid_key.data(), b3_sid_key.length());
  const char *b3_sid      = nullptr;
  int b3_sid_len          = 0;
  if (b3_sid_field_loc) {
    b3_sid = TSMimeHdrFieldValueStringGet(buf, hdr_loc, b3_sid_field_loc, -1, &b3_sid_len);
  }
  std::string_view b3_sid_str(b3_sid, b3_sid_len);

  TSMLoc b3_s_field_loc = TSMimeHdrFieldFind(buf, hdr_loc, b3_s_key.data(), b3_s_key.length());
  const char *b3_s      = nullptr;
  int b3_s_len          = 0;
  if (b3_s_field_loc) {
    b3_s = TSMimeHdrFieldValueStringGet(buf, hdr_loc, b3_s_field_loc, -1, &b3_s_len);
  }
  std::string_view b3_s_str(b3_s, b3_s_len);

  // TODO: add remote ip, port to attributes

  // create parent context
  TSDebug(PLUGIN_NAME, "[%s] Creating parent context from incoming request headers", __FUNCTION__);
  std::map<std::string, std::string> parent_headers;
  if (b3_len != 0) {
    parent_headers[std::string{b3_key}] = b3_str;
  }
  if (b3_tid_len != 0) {
    parent_headers[std::string{b3_tid_key}] = b3_tid_str;
  }
  if (b3_sid_len != 0) {
    parent_headers[std::string{b3_sid_key}] = b3_sid_str;
  }
  if (b3_s_len != 0) {
    parent_headers[std::string{b3_s_key}] = b3_s_str;
  }

  // create trace span and activate
  TSDebug(PLUGIN_NAME, "[%s] Create span with a name, attributes, parent context and activate it", __FUNCTION__);
  auto span = get_tracer("ats")->StartSpan(
    get_span_name(path_str), get_span_attributes(method_str, target_str, path_str, host_str, ua_str, port, scheme_str),
    get_span_options(parent_headers));

  auto scope = get_tracer("ats")->WithActiveSpan(span);

  std::map<std::string, std::string> trace_headers = get_trace_headers();
  // insert headers to request
  TSDebug(PLUGIN_NAME, "[%s] Insert trace headers to upstream request", __FUNCTION__);
  for (auto &p : trace_headers) {
    set_request_header(buf, hdr_loc, p.first.c_str(), p.first.size(), p.second.c_str(), p.second.size());
  }

  // pass the span
  TSDebug(PLUGIN_NAME, "[%s] Add close hook to add status code to span attribute", __FUNCTION__);
  TSCont close_txn_contp = TSContCreate(close_txn, nullptr);
  if (!close_txn_contp) {
    TSError("[otel_tracer][%s] Could not create continuation", __FUNCTION__);
  } else {
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, close_txn_contp);
    ExtraRequestData *req_data = new ExtraRequestData;
    req_data->span             = span;
    TSContDataSet(close_txn_contp, req_data);
  }

  // clean up
  TSDebug(PLUGIN_NAME, "[%s] Cleanig up", __FUNCTION__);
  if (target != nullptr) {
    TSfree(target);
  }
  if (host_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, host_field_loc);
  }
  if (l_host_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, l_host_field_loc);
  }
  if (ua_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, ua_field_loc);
  }
  if (b3_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, b3_field_loc);
  }
  if (b3_tid_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, b3_tid_field_loc);
  }
  if (b3_sid_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, b3_sid_field_loc);
  }
  if (b3_s_field_loc != TS_NULL_MLOC) {
    TSHandleMLocRelease(buf, hdr_loc, b3_s_field_loc);
  }
  if (url_loc != nullptr) {
    TSHandleMLocRelease(buf, hdr_loc, url_loc);
  }
  TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static int
plugin_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    read_request(txnp, contp);
    return 0;
  default:
    break;
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  // Get parameter: service name, sampling rate,
  std::string url          = "";
  std::string service_name = "otel_tracer";
  double rate              = 1.0;
  if (argc > 1) {
    int c;
    static const struct option longopts[] = {
      {const_cast<char *>("url"), required_argument, nullptr, 'u'},
      {const_cast<char *>("service-name"), required_argument, nullptr, 's'},
      {const_cast<char *>("sampling-rate"), required_argument, nullptr, 'r'},
      {nullptr, 0, nullptr, 0},
    };

    int longindex = 0;
    while ((c = getopt_long(argc, const_cast<char *const *>(argv), "u:s:r:", longopts, &longindex)) != -1) {
      switch (c) {
      case 'u':
        url = optarg;
        break;
      case 's':
        service_name = optarg;
        break;
      case 'r':
        rate = atof(optarg);
        break;
      default:
        break;
      }
    }
  }
  InitTracer(url, service_name, rate);

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    goto error;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(plugin_handler, nullptr));
  goto done;

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);

done:
  TSDebug(PLUGIN_NAME, "Plugin initialized");
  return;
}
