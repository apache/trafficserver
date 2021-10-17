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

#include "tscore/ink_defs.h"
#include "serverIntercept.h"

#include <string>
#include <climits>
#include <strings.h>
#include <cstdio>

const char *ECHO_HEADER_PREFIX        = "Echo-";
const int ECHO_HEADER_PREFIX_LEN      = 5;
const char *SERVER_INTERCEPT_HEADER   = "Esi-Internal";
const int SERVER_INTERCEPT_HEADER_LEN = 12;

using std::string;

#define DEBUG_TAG "plugin_esi_intercept"

struct SContData {
  TSVConn net_vc;
  TSCont contp;

  struct IoHandle {
    TSVIO vio               = nullptr;
    TSIOBuffer buffer       = nullptr;
    TSIOBufferReader reader = nullptr;
    IoHandle()              = default;
    ;
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

  SContData(TSCont cont)
    : net_vc(nullptr),
      contp(cont),
      input(),
      output(),
      body(""),
      req_content_len(0),
      req_hdr_bufp(nullptr),
      req_hdr_loc(nullptr),
      req_hdr_parsed(false),
      initialized(false)
  {
    http_parser = TSHttpParserCreate();
  }

  bool init(TSVConn vconn);

  void setupWrite();

  ~SContData()
  {
    TSDebug(DEBUG_TAG, "[%s] Destroying continuation data", __FUNCTION__);
    TSHttpParserDestroy(http_parser);
    if (req_hdr_loc) {
      TSHandleMLocRelease(req_hdr_bufp, TS_NULL_MLOC, req_hdr_loc);
    }
    if (req_hdr_bufp) {
      TSMBufferDestroy(req_hdr_bufp);
    }
  };
};

bool
SContData::init(TSVConn vconn)
{
  if (initialized) {
    TSError("[server_intercept][%s] SContData already initialized!", __FUNCTION__);
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
  TSDebug(DEBUG_TAG, "[%s] SContData initialized!", __FUNCTION__);
  return true;
}

void
SContData::setupWrite()
{
  TSAssert(output.buffer == nullptr);
  output.buffer = TSIOBufferCreate();
  output.reader = TSIOBufferReaderAlloc(output.buffer);
  output.vio    = TSVConnWrite(net_vc, contp, output.reader, INT_MAX);
}

static bool
handleRead(SContData *cont_data, bool &read_complete)
{
  int avail = TSIOBufferReaderAvail(cont_data->input.reader);
  if (avail == TS_ERROR) {
    TSError("[server_intercept][%s] Error while getting number of bytes available", __FUNCTION__);
    return false;
  }

  TSDebug(DEBUG_TAG, "[%s] Parsed header, avail: %d", __FUNCTION__, avail);

  int consumed = 0;
  if (avail > 0) {
    int64_t data_len;
    const char *data;
    TSIOBufferBlock block = TSIOBufferReaderStart(cont_data->input.reader);
    while (block != nullptr) {
      data = TSIOBufferBlockReadStart(block, cont_data->input.reader, &data_len);
      if (!cont_data->req_hdr_parsed) {
        const char *endptr = data + data_len;
        if (TSHttpHdrParseReq(cont_data->http_parser, cont_data->req_hdr_bufp, cont_data->req_hdr_loc, &data, endptr) ==
            TS_PARSE_DONE) {
          TSDebug(DEBUG_TAG, "[%s] Parsed header", __FUNCTION__);
          TSMLoc content_len_loc =
            TSMimeHdrFieldFind(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, TS_MIME_FIELD_CONTENT_LENGTH, -1);
          if (!content_len_loc) {
            TSError("[server_intercept][%s] request doesn't contain content length header [%s]", __FUNCTION__,
                    TS_MIME_FIELD_CONTENT_TYPE);
            return false;
          }
          cont_data->req_content_len =
            TSMimeHdrFieldValueIntGet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, content_len_loc, 0);
          TSHandleMLocRelease(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, content_len_loc);
          TSDebug(DEBUG_TAG, "[%s] Got content length as %d", __FUNCTION__, cont_data->req_content_len);
          if (cont_data->req_content_len <= 0) {
            TSError("[server_intercept][%s] Invalid content length [%d]", __FUNCTION__, cont_data->req_content_len);
            return false;
          }
          if (endptr - data) {
            TSDebug(DEBUG_TAG, "[%s] Appending %ld bytes to body", __FUNCTION__, static_cast<long int>(endptr - data));
            cont_data->body.append(data, endptr - data);
          }
          cont_data->req_hdr_parsed = true;
        }
      } else {
        TSDebug(DEBUG_TAG, "[%s] Appending %" PRId64 " bytes to body", __FUNCTION__, data_len);
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
processRequest(SContData *cont_data)
{
  string reply_header("HTTP/1.1 200 OK\r\n");

  TSMLoc field_loc = TSMimeHdrFieldGet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, 0);
  while (field_loc) {
    TSMLoc next_field_loc;
    const char *name;
    int name_len;
    name = TSMimeHdrFieldNameGet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, field_loc, &name_len);
    if (name) {
      bool echo_header = false;
      if ((name_len > ECHO_HEADER_PREFIX_LEN) && (strncasecmp(name, ECHO_HEADER_PREFIX, ECHO_HEADER_PREFIX_LEN) == 0)) {
        echo_header = true;
        reply_header.append(name + ECHO_HEADER_PREFIX_LEN, name_len - ECHO_HEADER_PREFIX_LEN);
      } else if ((name_len == SERVER_INTERCEPT_HEADER_LEN) && (strncasecmp(name, SERVER_INTERCEPT_HEADER, name_len) == 0)) {
        echo_header = true;
        reply_header.append(name, name_len);
      }
      if (echo_header) {
        reply_header.append(": ");
        int n_field_values = TSMimeHdrFieldValuesCount(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, field_loc);
        for (int i = 0; i < n_field_values; ++i) {
          const char *value;
          int value_len;
          value = TSMimeHdrFieldValueStringGet(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, field_loc, i, &value_len);
          if (!value_len) {
            TSDebug(DEBUG_TAG, "[%s] Error while getting value #%d of header [%.*s]", __FUNCTION__, i, name_len, name);
          } else {
            if (reply_header[reply_header.size() - 2] != ':') {
              reply_header.append(", ");
            }
            reply_header.append(value, value_len);
          }
        }
        reply_header.append("\r\n");
      }
    }
    next_field_loc = TSMimeHdrFieldNext(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, field_loc);
    TSHandleMLocRelease(cont_data->req_hdr_bufp, cont_data->req_hdr_loc, field_loc);
    field_loc = next_field_loc;
  }

  int body_size = static_cast<int>(cont_data->body.size());
  if (cont_data->req_content_len != body_size) {
    TSError("[server_intercept][%s] Read only %d bytes of body; expecting %d bytes", __FUNCTION__, body_size,
            cont_data->req_content_len);
  }

  char buf[64];
  snprintf(buf, 64, "%s: %d\r\n\r\n", TS_MIME_FIELD_CONTENT_LENGTH, body_size);
  reply_header.append(buf);

  // TSError("[%s] reply header: \n%s", __FUNCTION__, reply_header.data());

  cont_data->setupWrite();
  if (TSIOBufferWrite(cont_data->output.buffer, reply_header.data(), reply_header.size()) == TS_ERROR) {
    TSError("[server_intercept][%s] Error while writing reply header", __FUNCTION__);
    return false;
  }
  if (TSIOBufferWrite(cont_data->output.buffer, cont_data->body.data(), body_size) == TS_ERROR) {
    TSError("[server_intercept][%s] Error while writing content", __FUNCTION__);
    return false;
  }
  int total_bytes_written = reply_header.size() + body_size;
  TSDebug(DEBUG_TAG, "[%s] Wrote reply of size %d", __FUNCTION__, total_bytes_written);
  TSVIONBytesSet(cont_data->output.vio, total_bytes_written);

  TSVIOReenable(cont_data->output.vio);
  return true;
}

static int
serverIntercept(TSCont contp, TSEvent event, void *edata)
{
  TSDebug(DEBUG_TAG, "[%s] Received event: %d", __FUNCTION__, static_cast<int>(event));

  SContData *cont_data = static_cast<SContData *>(TSContDataGet(contp));
  bool read_complete   = false;
  bool shutdown        = false;
  switch (event) {
  case TS_EVENT_NET_ACCEPT:
    TSDebug(DEBUG_TAG, "[%s] Received net accept event", __FUNCTION__);
    TSAssert(cont_data->initialized == false);
    if (!cont_data->init(static_cast<TSVConn>(edata))) {
      TSError("[server_intercept][%s] Could not initialize continuation data!", __FUNCTION__);
      return 1;
    }
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSDebug(DEBUG_TAG, "[%s] Received read ready event", __FUNCTION__);
    if (!handleRead(cont_data, read_complete)) {
      TSError("[server_intercept][%s] Error while reading from input vio", __FUNCTION__);
      return 0;
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
    TSError("[server_intercept][%s] Received error event; going to shutdown, event: %d", __FUNCTION__, event);
    shutdown = true;
    break;
  default:
    break;
  }

  if (read_complete) {
    if (!processRequest(cont_data)) {
      TSError("[server_intercept][%s] Failed to process process", __FUNCTION__);
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
setupServerIntercept(TSHttpTxn txnp)
{
  TSCont contp = TSContCreate(serverIntercept, TSMutexCreate());
  if (!contp) {
    TSError("[server_intercept][%s] Could not create intercept request", __FUNCTION__);
    return false;
  }
  SContData *cont_data = new SContData(contp);
  TSContDataSet(contp, cont_data);
  TSHttpTxnServerIntercept(contp, txnp);
  TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_RESPONSE_CACHEABLE, true);
  TSHttpTxnCntlSet(txnp, TS_HTTP_CNTL_REQUEST_CACHEABLE, true);
  TSDebug(DEBUG_TAG, "[%s] Setup server intercept successfully", __FUNCTION__);
  return true;
}
