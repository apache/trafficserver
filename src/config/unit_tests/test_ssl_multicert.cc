/** @file

  Unit tests for ssl_multicert configuration parsing and marshalling.

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

#include "config/ssl_multicert.h"

#include <catch2/catch_test_macros.hpp>

using namespace config;

// Sample legacy config content.
static constexpr char LEGACY_CONFIG[] = R"(# Comment line
ssl_cert_name=server.pem ssl_key_name=server.key dest_ip=*
ssl_cert_name=another.pem dest_ip="[::1]:8443" ssl_ticket_enabled=1
ssl_cert_name=quoted.pem ssl_key_dialog="exec:/usr/bin/getpass arg1 'arg 2'"
)";

// Sample YAML config content.
static constexpr char YAML_CONFIG[] = R"(ssl_multicert:
  - ssl_cert_name: server.pem
    ssl_key_name: server.key
    dest_ip: "*"
  - ssl_cert_name: another.pem
    dest_ip: "[::1]:8443"
    ssl_ticket_enabled: 1
  - ssl_cert_name: quoted.pem
    ssl_key_dialog: "exec:/usr/bin/getpass arg1 'arg 2'"
)";

TEST_CASE("SSLMultiCertParser parses legacy config format", "[ssl_multicert][parser][legacy]")
{
  SSLMultiCertParser parser;
  auto               result = parser.parse_string(LEGACY_CONFIG, "ssl_multicert.config");

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);

  SECTION("First entry")
  {
    auto const &entry = result.value[0];
    CHECK(entry.ssl_cert_name == "server.pem");
    CHECK(entry.ssl_key_name == "server.key");
    CHECK(entry.dest_ip == "*");
    CHECK_FALSE(entry.ssl_ticket_enabled.has_value());
  }

  SECTION("Second entry with IPv6 and ticket enabled")
  {
    auto const &entry = result.value[1];
    CHECK(entry.ssl_cert_name == "another.pem");
    CHECK(entry.dest_ip == "[::1]:8443");
    REQUIRE(entry.ssl_ticket_enabled.has_value());
    CHECK(entry.ssl_ticket_enabled.value() == 1);
  }

  SECTION("Third entry with quoted dialog")
  {
    auto const &entry = result.value[2];
    CHECK(entry.ssl_cert_name == "quoted.pem");
    CHECK(entry.ssl_key_dialog == "exec:/usr/bin/getpass arg1 'arg 2'");
  }
}

TEST_CASE("SSLMultiCertParser parses YAML config format", "[ssl_multicert][parser][yaml]")
{
  SSLMultiCertParser parser;
  auto               result = parser.parse_string(YAML_CONFIG, "ssl_multicert.yaml");

  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 3);

  SECTION("First entry")
  {
    auto const &entry = result.value[0];
    CHECK(entry.ssl_cert_name == "server.pem");
    CHECK(entry.ssl_key_name == "server.key");
    CHECK(entry.dest_ip == "*");
  }

  SECTION("Second entry with IPv6 and ticket enabled")
  {
    auto const &entry = result.value[1];
    CHECK(entry.ssl_cert_name == "another.pem");
    CHECK(entry.dest_ip == "[::1]:8443");
    REQUIRE(entry.ssl_ticket_enabled.has_value());
    CHECK(entry.ssl_ticket_enabled.value() == 1);
  }

  SECTION("Third entry with quoted dialog")
  {
    auto const &entry = result.value[2];
    CHECK(entry.ssl_cert_name == "quoted.pem");
    CHECK(entry.ssl_key_dialog == "exec:/usr/bin/getpass arg1 'arg 2'");
  }
}

TEST_CASE("SSLMultiCertParser auto-detects format from filename", "[ssl_multicert][parser][detection]")
{
  SSLMultiCertParser parser;

  SECTION("Detects YAML from .yaml extension")
  {
    auto result = parser.parse_string(YAML_CONFIG, "config.yaml");
    REQUIRE(result.ok());
    CHECK(result.value.size() == 3);
  }

  SECTION("Detects YAML from .yml extension")
  {
    auto result = parser.parse_string(YAML_CONFIG, "config.yml");
    REQUIRE(result.ok());
    CHECK(result.value.size() == 3);
  }

  SECTION("Detects legacy from .config extension")
  {
    auto result = parser.parse_string(LEGACY_CONFIG, "ssl_multicert.config");
    REQUIRE(result.ok());
    CHECK(result.value.size() == 3);
  }

  SECTION("Detects YAML from content when no extension hint")
  {
    auto result = parser.parse_string(YAML_CONFIG, "");
    REQUIRE(result.ok());
    CHECK(result.value.size() == 3);
  }

  SECTION("Detects legacy from content when no extension hint")
  {
    auto result = parser.parse_string(LEGACY_CONFIG, "");
    REQUIRE(result.ok());
    CHECK(result.value.size() == 3);
  }
}

TEST_CASE("SSLMultiCertParser handles edge cases", "[ssl_multicert][parser][edge]")
{
  SSLMultiCertParser parser;

  SECTION("Empty content returns empty config")
  {
    auto result = parser.parse_string("", "config.yaml");
    REQUIRE(result.ok());
    CHECK(result.value.empty());
  }

  SECTION("Comments only in legacy format")
  {
    auto result = parser.parse_string("# Just a comment\n# Another comment\n", "config.config");
    REQUIRE(result.ok());
    CHECK(result.value.empty());
  }

  SECTION("Invalid YAML returns error")
  {
    auto result = parser.parse_string("ssl_multicert: [not: valid: yaml", "config.yaml");
    CHECK_FALSE(result.ok());
  }

  SECTION("Missing ssl_multicert key returns error")
  {
    auto result = parser.parse_string("other_key:\n  - value: 1\n", "config.yaml");
    CHECK_FALSE(result.ok());
  }
}

TEST_CASE("SSLMultiCertParser handles all entry fields", "[ssl_multicert][parser][fields]")
{
  SSLMultiCertParser parser;

  static constexpr char FULL_YAML[] = R"(ssl_multicert:
  - ssl_cert_name: cert.pem
    dest_ip: "192.168.1.1"
    ssl_key_name: key.pem
    ssl_ca_name: ca.pem
    ssl_ocsp_name: ocsp.der
    ssl_key_dialog: "builtin"
    dest_fqdn: "example.com"
    action: tunnel
    ssl_ticket_enabled: 1
    ssl_ticket_number: 5
)";

  auto result = parser.parse_string(FULL_YAML, "config.yaml");
  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);

  auto const &entry = result.value[0];
  CHECK(entry.ssl_cert_name == "cert.pem");
  CHECK(entry.dest_ip == "192.168.1.1");
  CHECK(entry.ssl_key_name == "key.pem");
  CHECK(entry.ssl_ca_name == "ca.pem");
  CHECK(entry.ssl_ocsp_name == "ocsp.der");
  CHECK(entry.ssl_key_dialog == "builtin");
  CHECK(entry.dest_fqdn == "example.com");
  CHECK(entry.action == "tunnel");
  REQUIRE(entry.ssl_ticket_enabled.has_value());
  CHECK(entry.ssl_ticket_enabled.value() == 1);
  REQUIRE(entry.ssl_ticket_number.has_value());
  CHECK(entry.ssl_ticket_number.value() == 5);
}

TEST_CASE("SSLMultiCertMarshaller produces valid YAML", "[ssl_multicert][marshaller][yaml]")
{
  SSLMultiCertConfig config;

  SSLMultiCertEntry entry1;
  entry1.ssl_cert_name = "server.pem";
  entry1.dest_ip       = "*";
  entry1.ssl_key_name  = "server.key";
  config.push_back(entry1);

  SSLMultiCertEntry entry2;
  entry2.ssl_cert_name      = "another.pem";
  entry2.dest_ip            = "[::1]:8443";
  entry2.ssl_ticket_enabled = 1;
  config.push_back(entry2);

  SSLMultiCertMarshaller marshaller;
  std::string            yaml = marshaller.to_yaml(config);

  SECTION("YAML contains expected structure")
  {
    CHECK(yaml.find("ssl_multicert:") != std::string::npos);
    CHECK(yaml.find("ssl_cert_name: server.pem") != std::string::npos);
    CHECK(yaml.find("ssl_key_name: server.key") != std::string::npos);
    CHECK(yaml.find("ssl_cert_name: another.pem") != std::string::npos);
    CHECK(yaml.find("ssl_ticket_enabled: 1") != std::string::npos);
  }

  SECTION("YAML can be re-parsed")
  {
    SSLMultiCertParser parser;
    auto               result = parser.parse_string(yaml, "config.yaml");
    REQUIRE(result.ok());
    REQUIRE(result.value.size() == 2);
    CHECK(result.value[0].ssl_cert_name == "server.pem");
    CHECK(result.value[1].ssl_cert_name == "another.pem");
  }
}

TEST_CASE("SSLMultiCertMarshaller produces valid JSON", "[ssl_multicert][marshaller][json]")
{
  SSLMultiCertConfig config;

  SSLMultiCertEntry entry1;
  entry1.ssl_cert_name = "server.pem";
  entry1.dest_ip       = "*";
  config.push_back(entry1);

  SSLMultiCertEntry entry2;
  entry2.ssl_cert_name      = "another.pem";
  entry2.dest_ip            = "[::1]:8443";
  entry2.ssl_ticket_enabled = 1;
  entry2.ssl_ticket_number  = 5;
  config.push_back(entry2);

  SSLMultiCertMarshaller marshaller;
  std::string            json = marshaller.to_json(config);

  SECTION("JSON contains expected structure")
  {
    CHECK(json.find("\"ssl_multicert\"") != std::string::npos);
    CHECK(json.find("\"ssl_cert_name\": \"server.pem\"") != std::string::npos);
    CHECK(json.find("\"ssl_cert_name\": \"another.pem\"") != std::string::npos);
    CHECK(json.find("\"ssl_ticket_enabled\": 1") != std::string::npos);
    CHECK(json.find("\"ssl_ticket_number\": 5") != std::string::npos);
  }

  SECTION("JSON has valid array structure")
  {
    CHECK(json.find("[") != std::string::npos);
    CHECK(json.find("]") != std::string::npos);
    CHECK(json.find("{") != std::string::npos);
    CHECK(json.find("}") != std::string::npos);
  }
}

TEST_CASE("SSLMultiCertMarshaller escapes special characters", "[ssl_multicert][marshaller][escaping]")
{
  SSLMultiCertConfig config;

  SSLMultiCertEntry entry;
  entry.ssl_cert_name  = "server.pem";
  entry.dest_ip        = "*";
  entry.ssl_key_dialog = "exec:/path/to/script \"with quotes\"";
  config.push_back(entry);

  SSLMultiCertMarshaller marshaller;

  SECTION("YAML escapes quotes")
  {
    std::string yaml = marshaller.to_yaml(config);
    CHECK(yaml.find("ssl_key_dialog:") != std::string::npos);
  }

  SECTION("JSON escapes quotes")
  {
    std::string json = marshaller.to_json(config);
    CHECK(json.find("\\\"with quotes\\\"") != std::string::npos);
  }
}

TEST_CASE("Round-trip: legacy -> parse -> yaml -> parse", "[ssl_multicert][roundtrip]")
{
  SSLMultiCertParser     parser;
  SSLMultiCertMarshaller marshaller;

  // Parse legacy format.
  auto legacy_result = parser.parse_string(LEGACY_CONFIG, "ssl_multicert.config");
  REQUIRE(legacy_result.ok());

  // Marshal to YAML.
  std::string yaml = marshaller.to_yaml(legacy_result.value);

  // Re-parse YAML.
  auto yaml_result = parser.parse_string(yaml, "ssl_multicert.yaml");
  REQUIRE(yaml_result.ok());

  // Verify same number of entries.
  REQUIRE(legacy_result.value.size() == yaml_result.value.size());

  // Verify entries match.
  for (size_t i = 0; i < legacy_result.value.size(); ++i) {
    CHECK(legacy_result.value[i].ssl_cert_name == yaml_result.value[i].ssl_cert_name);
    CHECK(legacy_result.value[i].ssl_key_name == yaml_result.value[i].ssl_key_name);
    CHECK(legacy_result.value[i].ssl_key_dialog == yaml_result.value[i].ssl_key_dialog);
  }
}
