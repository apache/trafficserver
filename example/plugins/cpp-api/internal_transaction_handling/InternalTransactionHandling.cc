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

#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/Logger.h"
#include "tscpp/api/PluginInit.h"
#include "tscpp/api/AsyncHttpFetch.h"

using namespace atscppapi;
using std::string;

namespace
{
GlobalPlugin *plugin;
GlobalPlugin *plugin2;
} // namespace

#define TAG "internal_transaction_handling"

class AllTransactionsGlobalPlugin : public GlobalPlugin
{
public:
  AllTransactionsGlobalPlugin() : GlobalPlugin()
  {
    TS_DEBUG(TAG, "Registering a global hook HOOK_READ_REQUEST_HEADERS_POST_REMAP");
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    TS_DEBUG(TAG, "Received a request in handleReadRequestHeadersPostRemap.");
    transaction.resume();
  }
};

class NoInternalTransactionsGlobalPlugin : public GlobalPlugin, public AsyncReceiver<AsyncHttpFetch>
{
public:
  NoInternalTransactionsGlobalPlugin() : GlobalPlugin(true)
  {
    TS_DEBUG(TAG, "Registering a global hook HOOK_READ_REQUEST_HEADERS_POST_REMAP");
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    TS_DEBUG(TAG, "Received a request in handleReadRequestHeadersPostRemap.");
    std::shared_ptr<Mutex> mutex(new Mutex());                                            // required for async operation
    Async::execute<AsyncHttpFetch>(this, new AsyncHttpFetch("http://127.0.0.1/"), mutex); // internal transaction
    transaction.resume();
  }

  void
  handleAsyncComplete(AsyncHttpFetch &provider ATSCPPAPI_UNUSED) override
  {
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_InternalTransactionHandling", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  TS_DEBUG(TAG, "Loaded async_http_fetch_example plugin");
  plugin  = new AllTransactionsGlobalPlugin();
  plugin2 = new NoInternalTransactionsGlobalPlugin();
}
