/** @file

  Unit tests for the Prometheus rules of stats_over_http.

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

#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <yaml-cpp/yaml.h>

#include "prometheus_render.h"
#include "prometheus_rules.h"
#include "test_stats.h"

namespace
{
// Rules that turn the names of Traffic Server metrics into labeled families.
constexpr char METRIC_RULES[] = R"(
help: false
name:
  replace: [{from: '+', to: 'plus'}]
  invalid: '[^a-zA-Z0-9_]'
types:
  source: rules
  rules:
    - {match: 'current', type: gauge}
    - {match: '', type: counter}
exclude:
  names: [proxy.process.http.tunnels]
  match: ['^proxy\.process\.http\.total_client_connections$', '^proxy\.process\.ssl\.total_success_handshake_count$']
strings: {label: value, names: [proxy.process.version.server.short]}
rules:
  - {match: '^(proxy\.process\.cache\.volume)_([0-9]+)\.(.+)$', labels: {volume: 2}}
  - {match: '^(proxy\.process\.http)\.([0-9xX]{3})_(responses)$', labels: {code: 2}}
  - {match: '^(proxy\.process\.ssl\.cipher\.user_agent)\.(.+)$', labels: {cipher: 2}}
  - {match: '^(proxy\.process\.ssl\.total_success_handshake_count)_(.+)$', labels: {direction: 2}}
  - match: '^(proxy\.process\.http\.per_server)\.([a-z]+_connection)\.(.+)\.([0-9]{1,3}(?:\.[0-9]{1,3}){3}):([0-9]+)$'
    labels: {origin: 3, origin_ip: 4, port: 5}
  - {match: '^(proxy\.process\.http\.per_server)\.([a-z]+_connection)\.(max)\.(.+)$', labels: {origin: 4}}
  - {match: '^(proxy\.process\.http\.total_client_connections)_(ipv[46])$', labels: {protocol: 2}}
)";

std::shared_ptr<const PrometheusRules>
load(const std::string &yaml)
{
  auto rules = std::make_shared<PrometheusRules>();

  REQUIRE(rules->load(YAML::Load(yaml)) == "");
  return rules;
}

std::string
load_error(const std::string &yaml)
{
  PrometheusRules rules;

  return rules.load(YAML::Load(yaml));
}

// The family, the labels in the order of their rule, and the type, for example "family{a=x,value=*} gauge".  The string
// value is "*".
std::string
describe(const PrometheusRuleMatch &match)
{
  if (match.result == PrometheusRuleMatch::Result::EXCLUDED) {
    return "excluded";
  }
  if (match.result == PrometheusRuleMatch::Result::INVALID_NAME) {
    return "invalid " + match.family;
  }

  std::string result = match.family;

  if (!match.label_names->empty()) {
    result += '{';
    for (size_t i = 0; i < match.label_names->size(); ++i) {
      result.append(i > 0 ? "," : "").append((*match.label_names)[i]).append("=");
      result.append(i < match.label_values.size() ? match.label_values[i] : std::string{"*"});
    }
    result += '}';
  }
  return result.append(" ").append(prometheus_type_name(match.type));
}

std::vector<std::string> warnings;

void
record_warning(const std::string &message)
{
  warnings.push_back(message);
}

struct Rendered {
  std::string           body;
  PrometheusRenderStats stats;
};

class Renderer
{
public:
  explicit Renderer(const std::string &yaml) : _renderer(options(load(yaml))) { warnings.clear(); }

  Rendered
  render(const std::vector<Stat> &stats)
  {
    return {::render(_renderer, stats), _renderer.stats()};
  }

private:
  static PrometheusOptions
  options(std::shared_ptr<const PrometheusRules> rules)
  {
    PrometheusOptions result;

    result.help  = rules->help();
    result.rules = std::move(rules);
    result.warn  = record_warning;
    return result;
  }

  PrometheusRenderer _renderer;
};

struct ErrorCase {
  const char *yaml;
  const char *error;
};
} // namespace

TEST_CASE("Rules name, label and type records", "[prometheus_rules]")
{
  auto const rules = load(METRIC_RULES);
  auto const check = [&rules](const char *name, TSRecordDataType data_type, const char *expected) {
    CAPTURE(name);
    CHECK(describe(rules->translate(name, data_type)) == expected);
  };

  // A name that matches no rule has no labels.
  check("proxy.process.http.incoming_requests", TS_RECORDDATATYPE_COUNTER, "proxy_process_http_incoming_requests counter");

  // Type rules match anywhere in the name, and the first one that matches wins.
  check("proxy.process.net.connections_currently_open", TS_RECORDDATATYPE_COUNTER,
        "proxy_process_net_connections_currently_open gauge");
  check("proxy.process.http.current_active_client_connections", TS_RECORDDATATYPE_INT,
        "proxy_process_http_current_active_client_connections gauge");

  // Exact names and regular expressions exclude records.
  check("proxy.process.http.tunnels", TS_RECORDDATATYPE_COUNTER, "excluded");
  check("proxy.process.http.total_client_connections", TS_RECORDDATATYPE_COUNTER, "excluded");
  check("proxy.process.ssl.total_success_handshake_count", TS_RECORDDATATYPE_COUNTER, "excluded");

  // The family joins the capture groups that are not labels with '_'.  Text outside the groups is not in the family.
  check("proxy.process.http.200_responses", TS_RECORDDATATYPE_COUNTER, "proxy_process_http_responses{code=200} counter");
  check("proxy.process.http.5xx_responses", TS_RECORDDATATYPE_COUNTER, "proxy_process_http_responses{code=5xx} counter");
  check("proxy.process.ssl.total_success_handshake_count_in", TS_RECORDDATATYPE_COUNTER,
        "proxy_process_ssl_total_success_handshake_count{direction=in} counter");
  check("proxy.process.http.total_client_connections_ipv4", TS_RECORDDATATYPE_COUNTER,
        "proxy_process_http_total_client_connections{protocol=ipv4} counter");

  // The volume number is a label, and the rest of the name joins the family.
  check("proxy.process.cache.volume_1.bytes_used", TS_RECORDDATATYPE_INT,
        "proxy_process_cache_volume_bytes_used{volume=1} counter");
  check("proxy.process.cache.volume_12.bytes_used", TS_RECORDDATATYPE_INT,
        "proxy_process_cache_volume_bytes_used{volume=12} counter");

  // '+' becomes "plus" in the family, but a label value keeps it.
  check("proxy.process.example.a+b", TS_RECORDDATATYPE_COUNTER, "proxy_process_example_aplusb counter");
  check("proxy.process.ssl.cipher.user_agent.TLS+AES", TS_RECORDDATATYPE_COUNTER,
        "proxy_process_ssl_cipher_user_agent{cipher=TLS+AES} counter");

  // The type rules match the name without its label values, so an origin cannot change the type of its family.
  check("proxy.process.http.per_server.current_connection.origin.example.com.192.0.2.1:443", TS_RECORDDATATYPE_INT,
        "proxy_process_http_per_server_current_connection{origin=origin.example.com,origin_ip=192.0.2.1,port=443} gauge");
  check("proxy.process.http.per_server.total_connection.concurrent.example.com.192.0.2.1:443", TS_RECORDDATATYPE_COUNTER,
        "proxy_process_http_per_server_total_connection{origin=concurrent.example.com,origin_ip=192.0.2.1,port=443} counter");
  check("proxy.process.http.per_server.current_connection.max.origin.example.com", TS_RECORDDATATYPE_INT,
        "proxy_process_http_per_server_current_connection_max{origin=origin.example.com} gauge");

  // The per_server rule needs an IPv4 address, so an IPv6 key is a family of its own.
  check("proxy.process.http.per_server.current_connection.origin.example.com.2001:db8::1:443", TS_RECORDDATATYPE_INT,
        "proxy_process_http_per_server_current_connection_origin_example_com_2001_db8__1_443 gauge");

  // A listed string record puts its value in a label.  Another string record is a sample when its value is a number.
  check("proxy.process.version.server.short", TS_RECORDDATATYPE_STRING, "proxy_process_version_server_short{value=*} counter");
  check("proxy.process.version.server.build_number", TS_RECORDDATATYPE_STRING, "proxy_process_version_server_build_number counter");
}

TEST_CASE("Rule details", "[prometheus_rules]")
{
  SECTION("A label at position 1 leaves the family with a leading '_'")
  {
    auto const rules = load("rules: [{match: '^([a-z]+)\\.(.+)$', labels: {host: 1}}]");

    CHECK(describe(rules->translate("origin.bytes", TS_RECORDDATATYPE_COUNTER)) == "_bytes{host=origin} counter");
  }

  SECTION("The first matching rule wins")
  {
    auto const rules = load("rules:\n"
                            "  - {match: '^(a)\\.(.+)$', labels: {first: 2}}\n"
                            "  - {match: '^(a)\\.(b)$', labels: {second: 2}}\n");
    auto const match = rules->translate("a.b", TS_RECORDDATATYPE_COUNTER);

    CHECK(describe(match) == "a{first=b} counter");
    CHECK(match.rule_index == 0);
    CHECK(rules->translate("c", TS_RECORDDATATYPE_COUNTER).rule_index == 2);
  }

  SECTION("Without type rules, the record type sets the type")
  {
    auto const rules = load("{}");

    CHECK(describe(rules->translate("a.current", TS_RECORDDATATYPE_COUNTER)) == "a_current counter");
    CHECK(describe(rules->translate("a.b", TS_RECORDDATATYPE_INT)) == "a_b gauge");
    CHECK(describe(rules->translate("a:b", TS_RECORDDATATYPE_FLOAT)) == "a:b gauge");
  }

  SECTION("A name must be a valid metric name")
  {
    auto const rules = load("{}");

    CHECK(describe(rules->translate("1st.metric", TS_RECORDDATATYPE_COUNTER)) == "invalid 1st_metric");
  }
}

TEST_CASE("Invalid rules", "[prometheus_rules]")
{
  auto const test = GENERATE(values<ErrorCase>({
    {"rules: [{match: 'proxy\\.process'}]",                            "has no capture group"                      },
    {"rules: [{match: '^(a)\\.(b)', labels: {x: 3}}]",                 "3 is not a capture group"                  },
    {"rules: [{match: '^(a'}]",                                        "cannot compile"                            },
    {"rules: [{labels: {x: 1}}]",                                      "needs match"                               },
    {"rules: [{match: '^(a)', labels: {value: 1}}]",                   "is also prometheus.strings.label"          },
    {"const_labels: {x: y}\nrules: [{match: '^(a)', labels: {x: 1}}]", "is also a constant label"                  },
    {"const_labels: {v: x}\nstrings: {label: v}",                      "is also a constant label"                  },
    {"const_labels: {value: x}\nstrings: {names: [a]}",                "is also a constant label"                  },
    {"rules: [{match: '^(a)', labels: {__x: 1}}]",                     "not a valid label name"                    },
    {"const_labels: {1x: y}",                                          "not a valid label name"                    },
    {"unknown: 1",                                                     "unknown key prometheus.unknown"            },
    {"types: {rules: [{match: 'x', type: gauge}]}",                    "needs prometheus.types.source set to rules"},
    {"types: {source: rules, rules: [{match: 'x', type: histogram}]}", "is not counter, gauge or untyped"          },
    {"types: {source: labels}",                                        "is not record or rules"                    },
    {"name: {invalid: 'x*'}",                                          "must not match an empty string"            },
    {"name: {replace: [{to: x}]}",                                     "replace.from is missing or empty"          },
    {"limits: {max_series: -1}",                                       "must not be negative"                      },
    {"strings: {names: x}",                                            "must be a list"                            },
    {"rules: {match: x}",                                              "must be a list"                            },
  }));

  CAPTURE(test.yaml);
  CHECK_THAT(load_error(test.yaml), Catch::Matchers::ContainsSubstring(test.error));
}

TEST_CASE("An error has the line of its node", "[prometheus_rules]")
{
  CHECK(load_error("help: false\nrules:\n  - {match: '^(a)', labels: {x: 2}}\n").starts_with("line 3: "));
  CHECK(load_error("help: false\nconst_labels:\n  value: x\nstrings: {names: [a]}\n").starts_with("line 3: "));
}

TEST_CASE("Rendered rules", "[prometheus_rules]")
{
  SECTION("Samples, strings and labels")
  {
    Renderer renderer{METRIC_RULES};

    auto const rendered = renderer.render({
      counter("proxy.process.http.200_responses", 10),
      counter("proxy.process.http.tunnels", 3),
      gauge("proxy.process.http.current_active_client_connections", -2),
      counter("proxy.process.http.404_responses", 5),
      string("proxy.process.version.server.short", "10.2.\"0\""),
      string("proxy.process.version.server.build_number", "1.5e3"),
      string("proxy.process.version.server.build_date", "Jan 1 2026"),
      string("proxy.process.example.infinity", "-Infinity"),
      string("proxy.process.example.nan", "NaN"),
      string("proxy.process.example.signed_nan", "-nan"),
      string("proxy.process.example.huge", "1e400"),
      floating("proxy.process.example.ratio", 0.1f),
    });

    CHECK(rendered.body == "# TYPE proxy_process_example_infinity counter\n"
                           "proxy_process_example_infinity -Inf\n"
                           "# TYPE proxy_process_example_nan counter\n"
                           "proxy_process_example_nan NaN\n"
                           "# TYPE proxy_process_example_ratio counter\n"
                           "proxy_process_example_ratio 0.1\n"
                           "# TYPE proxy_process_http_current_active_client_connections gauge\n"
                           "proxy_process_http_current_active_client_connections -2\n"
                           "# TYPE proxy_process_http_responses counter\n"
                           "proxy_process_http_responses{code=\"200\"} 10\n"
                           "proxy_process_http_responses{code=\"404\"} 5\n"
                           "# TYPE proxy_process_version_server_build_number counter\n"
                           "proxy_process_version_server_build_number 1500\n"
                           "# TYPE proxy_process_version_server_short counter\n"
                           "proxy_process_version_server_short{value=\"10.2.\\\"0\\\"\"} 1\n");
    CHECK(rendered.stats.series == 8);
    CHECK(rendered.stats.dropped == 0);
    CHECK(warnings.empty());
  }

  SECTION("A string value changes between renders")
  {
    Renderer renderer{METRIC_RULES};

    renderer.render({string("proxy.process.version.server.short", "10.2.0")});
    CHECK(renderer.render({string("proxy.process.version.server.short", "10.2.1")}).body ==
          "# TYPE proxy_process_version_server_short counter\n"
          "proxy_process_version_server_short{value=\"10.2.1\"} 1\n");
  }

  SECTION("String metrics with the same family and other labels are duplicates, whatever their strings")
  {
    // Both render paths: without and with a series limit.
    std::string const limits = GENERATE(as<std::string>{}, "", "limits: {max_series: 10}\n");
    Renderer          renderer{limits + "strings: {names: [version.one, version.two]}\n"
                                        "rules: [{match: '^(version)\\.(?:one|two)$'}]"};

    auto rendered = renderer.render({string("version.one", "v1"), string("version.two", "v2")});

    CHECK(rendered.body == "# TYPE version gauge\n"
                           "version{value=\"v1\"} 1\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.duplicates == 1);
    CHECK(rendered.stats.dropped == 0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "version.two has the same family and labels, apart from its string, as version.one, so each render "
                         "writes only the first of them that it finds");

    // The first metric writes the series, with its string of each render.
    rendered = renderer.render({string("version.one", "a\"b\\c"), string("version.two", "v2")});
    CHECK(rendered.body == "# TYPE version gauge\n"
                           "version{value=\"a\\\"b\\\\c\"} 1\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.duplicates == 1);
    CHECK(warnings.size() == 1);

    // Without the first metric, the next one writes the series.
    rendered = renderer.render({string("version.two", "v2")});
    CHECK(rendered.body == "# TYPE version gauge\n"
                           "version{value=\"v2\"} 1\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.duplicates == 0);
    CHECK(warnings.size() == 1);
  }

  SECTION("String metrics with other labels have their own series")
  {
    Renderer renderer{"strings: {names: [version.one, version.two]}\n"
                      "rules: [{match: '^(version)\\.(one|two)$', labels: {kind: 2}}]"};

    auto const rendered = renderer.render({string("version.two", "v1"), string("version.one", "v1")});

    CHECK(rendered.body == "# TYPE version gauge\n"
                           "version{kind=\"one\",value=\"v1\"} 1\n"
                           "version{kind=\"two\",value=\"v1\"} 1\n");
    CHECK(rendered.stats.series == 2);
    CHECK(rendered.stats.duplicates == 0);
    CHECK(rendered.stats.dropped == 0);
    CHECK(warnings.empty());
  }

  SECTION("The samples of a family of strings appear in the order of their other labels")
  {
    Renderer renderer{"strings: {names: [v.x, v.y]}\n"
                      "rules: [{match: '^(v)\\.(x|y)$', labels: {zone: 2}}]"};

    CHECK(renderer.render({string("v.y", "a"), string("v.x", "b")}).body == "# TYPE v gauge\n"
                                                                            "v{value=\"b\",zone=\"x\"} 1\n"
                                                                            "v{value=\"a\",zone=\"y\"} 1\n");
  }

  SECTION("A family of string metrics leaves out other metrics")
  {
    Renderer renderer{"strings: {names: [f.s]}\n"
                      "rules:\n"
                      "  - {match: '^(f)\\.n\\.(.+)$', labels: {k: 2}}\n"
                      "  - {match: '^(f)\\.s$'}"};

    // The string metric sets the label names, so the family is of string metrics, and the number is left out even though
    // it has as many labels.
    auto const rendered = renderer.render({gauge("f.n.x", 5), string("f.s", "x")});

    CHECK(rendered.body == "# TYPE f gauge\n"
                           "f{value=\"x\"} 1\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.dropped == 1);
    CHECK(rendered.stats.duplicates == 0);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "Leaving out f.n.x, because f takes its label names from f.s, a string metric in "
                         "prometheus.strings.names");
  }

  SECTION("A family of other metrics leaves out string metrics")
  {
    Renderer renderer{"strings: {names: [f.s]}\n"
                      "rules:\n"
                      "  - {match: '^(f)\\.s$'}\n"
                      "  - {match: '^(f)\\.n\\.(.+)$', labels: {k: 2}}"};

    auto rendered = renderer.render({string("f.s", "x"), gauge("f.n.y", 5)});

    CHECK(rendered.body == "# TYPE f gauge\n"
                           "f{k=\"y\"} 5\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.dropped == 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "Leaving out f.s, because it is a string metric in prometheus.strings.names and f takes its label names "
                         "from f.n.y, which is not");

    // The string metric stays out when its string changes.
    rendered = renderer.render({string("f.s", "y"), gauge("f.n.y", 5)});
    CHECK(rendered.body == "# TYPE f gauge\n"
                           "f{k=\"y\"} 5\n");
    CHECK(rendered.stats.series == 1);
    CHECK(rendered.stats.dropped == 1);
    CHECK(warnings.size() == 1);
  }

  SECTION("A string metric that is not in strings.names is of the other kind")
  {
    Renderer renderer{"strings: {names: [f.s]}\n"
                      "rules:\n"
                      "  - {match: '^(f)\\.n\\.(.+)$', labels: {k: 2}}\n"
                      "  - {match: '^(f)\\.s$'}"};

    auto const rendered = renderer.render({string("f.n.x", "5"), string("f.s", "x")});

    CHECK(rendered.body == "# TYPE f gauge\n"
                           "f{value=\"x\"} 1\n");
    CHECK(rendered.stats.dropped == 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "Leaving out f.n.x, because f takes its label names from f.s, a string metric in "
                         "prometheus.strings.names");
  }

  SECTION("Records of one rule give their family the same kind in any order")
  {
    std::string const yaml = "strings: {names: [v.short]}\n"
                             "rules: [{match: '^(v)\\.(.+)$', labels: {part: 2}}]";
    Renderer          short_first{yaml};
    auto const        one = short_first.render({string("v.short", "10.2.0"), string("v.build", "1500")});
    Renderer          build_first{yaml};
    auto const        two = build_first.render({string("v.build", "1500"), string("v.short", "10.2.0")});

    // At the same rule, the record that is not in strings.names sets the label names.
    CHECK(one.body == "# TYPE v gauge\n"
                      "v{part=\"build\"} 1500\n");
    CHECK(one.body == two.body);
    CHECK(one.stats.dropped == 1);
    CHECK(two.stats.dropped == 1);
  }

  SECTION("A record that its family leaves out does not report a type")
  {
    Renderer renderer{"strings: {names: [f.s]}\n"
                      "rules:\n"
                      "  - {match: '^(f)\\.n\\.(.+)$', labels: {k: 2}}\n"
                      "  - {match: '^(f)\\.s$'}"};

    auto const rendered = renderer.render({string("f.s", "x"), counter("f.n.x", 5)});

    CHECK(rendered.body == "# TYPE f gauge\n"
                           "f{value=\"x\"} 1\n");
    CHECK(rendered.stats.dropped == 1);
    CHECK(rendered.stats.type_conflicts == 0);
    REQUIRE(warnings.size() == 1);
    CHECK_THAT(warnings[0], Catch::Matchers::StartsWith("Leaving out f.n.x,"));
  }

  SECTION("HELP lines and constant labels")
  {
    Renderer renderer{"help: true\n"
                      "const_labels: {zone: b, host: a}\n"
                      "strings: {label: version, names: [v]}\n"
                      "rules: [{match: '^(req)\\.(.+)$', labels: {method: 2}}]"};

    CHECK(renderer.render({counter("req.get", 1), string("v", "1.0")}).body == "# HELP req req.get\n"
                                                                               "# TYPE req counter\n"
                                                                               "req{host=\"a\",method=\"get\",zone=\"b\"} 1\n"
                                                                               "# HELP v v\n"
                                                                               "# TYPE v gauge\n"
                                                                               "v{host=\"a\",version=\"1.0\",zone=\"b\"} 1\n");
  }

  SECTION("The last rule that defines a family sets its label names")
  {
    Renderer renderer{"rules:\n"
                      "  - {match: '^(conn)\\.open\\.(.+)$', labels: {origin: 2}}\n"
                      "  - {match: '^(conn)\\.(.+)\\.total$', labels: {peer: 2}}\n"
                      "  - {match: '^(conn)\\.(.+)\\.(.+)\\.pair$', labels: {a: 2, b: 3}}\n"};

    // A sample with as many labels gets the label names of the family.
    auto rendered = renderer.render({counter("conn.open.x", 1), counter("conn.y.total", 2)});

    CHECK(rendered.body == "# TYPE conn counter\n"
                           "conn{peer=\"x\"} 1\n"
                           "conn{peer=\"y\"} 2\n");
    CHECK(rendered.stats.relabeled == 1);
    REQUIRE(warnings.size() == 1);
    CHECK_THAT(warnings[0], Catch::Matchers::ContainsSubstring("conn.open.x"));

    // A sample with another number of labels is left out.
    rendered = renderer.render({counter("conn.open.x", 1), counter("conn.y.total", 2), counter("conn.p.q.pair", 3)});
    CHECK(rendered.body == "# TYPE conn counter\n"
                           "conn{a=\"p\",b=\"q\"} 3\n");
    CHECK(rendered.stats.dropped == 2);
    CHECK(rendered.stats.relabeled == 0);
  }

  SECTION("A sample with the label names of its family in another order keeps its values")
  {
    Renderer renderer{"rules:\n"
                      "  - {match: '^(conn)\\.(.+)\\.to\\.(.+)$', labels: {src: 2, dst: 3}}\n"
                      "  - {match: '^(conn)\\.(.+)\\.from\\.(.+)$', labels: {dst: 2, src: 3}}\n"};

    auto const rendered = renderer.render({counter("conn.a.to.b", 1), counter("conn.d.from.s", 2)});

    CHECK(rendered.body == "# TYPE conn counter\n"
                           "conn{dst=\"b\",src=\"a\"} 1\n"
                           "conn{dst=\"d\",src=\"s\"} 2\n");
    CHECK(rendered.stats.relabeled == 0);
    CHECK(warnings.empty());
  }

  SECTION("The warnings name the samples that the render leaves out or relabels")
  {
    Renderer                renderer{"rules:\n"
                                     "  - {match: '^(f)\\.x\\.(.+)$', labels: {a: 2}}\n"
                                     "  - {match: '^(f)\\.y\\.(.+)\\.(.+)$', labels: {b: 2, c: 3}}\n"
                                     "  - {match: '^(f)\\.z\\.(.+)$', labels: {d: 2}}\n"};
    std::vector<Stat> const stats{counter("f.x.1", 1), counter("f.y.2.3", 2), counter("f.z.4", 4)};

    // Each new record with a later rule sets the label names of the family again, before the render.
    auto const rendered = renderer.render(stats);

    CHECK(rendered.body == "# TYPE f counter\n"
                           "f{d=\"1\"} 1\n"
                           "f{d=\"4\"} 4\n");
    CHECK(rendered.stats.dropped == 1);
    CHECK(rendered.stats.relabeled == 1);
    REQUIRE(warnings.size() == 2);
    CHECK_THAT(warnings[0], Catch::Matchers::StartsWith("Leaving out f.y.2.3,"));
    CHECK_THAT(warnings[1], Catch::Matchers::StartsWith("Writing f.x.1 "));

    renderer.render(stats);
    CHECK(warnings.size() == 2);
  }

  SECTION("A name that matches no rule sets the label names of its family")
  {
    Renderer renderer{"rules: [{match: '^(a)_(.+)$', labels: {x: 2}}]"};

    auto const rendered = renderer.render({counter("a_b", 1), counter("a", 2)});

    CHECK(rendered.body == "# TYPE a counter\n"
                           "a 2\n");
    CHECK(rendered.stats.dropped == 1);
  }

  SECTION("A record that leaves the statistics still sets the label names of its family")
  {
    Renderer renderer{"rules:\n"
                      "  - {match: '^(g)\\.a\\.(.+)$', labels: {k: 2}}\n"
                      "  - {match: '^(g)\\.b\\.(.+)\\.(.+)$', labels: {k: 2, m: 3}}"};

    renderer.render({counter("g.a.1", 1), counter("g.b.2.3", 2)});

    auto const rendered = renderer.render({counter("g.a.1", 1)});

    CHECK(rendered.body == "");
    CHECK(rendered.stats.dropped == 1);
  }

  SECTION("The first record with a series writes it")
  {
    Renderer renderer{"{}"};

    auto rendered = renderer.render({gauge("a.b", 1), gauge("a_b", 2), gauge("a-b", 3)});

    CHECK(rendered.body == "# TYPE a_b gauge\n"
                           "a_b 1\n");
    CHECK(rendered.stats.duplicates == 2);
    CHECK(warnings.size() == 1);

    // Without the first record, the next one writes the series.
    rendered = renderer.render({gauge("a_b", 2), gauge("a-b", 3)});
    CHECK(rendered.body == "# TYPE a_b gauge\n"
                           "a_b 2\n");
    CHECK(rendered.stats.duplicates == 1);
  }

  SECTION("After a regroup, the warning names the record that writes the series")
  {
    Renderer renderer{"rules:\n"
                      "  - {match: '^(f)\\.y\\.(.+)\\.(.+)$', labels: {c: 2, d: 3}}\n"
                      "  - {match: '^(f)\\.x\\.(.+)\\.(.+)$', labels: {a: 2, b: 3}}\n"
                      "  - {match: '^(f)\\.w\\.(.+)\\.(.+)$', labels: {d: 2, c: 3}}"};

    renderer.render({counter("f.y.2.1", 1), counter("f.x.1.2", 2)});
    warnings.clear();

    auto const rendered = renderer.render({counter("f.y.2.1", 1), counter("f.x.1.2", 2), counter("f.w.9.9", 3)});

    CHECK(rendered.body == "# TYPE f counter\n"
                           "f{c=\"2\",d=\"1\"} 1\n"
                           "f{c=\"9\",d=\"9\"} 3\n");
    CHECK(rendered.stats.duplicates == 1);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "f.x.1.2 has the same series as f.y.2.1, so each render writes only the first of them that it finds");
  }

  SECTION("The first record of a family sets its type")
  {
    Renderer renderer{"types: {source: rules, rules: [{match: 'current', type: gauge}]}\n"
                      "rules: [{match: '^(conn)\\.(?:current|total)\\.(.+)$', labels: {origin: 2}}]"};

    auto const rendered = renderer.render({counter("conn.current.x", 1), counter("conn.total.y", 2), counter("conn.total.z", 3)});

    CHECK(rendered.body == "# TYPE conn gauge\n"
                           "conn{origin=\"x\"} 1\n"
                           "conn{origin=\"y\"} 2\n"
                           "conn{origin=\"z\"} 3\n");
    CHECK(rendered.stats.type_conflicts == 2);
    REQUIRE(warnings.size() == 1);
    CHECK(warnings[0] == "Writing conn.total.y with the type (gauge) of conn instead of its own type (counter)");
  }

  SECTION("The series limit keeps the oldest series")
  {
    Renderer renderer{"limits: {max_series: 2}"};

    renderer.render({gauge("b", 1), gauge("c", 2)});

    auto rendered = renderer.render({gauge("a", 0), gauge("b", 1), gauge("c", 2)});

    CHECK(rendered.body == "# TYPE b gauge\n"
                           "b 1\n"
                           "# TYPE c gauge\n"
                           "c 2\n");
    CHECK(rendered.stats.series == 2);
    CHECK(rendered.stats.dropped == 1);

    // Without an older series, the newest one fits.
    rendered = renderer.render({gauge("a", 0), gauge("c", 2)});
    CHECK(rendered.body == "# TYPE a gauge\n"
                           "a 0\n"
                           "# TYPE c gauge\n"
                           "c 2\n");
    CHECK(rendered.stats.dropped == 0);
  }

  SECTION("The families appear by name, and the samples of a family by their labels")
  {
    Renderer renderer{"rules: [{match: '^(req)\\.(.+)$', labels: {method: 2}}]"};

    CHECK(renderer.render({counter("req.put", 1), gauge("b", 2), counter("req.get", 3)}).body == "# TYPE b gauge\n"
                                                                                                 "b 2\n"
                                                                                                 "# TYPE req counter\n"
                                                                                                 "req{method=\"get\"} 3\n"
                                                                                                 "req{method=\"put\"} 1\n");

    // New records take their places in the order.
    auto const rendered =
      renderer.render({counter("req.put", 1), gauge("b", 2), counter("req.get", 3), counter("req.head", 4), gauge("a", 5)});

    CHECK(rendered.body == "# TYPE a gauge\n"
                           "a 5\n"
                           "# TYPE b gauge\n"
                           "b 2\n"
                           "# TYPE req counter\n"
                           "req{method=\"get\"} 3\n"
                           "req{method=\"head\"} 4\n"
                           "req{method=\"put\"} 1\n");
  }

  SECTION("A record with an invalid name is left out")
  {
    Renderer renderer{"{}"};

    auto const rendered = renderer.render({gauge("1a", 1), gauge("2b", 2), gauge("c", 3)});

    CHECK(rendered.body == "# TYPE c gauge\n"
                           "c 3\n");
    CHECK(rendered.stats.dropped == 2);
    CHECK(warnings.size() == 1);
  }
}
