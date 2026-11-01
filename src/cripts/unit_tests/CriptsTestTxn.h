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

#include "iocore/eventsystem/ConfigProcessor.h"
#include "proxy/http/HttpConfig.h"
#include "proxy/http/HttpSM.h"
#include "proxy/hdrs/HTTP.h"

// The ATS core headers define macros that collide with cripts names.
#undef Status
#undef Error

#include "cripts/Lulu.hpp"
#include "cripts/Context.hpp"

#include <catch2/catch_test_macros.hpp>

#include <iterator>
#include <memory>
#include <string>
#include <string_view>

namespace cripts_test
{
// Catch2 cannot print the cripts components, which only convert to a view through a non-const GetSV().
template <typename T>
std::string
str(T &&component)
{
  return std::string{component.GetSV()};
}

inline void
parse_into(HTTPHdr &hdr, HTTPType type, std::string_view msg)
{
  HTTPParser  parser;
  const char *start = msg.data();

  http_parser_init(&parser);
  // create() reuses an existing heap but keeps the old header's cached host and URL.
  hdr.destroy();
  hdr.create(type, HTTP_1_1);
  if (type == HTTPType::REQUEST) {
    REQUIRE(hdr.parse_req(&parser, &start, msg.data() + msg.size(), true) == ParseResult::DONE);
  } else {
    REQUIRE(hdr.parse_resp(&parser, &start, msg.data() + msg.size(), true) == ParseResult::DONE);
  }
  http_parser_clear(&parser);
}

// A transaction that is just alive enough for the TSHttpTxn*Get() header accessors, which is all the
// cripts header and URL objects need. Nothing here runs the state machine.
class Txn
{
public:
  explicit Txn(std::string_view client_request)
  {
    if (HttpConfig::m_id == 0) {
      HttpConfig::m_id = configProcessor.set(HttpConfig::m_id, new HttpConfigParams);
    }

    _sm.magic                     = HttpSmMagic_t::ALIVE;
    _sm.t_state.http_config_param = HttpConfig::acquire();
    _sm.t_state.txn_conf          = &_sm.t_state.http_config_param->oride;
    parse_into(_sm.t_state.hdr_info.client_request, HTTPType::REQUEST, client_request);

    _context = std::make_unique<cripts::Context>(reinterpret_cast<TSHttpTxn>(&_sm), nullptr, nullptr, _instance);
  }

  // The Context points back into this object.
  Txn(const Txn &)            = delete;
  Txn &operator=(const Txn &) = delete;

  ~Txn()
  {
    _context->reset();
    _context.reset();
    _sm.t_state.hdr_info.client_request.destroy();
    _sm.t_state.hdr_info.client_response.destroy();
    _sm.t_state.hdr_info.server_request.destroy();
    _sm.t_state.hdr_info.server_response.destroy();
  }

  void
  set_server_response(std::string_view msg)
  {
    parse_into(_sm.t_state.hdr_info.server_response, HTTPType::RESPONSE, msg);
  }

  // Raw MIME value of the client request, bypassing cripts, to check what cripts actually wrote.
  std::string
  client_request_field(std::string_view name) const
  {
    auto const *f = _sm.t_state.hdr_info.client_request.field_find(name);

    if (f == nullptr) {
      return {};
    }
    return std::string{f->value_get()};
  }

  int
  client_request_field_count(std::string_view name) const
  {
    int count = 0;

    for (auto const *f = _sm.t_state.hdr_info.client_request.field_find(name); f != nullptr; f = f->m_next_dup) {
      ++count;
    }
    return count;
  }

  // The client request URL as the core sees it, bypassing anything cripts has cached.
  URL &
  client_url()
  {
    return *_sm.t_state.hdr_info.client_request.url_get();
  }

  cripts::Context *
  context()
  {
    return _context.get();
  }

  HttpSM &
  sm()
  {
    return _sm;
  }

private:
  static inline const char *_argv[] = {"http://from.example.com", "http://to.example.com"};

  HttpSM                           _sm;
  cripts::Instance                 _instance{static_cast<int>(std::size(_argv)), _argv};
  std::unique_ptr<cripts::Context> _context;
};

} // namespace cripts_test
