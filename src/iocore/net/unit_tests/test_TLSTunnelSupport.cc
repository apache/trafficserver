/** @file

  Unit tests for TLSTunnelSupport.

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

#include "iocore/net/TLSTunnelSupport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

namespace
{
// Hand the connection a destination as the SNI action does, with its match
// groups and port template already filled in.
void
connect_to(TLSTunnelSupport &tunnel, std::string const &destination)
{
  tunnel.set_tunnel_destination(destination, SNIRoutingType::FORWARD, TLSTunnelSupport::PORT_IS_DYNAMIC,
                                YamlSNIConfig::TunnelPreWarm::UNSET);
}
} // namespace

TEST_CASE("TLSTunnelSupport takes a filled-in destination port that is all port")
{
  auto [destination, host, port] = GENERATE(table<std::string, std::string, in_port_t>({
    {"backend.example.com:443",   "backend.example.com", 443  },
    {"backend.example.com:1",     "backend.example.com", 1    },
    {"backend.example.com:65535", "backend.example.com", 65535},
    {"[::1]:443",                 "::1",                 443  },
    {" [::1]:443",                "::1",                 443  },
    // With no port, the tunnel falls back to the inbound port.
    {"backend.example.com",       "backend.example.com", 0    },
    {"[::1]",                     "::1",                 0    },
    {"::1",                       "::1",                 0    },
  }));

  CAPTURE(destination);
  TLSTunnelSupport tunnel;
  connect_to(tunnel, destination);
  CHECK(tunnel.get_tunnel_host() == host);
  CHECK(tunnel.get_tunnel_port() == port);
}

TEST_CASE("TLSTunnelSupport refuses a filled-in destination port a connection cannot use")
{
  std::string destination{GENERATE(
    "backend.example.com:443x", "backend.example.com:65979", "backend.example.com:99999999999", "backend.example.com:0",
    "backend.example.com:0443", "backend.example.com:", "backend.example.com:https", "[::1]:443x", "[::1]:", "[::1]443", "[::1")};

  CAPTURE(destination);
  TLSTunnelSupport tunnel;
  connect_to(tunnel, destination);
  CHECK_FALSE(tunnel.has_tunnel_destination());
  CHECK(tunnel.get_tunnel_port() == 0);
}
