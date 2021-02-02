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
#include "RPCServer.h"
#include "rpc/server/IPCSocketServer.h"

inkcoreapi rpc::RPCServer *jsonrpcServer = nullptr;

namespace rpc
{
static const auto logTag{"rpc"};

RPCServer::RPCServer(config::RPCConfig const &conf)
{
  switch (conf.get_comm_type()) {
  case config::RPCConfig::CommType::UNIX: {
    _socketImpl     = std::make_unique<comm::IPCSocketServer>();
    auto const &ret = _socketImpl->configure(conf.get_comm_config_params());
    if (ret) {
      Warning("Unable to configure the socket impl: %s", ret.top().text().c_str());
    }
  } break;
  default:;
    throw std::runtime_error("Unsupported communication type.");
  };

  assert(_socketImpl != nullptr);

  // TODO: handle this properly.
  if (auto error = _socketImpl->init(); error.size() > 0) {
    throw std::runtime_error(error.top().text().c_str());
  }
}

std::string_view
RPCServer::selected_comm_name() const noexcept
{
  return _socketImpl->name();
}

RPCServer::~RPCServer()
{
  stop_thread();
}

void * /* static */
RPCServer::run_thread(void *a)
{
  void *ret = a;
  if (jsonrpcServer->_init) {
    jsonrpcServer->_rpcThread = jsonrpcServer->_init();
  }
  jsonrpcServer->_socketImpl->run();
  Debug(logTag, "Socket stopped");
  return ret;
}

void
RPCServer::start_thread(std::function<TSThread()> const &cb_init, std::function<void(TSThread)> const &cb_destroy)
{
  Debug(logTag, "Starting RPC Server on: %s", _socketImpl->name().data());
  _init    = cb_init;
  _destroy = cb_destroy;

  ink_thread_create(&_this_thread, run_thread, nullptr, 0, 0, nullptr);
}

void
RPCServer::stop_thread()
{
  _socketImpl->stop();

  ink_thread_join(_this_thread);
  _this_thread = ink_thread_null();
  Debug(logTag, "Stopping RPC server on: %s", _socketImpl->name().data());
}
} // namespace rpc
