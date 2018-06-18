/**
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

#include <iostream>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>

using namespace atscppapi;

#define TAG "timeout_example_plugin"

namespace
{
GlobalPlugin *plugin;
}

class TimeoutExamplePlugin : public GlobalPlugin
{
public:
  TimeoutExamplePlugin()
  {
    registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
    registerHook(HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    TS_DEBUG(TAG, "Sending response headers to the client, status=%d", transaction.getClientResponse().getStatusCode());
    transaction.resume();
  }

  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    TS_DEBUG(TAG, "Setting all timeouts to 1ms, this will likely cause the transaction to receive a 504.");
    transaction.setTimeout(Transaction::TIMEOUT_CONNECT, 1);
    transaction.setTimeout(Transaction::TIMEOUT_ACTIVE, 1);
    transaction.setTimeout(Transaction::TIMEOUT_DNS, 1);
    transaction.setTimeout(Transaction::TIMEOUT_NO_ACTIVITY, 1);
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_Timeout", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  TS_DEBUG(TAG, "TSPluginInit");
  plugin = new TimeoutExamplePlugin();
}
