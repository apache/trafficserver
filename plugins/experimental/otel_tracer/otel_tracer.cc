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
#include <unistd.h>
#include <getopt.h>

#include "ts/ts.h"

#define PLUGIN_NAME "otel_tracer"

#include "tracer_common.h"
namespace trace    = opentelemetry::trace;
namespace nostd    = opentelemetry::nostd;
namespace sdktrace = opentelemetry::sdk::trace;

static int
close_txn(TSCont contp, TSEvent event, void *edata)
{
  int retval = 0;
  TSMBuffer buf;
  TSMLoc hdr_loc;

  ExtraRequestData *req_data = static_cast<ExtraRequestData *>(TSContDataGet(contp));

  TSHttpTxn txnp           = static_cast<TSHttpTxn>(edata);
  if (event != TS_EVENT_HTTP_TXN_CLOSE) {
    TSError("[otel_tracer][%s] Unexpected event (%d)", __FUNCTION__, event);
    goto lReturn;
  }

  if (TSHttpTxnClientRespGet(txnp, &buf, &hdr_loc) == TS_SUCCESS) {
    int status = TSHttpHdrStatusGet(buf, hdr_loc);
    req_data->span->SetAttribute("http.status_code", status);
    if (status > 499) {
      req_data->span->SetStatus(trace::StatusCode::kError);
    }

    TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
  }

  retval = 1;

lReturn:
  req_data->Destruct(req_data);
  delete req_data;

  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return retval;
}

static void
set_request_header(TSMBuffer buf, TSMLoc hdr_loc, const char *key, int key_len, const char *val, int val_len)
{
  TSMLoc field_loc, tmp;
  int first;

  field_loc = TSMimeHdrFieldFind(buf, hdr_loc, key, key_len);

  if (field_loc != TS_NULL_MLOC) {
    first = 1;
    while (field_loc != TS_NULL_MLOC) {
      tmp = TSMimeHdrFieldNextDup(buf, hdr_loc, field_loc);
      if (first) {
        first = 0;
        TSMimeHdrFieldValueStringSet(buf, hdr_loc, field_loc, -1, val, val_len);
      } else {
        TSMimeHdrFieldDestroy(buf, hdr_loc, field_loc);
      }
      TSHandleMLocRelease(buf, hdr_loc, field_loc);
      field_loc = tmp;
    }
  } else if (TSMimeHdrFieldCreateNamed(buf, hdr_loc, key, key_len, &field_loc) !=
             TS_SUCCESS) {
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

static std::string
read_request_header(TSMBuffer buf, TSMLoc hdr_loc, const char *key, int key_len)
{
  std::string retval = "";
  const char *field;
  int field_len;
  TSMLoc field_loc;
  field_loc = TSMimeHdrFieldFind(buf, hdr_loc, key, key_len);
  if (field_loc) {
    field = TSMimeHdrFieldValueStringGet(buf, hdr_loc, field_loc, -1, &field_len);
    retval.assign(field, field_len);
    TSHandleMLocRelease(buf, hdr_loc, field_loc);
  }

  return retval;
}

static void
read_request(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer buf;
  TSMLoc hdr_loc;

  if (TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] cannot retrieve client request", __FUNCTION__);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  // method
  const char *method;
  int method_len;
  method = TSHttpHdrMethodGet(buf, hdr_loc, &method_len);
  std::string method_str = "";
  method_str.assign(method, method_len);

  // TO-DO: add http flavor as attribute
  //const char *h2_tag = TSHttpTxnClientProtocolStackContains(txnp, "h2");
  //const char *h1_tag = TSHttpTxnClientProtocolStackContains(txnp, "http/1.0");
  //const char *h11_tag = TSHttpTxnClientProtocolStackContains(txnp, "http/1.1");

  // target
  char *target = nullptr;
  int target_len = 0;
  std::string target_str = "";
  target = TSHttpTxnEffectiveUrlStringGet(txnp, &target_len);
  if(target) {
    target_str.assign(target, target_len);
    free(target);
  }

  // path, host, port, scheme
  TSMLoc url_loc;
  if (TSHttpHdrUrlGet(buf, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] cannot retrieve client request url", __FUNCTION__);
  }

  const char *path;
  int path_len = 0;
  path = TSUrlPathGet(buf, url_loc, &path_len);
  std::string path_str = "";
  path_str.assign(path, path_len);
  path_str = "/" + path_str;

  const char *host;
  int host_len = 0;
  std::string host_str = "";
  host = TSUrlHostGet(buf, url_loc, &host_len);
  if (host_len == 0) {
    const char *key   = "Host";
    const char *l_key = "host";
    int key_len = 4;

    host_str = read_request_header(buf, hdr_loc, key, key_len);
    if (host_str == "") {
      host_str = read_request_header(buf, hdr_loc, l_key, key_len);
    }
  } else {
    host_str.assign(host, host_len);
  }

  const char *scheme;
  int scheme_len = 0;
  scheme = TSUrlSchemeGet(buf, url_loc, &scheme_len);
  std::string scheme_str = "";
  scheme_str.assign(scheme, scheme_len);

  int port;
  port = TSUrlPortGet(buf, url_loc);

  TSHandleMLocRelease(buf, hdr_loc, url_loc);

  // user-agent
  const char *ua_key   = "User-Agent";
  int ua_key_len = 10;
  std::string ua_str = read_request_header(buf, hdr_loc, ua_key, ua_key_len);

  // B3 headers
  const char *b3_key = "b3";
  int b3_key_len = 2;
  std::string b3_str = read_request_header(buf, hdr_loc, b3_key, b3_key_len);

  const char *b3_tid_key = "X-B3-TraceId";
  int b3_tid_key_len = 12;
  std::string b3_tid_str = read_request_header(buf, hdr_loc, b3_tid_key, b3_tid_key_len);

  const char *b3_sid_key = "X-B3-SpanId";
  int b3_sid_key_len = 11;
  std::string b3_sid_str = read_request_header(buf, hdr_loc, b3_sid_key, b3_sid_key_len);

  const char *b3_s_key = "X-B3-Sampled";
  int b3_s_key_len = 12;
  std::string b3_s_str = read_request_header(buf, hdr_loc, b3_s_key, b3_s_key_len);

  // TODO: add remote ip, port to attributes

  // done with client request
  TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

  trace::StartSpanOptions options;
  options.kind          = trace::SpanKind::kServer;  // server
  std::string span_name = path_str;

  // create parent context
  std::map<std::string, std::string> parent_headers;
  if (b3_str != "") {
    parent_headers["b3"] = b3_str;
  }
  if(b3_tid_str != "") {
    parent_headers["X-B3-TraceId"] = b3_tid_str;
  }
  if(b3_sid_str != "") {
    parent_headers["X-B3-SpanId"] = b3_sid_str;
  }
  if(b3_s_str != "") {
    parent_headers["X-B3-Sampled"] = b3_s_str;
  }
  const HttpTextMapCarrier<std::map<std::string, std::string>> parent_carrier(parent_headers);
  auto parent_prop        = context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
  auto parent_current_ctx = context::RuntimeContext::GetCurrent();
  auto parent_new_context = parent_prop->Extract(parent_carrier, parent_current_ctx);
  options.parent   = trace::GetSpan(parent_new_context)->GetContext();

  auto span = get_tracer("ats")->StartSpan(span_name,
                                {{"http.method", method_str},
                                 {"http.url", target_str},
                                 {"http.route", path_str},
                                 {"http.host", host_str},
                                 {"http.user_agent", ua_str},
                                 {"net.host.port", port},
                                 {"http.scheme", scheme_str}},
                                options);

  auto scope = get_tracer("ats")->WithActiveSpan(span);

  auto current_ctx = opentelemetry::context::RuntimeContext::GetCurrent();
  HttpTextMapCarrier<opentelemetry::ext::http::client::Headers> carrier;
  auto prop = opentelemetry::context::propagation::GlobalTextMapPropagator::GetGlobalPropagator();
  prop->Inject(carrier, current_ctx);

  // insert headers to request
  if (TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc) != TS_SUCCESS) {
    TSError("[otel_tracer][%s] cannot retrieve client request", __FUNCTION__);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }
  for (auto &p: carrier.headers_) {
    TSDebug(PLUGIN_NAME, "[%s] adding header %s %s ", __FUNCTION__, p.first.c_str(), p.second.c_str());
    set_request_header(buf, hdr_loc, p.first.c_str(), p.first.size(), p.second.c_str(), p.second.size());
  }
  // done with client request for setting request header
  TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);

  // pass the span
  TSCont close_txn_contp = TSContCreate(close_txn, nullptr);
  if (!close_txn_contp) {
    TSError("[otel_tracer][%s] Could not create continuation", __FUNCTION__);
    return;
  }
  TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, close_txn_contp);
  ExtraRequestData *req_data = new ExtraRequestData;
  req_data->span = span;
  TSContDataSet(close_txn_contp, req_data);

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
  info.vendor_name   = "Yahoo";
  info.support_email = "kichan@yahooinc.com";

  // Get parameter: service name, sampling rate,
  std::string url = "";
  std::string service_name = "otel_tracer";
  double rate = 1.0;
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
  return;
}
