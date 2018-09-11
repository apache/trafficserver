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
#include <vector>
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/TransactionPlugin.h"
#include "tscpp/api/PluginInit.h"

using namespace atscppapi;
namespace
{
GlobalPlugin *plugin;
}

class MultipleTransactionHookPluginsOne : public atscppapi::TransactionPlugin
{
public:
  MultipleTransactionHookPluginsOne(Transaction &transaction) : TransactionPlugin(transaction)
  {
    TransactionPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
    std::cout << "Constructed MultipleTransactionHookPluginsOne!" << std::endl;
  }

  ~MultipleTransactionHookPluginsOne() override { std::cout << "Destroyed MultipleTransactionHookPluginsOne!" << std::endl; }
  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    std::cerr << "MultipleTransactionHookPluginsOne -- Send response headers!" << std::endl;
    transaction.resume();
  }
};

class MultipleTransactionHookPluginsTwo : public atscppapi::TransactionPlugin
{
public:
  MultipleTransactionHookPluginsTwo(Transaction &transaction) : TransactionPlugin(transaction)
  {
    TransactionPlugin::registerHook(HOOK_SEND_REQUEST_HEADERS);
    TransactionPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
    std::cout << "Constructed MultipleTransactionHookPluginsTwo!" << std::endl;
  }

  ~MultipleTransactionHookPluginsTwo() override { std::cout << "Destroyed MultipleTransactionHookPluginsTwo!" << std::endl; }
  void
  handleSendRequestHeaders(Transaction &transaction) override
  {
    std::cout << "MultipleTransactionHookPluginsTwo -- Send request headers!" << std::endl;
    some_container_.push_back("We have transaction scoped storage in Transaction Hooks!");
    transaction.resume();
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    std::cout << "MultipleTransactionHookPluginsTwo -- Send response headers!" << std::endl;

    // Demonstrate the concept of transaction scoped storage.
    if (some_container_.size()) {
      std::cout << some_container_.back() << std::endl;
    }

    transaction.resume();
  }

private:
  std::vector<std::string> some_container_;
};

class GlobalHookPlugin : public atscppapi::GlobalPlugin
{
public:
  GlobalHookPlugin() { GlobalPlugin::registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP); }
  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    std::cout << "Hello from handleReadRequesHeadersPreRemap!" << std::endl;

    // We need not store the addresses of the transaction plugins
    // because they will be cleaned up automatically when the transaction
    // closes.

    transaction.addPlugin(new MultipleTransactionHookPluginsOne(transaction));
    transaction.addPlugin(new MultipleTransactionHookPluginsTwo(transaction));

    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_MultipleTransactionHook", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new GlobalHookPlugin();
}
