/** @file

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

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <functional>
#include <atomic>

#include "ts/ts.h"

static const std::string_view MultipartBoundary{"\r\n--- ATS xDebug Probe Injection Boundary ---\r\n\r\n"};

struct BodyBuilder {
  TSVIO output_vio               = nullptr;
  TSIOBuffer output_buffer       = nullptr;
  TSIOBufferReader output_reader = nullptr;
  bool wrote_prebody             = false;
  bool wrote_body                = false;
  bool hdr_ready                 = false;
  std::atomic_flag wrote_postbody;

  int64_t nbytes = 0;
  TSHttpTxn txn  = nullptr;
};

static char Hostname[1024];

static std::string
getPreBody(TSHttpTxn txn)
{
  std::stringstream output;
  output << "{'xDebugProbeAt' : '" << Hostname << "'\n   'captured':[";
  print_request_headers(txn, output);
  output << "\n   ]\n}";
  output << MultipartBoundary;
  return output.str();
}

static std::string
getPostBody(TSHttpTxn txn)
{
  std::stringstream output;
  output << MultipartBoundary;
  output << "{'xDebugProbeAt' : '" << Hostname << "'\n   'captured':[";
  print_response_headers(txn, output);
  output << "\n   ]\n}";
  return output.str();
}

static void
writePostBody(BodyBuilder *data)
{
  if (data->wrote_body && data->hdr_ready && !data->wrote_postbody.test_and_set()) {
    TSDebug("xdebug_transform", "body_transform(): Writing postbody headers...");
    std::string postbody = getPostBody(data->txn);
    TSIOBufferWrite(data->output_buffer, postbody.data(), postbody.length());
    data->nbytes += postbody.length();
    TSVIONBytesSet(data->output_vio, data->nbytes);
    TSVIOReenable(data->output_vio);
  }
}

static int
body_transform(TSCont contp, TSEvent event, void *edata)
{
  BodyBuilder *data = static_cast<BodyBuilder *>(TSContDataGet(contp));
  if (!data) {
    TSContDestroy(contp);
    return TS_ERROR;
  }
  if (TSVConnClosedGet(contp)) {
    // write connection destoried. cleanup.
    delete data;
    TSContDestroy(contp);
    return 0;
  }

  TSVIO src_vio = TSVConnWriteVIOGet(contp);

  switch (event) {
  case TS_EVENT_ERROR: {
    // Notify input vio of this error event
    TSContCall(TSVIOContGet(src_vio), TS_EVENT_ERROR, src_vio);
    return 0;
  }
  case TS_EVENT_VCONN_WRITE_COMPLETE: {
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    return 0;
  }
  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug("xdebug_transform", "body_transform(): Event is TS_EVENT_VCONN_WRITE_READY");
  // fallthru
  default:
    if (!data->output_buffer) {
      data->output_buffer = TSIOBufferCreate();
      data->output_reader = TSIOBufferReaderAlloc(data->output_buffer);
      data->output_vio    = TSVConnWrite(TSTransformOutputVConnGet(contp), contp, data->output_reader, INT64_MAX);
    }

    if (data->wrote_prebody == false) {
      TSDebug("xdebug_transform", "body_transform(): Writing prebody headers...");
      std::string prebody = getPreBody(data->txn);
      TSIOBufferWrite(data->output_buffer, prebody.data(), prebody.length()); // write prebody
      data->wrote_prebody = true;
      data->nbytes += prebody.length();
    }

    TSIOBuffer src_buf = TSVIOBufferGet(src_vio);

    if (!src_buf) {
      // upstream continuation shuts down write operation.
      data->wrote_body = true;
      writePostBody(data);
      return 0;
    }

    int64_t towrite = TSVIONTodoGet(src_vio);
    TSDebug("xdebug_transform", "body_transform(): %" PRId64 " bytes of body is expected", towrite);
    int64_t avail = TSIOBufferReaderAvail(TSVIOReaderGet(src_vio));
    towrite       = towrite > avail ? avail : towrite;
    if (towrite > 0) {
      TSIOBufferCopy(TSVIOBufferGet(data->output_vio), TSVIOReaderGet(src_vio), towrite, 0);
      TSIOBufferReaderConsume(TSVIOReaderGet(src_vio), towrite);
      TSVIONDoneSet(src_vio, TSVIONDoneGet(src_vio) + towrite);
      TSDebug("xdebug_transform", "body_transform(): writing %" PRId64 " bytes of body", towrite);
    }

    if (TSVIONTodoGet(src_vio) > 0) {
      TSVIOReenable(data->output_vio);
      TSContCall(TSVIOContGet(src_vio), TS_EVENT_VCONN_WRITE_READY, src_vio);
    } else {
      // End of src vio
      // Write post body content and update output VIO
      data->wrote_body = true;
      data->nbytes += TSVIONDoneGet(src_vio);
      writePostBody(data);
      TSContCall(TSVIOContGet(src_vio), TS_EVENT_VCONN_WRITE_COMPLETE, src_vio);
    }
  }
  return 0;
}
