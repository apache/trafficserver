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

#include "TLSTunnelSupport.h"
#include "tscore/ink_assert.h"

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
  ats_free(_tunnel_host);
}

void
TLSTunnelSupport::set_tunnel_destination(const std::string_view &destination, SNIRoutingType type,
                                         YamlSNIConfig::TunnelPreWarm prewarm)
{
  _tunnel_type    = type;
  _tunnel_prewarm = prewarm;

  ats_free(_tunnel_host);

  auto pos = destination.find(":");
  if (pos != std::string::npos) {
    _tunnel_port = std::stoi(destination.substr(pos + 1).data());
    _tunnel_host = ats_strndup(destination.substr(0, pos).data(), pos);
  } else {
    _tunnel_port = 0;
    _tunnel_host = ats_strndup(destination.data(), destination.length());
  }
}
