/** @file

  Unit tests for the Prometheus renderer of stats_over_http.

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

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "prometheus_render.h"
#include "test_stats.h"

namespace
{
int namer_calls = 0;

// "family.value" becomes family{k="value"}.
PrometheusName
test_name(std::string_view name, TSRecordDataType data_type)
{
  PrometheusName result;
  auto const     dot = name.find('.');

  ++namer_calls;
  result.family = name.substr(0, dot);
  if (dot != std::string_view::npos) {
    result.labels = "k=\"" + std::string{name.substr(dot + 1)} + "\"";
  }
  if (data_type == TS_RECORDDATATYPE_COUNTER) {
    result.type = PrometheusType::COUNTER;
  } else if (data_type == TS_RECORDDATATYPE_INT) {
    result.type = PrometheusType::GAUGE;
  }
  return result;
}

PrometheusOptions
options(bool help = true, bool wrap_counters = false)
{
  PrometheusOptions result;

  result.namer         = test_name;
  result.help          = help;
  result.wrap_counters = wrap_counters;
  return result;
}
} // namespace

TEST_CASE("Each family has one header and its samples together", "[prometheus_render]")
{
  PrometheusRenderer renderer{options()};

  CHECK(render(renderer, {counter("req.get", 5), gauge("conn", -3), counter("req.post", 7), floating("load", 0.5),
                          string("version", "10.2.0")}) == "# HELP req req.get\n"
                                                           "# TYPE req counter\n"
                                                           "req{k=\"get\"} 5\n"
                                                           "req{k=\"post\"} 7\n"
                                                           "# HELP conn conn\n"
                                                           "# TYPE conn gauge\n"
                                                           "conn -3\n"
                                                           "# HELP load load\n"
                                                           "load 0.5\n");
}

TEST_CASE("The first record of a family sets its type", "[prometheus_render]")
{
  PrometheusRenderer renderer{options()};

  CHECK(render(renderer, {gauge("req.get", 1), counter("req.post", 2)}) == "# HELP req req.get\n"
                                                                           "# TYPE req gauge\n"
                                                                           "req{k=\"get\"} 1\n"
                                                                           "req{k=\"post\"} 2\n");
}

TEST_CASE("HELP lines are optional", "[prometheus_render]")
{
  PrometheusRenderer renderer{options(false)};

  CHECK(render(renderer, {counter("req.get", 5), floating("load", 0.5)}) == "# TYPE req counter\n"
                                                                            "req{k=\"get\"} 5\n"
                                                                            "load 0.5\n");
}

TEST_CASE("Values", "[prometheus_render]")
{
  std::vector<Stat> const stats{counter("max", -1), gauge("min", std::numeric_limits<int64_t>::min()),
                                counter("big", std::numeric_limits<int64_t>::max()), floating("large", 1e20)};

  SECTION("Counters are unsigned and gauges signed")
  {
    PrometheusRenderer renderer{options(false)};

    CHECK(render(renderer, stats) == "# TYPE max counter\n"
                                     "max 18446744073709551615\n"
                                     "# TYPE min gauge\n"
                                     "min -9223372036854775808\n"
                                     "# TYPE big counter\n"
                                     "big 9223372036854775807\n"
                                     "large 1e+20\n");
  }

  SECTION("Only counters above INT64_MAX wrap")
  {
    PrometheusRenderer renderer{options(false, true)};

    CHECK(render(renderer, stats) == "# TYPE max counter\n"
                                     "max 1\n"
                                     "# TYPE min gauge\n"
                                     "min -9223372036854775808\n"
                                     "# TYPE big counter\n"
                                     "big 9223372036854775807\n"
                                     "large 1e+20\n");
  }
}

TEST_CASE("A cached render has the current values", "[prometheus_render]")
{
  PrometheusRenderer renderer{options(false)};

  namer_calls = 0;
  render(renderer, {counter("req.get", 1), gauge("conn", 2)});
  CHECK(render(renderer, {counter("req.get", 10), gauge("conn", -20)}) == "# TYPE req counter\n"
                                                                          "req{k=\"get\"} 10\n"
                                                                          "# TYPE conn gauge\n"
                                                                          "conn -20\n");
  CHECK(namer_calls == 2);
}

TEST_CASE("A new record joins its family", "[prometheus_render]")
{
  PrometheusRenderer renderer{options()};

  namer_calls = 0;
  render(renderer, {counter("req.get", 1), gauge("conn", 3)});
  CHECK(render(renderer, {counter("req.get", 1), gauge("conn", 3), counter("req.post", 2), gauge("fresh", 4)}) ==
        "# HELP req req.get\n"
        "# TYPE req counter\n"
        "req{k=\"get\"} 1\n"
        "req{k=\"post\"} 2\n"
        "# HELP conn conn\n"
        "# TYPE conn gauge\n"
        "conn 3\n"
        "# HELP fresh fresh\n"
        "# TYPE fresh gauge\n"
        "fresh 4\n");
  CHECK(namer_calls == 4);
}

TEST_CASE("A missing record leaves out only its own sample", "[prometheus_render]")
{
  PrometheusRenderer renderer{options(false)};

  namer_calls = 0;
  render(renderer, {counter("req.get", 1), gauge("conn", 2), counter("req.post", 3), gauge("last", 4)});

  // The positions after a missing record shift down.
  CHECK(render(renderer, {counter("req.get", 1), counter("req.post", 3), gauge("last", 4)}) == "# TYPE req counter\n"
                                                                                               "req{k=\"get\"} 1\n"
                                                                                               "req{k=\"post\"} 3\n"
                                                                                               "# TYPE last gauge\n"
                                                                                               "last 4\n");
  CHECK(render(renderer, {counter("req.post", 3), gauge("last", 4)}) == "# TYPE req counter\n"
                                                                        "req{k=\"post\"} 3\n"
                                                                        "# TYPE last gauge\n"
                                                                        "last 4\n");

  // A record that returns keeps its place in the output.
  CHECK(render(renderer, {counter("req.post", 3), gauge("last", 4), counter("req.get", 5), gauge("conn", 6)}) ==
        "# TYPE req counter\n"
        "req{k=\"get\"} 5\n"
        "req{k=\"post\"} 3\n"
        "# TYPE conn gauge\n"
        "conn 6\n"
        "# TYPE last gauge\n"
        "last 4\n");
  CHECK(namer_calls == 4);
}

TEST_CASE("Records are found when they move or their name moves", "[prometheus_render]")
{
  PrometheusRenderer renderer{options(false)};

  namer_calls = 0;
  render(renderer, {counter("req.get", 1), counter("req.post", 2), gauge("conn", 3)});
  REQUIRE(namer_calls == 3);

  SECTION("Swapped positions")
  {
    CHECK(render(renderer, {gauge("conn", 30), counter("req.post", 20), counter("req.get", 10)}) == "# TYPE req counter\n"
                                                                                                    "req{k=\"get\"} 10\n"
                                                                                                    "req{k=\"post\"} 20\n"
                                                                                                    "# TYPE conn gauge\n"
                                                                                                    "conn 30\n");
    CHECK(namer_calls == 3);
  }

  SECTION("The same name at another address")
  {
    std::string const get{"req.get"};

    CHECK(render(renderer, {counter(get.c_str(), 10), counter("req.post", 20), gauge("conn", 30)}) == "# TYPE req counter\n"
                                                                                                      "req{k=\"get\"} 10\n"
                                                                                                      "req{k=\"post\"} 20\n"
                                                                                                      "# TYPE conn gauge\n"
                                                                                                      "conn 30\n");
    CHECK(namer_calls == 3);
  }

  SECTION("The same name with another type")
  {
    CHECK(render(renderer, {counter("req.get", 1), counter("req.post", 2), floating("conn", 2.5)}) == "# TYPE req counter\n"
                                                                                                      "req{k=\"get\"} 1\n"
                                                                                                      "req{k=\"post\"} 2\n"
                                                                                                      "# TYPE conn gauge\n"
                                                                                                      "conn 2.5\n");
    CHECK(namer_calls == 4);
  }
}
