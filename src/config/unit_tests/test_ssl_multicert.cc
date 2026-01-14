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

#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace config;

namespace
{
// Helper to create a temporary file with content.
class TempFile
{
public:
  TempFile(std::string const &filename, std::string const &content)
  {
    _path = std::filesystem::temp_directory_path() / filename;
    std::ofstream ofs(_path);
    ofs << content;
  }

  ~TempFile() { std::filesystem::remove(_path); }

  std::string
  path() const
  {
    return _path.string();
  }

private:
  std::filesystem::path _path;
};

// Helper to parse content via temp file.
ConfigResult<SSLMultiCertConfig>
parse_content(std::string const &content, std::string const &filename)
{
  TempFile           file(filename, content);
  SSLMultiCertParser parser;
  return parser.parse(file.path());
}
} // namespace

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
  auto result = parse_content(LEGACY_CONFIG, "ssl_multicert.config");

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
  auto result = parse_content(YAML_CONFIG, "ssl_multicert.yaml");

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
  auto [description, content, filename] = GENERATE(table<char const *, char const *, char const *>({
    {"YAML from .yaml extension",          YAML_CONFIG,   "config.yaml"         },
    {"YAML from .yml extension",           YAML_CONFIG,   "config.yml"          },
    {"legacy from .config extension",      LEGACY_CONFIG, "ssl_multicert.config"},
    {"YAML from content (no extension)",   YAML_CONFIG,   "config"              },
    {"legacy from content (no extension)", LEGACY_CONFIG, "config"              },
  }));

  CAPTURE(description, filename);

  auto result = parse_content(content, filename);
  REQUIRE(result.ok());
  CHECK(result.value.size() == 3);
}

TEST_CASE("SSLMultiCertParser returns empty config for empty input", "[ssl_multicert][parser][edge]")
{
  auto [description, content, filename] = GENERATE(table<char const *, char const *, char const *>({
    {"empty YAML content",          "",                                      "config.yaml"  },
    {"empty legacy content",        "",                                      "config.config"},
    {"comments only in legacy",     "# Just a comment\n# Another comment\n", "config.config"},
    {"whitespace only",             "   \n\t\n   ",                          "config.config"},
    {"empty ssl_multicert in YAML", "ssl_multicert: []\n",                   "config.yaml"  },
  }));

  CAPTURE(description);

  auto result = parse_content(content, filename);
  REQUIRE(result.ok());
  CHECK(result.value.empty());
}

TEST_CASE("SSLMultiCertParser returns error for invalid input", "[ssl_multicert][parser][edge]")
{
  auto [description, content, filename] = GENERATE(table<char const *, char const *, char const *>({
    {"invalid YAML syntax",       "ssl_multicert: [not: valid: yaml", "config.yaml"},
    {"missing ssl_multicert key", "other_key:\n  - value: 1\n",       "config.yaml"},
  }));

  CAPTURE(description);

  auto result = parse_content(content, filename);
  CHECK_FALSE(result.ok());
}

TEST_CASE("SSLMultiCertParser handles all YAML entry fields", "[ssl_multicert][parser][fields]")
{
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

  auto result = parse_content(FULL_YAML, "config.yaml");
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
    auto result = parse_content(yaml, "config.yaml");
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

  CHECK(json.find("\"ssl_multicert\"") != std::string::npos);
  CHECK(json.find("\"ssl_cert_name\": \"server.pem\"") != std::string::npos);
  CHECK(json.find("\"ssl_cert_name\": \"another.pem\"") != std::string::npos);
  CHECK(json.find("\"ssl_ticket_enabled\": 1") != std::string::npos);
  CHECK(json.find("\"ssl_ticket_number\": 5") != std::string::npos);
  CHECK(json.find('[') != std::string::npos);
  CHECK(json.find(']') != std::string::npos);
}

TEST_CASE("SSLMultiCertMarshaller handles special characters", "[ssl_multicert][marshaller][escaping]")
{
  SSLMultiCertConfig config;

  SSLMultiCertEntry entry;
  entry.ssl_cert_name  = "server.pem";
  entry.dest_ip        = "*";
  entry.ssl_key_dialog = "exec:/path/to/script \"with quotes\"";
  config.push_back(entry);

  SSLMultiCertMarshaller marshaller;

  SECTION("YAML output contains the field and can be re-parsed")
  {
    std::string yaml = marshaller.to_yaml(config);
    CHECK(yaml.find("ssl_key_dialog:") != std::string::npos);

    // Verify round-trip preserves the value.
    auto result = parse_content(yaml, "test.yaml");
    REQUIRE(result.ok());
    REQUIRE(result.value.size() == 1);
    CHECK(result.value[0].ssl_key_dialog == "exec:/path/to/script \"with quotes\"");
  }

  SECTION("JSON escapes quotes")
  {
    std::string json = marshaller.to_json(config);
    CHECK(json.find("\\\"with quotes\\\"") != std::string::npos);
  }
}

TEST_CASE("Round-trip: legacy -> parse -> yaml -> parse", "[ssl_multicert][roundtrip]")
{
  SSLMultiCertMarshaller marshaller;

  // Parse legacy format.
  auto legacy_result = parse_content(LEGACY_CONFIG, "ssl_multicert.config");
  REQUIRE(legacy_result.ok());

  // Marshal to YAML.
  std::string yaml = marshaller.to_yaml(legacy_result.value);

  // Re-parse YAML.
  auto yaml_result = parse_content(yaml, "ssl_multicert.yaml");
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

TEST_CASE("SSLMultiCertParser::parse returns error for missing file", "[ssl_multicert][parser][file]")
{
  SSLMultiCertParser parser;

  auto result = parser.parse("/nonexistent/path/to/ssl_multicert.yaml");
  CHECK_FALSE(result.ok());
}

// ============================================================================
// Legacy format edge cases (parameterized)
// ============================================================================

TEST_CASE("Legacy parser handles whitespace variations", "[ssl_multicert][parser][legacy][whitespace]")
{
  auto [description, config, expected_cert, expected_key] = GENERATE(table<char const *, char const *, char const *, char const *>({
    {"multiple spaces between pairs", "ssl_cert_name=a.pem    ssl_key_name=a.key",   "a.pem", "a.key"},
    {"tabs between pairs",            "ssl_cert_name=a.pem\tssl_key_name=a.key",     "a.pem", "a.key"},
    {"leading whitespace",            "   ssl_cert_name=a.pem ssl_key_name=a.key",   "a.pem", "a.key"},
    {"trailing whitespace",           "ssl_cert_name=a.pem ssl_key_name=a.key   ",   "a.pem", "a.key"},
    {"leading tabs",                  "\t\tssl_cert_name=a.pem ssl_key_name=a.key",  "a.pem", "a.key"},
    {"mixed leading whitespace",      "  \t ssl_cert_name=a.pem ssl_key_name=a.key", "a.pem", "a.key"},
  }));

  CAPTURE(description, config);

  auto result = parse_content(config, "test.config");
  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].ssl_cert_name == expected_cert);
  CHECK(result.value[0].ssl_key_name == expected_key);
}

TEST_CASE("Legacy parser handles quoted values", "[ssl_multicert][parser][legacy][quotes]")
{
  auto [description, config, expected_field, expected_value] =
    GENERATE(table<char const *, char const *, char const *, char const *>({
      {"double-quoted with spaces",   R"(ssl_cert_name="path with spaces.pem")",                      "ssl_cert_name",  "path with spaces.pem"},
      {"single-quoted with spaces",   R"(ssl_cert_name='path with spaces.pem')",                      "ssl_cert_name",  "path with spaces.pem"},
      {"quoted followed by unquoted", R"(ssl_key_dialog="exec:/bin/script arg" ssl_cert_name=c.pem)", "ssl_key_dialog",
       "exec:/bin/script arg"                                                                                                                 },
      {"IPv6 in quotes",              R"(dest_ip="[::1]:443" ssl_cert_name=cert.pem)",                "dest_ip",        "[::1]:443"           },
      {"equals inside quotes",        R"(ssl_cert_name="value=with=equals")",                         "ssl_cert_name",  "value=with=equals"   },
  }));

  CAPTURE(description, config);

  auto result = parse_content(config, "test.config");
  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);

  auto const &entry = result.value[0];
  if (expected_field == std::string("ssl_cert_name")) {
    CHECK(entry.ssl_cert_name == expected_value);
  } else if (expected_field == std::string("ssl_key_dialog")) {
    CHECK(entry.ssl_key_dialog == expected_value);
  } else if (expected_field == std::string("dest_ip")) {
    CHECK(entry.dest_ip == expected_value);
  }
}

TEST_CASE("Legacy parser handles multiline content", "[ssl_multicert][parser][legacy][multiline]")
{
  auto [description, config, expected_count] = GENERATE(table<char const *, char const *, size_t>({
    {"three entries",             "ssl_cert_name=first.pem\nssl_cert_name=second.pem\nssl_cert_name=third.pem", 3},
    {"with comments and blanks",  "# Header\nssl_cert_name=first.pem\n\n# Comment\nssl_cert_name=second.pem\n", 2},
    {"Windows CRLF line endings", "ssl_cert_name=first.pem\r\nssl_cert_name=second.pem\r\n",                    2},
    {"single line no newline",    "ssl_cert_name=only.pem",                                                     1},
    {"single line with newline",  "ssl_cert_name=only.pem\n",                                                   1},
  }));

  CAPTURE(description);

  auto result = parse_content(config, "test.config");
  REQUIRE(result.ok());
  CHECK(result.value.size() == expected_count);
}

TEST_CASE("Legacy parser handles all field types", "[ssl_multicert][parser][legacy][fields]")
{
  static constexpr char FULL_LEGACY[] = "ssl_cert_name=cert.pem dest_ip=192.168.1.1 ssl_key_name=key.pem ssl_ca_name=ca.pem "
                                        "ssl_ocsp_name=ocsp.der ssl_key_dialog=builtin dest_fqdn=example.com action=tunnel "
                                        "ssl_ticket_enabled=1 ssl_ticket_number=5";

  auto result = parse_content(FULL_LEGACY, "test.config");
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

TEST_CASE("Legacy parser handles dual certificates", "[ssl_multicert][parser][legacy][dual-cert]")
{
  auto result =
    parse_content("ssl_cert_name=server-ec.pem,server-rsa.pem ssl_key_name=server-ec.key,server-rsa.key", "test.config");
  REQUIRE(result.ok());
  REQUIRE(result.value.size() == 1);
  CHECK(result.value[0].ssl_cert_name == "server-ec.pem,server-rsa.pem");
  CHECK(result.value[0].ssl_key_name == "server-ec.key,server-rsa.key");
}

TEST_CASE("Legacy parser skips malformed entries", "[ssl_multicert][parser][legacy][malformed]")
{
  auto [description, config, expected_count] = GENERATE(table<char const *, char const *, size_t>({
    {"line without equals",        "ssl_cert_name=valid.pem\nmalformed_no_equals\nssl_cert_name=another.pem", 2},
    {"blank line between entries", "ssl_cert_name=first.pem\n\nssl_cert_name=second.pem",                     2},
    {"comment before valid entry", "# only comment\nssl_cert_name=valid.pem",                                 1},
  }));

  CAPTURE(description);

  auto result = parse_content(config, "test.config");
  REQUIRE(result.ok());
  CHECK(result.value.size() == expected_count);
}
