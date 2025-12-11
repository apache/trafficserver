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

#pragma once

#include <string>
#include <vector>
#include <optional>

#include "swoc/Errata.h"

/**
 * Configuration structure for loading ssl_multicert.yaml.
 *
 * This class parses the YAML configuration file that maps SSL certificates
 * to IP addresses or FQDNs.
 */
struct YamlSSLMultiCertConfig {
  /**
   * Represents a single certificate entry in the configuration.
   */
  struct Item {
    std::string        ssl_cert_name;      ///< Certificate file name (required unless action is tunnel).
    std::string        dest_ip;            ///< IP address to match (optional, default "*").
    std::string        ssl_key_name;       ///< Private key file name (optional).
    std::string        ssl_ca_name;        ///< CA certificate file name (optional).
    std::string        ssl_ocsp_name;      ///< OCSP response file name (optional).
    std::string        ssl_key_dialog;     ///< Passphrase dialog method (optional).
    std::string        dest_fqdn;          ///< Destination FQDN (optional).
    std::string        action;             ///< Action (e.g., "tunnel").
    std::optional<int> ssl_ticket_enabled; ///< Session ticket enabled (optional).
    std::optional<int> ssl_ticket_number;  ///< Number of session tickets (optional).
  };

  /**
   * Load and parse the ssl_multicert.yaml configuration file.
   *
   * @param cfgFilename Path to the configuration file.
   * @return Errata containing any errors or warnings from parsing.
   */
  swoc::Errata loader(const std::string &cfgFilename);

  std::vector<Item> items; ///< Parsed certificate entries.
};
