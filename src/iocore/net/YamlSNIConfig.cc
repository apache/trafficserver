/** @file

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

#include "iocore/net/YamlSNIConfig.h"

#include <utility>
#include <unordered_map>
#include <set>
#include <string_view>
#include <string>
#include <limits>
#include <exception>
#include <cstdint>
#include <algorithm>

#include <yaml-cpp/yaml.h>
#include <openssl/ssl.h>
#include <netinet/in.h>

#include "swoc/TextView.h"
#include "swoc/bwf_base.h"

#include "iocore/net/YamlSNIConfig.h"
#include "SNIActionPerformer.h"
#include "P_SSLConfig.h"
#include "P_SSLNetVConnection.h"

#include "tsutil/ts_ip.h"

#include "swoc/bwf_fwd.h"
#include "tscore/Diags.h"
#include "tscore/EnumDescriptor.h"
#include "tscore/ink_assert.h"

#include "records/RecCore.h"
#include "records/RecHttp.h"

namespace
{
// Assuming node is value of [TS_tunnel_alpn]
void
load_tunnel_alpn(std::vector<int> &dst, const YAML::Node &node)
{
  if (!node.IsSequence()) {
    throw YAML::ParserException(node.Mark(), "\"tunnel_alpn\" is not sequence");
  }

  for (const auto &alpn : node) {
    auto value = alpn.as<std::string>();
    int  index = globalSessionProtocolNameRegistry.indexFor(value);
    if (index == SessionProtocolNameRegistry::INVALID) {
      throw YAML::ParserException(alpn.Mark(), "unknown value \"" + value + "\"");
    } else {
      dst.push_back(index);
    }
  }
}

} // namespace

swoc::Errata
YamlSNIConfig::loader(const std::string &cfgFilename)
{
  try {
    YAML::Node config = YAML::LoadFile(cfgFilename);
    if (config.IsNull()) {
      return {};
    }

    if (!config["sni"]) {
      return swoc::Errata("expected a toplevel 'sni' node");
    }

    config = config["sni"];
    if (!config.IsSequence()) {
      return swoc::Errata("expected sequence");
    }

    for (auto it = config.begin(); it != config.end(); ++it) {
      items.push_back(it->as<YamlSNIConfig::Item>());
    }
  } catch (std::exception &ex) {
    return swoc::Errata("exception - {}", ex.what());
  }

  return swoc::Errata();
}

void
YamlSNIConfig::Item::EnableProtocol(YamlSNIConfig::TLSProtocol proto)
{
  if (proto <= YamlSNIConfig::TLSProtocol::TLS_MAX) {
    if (protocol_unset) {
      protocol_mask  = TLSValidProtocols::max_mask;
      protocol_unset = false;
    }
    switch (proto) {
    case YamlSNIConfig::TLSProtocol::TLSv1:
      protocol_mask &= ~SSL_OP_NO_TLSv1;
      break;
    case YamlSNIConfig::TLSProtocol::TLSv1_1:
      protocol_mask &= ~SSL_OP_NO_TLSv1_1;
      break;
    case YamlSNIConfig::TLSProtocol::TLSv1_2:
      protocol_mask &= ~SSL_OP_NO_TLSv1_2;
      break;
    case YamlSNIConfig::TLSProtocol::TLSv1_3:
#ifdef SSL_OP_NO_TLSv1_3
      protocol_mask &= ~SSL_OP_NO_TLSv1_3;
#endif
      break;
    }
  }
}

void
YamlSNIConfig::Item::populate_sni_actions(action_vector_t &actions)
{
  if (offer_h2.has_value()) {
    actions.push_back(std::make_unique<ControlH2>(offer_h2.value()));
  }
  if (offer_quic.has_value()) {
    actions.push_back(std::make_unique<ControlQUIC>(offer_quic.value()));
  }
  if (verify_client_level != 255) {
    actions.push_back(std::make_unique<VerifyClient>(verify_client_level, verify_client_ca_file, verify_client_ca_dir));
  }
  if (host_sni_policy != 255) {
    actions.push_back(std::make_unique<HostSniPolicy>(host_sni_policy));
  }
  if (valid_tls_version_min_in >= 0 || valid_tls_version_max_in >= 0) {
    actions.push_back(std::make_unique<TLSValidProtocols>(valid_tls_version_min_in, valid_tls_version_max_in));
  } else if (!protocol_unset) {
    actions.push_back(std::make_unique<TLSValidProtocols>(protocol_mask));
  }
  if (tunnel_destination.length() > 0) {
    actions.push_back(std::make_unique<TunnelDestination>(tunnel_destination, tunnel_type, tunnel_prewarm, tunnel_alpn));
  }
  if (!client_sni_policy.empty()) {
    actions.push_back(std::make_unique<OutboundSNIPolicy>(client_sni_policy));
  }
  if (http2_buffer_water_mark.has_value()) {
    actions.push_back(std::make_unique<HTTP2BufferWaterMark>(http2_buffer_water_mark.value()));
  }
  if (http2_initial_window_size_in.has_value()) {
    actions.push_back(std::make_unique<HTTP2InitialWindowSizeIn>(http2_initial_window_size_in.value()));
  }
  if (http2_max_settings_frames_per_minute.has_value()) {
    actions.push_back(std::make_unique<HTTP2MaxSettingsFramesPerMinute>(http2_max_settings_frames_per_minute.value()));
  }
  if (http2_max_ping_frames_per_minute.has_value()) {
    actions.push_back(std::make_unique<HTTP2MaxPingFramesPerMinute>(http2_max_ping_frames_per_minute.value()));
  }
  if (http2_max_priority_frames_per_minute.has_value()) {
    actions.push_back(std::make_unique<HTTP2MaxPriorityFramesPerMinute>(http2_max_priority_frames_per_minute.value()));
  }
  if (http2_max_rst_stream_frames_per_minute.has_value()) {
    actions.push_back(std::make_unique<HTTP2MaxRstStreamFramesPerMinute>(http2_max_rst_stream_frames_per_minute.value()));
  }
  if (http2_max_continuation_frames_per_minute.has_value()) {
    actions.push_back(std::make_unique<HTTP2MaxContinuationFramesPerMinute>(http2_max_continuation_frames_per_minute.value()));
  }

  actions.push_back(std::make_unique<ServerMaxEarlyData>(server_max_early_data));
  actions.push_back(std::make_unique<SNI_IpAllow>(ip_allow, fqdn));
}

VerifyClient::~VerifyClient() {}

TsEnumDescriptor LEVEL_DESCRIPTOR = {
  {{"NONE", 0}, {"MODERATE", 1}, {"STRICT", 2}}
};
TsEnumDescriptor POLICY_DESCRIPTOR = {
  {{"DISABLED", 0}, {"PERMISSIVE", 1}, {"ENFORCED", 2}}
};
TsEnumDescriptor PROPERTIES_DESCRIPTOR = {
  {{"NONE", 0}, {"SIGNATURE", 0x1}, {"NAME", 0x2}, {"ALL", 0x3}}
};
TsEnumDescriptor TLS_PROTOCOLS_DESCRIPTOR = {
  {{"TLSv1", 0}, {"TLSv1_1", 1}, {"TLSv1_2", 2}, {"TLSv1_3", 3}}
};

std::set<std::string> valid_sni_config_keys = {TS_fqdn,
                                               TS_inbound_port_ranges,
                                               TS_verify_client,
                                               TS_verify_client_ca_certs,
                                               TS_tunnel_route,
                                               TS_forward_route,
                                               TS_partial_blind_route,
                                               TS_tunnel_alpn,
                                               TS_tunnel_prewarm,
                                               TS_tunnel_prewarm_min,
                                               TS_tunnel_prewarm_max,
                                               TS_tunnel_prewarm_rate,
                                               TS_tunnel_prewarm_connect_timeout,
                                               TS_tunnel_prewarm_inactive_timeout,
                                               TS_tunnel_prewarm_srv,
                                               TS_verify_server_policy,
                                               TS_verify_server_properties,
                                               TS_client_cert,
                                               TS_client_key,
                                               TS_client_sni_policy,
                                               TS_http2,
                                               TS_http2_buffer_water_mark,
                                               TS_http2_initial_window_size_in,
                                               TS_http2_max_settings_frames_per_minute,
                                               TS_http2_max_ping_frames_per_minute,
                                               TS_http2_max_priority_frames_per_minute,
                                               TS_http2_max_rst_stream_frames_per_minute,
                                               TS_http2_max_continuation_frames_per_minute,
                                               TS_quic,
                                               TS_ip_allow,
#if TS_USE_HELLO_CB || defined(OPENSSL_IS_BORINGSSL)
                                               TS_valid_tls_versions_in,
                                               TS_valid_tls_version_min_in,
                                               TS_valid_tls_version_max_in,
#endif
                                               TS_host_sni_policy,
                                               TS_server_max_early_data};

namespace YAML
{
template <> struct convert<YamlSNIConfig::Item> {
  static bool
  decode(const Node &node, YamlSNIConfig::Item &item)
  {
    for (const auto &elem : node) {
      if (std::none_of(valid_sni_config_keys.begin(), valid_sni_config_keys.end(),
                       [&elem](const std::string &s) { return s == elem.first.as<std::string>(); })) {
        Warning("unsupported key '%s' in SNI config", elem.first.as<std::string>().c_str());
      }
    }

    if (node[TS_fqdn]) {
      item.fqdn = node[TS_fqdn].as<std::string>();
    } else {
      return false; // servername must be present
    }

    if (node[TS_inbound_port_ranges]) {
      item.inbound_port_ranges = parse_inbound_port_ranges(node[TS_inbound_port_ranges]);
    } else {
      item.inbound_port_ranges.emplace_back(1, ts::MAX_PORT_VALUE);
    }
    if (node[TS_http2]) {
      item.offer_h2 = node[TS_http2].as<bool>();
    }
    if (node[TS_http2_buffer_water_mark]) {
      item.http2_buffer_water_mark = node[TS_http2_buffer_water_mark].as<int>();
    }
    if (node[TS_http2_initial_window_size_in]) {
      item.http2_initial_window_size_in = node[TS_http2_initial_window_size_in].as<int>();
    }
    if (node[TS_http2_max_settings_frames_per_minute]) {
      item.http2_max_settings_frames_per_minute = node[TS_http2_max_settings_frames_per_minute].as<int>();
    }
    if (node[TS_http2_max_ping_frames_per_minute]) {
      item.http2_max_ping_frames_per_minute = node[TS_http2_max_ping_frames_per_minute].as<int>();
    }
    if (node[TS_http2_max_priority_frames_per_minute]) {
      item.http2_max_priority_frames_per_minute = node[TS_http2_max_priority_frames_per_minute].as<int>();
    }
    if (node[TS_http2_max_rst_stream_frames_per_minute]) {
      item.http2_max_rst_stream_frames_per_minute = node[TS_http2_max_rst_stream_frames_per_minute].as<int>();
    }
    if (node[TS_http2_max_continuation_frames_per_minute]) {
      item.http2_max_continuation_frames_per_minute = node[TS_http2_max_continuation_frames_per_minute].as<int>();
    }
    if (node[TS_quic]) {
      item.offer_quic = node[TS_quic].as<bool>();
    }

    // enum
    if (node[TS_verify_client]) {
      auto value = node[TS_verify_client].as<std::string>();
      int  level = LEVEL_DESCRIPTOR.get(value);
      if (level < 0) {
        throw YAML::ParserException(node[TS_verify_client].Mark(), "unknown value \"" + value + "\"");
      }
      item.verify_client_level = static_cast<uint8_t>(level);
    }

    if (node[TS_verify_client_ca_certs]) {
#if !TS_HAS_VERIFY_CERT_STORE
      // TS was compiled with an older version of the OpenSSL interface, that doesn't have
      // SSL_set1_verify_cert_store().  We need this macro in order to set the CA certs for verifying clients
      // after the client sends the SNI server name.
      //
      throw YAML::ParserException(node[TS_verify_client_ca_certs].Mark(),
                                  std::string(TS_verify_client_ca_certs) + " requires features from OpenSSL 1.0.2 or later");
#else
      std::string file, dir;
      auto const &n = node[TS_verify_client_ca_certs];

      if (n.IsMap()) {
        for (const auto &elem : n) {
          std::string key = elem.first.as<std::string>();
          if ("file" == key) {
            if (!file.empty()) {
              throw YAML::ParserException(elem.first.Mark(), "duplicate key \"file\"");
            }
            file = elem.second.as<std::string>();

          } else if ("dir" == key) {
            if (!dir.empty()) {
              throw YAML::ParserException(elem.first.Mark(), "duplicate key \"dir\"");
            }
            dir = elem.second.as<std::string>();

          } else {
            throw YAML::ParserException(elem.first.Mark(), "unsupported key " + elem.first.as<std::string>());
          }
        }
      } else {
        // Value should be string scalar with file.
        //
        file = n.as<std::string>();
      }
      ink_assert(!(file.empty() && dir.empty()));

      if (!file.empty() && (file[0] != '/')) {
        file = RecConfigReadConfigDir() + '/' + file;
      }
      if (!dir.empty() && (dir[0] != '/')) {
        dir = RecConfigReadConfigDir() + '/' + dir;
      }
      item.verify_client_ca_file = file;
      item.verify_client_ca_dir  = dir;
#endif
    }

    if (node[TS_host_sni_policy]) {
      auto value           = node[TS_host_sni_policy].as<std::string>();
      int  policy          = POLICY_DESCRIPTOR.get(value);
      item.host_sni_policy = static_cast<uint8_t>(policy);
    }

    YamlSNIConfig::TunnelPreWarm t_prewarm          = YamlSNIConfig::TunnelPreWarm::UNSET;
    uint32_t                     t_min              = item.tunnel_prewarm_min;
    int32_t                      t_max              = item.tunnel_prewarm_max;
    double                       t_rate             = item.tunnel_prewarm_rate;
    uint32_t                     t_connect_timeout  = item.tunnel_prewarm_connect_timeout;
    uint32_t                     t_inactive_timeout = item.tunnel_prewarm_inactive_timeout;
    bool                         t_srv              = item.tunnel_prewarm_srv;

    if (node[TS_tunnel_prewarm]) {
      auto is_prewarm_enabled = node[TS_tunnel_prewarm].as<bool>();
      if (is_prewarm_enabled) {
        t_prewarm = YamlSNIConfig::TunnelPreWarm::ENABLED;
      } else {
        t_prewarm = YamlSNIConfig::TunnelPreWarm::DISABLED;
      }
    }
    if (node[TS_tunnel_prewarm_min]) {
      t_min = node[TS_tunnel_prewarm_min].as<uint32_t>();
    }
    if (node[TS_tunnel_prewarm_max]) {
      t_max = node[TS_tunnel_prewarm_max].as<int32_t>();
    }
    if (node[TS_tunnel_prewarm_rate]) {
      t_rate = node[TS_tunnel_prewarm_rate].as<double>();
    }
    if (node[TS_tunnel_prewarm_connect_timeout]) {
      t_connect_timeout = node[TS_tunnel_prewarm_connect_timeout].as<uint32_t>();
    }
    if (node[TS_tunnel_prewarm_inactive_timeout]) {
      t_inactive_timeout = node[TS_tunnel_prewarm_inactive_timeout].as<uint32_t>();
    }
    if (node[TS_tunnel_prewarm_srv]) {
      t_srv = node[TS_tunnel_prewarm_srv].as<bool>();
    }

    if (node[TS_tunnel_route]) {
      item.tunnel_destination = node[TS_tunnel_route].as<std::string>();
      item.tunnel_type        = SNIRoutingType::BLIND;
    } else if (node[TS_forward_route]) {
      item.tunnel_destination              = node[TS_forward_route].as<std::string>();
      item.tunnel_type                     = SNIRoutingType::FORWARD;
      item.tunnel_prewarm                  = t_prewarm;
      item.tunnel_prewarm_min              = t_min;
      item.tunnel_prewarm_max              = t_max;
      item.tunnel_prewarm_rate             = t_rate;
      item.tunnel_prewarm_connect_timeout  = t_connect_timeout;
      item.tunnel_prewarm_inactive_timeout = t_inactive_timeout;
      item.tunnel_prewarm_srv              = t_srv;
    } else if (node[TS_partial_blind_route]) {
      item.tunnel_destination              = node[TS_partial_blind_route].as<std::string>();
      item.tunnel_type                     = SNIRoutingType::PARTIAL_BLIND;
      item.tunnel_prewarm                  = t_prewarm;
      item.tunnel_prewarm_min              = t_min;
      item.tunnel_prewarm_max              = t_max;
      item.tunnel_prewarm_rate             = t_rate;
      item.tunnel_prewarm_connect_timeout  = t_connect_timeout;
      item.tunnel_prewarm_inactive_timeout = t_inactive_timeout;
      item.tunnel_prewarm_srv              = t_srv;

      if (node[TS_tunnel_alpn]) {
        load_tunnel_alpn(item.tunnel_alpn, node[TS_tunnel_alpn]);
      }
    }

    if (node[TS_verify_server_policy]) {
      auto value  = node[TS_verify_server_policy].as<std::string>();
      int  policy = POLICY_DESCRIPTOR.get(value);
      if (policy < 0) {
        throw YAML::ParserException(node[TS_verify_server_policy].Mark(), "unknown value \"" + value + "\"");
      }
      item.verify_server_policy = static_cast<YamlSNIConfig::Policy>(policy);
    }

    if (node[TS_verify_server_properties]) {
      auto value      = node[TS_verify_server_properties].as<std::string>();
      int  properties = PROPERTIES_DESCRIPTOR.get(value);
      if (properties < 0) {
        throw YAML::ParserException(node[TS_verify_server_properties].Mark(), "unknown value \"" + value + "\"");
      }
      item.verify_server_properties = static_cast<YamlSNIConfig::Property>(properties);
    }

    if (node[TS_client_cert]) {
      item.client_cert = node[TS_client_cert].as<std::string>();
    }
    if (node[TS_client_key]) {
      item.client_key = node[TS_client_key].as<std::string>();
    }
    if (node[TS_client_sni_policy]) {
      item.client_sni_policy = node[TS_client_sni_policy].as<std::string>();
    }

    if (node[TS_ip_allow]) {
      item.ip_allow = node[TS_ip_allow].as<std::string>();
    }
    if (node[TS_valid_tls_versions_in]) {
      for (unsigned int i = 0; i < node[TS_valid_tls_versions_in].size(); i++) {
        auto value    = node[TS_valid_tls_versions_in][i].as<std::string>();
        int  protocol = TLS_PROTOCOLS_DESCRIPTOR.get(value);
        item.EnableProtocol(static_cast<YamlSNIConfig::TLSProtocol>(protocol));
      }
    }
    if (node[TS_valid_tls_version_min_in]) {
      item.valid_tls_version_min_in = TLS_PROTOCOLS_DESCRIPTOR.get(node[TS_valid_tls_version_min_in].as<std::string>());
    }
    if (node[TS_valid_tls_version_max_in]) {
      item.valid_tls_version_max_in = TLS_PROTOCOLS_DESCRIPTOR.get(node[TS_valid_tls_version_max_in].as<std::string>());
    }

    if (node[TS_server_max_early_data]) {
      item.server_max_early_data = node[TS_server_max_early_data].as<uint32_t>();
    } else {
      item.server_max_early_data = SSLConfigParams::server_max_early_data;
    }

    return true;
  }

  static std::vector<ts::port_range_t>
  parse_inbound_port_ranges(Node const &port_ranges)
  {
    std::vector<ts::port_range_t> result;
    if (port_ranges.IsSequence()) {
      for (Node const &port_range : port_ranges) {
        result.emplace_back(parse_single_inbound_port_range(port_range, port_range.Scalar()));
      }
    } else {
      result.emplace_back(parse_single_inbound_port_range(port_ranges, port_ranges.Scalar()));
    }

    return result;
  }

  static ts::port_range_t
  parse_single_inbound_port_range(Node const &node, swoc::TextView port_view)
  {
    auto min{port_view.split_prefix_at('-')};
    if (!min) {
      min = port_view;
    }
    auto max{port_view};

    swoc::TextView parsed_min;
    auto           min_port{swoc::svtoi(min, &parsed_min)};
    swoc::TextView parsed_max;
    auto           max_port{swoc::svtoi(max, &parsed_max)};
    if (parsed_min != min || min_port < 1 || parsed_max != max || max_port > std::numeric_limits<in_port_t>::max() ||
        max_port < min_port) {
      throw YAML::ParserException(node.Mark(), swoc::bwprint(ts::bw_dbg, "bad port range: {}-{}", min, max));
    }

    return {static_cast<in_port_t>(min_port), static_cast<in_port_t>(max_port)};
  }
};
} // namespace YAML

// Initialize the static TunnelDestination::fix_destination.
std::array<std::function<std::string(std::string_view,            // destination view
                                     size_t,                      // The start position for any relevant tunnel_route variable.
                                     const ActionItem::Context &, // Context
                                     SSLNetVConnection *,         // Net vc to get the port.
                                     bool &                       // Whether the port is derived from information on the wire.
                                     )>,
           TunnelDestination::OpId::MAX>
  TunnelDestination::fix_destination = {

    // Replace wildcards with matched groups.
    [](std::string_view destination, size_t var_start_pos, const Context &ctx, SSLNetVConnection *,
       bool &port_is_dynamic) -> std::string {
      port_is_dynamic = false;
      if ((destination.find_first_of('$') != std::string::npos) && ctx._fqdn_wildcard_captured_groups) {
        auto const fixed_dst =
          TunnelDestination::replace_match_groups(destination, *ctx._fqdn_wildcard_captured_groups, port_is_dynamic);
        return fixed_dst;
      }
      return {};
    },

    // Use local port for the tunnel.
    [](std::string_view destination, size_t var_start_pos, const Context &ctx, SSLNetVConnection *vc,
       bool &port_is_dynamic) -> std::string {
      port_is_dynamic = true;
      if (vc) {
        if (var_start_pos != std::string::npos) {
          const auto  local_port = vc->get_local_port();
          std::string dst{destination.substr(0, var_start_pos)};
          dst += std::to_string(local_port);
          return dst;
        }
      }
      return {};
    },

    // Use the Proxy Protocol port for the tunnel.
    [](std::string_view destination, size_t var_start_pos, const Context &ctx, SSLNetVConnection *vc,
       bool &port_is_dynamic) -> std::string {
      port_is_dynamic = true;
      if (vc) {
        if (var_start_pos != std::string::npos) {
          const auto  local_port = vc->get_proxy_protocol_dst_port();
          std::string dst{destination.substr(0, var_start_pos)};
          dst += std::to_string(local_port);
          return dst;
        }
      }
      return {};
    },
};
