/*
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

#include <array>
#include <variant>
#include "ts/ts.h"
#include "ts/remap.h"

#include "cripts/Instance.hpp"
#include "cripts/Headers.hpp"
#include "cripts/Urls.hpp"
#include "cripts/Connections.hpp"

// These are pretty arbitrary for now
constexpr int CONTEXT_DATA_SLOTS = 4;

namespace Cript
{
class Context
{
  using self_type = Context;
  using DataType  = std::variant<integer, double, boolean, void *, Cript::string>;

public:
  Context()                         = delete;
  Context(const self_type &)        = delete;
  void operator=(const self_type &) = delete;

  // This will, and should, only be called via the ProxyAllocator as used in the factory.
  Context(TSHttpTxn txn_ptr, TSHttpSsn ssn_ptr, TSRemapRequestInfo *rri_ptr, Cript::Instance &inst) : rri(rri_ptr), p_instance(inst)
  {
    state.txnp    = txn_ptr;
    state.ssnp    = ssn_ptr;
    state.context = this;
  }

  // Clear the cached header mloc's etc.
  void reset();

  // Freelist management, a thread_local freelist of Context objects. These are implemented in Lulu.cc.
  static self_type *factory(TSHttpTxn txn_ptr, TSHttpSsn ssn_ptr, TSRemapRequestInfo *rri_ptr, Cript::Instance &inst);
  void              release();

  // These fields are preserving the parameters as setup in DoRemap()
  Cript::Transaction                       state;
  std::array<DataType, CONTEXT_DATA_SLOTS> data;
  TSCont                                   default_cont = nullptr;
  TSRemapRequestInfo                      *rri          = nullptr;
  Cript::Instance                         &p_instance; // p_ == public_, since we can't use "instance"

  // These are private, but needs to be visible to our friend classes that
  // depends on the Context.
private:
  friend class Client::Request;
  friend class Client::Response;
  friend class Client::Connection;
  friend class Client::URL;
  friend class Remap::From::URL;
  friend class Remap::To::URL;
  friend class Server::Request;
  friend class Server::Response;
  friend class Server::Connection;
  friend class Pristine::URL;
  friend class Cache::URL;
  friend class Parent::URL;
  friend class Plugin::Remap;

  // These are "pre-allocated", but not initialized. They will be initialized
  // when used via a factory.
  Client::Response   _client_resp_header;
  Client::Request    _client_req_header;
  Client::Connection _client_conn;
  Client::URL        _client_url;
  Remap::From::URL   _remap_from_url;
  Remap::To::URL     _remap_to_url;
  Pristine::URL      _pristine_url;
  Server::Response   _server_resp_header;
  Server::Request    _server_req_header;
  Server::Connection _server_conn;
  Cache::URL         _cache_url;
  Parent::URL        _parent_url;
}; // End class Context

} // namespace Cript

// This may be weird, but oh well for now.
#define Get()               _get(context)
#define Set(_value)         _set(context, _value)
#define update()            _update(context)
#define runRemap()          _runRemap(context)
#define activate()          _activate(context->p_instance)
#define transaction         context->state
#define txn_data            context->data
#define instance            context->p_instance
#define CDebug(...)         context->p_instance.debug(__VA_ARGS__)
#define CDebugOn()          context->p_instance.debugOn()
#define DisableCallback(cb) context->state.disableCallback(cb)
