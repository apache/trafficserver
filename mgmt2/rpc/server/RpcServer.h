/**
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

#include <atomic>
#include <thread>
#include <memory>

#include "tscore/Diags.h"

#include "rpc/jsonrpc/JsonRpc.h"
#include "rpc/config/JsonRpcConfig.h"

namespace rpc
{
namespace transport
{
  struct BaseTransportInterface;
}

class RpcServer
{
public:
  RpcServer() = default;
  RpcServer(config::RPCConfig const &conf);

  ~RpcServer();
  void thread_start();
  void stop();
  bool is_running() const;

private:
  void join_thread();
  std::thread running_thread;
  std::unique_ptr<transport::BaseTransportInterface> _transport;
};
} // namespace rpc

inkcoreapi extern rpc::RpcServer *jsonrpcServer;