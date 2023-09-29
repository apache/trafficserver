/** @file

  Test a plugin's interaction with the logging interface.

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
#include <string>
#include <cstring>

constexpr auto plugin_name = "test_log_interface";

static TSTextLogObject pluginlog;

bool
is_get_request(TSHttpTxn transaction)
{
  TSMLoc req_loc;
  TSMBuffer req_bufp;
  if (TSHttpTxnClientReqGet(transaction, &req_bufp, &req_loc) == TS_ERROR) {
    TSError("Error while retrieving client request header\n");
    return false;
  }
  int method_len     = 0;
  const char *method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);
  if (method_len != static_cast<int>(strlen(TS_HTTP_METHOD_GET)) || strncasecmp(method, TS_HTTP_METHOD_GET, method_len) != 0) {
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
    return false;
  }
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
  return true;
}

int
global_handler(TSCont continuation, TSEvent event, void *data)
{
  TSHttpSsn session     = static_cast<TSHttpSsn>(data);
  TSHttpTxn transaction = static_cast<TSHttpTxn>(data);

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    if (is_get_request(transaction)) {
      const std::string long_line(5000, 's');
      TSTextLogObjectWrite(pluginlog, "Got a GET request. Writing a long line: %s", long_line.c_str());
    }
    TSHttpTxnReenable(transaction, TS_EVENT_HTTP_CONTINUE);
    return 0;

  default:
    TSError("[%s] global_handler: unexpected event: %d\n", plugin_name, event);
    break;
  }

  TSHttpSsnReenable(session, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
TSPluginInit(int argc, const char **argv)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = const_cast<char *>(plugin_name);
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");
  info.vendor_name   = const_cast<char *>("Verizon Media");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] plugin registration failed\n", plugin_name);
    return;
  }

  TSAssert(TS_SUCCESS == TSTextLogObjectCreate(plugin_name, TS_LOG_MODE_ADD_TIMESTAMP, &pluginlog));
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(global_handler, nullptr));
}
