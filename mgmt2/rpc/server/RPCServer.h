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

#include "tscore/ink_thread.h"
#include "tscore/ink_mutex.h"

#include "tscore/ink_apidefs.h"
#include <ts/apidefs.h>
#include "rpc/jsonrpc/JsonRPCManager.h"
#include "rpc/config/JsonRPCConfig.h"

namespace rpc
{
namespace comm
{
  struct BaseCommInterface;
}

///
/// @brief RPC Server implementation for the JSONRPC Logic. This class holds a transport which implements @see
/// BaseCommInterface
/// Objects of this class can start @see start_thread , stop @see stop_thread the server at any? time. More than one instance of
/// this class can be created as long as they use different transport configuration.
class RPCServer
{
public:
  RPCServer() = default;
  ///
  /// @brief Construct a new Rpc Server object
  ///        This function have one main goal, select the transport type base on the configuration and  initialize it.
  ///
  /// @throw std::runtime_error if:
  ///        1 - It the configured transport isn't valid for the server to create it, then an exception will be thrown.
  ///        2 - The transport layer cannot be initialized.
  /// @param conf the configuration object.
  ///
  RPCServer(config::RPCConfig const &conf);

  /// @brief The destructor will join the thread.
  ~RPCServer();

  /// @brief Returns a descriptive name that was set by the transport. Check @see BaseCommInterface
  std::string_view selected_comm_name() const noexcept;

  /// @brief Thread function that runs the transport.
  void start_thread(std::function<TSThread()> const &cb_init        = std::function<TSThread()>(),
                    std::function<void(TSThread)> const &cb_destroy = std::function<void(TSThread)>());

  /// @brief Function to stop the transport and join the thread to finish.
  void stop_thread();

private:
  /// @brief Actual thread routine. This will start the socket.
  static void *run_thread(void *);

  std::function<TSThread()> _init;
  std::function<void(TSThread)> _destroy;
  ink_thread _this_thread{ink_thread_null()};
  TSThread _rpcThread{nullptr};

  std::thread running_thread;
  std::unique_ptr<comm::BaseCommInterface> _socketImpl;
};
} // namespace rpc

extern rpc::RPCServer *jsonrpcServer;
