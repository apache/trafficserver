/** @file

  TLSSTunnelSupport.cc provides implementations for
  TLSTunnelSupport methods

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

#include "iocore/net/PreWarm.h"
#include "iocore/net/SSLTypes.h"
#include "iocore/net/TLSTunnelSupport.h"
#include "tscore/ink_assert.h"
#include "tscore/Diags.h"

#include "swoc/IPEndpoint.h"

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

void
TLSTunnelSupport::set_tunnel_destination(const std::string_view &destination, SNIRoutingType type, bool port_is_dynamic,
                                         YamlSNIConfig::TunnelPreWarm prewarm)
{
  _tunnel_type     = type;
  _tunnel_prewarm  = prewarm;
  _port_is_dynamic = port_is_dynamic;

  if (std::string_view host, port; swoc::IPEndpoint::tokenize(destination, &host, &port)) {
    _tunnel_port = swoc::svtou(port);
    _tunnel_host = host;
  } else {
    Warning("Invalid destination \"%.*s\" in SNI configuration.", int(destination.size()), destination.data());
  }
}

PreWarm::SPtrConstDst
TLSTunnelSupport::create_dst(int pid) const
{
  return std::make_shared<const PreWarm::Dst>(get_tunnel_host(), get_tunnel_port(),
                                              is_upstream_tls() ? SNIRoutingType::PARTIAL_BLIND : SNIRoutingType::FORWARD, pid);
}
