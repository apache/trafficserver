/** @file

  TLSSTunnelSupport.cc provides implementations for
  TLSTunnelSupport methods

  @par License

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

#include "iocore/net/PreWarm.h"
#include "iocore/net/SSLTypes.h"
#include "iocore/net/TLSTunnelSupport.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"

#include "swoc/IPEndpoint.h"
#include "swoc/TextView.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <memory>

int TLSTunnelSupport::_ex_data_index = -1;

void
TLSTunnelSupport::initialize()
{
  ink_assert(_ex_data_index == -1);
  if (_ex_data_index == -1) {
    _ex_data_index = SSL_get_ex_new_index(0, (void *)"TLSTunnelSupport index", nullptr, nullptr, nullptr);
  }
}

TLSTunnelSupport *
TLSTunnelSupport::getInstance(SSL *ssl)
{
  return static_cast<TLSTunnelSupport *>(SSL_get_ex_data(ssl, _ex_data_index));
}

void
TLSTunnelSupport::bind(SSL *ssl, TLSTunnelSupport *srs)
{
  SSL_set_ex_data(ssl, _ex_data_index, srs);
}

void
TLSTunnelSupport::unbind(SSL *ssl)
{
  SSL_set_ex_data(ssl, _ex_data_index, nullptr);
}

void
TLSTunnelSupport::_clear()
{
}

bool
TLSTunnelSupport::set_tunnel_destination(const std::string_view &destination, SNIRoutingType type, bool port_is_dynamic,
                                         YamlSNIConfig::TunnelPreWarm prewarm)
{
  auto const               authority = split_destination(destination);
  std::optional<in_port_t> port;
  if (authority && authority->port) {
    port = parse_port(*authority->port);
  }
  if (!authority || authority->host.empty() || (authority->port && !port)) {
    _tunnel_host.clear();
    _tunnel_port = 0;
    return false;
  }

  // No port leaves 0, for which the tunnel uses the inbound port.
  _tunnel_port     = port.value_or(0);
  _tunnel_host     = authority->host;
  _tunnel_type     = type;
  _tunnel_prewarm  = prewarm;
  _port_is_dynamic = port_is_dynamic;
  return true;
}

PreWarm::SPtrConstDst
TLSTunnelSupport::create_dst(int pid) const
{
  return std::make_shared<const PreWarm::Dst>(get_tunnel_host(), get_tunnel_port(),
                                              is_upstream_tls() ? SNIRoutingType::PARTIAL_BLIND : SNIRoutingType::FORWARD, pid);
}

std::optional<TLSTunnelSupport::Authority>
TLSTunnelSupport::split_destination(std::string_view destination)
{
  std::string_view host, digits, rest;
  swoc::IPEndpoint::tokenize(destination, &host, &digits, &rest);

  // tokenize takes everything after an unclosed bracket as the address, and leaves text after a
  // closing bracket that is not a port in rest.
  if (swoc::TextView{destination}.ltrim_if(&isspace).starts_with("[") && destination.find(']') == std::string_view::npos) {
    return std::nullopt;
  }
  if (!digits.empty()) {
    // The leading digits of the port, which rest continues.
    return Authority{
      host, std::string_view{digits.data(), digits.size() + rest.size()}
    };
  }
  if (rest.empty()) {
    return Authority{host, std::nullopt};
  }
  // With no digits after the separator, tokenize leaves the separator at the front of rest.
  if (rest.front() != ':') {
    return std::nullopt;
  }
  return Authority{host, rest.substr(1)};
}

std::optional<in_port_t>
TLSTunnelSupport::parse_port(std::string_view text)
{
  if (text.empty() || text.size() > 5 || text.front() == '0' ||
      !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
    return std::nullopt;
  }
  if (auto const port = swoc::svtou(text); port <= std::numeric_limits<in_port_t>::max()) {
    return static_cast<in_port_t>(port);
  }
  return std::nullopt;
}
