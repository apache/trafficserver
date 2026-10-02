/** @file

  A test plugin that HTML-escapes and unescapes origin response bodies.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing,
  software distributed under the License is distributed on an
  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
  KIND, either express or implied.  See the License for the
  specific language governing permissions and limitations
  under the License.
 */

#include "ts/ts.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace
{
constexpr char PLUGIN_NAME[] = "test_TSStringHtmlEscape";

enum class HtmlOperation { ESCAPE, UNESCAPE };

struct TransformData {
  explicit TransformData(HtmlOperation operation) : operation(operation)
  {
    output_buffer = TSIOBufferCreate();
    output_reader = TSIOBufferReaderAlloc(output_buffer);
  }

  ~TransformData() { TSIOBufferDestroy(output_buffer); }

  TSVIO            output_vio    = nullptr;
  TSIOBuffer       output_buffer = nullptr;
  TSIOBufferReader output_reader = nullptr;
  std::string      input;
  HtmlOperation    operation;
  bool             output_complete = false;
};

bool
write_transformed_output(TransformData &data)
{
  if (data.input.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
    return false;
  }

  size_t output_capacity = data.input.size() + 1;
  if (data.operation == HtmlOperation::ESCAPE) {
    if (data.input.size() > (std::numeric_limits<size_t>::max() - 1) / 6) {
      return false;
    }
    output_capacity = data.input.size() * 6 + 1;
  }

  std::vector<char> output(output_capacity);
  size_t            output_length = 0;

  TSReturnCode result =
    data.operation == HtmlOperation::ESCAPE ?
      TSStringHtmlEscape(data.input.data(), static_cast<int>(data.input.size()), output.data(), output.size(), &output_length,
                         !TS_HTML_ESCAPE_USE_ATTRIBUTE_MODE) :
      TSStringHtmlUnescape(data.input.data(), static_cast<int>(data.input.size()), output.data(), output.size(), &output_length);
  if (result != TS_SUCCESS) {
    return false;
  }
  if (TSIOBufferWrite(data.output_buffer, output.data(), output_length) != static_cast<int64_t>(output_length)) {
    return false;
  }

  TSVIONBytesSet(data.output_vio, output_length);
  data.output_complete = true;
  TSVIOReenable(data.output_vio);
  return true;
}

void
handle_transform(TSCont contp, HtmlOperation operation)
{
  TSVIO input_vio = TSVConnWriteVIOGet(contp);
  auto *data      = static_cast<TransformData *>(TSContDataGet(contp));

  if (!data) {
    data             = new TransformData{operation};
    data->output_vio = TSVConnWrite(TSTransformOutputVConnGet(contp), contp, data->output_reader, INT64_MAX);
    TSContDataSet(contp, data);
  }

  if (!TSVIOBufferGet(input_vio)) {
    if (!data->output_complete && !write_transformed_output(*data)) {
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    }
    return;
  }

  int64_t to_read = std::min(TSVIONTodoGet(input_vio), TSIOBufferReaderAvail(TSVIOReaderGet(input_vio)));
  if (to_read > 0) {
    size_t old_size = data->input.size();

    data->input.resize(old_size + static_cast<size_t>(to_read));
    TSIOBufferReaderCopy(TSVIOReaderGet(input_vio), data->input.data() + old_size, to_read);
    TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), to_read);
    TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + to_read);
  }

  if (TSVIONTodoGet(input_vio) > 0) {
    if (to_read > 0) {
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    }
    return;
  }

  if (!data->output_complete && !write_transformed_output(*data)) {
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    return;
  }
  TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
}

int
transform_handler(TSCont contp, TSEvent event, HtmlOperation operation)
{
  if (TSVConnClosedGet(contp)) {
    delete static_cast<TransformData *>(TSContDataGet(contp));
    TSContDestroy(contp);
    return 0;
  }

  switch (event) {
  case TS_EVENT_ERROR: {
    TSVIO input_vio = TSVConnWriteVIOGet(contp);

    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
    break;
  }
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;
  default:
    handle_transform(contp, operation);
    break;
  }
  return 0;
}

int
escape_transform_handler(TSCont contp, TSEvent event, void * /* edata */)
{
  return transform_handler(contp, event, HtmlOperation::ESCAPE);
}

int
unescape_transform_handler(TSCont contp, TSEvent event, void * /* edata */)
{
  return transform_handler(contp, event, HtmlOperation::UNESCAPE);
}

int
response_hook(TSCont /* contp */, TSEvent event, void *edata)
{
  auto txnp = static_cast<TSHttpTxn>(edata);

  if (event == TS_EVENT_HTTP_READ_RESPONSE_HDR) {
    int   url_length = 0;
    char *url        = TSHttpTxnEffectiveUrlStringGet(txnp, &url_length);
    bool  unescape   = url && std::string_view{url, static_cast<size_t>(url_length)}.ends_with("/unescape");

    TSfree(url);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK,
                     TSTransformCreate(unescape ? unescape_transform_handler : escape_transform_handler, txnp));
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}
} // namespace

void
TSPluginInit(int /* argc */, const char ** /* argv */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, TSContCreate(response_hook, nullptr));
}
