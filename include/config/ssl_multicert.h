/** @file

  SSL Multi-Certificate configuration parsing and marshalling.

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

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "swoc/Errata.h"

namespace config
{

/**
 * Represents a single certificate entry in ssl_multicert configuration.
 */
struct SSLMultiCertEntry {
  std::string        ssl_cert_name;      ///< Certificate file name (required unless action is tunnel).
  std::string        dest_ip{"*"};       ///< IP address to match (default "*").
  std::string        ssl_key_name;       ///< Private key file name (optional).
  std::string        ssl_ca_name;        ///< CA certificate file name (optional).
  std::string        ssl_ocsp_name;      ///< OCSP response file name (optional).
  std::string        ssl_key_dialog;     ///< Passphrase dialog method (optional).
  std::string        dest_fqdn;          ///< Destination FQDN (optional).
  std::string        action;             ///< Action (e.g., "tunnel").
  std::optional<int> ssl_ticket_enabled; ///< Session ticket enabled (optional).
  std::optional<int> ssl_ticket_number;  ///< Number of session tickets (optional).
};

/// A configuration is a vector of certificate entries.
using SSLMultiCertConfig = std::vector<SSLMultiCertEntry>;

/**
 * Result of a configuration parse operation.
 *
 * @tparam T The configuration type.
 */
template <typename T> struct ConfigResult {
  T            value;  ///< The parsed configuration value.
  swoc::Errata errata; ///< Errors or warnings from parsing.

  /**
   * Check if parsing succeeded without errors.
   *
   * @return true if no errors occurred, false otherwise.
   */
  bool
  ok() const
  {
    return errata.is_ok();
  }
};

/**
 * Parser for ssl_multicert configuration files.
 *
 * Supports both YAML (.yaml) and legacy (.config) formats with automatic
 * format detection.
 */
class SSLMultiCertParser
{
public:
  /**
   * Parse an ssl_multicert configuration file.
   *
   * The format is auto-detected based on file extension and content.
   *
   * @param filename Path to the configuration file.
   * @return ConfigResult containing the parsed entries or errors.
   */
  ConfigResult<SSLMultiCertConfig> parse(std::string const &filename);

  /**
   * Parse ssl_multicert configuration from a string.
   *
   * @param content The configuration content.
   * @param filename Optional filename hint for format detection.
   * @return ConfigResult containing the parsed entries or errors.
   */
  ConfigResult<SSLMultiCertConfig> parse_string(std::string_view content, std::string const &filename = "");

private:
  enum class Format { YAML, Legacy };

  /**
   * Detect the configuration format from content and filename.
   *
   * @param content The configuration content.
   * @param filename The filename (used for extension-based detection).
   * @return The detected format.
   */
  Format detect_format(std::string_view content, std::string const &filename);

  /**
   * Parse YAML-formatted configuration content.
   *
   * @param content The YAML content.
   * @return ConfigResult containing the parsed entries or errors.
   */
  ConfigResult<SSLMultiCertConfig> parse_yaml(std::string_view content);

  /**
   * Parse legacy (.config) formatted configuration content.
   *
   * @param content The legacy config content.
   * @return ConfigResult containing the parsed entries or errors.
   */
  ConfigResult<SSLMultiCertConfig> parse_legacy(std::string_view content);
};

/**
 * Marshaller for ssl_multicert configuration.
 *
 * Serializes configuration to YAML or JSON format.
 */
class SSLMultiCertMarshaller
{
public:
  /**
   * Serialize configuration to YAML format.
   *
   * @param config The configuration to serialize.
   * @return YAML string representation.
   */
  std::string to_yaml(SSLMultiCertConfig const &config);

  /**
   * Serialize configuration to JSON format.
   *
   * @param config The configuration to serialize.
   * @return JSON string representation.
   */
  std::string to_json(SSLMultiCertConfig const &config);
};

} // namespace config
