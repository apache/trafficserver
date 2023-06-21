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

#include <string>
#include <unordered_map>

#include "proxy_wasm_intrinsics.h"

class ExampleRootContext : public RootContext
{
public:
  explicit ExampleRootContext(uint32_t id, std::string_view root_id) : RootContext(id, root_id) {}

  bool onStart(size_t) override;
};

class ExampleContext : public Context
{
public:
  explicit ExampleContext(uint32_t id, RootContext *root) : Context(id, root) {}

  FilterHeadersStatus onRequestHeaders(uint32_t headers, bool end_of_stream) override;
};
static RegisterContextFactory register_ExampleContext(CONTEXT_FACTORY(ExampleContext), ROOT_FACTORY(ExampleRootContext),
                                                      "myproject");

bool ExampleRootContext::onStart(size_t)
{
  logInfo(std::string("onStart"));
  return true;
}

FilterHeadersStatus
ExampleContext::onRequestHeaders(uint32_t headers, bool end_of_stream)
{
  // print UA
  auto ua = getRequestHeader("User-Agent");
  logInfo(std::string("UA ") + std::string(ua->view()));

  auto context_id = id();
  auto callback = [context_id](uint32_t, size_t body_size, uint32_t) {
    logInfo("async call done");
    if (body_size == 0) {
      logInfo("async_call failed");
      return;
    }
    auto response_headers = getHeaderMapPairs(WasmHeaderMapType::HttpCallResponseHeaders);
    auto body = getBufferBytes(WasmBufferType::HttpCallResponseBody, 0, body_size);
    for (auto& p : response_headers->pairs()) {
      logInfo(std::string(p.first) + std::string(" -> ") + std::string(p.second));
    }
    logInfo(std::string(body->view()));

    getContext(context_id)->setEffectiveContext();
    logInfo("continueRequest");
    continueRequest();
  };
  std::string test = std::string(ua->view());
  if (test == "test") {
    root()->httpCall("cluster", {{":method", "GET"}, {":path", "/.well-known/security.txt"}, {":authority", "www.google.com"}},
                       "", {}, 10000, callback);
    return FilterHeadersStatus::StopIteration;
  }

  return FilterHeadersStatus::Continue;
}
