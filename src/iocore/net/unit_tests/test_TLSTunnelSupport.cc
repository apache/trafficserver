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
#include "../SNIActionPerformer.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <optional>
#include <string>

namespace
{
// Hand the connection a destination as the SNI action does, with its match
// groups and port template already filled in. Returns whether the connection
// took it.
bool
connect_to(TLSTunnelSupport &tunnel, std::string const &destination)
{
  return tunnel.set_tunnel_destination(destination, SNIRoutingType::FORWARD, TLSTunnelSupport::PORT_IS_DYNAMIC,
                                       YamlSNIConfig::TunnelPreWarm::UNSET);
}
} // namespace

TEST_CASE("TLSTunnelSupport splits a destination into host and port text")
{
  struct Split {
    std::string                destination;
    bool                       splits;
    std::string                host;
    std::optional<std::string> port;
  };
  auto row = GENERATE(values<Split>({
    {"backend.example.com:443",    true,  "backend.example.com", "443"                 },
    {" backend.example.com:443",   true,  "backend.example.com", "443"                 },
    {"backend.example.com:443x",   true,  "backend.example.com", "443x"                },
    {"backend.example.com:$1",     true,  "backend.example.com", "$1"                  },
    // An empty port is a port the connection cannot use, not a missing one.
    {"backend.example.com:",       true,  "backend.example.com", ""                    },
    {"backend.example.com",        true,  "backend.example.com", std::nullopt          },
    {"[::1]:443",                  true,  "::1",                 "443"                 },
    {" [::1]:443",                 true,  "::1",                 "443"                 },
    {"[::1]:{inbound_local_port}", true,  "::1",                 "{inbound_local_port}"},
    {"[::1]:",                     true,  "::1",                 ""                    },
    {"[::1]",                      true,  "::1",                 std::nullopt          },
    {"::1",                        true,  "::1",                 std::nullopt          },
    // Filled in from a match group: "127.0.0.1:$1" with "1:2" has more than
    // one colon and so no port, while "$1" with "10.0.0.1:22" gains one.
    {"127.0.0.1:1:2",              true,  "127.0.0.1:1:2",       std::nullopt          },
    {"10.0.0.1:22",                true,  "10.0.0.1",            "22"                  },
    {":443",                       true,  "",                    "443"                 },
    {"[::1]443",                   false, "",                    std::nullopt          },
    {"[::1]x",                     false, "",                    std::nullopt          },
    {"[::1",                       false, "",                    std::nullopt          },
  }));

  CAPTURE(row.destination);
  auto const authority = TLSTunnelSupport::split_destination(row.destination);
  REQUIRE(authority.has_value() == row.splits);
  if (authority) {
    CHECK(authority->host == row.host);
    REQUIRE(authority->port.has_value() == row.port.has_value());
    if (row.port) {
      CHECK(*authority->port == *row.port);
    }
  }
}

TEST_CASE("TLSTunnelSupport takes a filled-in destination with a valid port or none")
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
  CHECK(connect_to(tunnel, destination));
  CHECK(tunnel.get_tunnel_host() == host);
  CHECK(tunnel.get_tunnel_port() == port);
}

TEST_CASE("TLSTunnelSupport refuses a filled-in destination port a connection cannot use")
{
  std::string destination{GENERATE("backend.example.com:443x", "backend.example.com:65979", "backend.example.com:99999999999",
                                   "backend.example.com:0", "backend.example.com:0443", "backend.example.com:",
                                   "backend.example.com:https", "[::1]:443x", "[::1]:", "[::1]443", "[::1", ":443", "[]:443", "")};

  CAPTURE(destination);
  TLSTunnelSupport tunnel;
  CHECK_FALSE(connect_to(tunnel, destination));
  CHECK_FALSE(tunnel.has_tunnel_destination());
  CHECK(tunnel.get_tunnel_port() == 0);
  CHECK(tunnel.get_tunnel_type() == SNIRoutingType::NONE);
  CHECK_FALSE(tunnel.tunnel_port_is_dynamic());
}

TEST_CASE("TLSTunnelSupport refusing a destination clears the one it took before")
{
  TLSTunnelSupport tunnel;
  REQUIRE(tunnel.set_tunnel_destination("backend.example.com:443", SNIRoutingType::PARTIAL_BLIND, TLSTunnelSupport::PORT_IS_DYNAMIC,
                                        YamlSNIConfig::TunnelPreWarm::ENABLED));

  CHECK_FALSE(connect_to(tunnel, "backend.example.com:443x"));
  CHECK_FALSE(tunnel.has_tunnel_destination());
  CHECK(tunnel.get_tunnel_port() == 0);
  CHECK(tunnel.get_tunnel_type() == SNIRoutingType::NONE);
  CHECK(tunnel.get_tunnel_prewarm_configuration() == YamlSNIConfig::TunnelPreWarm::UNSET);
  CHECK_FALSE(tunnel.tunnel_port_is_dynamic());
}

TEST_CASE("TunnelDestination leaves a '$' it cannot fill in as written")
{
  ActionItem::Context::CapturedGroupViewVec const groups{"a", "8443"};
  auto [destination, filled, port_is_dynamic] = GENERATE(table<std::string, std::string, bool>({
    {"$0.x",           "$0.x",           false},
    {"$$1",            "$a",             false},
    {"abc$",           "abc$",           false},
    {"$3.x",           "$3.x",           false},
    {"$1.backend:$2",  "a.backend:8443", true },
    {"backend.$1:443", "backend.a:443",  false},
  }));

  CAPTURE(destination);
  bool dynamic = !port_is_dynamic;
  CHECK(TunnelDestination::replace_match_groups(destination, groups, dynamic) == filled);
  CHECK(dynamic == port_is_dynamic);
}
