/** @file

    A brief file description

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

#include "ats_beacon_intercept.h"
#include "ats_pagespeed.h"
#include "ats_server_context.h"

#include "net/instaweb/system/public/system_request_context.h"

#include <string>
#include <limits.h>
#include <strings.h>
#include <stdio.h>

using std::string;
using namespace net_instaweb;

#define DEBUG_TAG "ats_pagespeed_beacon"

struct InterceptCtx {
  TSVConn net_vc;
  TSCont contp;

  struct IoHandle {
    TSVIO vio;
    TSIOBuffer buffer;
    TSIOBufferReader reader;
    IoHandle() : vio(0), buffer(0), reader(0){};
    ~IoHandle()
    {
      if (reader) {
        TSIOBufferReaderFree(reader);
      }
      if (buffer) {
        TSIOBufferDestroy(buffer);
      }
    };
  };

  IoHandle input;
  IoHandle output;

  TSHttpParser http_parser;
  string body;
  int req_content_len;
  TSMBuffer req_hdr_bufp;
  TSMLoc req_hdr_loc;
  bool req_hdr_parsed;
  bool initialized;
  TransformCtx *request_context;
  InterceptCtx(TSCont cont)
    : net_vc(0),
      contp(cont),
      input(),
      output(),
      body(""),
      req_content_len(0),
      req_hdr_bufp(0),
      req_hdr_loc(0),
      req_hdr_parsed(false),
      initialized(false)
  {
    http_parser = TSHttpParserCreate();
  }

  bool init(TSVConn vconn);

  void setupWrite();

  ~InterceptCtx()
  {
    TSDebug(DEBUG_TAG, "[%s] Destroying continuation data", __FUNCTION__);
    TSHttpParserDestroy(http_parser);
    if (req_hdr_loc) {
      TSHandleMLocRelease(req_hdr_bufp, TS_NULL_MLOC, req_hdr_loc);
    }
    if (req_hdr_bufp) {
      TSMBufferDestroy(req_hdr_bufp);
    }
    if (request_context) {
      ats_ctx_destroy(request_context);
      request_context = NULL;
    }
  };
};

bool
InterceptCtx::init(TSVConn vconn)
{
  if (initialized) {
    TSError("[ats_beacon_intercept][%s] InterceptCtx already initialized!", __FUNCTION__);
    return false;
  }

  net_vc = vconn;

  input.buffer = TSIOBufferCreate();
  input.reader = TSIOBufferReaderAlloc(input.buffer);
  input.vio    = TSVConnRead(net_vc, contp, input.buffer, INT_MAX);

  req_hdr_bufp = TSMBufferCreate();
  req_hdr_loc  = TSHttpHdrCreate(req_hdr_bufp);
  TSHttpHdrTypeSet(req_hdr_bufp, req_hdr_loc, TS_HTTP_TYPE_REQUEST);

  initialized = true;
  TSDebug(DEBUG_TAG, "[%s] InterceptCtx initialized!", __FUNCTION__);
  return true;
}

void
InterceptCtx::setupWrite()
{
  TSAssert(output.buffer == 0);
  output.buffer = TSIOBufferCreate();
  output.reader = TSIOBufferReaderAlloc(output.buffer);
  output.vio    = TSVConnWrite(net_vc, contp, output.reader, INT_MAX);
}

// Parses out query params from the request.
void
ps_query_params_handler(StringPiece unparsed_uri, StringPiece *data)
{
  stringpiece_ssize_type question_mark_index = unparsed_uri.find("?");
  if (question_mark_index == StringPiece::npos) {
    *data = "";
  } else {
    *data = unparsed_uri.substr(question_mark_index + 1, unparsed_uri.size() - (question_mark_index + 1));
  }
}

static bool
handleRead(InterceptCtx *cont_data, bool &read_complete)
{
  int avail = TSIOBufferReaderAvail(cont_data->input.reader);
  if (avail == TS_ERROR) {
    TSError("[ats_beacon_intercept][%s] Error while getting number of bytes available", __FUNCTION__);
    return false;
  }

  TSDebug(DEBUG_TAG, "[%s] Parsed header, avail: %d", __FUNCTION__, avail);

  int consumed = 0;
  if (avail > 0) {
    int64_t data_len;
    const char *data;
    TSIOBufferBlock block = TSIOBufferReaderStart(cont_data->input.reader);
    while (block != NULL) {
      data = TSIOBufferBlockReadStart(block, cont_data->input.reader, &data_len);
      if (!cont_data->req_hdr_parsed) {
        const char *endptr = data + data_len;
        if (TSHttpHdrParseReq(cont_data->http_parser, cont_data->req_hdr_bufp, cont_data->req_hdr_loc, &data, endptr) ==
            TS_PARSE_DONE) {
          TSDebug(DEBUG_TAG, "[%s] Parsed header", __FUNCTION__);
          TSMLoc content_len_loc =
            TSMimeHdrFieldFind(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, TS_MIME_FIELD_CONTENT_LENGTH, -1);

          /*if (!content_len_loc) {
            TSError("[%s] Error while searching content length header [%s]",
                     __FUNCTION__, TS_MIME_FIELD_CONTENT_LENGTH);
            return false;
          }
          if (!content_len_loc) {
            TSError("[%s] request doesn't contain content length header [%s]",
                     __FUNCTION__, TS_MIME_FIELD_CONTENT_TYPE);
            return false;
            }*/
          if (!content_len_loc) {
            cont_data->req_content_len = 0;
          } else {
            cont_data->req_content_len =
              TSMimeHdrFieldValueIntGet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, content_len_loc, 0);
            TSHandleMLocRelease(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, content_len_loc);
          }
          TSDebug(DEBUG_TAG, "[%s] Got content length as %d", __FUNCTION__, cont_data->req_content_len);
          if (cont_data->req_content_len < 0) {
            TSError("[ats_beacon_intercept][%s] Invalid content length [%d]", __FUNCTION__, cont_data->req_content_len);
            return false;
          }
          if (endptr - data) {
            TSDebug(DEBUG_TAG, "[%s] Appending %ld bytes to body", __FUNCTION__, static_cast<long int>(endptr - data));
            cont_data->body.append(data, endptr - data);
          }
          cont_data->req_hdr_parsed = true;
        }
      } else {
        // TSDebug(DEBUG_TAG, "[%s] Appending %" PRId64" bytes to body", __FUNCTION__, data_len);
        cont_data->body.append(data, data_len);
      }
      consumed += data_len;
      block = TSIOBufferBlockNext(block);
    }
  }

  TSIOBufferReaderConsume(cont_data->input.reader, consumed);

  TSDebug(DEBUG_TAG, "[%s] Consumed %d bytes from input vio, avail: %d", __FUNCTION__, consumed, avail);

  // Modify the input VIO to reflect how much data we've completed.
  TSVIONDoneSet(cont_data->input.vio, TSVIONDoneGet(cont_data->input.vio) + consumed);

  if (static_cast<int>(cont_data->body.size()) == cont_data->req_content_len) {
    TSDebug(DEBUG_TAG, "[%s] Completely read body of size %d", __FUNCTION__, cont_data->req_content_len);
    read_complete = true;
  } else {
    read_complete = false;
    TSDebug(DEBUG_TAG, "[%s] Reenabling input vio as %ld bytes still need to be read", __FUNCTION__,
            static_cast<long int>(cont_data->req_content_len - cont_data->body.size()));
    TSVIOReenable(cont_data->input.vio);
  }
  return true;
}

static bool
processRequest(InterceptCtx *cont_data)
{
  // OS: Looks like on 5.x we sometimes receive read complete / EOS events twice,
  // which needs looking into. Probably this intercept is doing something it shouldn't
  if (cont_data->output.buffer) {
    TSDebug("ats_pagespeed", "Received read complete / EOS twice?!");
    return true;
  }
  string reply_header("HTTP/1.1 204 No Content\r\n");
  int body_size = static_cast<int>(cont_data->body.size());
  if (cont_data->req_content_len != body_size) {
    TSError("[ats_beacon_intercept][%s] Read only %d bytes of body; expecting %d bytes", __FUNCTION__, body_size,
            cont_data->req_content_len);
  }

  char buf[64];
  // snprintf(buf, 64, "%s: %d\r\n\r\n", TS_MIME_FIELD_CONTENT_LENGTH, body_size);
  snprintf(buf, 64, "%s: %d\r\n\r\n", TS_MIME_FIELD_CONTENT_LENGTH, 0);
  reply_header.append(buf);
  reply_header.append("Cache-Control: max-age=0, no-cache");
  // TSError("[%s] reply header: \n%s", __FUNCTION__, reply_header.data());

  StringPiece query_param_beacon_data;
  ps_query_params_handler(cont_data->request_context->url_string->c_str(), &query_param_beacon_data);

  GoogleString beacon_data      = net_instaweb::StrCat(query_param_beacon_data, "&", cont_data->body);
  ServerContext *server_context = cont_data->request_context->server_context;

  SystemRequestContext *system_request_context =
    new SystemRequestContext(server_context->thread_system()->NewMutex(), server_context->timer(),
                             // TODO(oschaaf): determine these for real.
                             "www.foo.com", 80, "127.0.0.1");

  if (!server_context->HandleBeacon(beacon_data, cont_data->request_context->user_agent->c_str(),
                                    net_instaweb::RequestContextPtr(system_request_context))) {
    TSError("[ats_beacon_intercept] Beacon handling failure!");
  } else {
    TSDebug(DEBUG_TAG, "Beacon post data processed OK: [%s]", beacon_data.c_str());
  }

  cont_data->setupWrite();
  if (TSIOBufferWrite(cont_data->output.buffer, reply_header.data(), reply_header.size()) == TS_ERROR) {
    TSError("[ats_beacon_intercept][%s] Error while writing reply header", __FUNCTION__);
    return false;
  }
  /*
  if (TSIOBufferWrite(cont_data->output.buffer, cont_data->body.data(), body_size) == TS_ERROR) {
    TSError("[%s] Error while writing content", __FUNCTION__);
    return false;
    }*/
  int total_bytes_written = reply_header.size() + body_size;
  TSDebug(DEBUG_TAG, "[%s] Wrote reply of size %d", __FUNCTION__, total_bytes_written);
  TSVIONBytesSet(cont_data->output.vio, total_bytes_written);

  TSVIOReenable(cont_data->output.vio);
  return true;
}

static int
txn_intercept(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG, "[%s] Received event: %d", __FUNCTION__, (int)event);

  InterceptCtx *cont_data = static_cast<InterceptCtx *>(TSContDataGet(contp));
  bool read_complete      = false;
  bool shutdown           = false;
  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    TSDebug(DEBUG_TAG, "[%s] Received net accept event", __FUNCTION__);
    TSAssert(cont_data->initialized == false);
    if (!cont_data->init(static_cast<TSVConn>(edata))) {
      TSError("[ats_beacon_intercept][%s] Could not initialize continuation data!", __FUNCTION__);
      return 1;
    }
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSDebug(DEBUG_TAG, "[%s] Received read ready event", __FUNCTION__);
    if (!handleRead(cont_data, read_complete)) {
      TSError("[ats_beacon_intercept][%s] Error while reading from input vio", __FUNCTION__);
      // return 0;
      read_complete = true;
    }
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
    // intentional fall-through
    TSDebug(DEBUG_TAG, "[%s] Received read complete/eos event %d", __FUNCTION__, event);
    read_complete = true;
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(DEBUG_TAG, "[%s] Received write ready event", __FUNCTION__);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(DEBUG_TAG, "[%s] Received write complete event", __FUNCTION__);
    shutdown = true;
    break;
  case TS_EVENT_ERROR:
    // todo: do some error handling here
    TSDebug(DEBUG_TAG, "[%s] Received error event; going to shutdown, event: %d", __FUNCTION__, event);
    TSError("[ats_beacon_intercept][%s] Received error event; going to shutdown, event: %d", __FUNCTION__, event);
    shutdown = true;
    break;
  default:
    break;
  }

  if (read_complete) {
    if (!processRequest(cont_data)) {
      TSError("[ats_beacon_intercept][%s] Failed to process process", __FUNCTION__);
    } else {
      TSDebug(DEBUG_TAG, "[%s] Processed request successfully", __FUNCTION__);
    }
  }

  if (shutdown) {
    TSDebug(DEBUG_TAG, "[%s] Completed request processing. Shutting down...", __FUNCTION__);
    if (cont_data->net_vc) {
      TSVConnClose(cont_data->net_vc);
    }
    delete cont_data;
    TSContDestroy(contp);
  }

  return 1;
}

bool
hook_beacon_intercept(TSHttpTxn txnp)
{
  TSCont contp = TSContCreate(txn_intercept, TSMutexCreate());
  if (!contp) {
    TSError("[ats_beacon_intercept][%s] Could not create intercept request", __FUNCTION__);
    return false;
  }
  InterceptCtx *cont_data    = new InterceptCtx(contp);
  cont_data->request_context = get_transaction_context(txnp);
  TSContDataSet(contp, cont_data);
  TSHttpTxnIntercept(contp, txnp);
  TSDebug(DEBUG_TAG, "[%s] Setup server intercept successfully", __FUNCTION__);
  return true;
}
