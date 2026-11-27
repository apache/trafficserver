/** @file

  TLSTunnelSupport implements common methods and members to
  support basic features on TLS connections

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

#pragma once

#include "iocore/net/PreWarm.h"

#include <openssl/ssl.h>

#include "tscore/ink_memory.h"
#include "tscore/ink_inet.h"
#include "iocore/net/YamlSNIConfig.h"

#include <optional>
#include <string_view>

class TLSTunnelSupport
{
public:
  virtual ~TLSTunnelSupport() = default;

  static void              initialize();
  static TLSTunnelSupport *getInstance(SSL *ssl);
  static void              bind(SSL *ssl, TLSTunnelSupport *srs);
  static void              unbind(SSL *ssl);

  bool is_decryption_needed() const;
  bool is_upstream_tls() const;

  SNIRoutingType   get_tunnel_type() const;
  std::string_view get_tunnel_host() const;
  ushort           get_tunnel_port() const;
  bool             tunnel_port_is_dynamic() const;

  bool has_tunnel_destination() const;

  static constexpr bool PORT_IS_DYNAMIC = true;
  /** Set where this connection tunnels to.
   *
   * @a destination has its match groups and port template filled in, so its port can come from
   * the SNI or the connection rather than from sni.yaml.
   *
   * @return false if @a destination does not split into a host and port (such as "[::1" or
   * "[::1]443"), names no host, or names a port that is not one parse_port() accepts. The
   * connection then has no tunnel destination and should be refused.
   */
  [[nodiscard]] bool set_tunnel_destination(const std::string_view &destination, SNIRoutingType type, bool port_is_dynamic,
                                            YamlSNIConfig::TunnelPreWarm prewarm);
  YamlSNIConfig::TunnelPreWarm get_tunnel_prewarm_configuration() const;

  PreWarm::SPtrConstDst create_dst(int pid) const;

  /// The host and port of a tunnel destination, as views into the destination it was split from.
  struct Authority {
    std::string_view host;
    /// The text after the port separator, or nullopt if the destination names no port.
    std::optional<std::string_view> port;
  };

  /** Split a tunnel destination into host and port where a tunnel connection splits it.
   *
   * @return nullopt if @a destination is a bracketed address that does not close, or that is
   * followed by text other than the port separator.
   */
  static std::optional<Authority> split_destination(std::string_view destination);

  /** Read all of @a text as a port: a number from 1 to 65535 with no leading zero (svtou would
   * read a leading zero as octal).
   *
   * @return nullopt if @a text is anything else.
   */
  static std::optional<in_port_t> parse_port(std::string_view text);

protected:
  void _clear();

private:
  static int _ex_data_index;

  std::string                  _tunnel_host;
  in_port_t                    _tunnel_port    = 0;
  SNIRoutingType               _tunnel_type    = SNIRoutingType::NONE;
  YamlSNIConfig::TunnelPreWarm _tunnel_prewarm = YamlSNIConfig::TunnelPreWarm::UNSET;

  /** Whether the tunnel destination port is statically configured or
   * dynamically derived from runtime information on the wire. */
  bool _port_is_dynamic = false;
};

inline SNIRoutingType
TLSTunnelSupport::get_tunnel_type() const
{
  return this->_tunnel_type;
}

inline bool
TLSTunnelSupport::has_tunnel_destination() const
{
  return !_tunnel_host.empty();
}

inline std::string_view
TLSTunnelSupport::get_tunnel_host() const
{
  return _tunnel_host;
}

inline ushort
TLSTunnelSupport::get_tunnel_port() const
{
  return _tunnel_port;
}

inline bool
TLSTunnelSupport::tunnel_port_is_dynamic() const
{
  return _port_is_dynamic;
}

/**
   Returns true if this vc was configured for forward_route or partial_blind_route
 */
inline bool
TLSTunnelSupport::is_decryption_needed() const
{
  return this->_tunnel_type == SNIRoutingType::FORWARD || this->_tunnel_type == SNIRoutingType::PARTIAL_BLIND;
}

/**
   Returns true if this vc was configured partial_blind_route
 */
inline bool
TLSTunnelSupport::is_upstream_tls() const
{
  return _tunnel_type == SNIRoutingType::PARTIAL_BLIND;
}

inline YamlSNIConfig::TunnelPreWarm
TLSTunnelSupport::get_tunnel_prewarm_configuration() const
{
  return _tunnel_prewarm;
}
