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
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/TransactionPlugin.h"
#include "tscpp/api/PluginInit.h"

using namespace atscppapi;

namespace
{
GlobalPlugin *plugin;
}

class TransactionHookPlugin : public atscppapi::TransactionPlugin
{
public:
  explicit TransactionHookPlugin(Transaction &transaction) : TransactionPlugin(transaction)
  {
    char_ptr_ = new char[100];
    TransactionPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
    std::cout << "Constructed!" << std::endl;
  }

  ~TransactionHookPlugin() override
  {
    delete[] char_ptr_; // cleanup
    std::cout << "Destroyed!" << std::endl;
  }
  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    std::cout << "Send response headers!" << std::endl;
    transaction.resume();
  }

private:
  char *char_ptr_;
};

class GlobalHookPlugin : public atscppapi::GlobalPlugin
{
public:
  GlobalHookPlugin() { GlobalPlugin::registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP); }
  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    std::cout << "Hello from handleReadRequesHeadersPreRemap!" << std::endl;
    transaction.addPlugin(new TransactionHookPlugin(transaction));
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_TransactionHook", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new GlobalHookPlugin();
}
