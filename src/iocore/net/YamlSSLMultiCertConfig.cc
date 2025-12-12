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

#include "iocore/net/YamlSSLMultiCertConfig.h"

#include <set>
#include <string>
#include <exception>

#include <yaml-cpp/yaml.h>

#include "tscore/Diags.h"

namespace
{
// YAML key names matching the config file format.
constexpr char YAML_SSL_CERT_NAME[]      = "ssl_cert_name";
constexpr char YAML_DEST_IP[]            = "dest_ip";
constexpr char YAML_SSL_KEY_NAME[]       = "ssl_key_name";
constexpr char YAML_SSL_CA_NAME[]        = "ssl_ca_name";
constexpr char YAML_SSL_OCSP_NAME[]      = "ssl_ocsp_name";
constexpr char YAML_SSL_KEY_DIALOG[]     = "ssl_key_dialog";
constexpr char YAML_DEST_FQDN[]          = "dest_fqdn";
constexpr char YAML_SSL_TICKET_ENABLED[] = "ssl_ticket_enabled";
constexpr char YAML_SSL_TICKET_NUMBER[]  = "ssl_ticket_number";
constexpr char YAML_ACTION[]             = "action";

const std::set<std::string> valid_ssl_multicert_keys = {
  YAML_SSL_CERT_NAME,  YAML_DEST_IP,   YAML_SSL_KEY_NAME,       YAML_SSL_CA_NAME,       YAML_SSL_OCSP_NAME,
  YAML_SSL_KEY_DIALOG, YAML_DEST_FQDN, YAML_SSL_TICKET_ENABLED, YAML_SSL_TICKET_NUMBER, YAML_ACTION,
};

} // namespace

namespace YAML
{
template <> struct convert<YamlSSLMultiCertConfig::Item> {
  static bool
  decode(const Node &node, YamlSSLMultiCertConfig::Item &item)
  {
    for (const auto &elem : node) {
      std::string key = elem.first.as<std::string>();
      if (valid_ssl_multicert_keys.find(key) == valid_ssl_multicert_keys.end()) {
        Warning("unsupported key '%s' in ssl_multicert config", key.c_str());
      }
    }

    if (node[YAML_SSL_CERT_NAME]) {
      item.ssl_cert_name = node[YAML_SSL_CERT_NAME].as<std::string>();
    }

    if (node[YAML_DEST_IP]) {
      item.dest_ip = node[YAML_DEST_IP].as<std::string>();
    }

    if (node[YAML_SSL_KEY_NAME]) {
      item.ssl_key_name = node[YAML_SSL_KEY_NAME].as<std::string>();
    }

    if (node[YAML_SSL_CA_NAME]) {
      item.ssl_ca_name = node[YAML_SSL_CA_NAME].as<std::string>();
    }

    if (node[YAML_SSL_OCSP_NAME]) {
      item.ssl_ocsp_name = node[YAML_SSL_OCSP_NAME].as<std::string>();
    }

    if (node[YAML_SSL_KEY_DIALOG]) {
      item.ssl_key_dialog = node[YAML_SSL_KEY_DIALOG].as<std::string>();
    }

    if (node[YAML_DEST_FQDN]) {
      item.dest_fqdn = node[YAML_DEST_FQDN].as<std::string>();
    }

    if (node[YAML_SSL_TICKET_ENABLED]) {
      item.ssl_ticket_enabled = node[YAML_SSL_TICKET_ENABLED].as<int>();
    }

    if (node[YAML_SSL_TICKET_NUMBER]) {
      item.ssl_ticket_number = node[YAML_SSL_TICKET_NUMBER].as<int>();
    }

    if (node[YAML_ACTION]) {
      item.action = node[YAML_ACTION].as<std::string>();
    }

    return true;
  }
};
} // namespace YAML

swoc::Errata
YamlSSLMultiCertConfig::loader(const std::string &cfgFilename)
{
  try {
    YAML::Node config = YAML::LoadFile(cfgFilename);
    if (config.IsNull()) {
      return {};
    }

    if (!config["ssl_multicert"]) {
      return swoc::Errata("expected a toplevel 'ssl_multicert' node");
    }

    config = config["ssl_multicert"];
    if (!config.IsSequence()) {
      return swoc::Errata("expected 'ssl_multicert' to be a sequence");
    }

    for (auto it = config.begin(); it != config.end(); ++it) {
      items.push_back(it->as<YamlSSLMultiCertConfig::Item>());
    }
  } catch (std::exception &ex) {
    return swoc::Errata("exception - {}", ex.what());
  }

  return swoc::Errata();
}
