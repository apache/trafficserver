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

#include "tscore/Diags.h"
#include "tscore/EnumDescriptor.h"
#include "tsconfig/Errata.h"
#include "P_SNIActionPerformer.h"

ts::Errata
YamlSNIConfig::loader(const char *cfgFilename)
{
  try {
    YAML::Node config = YAML::LoadFile(cfgFilename);
    if (config.IsNull()) {
      Warning("%s is empty", cfgFilename);
      return ts::Errata();
    }

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

TsEnumDescriptor LEVEL_DESCRIPTOR = {{{"NONE", 0}, {"MODERATE", 1}, {"STRICT", 2}}};

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

TsEnumDescriptor TLS_PROTOCOLS_DESCRIPTOR = {{{"TLSv1", 0}, {"TLSv1_1", 1}, {"TLSv1_2", 2}, {"TLSv1_3", 3}}};

std::set<std::string> valid_sni_config_keys = {TS_fqdn,
                                               TS_disable_H2,
                                               TS_verify_client,
                                               TS_tunnel_route,
                                               TS_verify_origin_server,
                                               TS_client_cert,
                                               TS_ip_allow
#if TS_USE_HELLO_CB
                                               ,
                                               TS_valid_tls_versions_in
#endif
};

namespace YAML
{
template <> struct convert<YamlSNIConfig::Item> {
  static bool
  decode(const Node &node, YamlSNIConfig::Item &item)
  {
    for (auto &&item : node) {
      if (std::none_of(valid_sni_config_keys.begin(), valid_sni_config_keys.end(),
                       [&item](std::string s) { return s == item.first.as<std::string>(); })) {
        throw std::runtime_error("unsupported key " + item.first.as<std::string>());
      }
    }

    if (node[TS_fqdn]) {
      item.fqdn = node[TS_fqdn].as<std::string>();
    } else {
      return false; // servername must be present
    }
    if (node[TS_disable_H2]) {
      item.disable_h2 = node[TS_disable_H2].as<bool>();
    }

    // enum
    if (node[TS_verify_client]) {
      auto value = node[TS_verify_client].as<std::string>();
      int level  = LEVEL_DESCRIPTOR.get(value);
      if (level < 0) {
        throw std::runtime_error("unknown value " + value);
      }
      item.verify_client_level = static_cast<uint8_t>(level);
    }

    if (node[TS_tunnel_route]) {
      item.tunnel_destination = node[TS_tunnel_route].as<std::string>();
    }

    if (node[TS_verify_origin_server]) {
      auto value = node[TS_verify_origin_server].as<std::string>();
      int level  = LEVEL_DESCRIPTOR.get(value);
      if (level < 0) {
        throw std::runtime_error("unknown value " + value);
      }
      item.verify_origin_server = static_cast<uint8_t>(level);
    }

    if (node[TS_client_cert]) {
      item.client_cert = node[TS_client_cert].as<std::string>();
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
