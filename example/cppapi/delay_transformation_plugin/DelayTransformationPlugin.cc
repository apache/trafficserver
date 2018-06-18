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
#include <string_view>
#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/TransactionPlugin.h>
#include <atscppapi/TransformationPlugin.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/Logger.h>

#include "ts/ts.h"

using namespace atscppapi;

/*
 * This example demonstrates how you can pause a transformation and resume it
 * after doing some work simply by calling the pause method.
 * To resume just schedule the return value of pause method (cont)
 */

namespace
{
#define TAG "delay_transformation"
GlobalPlugin *plugin;
} // namespace

class DelayTransformationPlugin : public TransformationPlugin
{
public:
  DelayTransformationPlugin(Transaction &transaction, TransformationPlugin::Type xformType)
    : TransformationPlugin(transaction, xformType)
  {
    registerHook((xformType == TransformationPlugin::REQUEST_TRANSFORMATION) ? HOOK_SEND_REQUEST_HEADERS :
                                                                               HOOK_SEND_RESPONSE_HEADERS);
  }

  void
  handleSendRequestHeaders(Transaction &transaction) override
  {
    transaction.getServerRequest().getHeaders()["X-Content-Delayed"] = "1";
    transaction.resume();
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    transaction.getClientResponse().getHeaders()["X-Content-Delayed"] = "1";
    transaction.resume();
  }

  void
  consume(std::string_view data) override
  {
    TS_DEBUG(TAG, "Consuming...");
    produce(data);

    TS_DEBUG(TAG, "Pausing...");
    TSCont cont = pause();

    if (cont) {
      TS_DEBUG(TAG, "Resuming in 2ms...");
      TSContSchedule(cont, 2, TS_THREAD_POOL_NET);
    }
  }

  void
  handleInputComplete() override
  {
    TS_DEBUG(TAG, "handleInputComplete");
    setOutputComplete();
  }

  ~DelayTransformationPlugin() override {}
};

class GlobalHookPlugin : public GlobalPlugin
{
public:
  GlobalHookPlugin()
  {
    registerHook(HOOK_READ_REQUEST_HEADERS_POST_REMAP);
    registerHook(HOOK_READ_RESPONSE_HEADERS);
  }

  void
  handleReadRequestHeadersPostRemap(Transaction &transaction) override
  {
    transaction.addPlugin(new DelayTransformationPlugin(transaction, TransformationPlugin::REQUEST_TRANSFORMATION));
    transaction.resume();
  }

  void
  handleReadResponseHeaders(Transaction &transaction) override
  {
    transaction.addPlugin(new DelayTransformationPlugin(transaction, TransformationPlugin::RESPONSE_TRANSFORMATION));
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_DelayTransformation", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  TS_DEBUG(TAG, "TSPluginInit");
  plugin = new GlobalHookPlugin();
}
