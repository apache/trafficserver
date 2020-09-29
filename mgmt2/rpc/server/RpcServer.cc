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
#include "RpcServer.h"
#include "rpc/server/LocalUnixSocket.h"

// rethink the whole global stuff/ singleton?
inkcoreapi rpc::RpcServer *jsonrpcServer = nullptr;

namespace rpc
{
static const auto logTag{"rpc"};
RpcServer::RpcServer(config::RPCConfig const &conf)
{
  switch (conf.get_transport_type()) {
  case config::RPCConfig::TransportType::UNIX_DOMAIN_SOCKET: {
    _transport      = std::make_unique<transport::LocalUnixSocket>();
    auto const &ret = _transport->configure(conf.get_transport_config_params());
    if (ret) {
      Warning("Unable to configure the transport: %s", ret.top().text().c_str());
    }
  } break;
  default:;
    throw std::runtime_error("Unsupported transport type.");
  };

  assert(_transport != nullptr);

  // TODO: handle this properly.
  if (auto error = _transport->init(); error.size() > 0) {
    throw std::runtime_error(error.top().text().c_str());
  }
}

std::string_view
RpcServer::selected_transport_name() const noexcept
{
  return _transport->name();
}

void
RpcServer::join_thread() noexcept
{
  if (running_thread.joinable()) {
    try {
      running_thread.join();
    } catch (std::system_error const &se) {
      Warning("Found an issue during join: %s", se.what());
    }
  }
}

RpcServer::~RpcServer()
{
  this->join_thread();
}

void
RpcServer::thread_start()
{
  Debug(logTag, "Starting RPC Server on: %s", _transport->name().data());
  running_thread = std::thread([&]() { _transport->run(); });
}

void
RpcServer::stop()
{
  _transport->stop();
  this->join_thread();
  Debug(logTag, "Stopping RPC server on: %s", _transport->name().data());
}
} // namespace rpc
