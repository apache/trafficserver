/** @file

  WebSocket termination example.

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

#pragma once

#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/InterceptPlugin.h"

#include <string>
#include <cstddef>

#include "WSBuffer.h"

// WebSocket InterceptPlugin

using atscppapi::InterceptPlugin;
using atscppapi::Transaction;
using atscppapi::GlobalPlugin;

class WebSocket : public InterceptPlugin
{
public:
  WebSocket(Transaction &transaction);
  ~WebSocket() override;

  void consume(const std::string &data, InterceptPlugin::RequestDataType type) override;
  void handleInputComplete() override;

  void ws_send(std::string const &data, int code);
  void ws_receive(std::string const &data, int code);

private:
  std::string headers_;
  std::string body_;

  std::string ws_key_; // value of sec-websocket-key header
  WSBuffer ws_buf_;    // incoming data.
};

class WebSocketInstaller : public GlobalPlugin
{
public:
  WebSocketInstaller();

  void handleReadRequestHeadersPreRemap(Transaction &transaction) override;
};
