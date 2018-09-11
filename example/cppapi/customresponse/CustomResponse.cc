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
#include <string>
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/TransactionPlugin.h"
#include "tscpp/api/PluginInit.h"

using namespace atscppapi;

using std::cout;
using std::endl;
using std::string;

/*
 *
 * This example demonstrates how you can exploit .error() to send
 * any response from any state by forcing the state machine to
 * jump to the error state. You will then send your custom
 * response in sendResponseHeaders() rather than the error page.
 *
 */

namespace
{
GlobalPlugin *plugin;
}

class CustomResponseTransactionPlugin : public atscppapi::TransactionPlugin
{
public:
  CustomResponseTransactionPlugin(Transaction &transaction, HttpStatus status, const string &reason, const string &body)
    : TransactionPlugin(transaction), status_(status), reason_(reason), body_(body)
  {
    TransactionPlugin::registerHook(HOOK_SEND_RESPONSE_HEADERS);
    transaction.error(body_); // Set the error body now, and change the status and reason later.
  }

  void
  handleSendResponseHeaders(Transaction &transaction) override
  {
    transaction.getClientResponse().setStatusCode(status_);
    transaction.getClientResponse().setReasonPhrase(reason_);
    transaction.resume();
  }

  ~CustomResponseTransactionPlugin() override {}

private:
  HttpStatus status_;
  string reason_;
  string body_;
};

class ClientRedirectGlobalPlugin : public GlobalPlugin
{
public:
  ClientRedirectGlobalPlugin() { registerHook(HOOK_SEND_REQUEST_HEADERS); }
  void
  handleSendRequestHeaders(Transaction &transaction) override
  {
    if (transaction.getClientRequest().getUrl().getQuery().find("custom=1") != string::npos) {
      transaction.addPlugin(new CustomResponseTransactionPlugin(transaction, HTTP_STATUS_OK, "Ok",
                                                                "Hello! This is a custom response without making "
                                                                "an origin request and no server intercept."));
      return; // dont forget to return since the CustomResponse call will call .error().
    }
    transaction.resume();
  }
};

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  if (!RegisterGlobalPlugin("CPP_Example_CustomResponse", "apache", "dev@trafficserver.apache.org")) {
    return;
  }
  plugin = new ClientRedirectGlobalPlugin();
}
