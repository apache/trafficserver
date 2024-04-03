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

#include "YamlSNIConfig.h"

#include <unordered_map>
#include <set>
#include <string_view>

#include <yaml-cpp/yaml.h>
#include <openssl/ssl.h>

#include "P_SNIActionPerformer.h"

#include "tscore/Diags.h"
#include "tscore/EnumDescriptor.h"
#include "tscore/Errata.h"
#include "tscore/ink_assert.h"

#include "records/I_RecCore.h"
#include "records/I_RecHttp.h"

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
    int index  = globalSessionProtocolNameRegistry.indexFor(value);
    if (index == SessionProtocolNameRegistry::INVALID) {
      throw YAML::ParserException(alpn.Mark(), "unknown value \"" + value + "\"");
    } else {
      dst.push_back(index);
    }
  }
}
} // namespace

ts::Errata
YamlSNIConfig::loader(const std::string &cfgFilename)
{
  try {
    YAML::Node config = YAML::LoadFile(cfgFilename);
    if (config.IsNull()) {
      return ts::Errata();
    }

    if (!config["sni"]) {
      return ts::Errata::Message(1, 1, "expected a toplevel 'sni' node");
    }

    config = config["sni"];
    if (!config.IsSequence()) {
      return ts::Errata::Message(1, 1, "expected sequence");
    }

    for (auto it = config.begin(); it != config.end(); ++it) {
      items.push_back(it->as<YamlSNIConfig::Item>());
    }
  } catch (std::exception &ex) {
    return ts::Errata::Message(1, 1, ex.what());
  }

  return ts::Errata();
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

VerifyClient::~VerifyClient() {}

TsEnumDescriptor LEVEL_DESCRIPTOR         = {{{"NONE", 0}, {"MODERATE", 1}, {"STRICT", 2}}};
TsEnumDescriptor POLICY_DESCRIPTOR        = {{{"DISABLED", 0}, {"PERMISSIVE", 1}, {"ENFORCED", 2}}};
TsEnumDescriptor PROPERTIES_DESCRIPTOR    = {{{"NONE", 0}, {"SIGNATURE", 0x1}, {"NAME", 0x2}, {"ALL", 0x3}}};
TsEnumDescriptor TLS_PROTOCOLS_DESCRIPTOR = {{{"TLSv1", 0}, {"TLSv1_1", 1}, {"TLSv1_2", 2}, {"TLSv1_3", 3}}};

std::set<std::string> valid_sni_config_keys = {TS_fqdn,
                                               TS_disable_h2,
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
                                               TS_http2_max_settings_frames_per_minute,
                                               TS_http2_max_ping_frames_per_minute,
                                               TS_http2_max_priority_frames_per_minute,
                                               TS_http2_max_rst_stream_frames_per_minute,
                                               TS_http2_max_continuation_frames_per_minute,
                                               TS_ip_allow,
#if TS_USE_HELLO_CB || defined(OPENSSL_IS_BORINGSSL)
                                               TS_valid_tls_versions_in,
#endif
                                               TS_host_sni_policy};

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
    if (node[TS_disable_h2]) {
      item.offer_h2 = false;
    }
    if (node[TS_http2]) {
      item.offer_h2 = node[TS_http2].as<bool>();
    }
    if (node[TS_http2_buffer_water_mark]) {
      item.http2_buffer_water_mark = node[TS_http2_buffer_water_mark].as<int>();
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

    // enum
    if (node[TS_verify_client]) {
      auto value = node[TS_verify_client].as<std::string>();
      int level  = LEVEL_DESCRIPTOR.get(value);
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
      int policy           = POLICY_DESCRIPTOR.get(value);
      item.host_sni_policy = static_cast<uint8_t>(policy);
    }

    YamlSNIConfig::TunnelPreWarm t_prewarm = YamlSNIConfig::TunnelPreWarm::UNSET;
    uint32_t t_min                         = item.tunnel_prewarm_min;
    int32_t t_max                          = item.tunnel_prewarm_max;
    double t_rate                          = item.tunnel_prewarm_rate;
    uint32_t t_connect_timeout             = item.tunnel_prewarm_connect_timeout;
    uint32_t t_inactive_timeout            = item.tunnel_prewarm_inactive_timeout;
    bool t_srv                             = item.tunnel_prewarm_srv;

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
      auto value = node[TS_verify_server_policy].as<std::string>();
      int policy = POLICY_DESCRIPTOR.get(value);
      if (policy < 0) {
        throw YAML::ParserException(node[TS_verify_server_policy].Mark(), "unknown value \"" + value + "\"");
      }
      item.verify_server_policy = static_cast<YamlSNIConfig::Policy>(policy);
    }

    if (node[TS_verify_server_properties]) {
      auto value     = node[TS_verify_server_properties].as<std::string>();
      int properties = PROPERTIES_DESCRIPTOR.get(value);
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
        auto value   = node[TS_valid_tls_versions_in][i].as<std::string>();
        int protocol = TLS_PROTOCOLS_DESCRIPTOR.get(value);
        item.EnableProtocol(static_cast<YamlSNIConfig::TLSProtocol>(protocol));
      }
    }
    return true;
  }
};
} // namespace YAML
