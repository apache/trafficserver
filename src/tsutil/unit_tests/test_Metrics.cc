/** @file

    TextView unit tests.

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

#include <catch2/catch_test_macros.hpp>

#include <iterator>

#include "tsutil/Metrics.h"
using ts::Metrics;

TEST_CASE("Metrics", "[libtsapi][Metrics]")
{
  auto &m = Metrics::instance();

  SECTION("iterator")
  {
    auto [name, type, value] = *m.begin();
    REQUIRE(value == 0);
    REQUIRE(type == Metrics::MetricType::COUNTER);
    REQUIRE(name == "proxy.process.api.metrics.bad_id");

    REQUIRE(m.begin() != m.end());

    // Other test cases share this process-wide store, so the number of metrics already present
    // is not knowable here. Assert the delta from creating one metric instead of an absolute
    // iterator position.
    auto pre_count = std::distance(m.begin(), m.end());

    Metrics::Counter::create("iterator.marker");
    REQUIRE(std::distance(m.begin(), m.end()) == pre_count + 1);

    auto it = m.begin();
    std::advance(it, pre_count);
    REQUIRE(it != m.end());
    ++it;
    REQUIRE(it == m.end());

    auto it2 = m.begin();
    std::advance(it2, pre_count);
    it2++;
    REQUIRE(it2 == m.end());
  }

  SECTION("New metric")
  {
    auto fooid = Metrics::Counter::create("foo");

    // Not an absolute id: that depends on how many metrics other test cases created first.
    // Assert the id is valid and round-trips through lookup.
    REQUIRE(fooid != ts::Metrics::NOT_FOUND);
    REQUIRE(m.lookup("foo") == fooid);
    REQUIRE(m.name(fooid) == "foo");
    REQUIRE(m.type(fooid) == Metrics::MetricType::COUNTER);

    REQUIRE(m[fooid].load() == 0);
    m.increment(fooid);
    REQUIRE(m[fooid].load() == 1);
  }

  SECTION("operator[] & store")
  {
    auto storeid = Metrics::Gauge::create("store");
    REQUIRE(m.type(storeid) == Metrics::MetricType::GAUGE);

    m[storeid].store(42);

    REQUIRE(m[storeid].load() == 42);
  }

  SECTION("Span allocation")
  {
    ts::Metrics::IdType span_id;
    auto                fooid = m.lookup("foo");
    auto                span  = Metrics::Counter::createSpan(17, &span_id);

    REQUIRE(span.size() == 17);
    // Not fixed offsets: those only hold against a virgin store. Assert instead that the span
    // was allocated above the earlier metric and that every id in it is valid. Both ids are
    // counters, so they are directly comparable -- ids encode the metric type, and so are not
    // ordered across differing types.
    REQUIRE(fooid != ts::Metrics::NOT_FOUND);
    REQUIRE(span_id != ts::Metrics::NOT_FOUND);
    REQUIRE(span_id > fooid);
    for (size_t i = 0; i < span.size(); ++i) {
      REQUIRE(m.valid(span_id + static_cast<ts::Metrics::IdType>(i)));
    }

    m.rename(span_id + 0, "span.0");
    m.rename(span_id + 1, "span.1");
    m.rename(span_id + 2, "span.2");
    REQUIRE(m.name(fooid) == "foo");
    REQUIRE(m.name(span_id + 0) == "span.0");
    REQUIRE(m.name(span_id + 1) == "span.1");
    REQUIRE(m.name(span_id + 2) == "span.2");
    m.rename(fooid, "foo-new");
    REQUIRE(m.name(fooid) == "foo-new");
    REQUIRE(m.lookup("foo") == ts::Metrics::NOT_FOUND);
    REQUIRE(m.lookup("foo-new") == fooid);
  }

  SECTION("lookup")
  {
    auto nm = m.lookup("notametric");
    REQUIRE(nm == ts::Metrics::NOT_FOUND);

    auto mid  = Metrics::Counter::create("ametric");
    auto fmid = m.lookup("ametric");

    REQUIRE(mid == fmid);

    std::string_view    name{};
    Metrics::MetricType type{};
    m.lookup(mid, &name, &type);

    REQUIRE(name == "ametric");
    REQUIRE(type == Metrics::MetricType::COUNTER);
  }

  SECTION("derived")
  {
    auto a = Metrics::Counter::createPtr("m-a");
    auto b = Metrics::Counter::createPtr("m-b");
    auto c = Metrics::Counter::createPtr("m-c");
    auto d = Metrics::Counter::createPtr("m-d");
    auto e = Metrics::Counter::createPtr("m-e");
    ts::Metrics::Derived::derive({
      {"derived-a-c", Metrics::MetricType::COUNTER, {a, b, c}               }, // test using ptr
      {"derived-cd",  Metrics::MetricType::COUNTER, {m.lookup("m-c"), "m-d"}}, // using IdType and string
      {"derived-ce",  Metrics::MetricType::COUNTER, {"derived-cd", "m-e"}   }  // using another derived
    });

    auto derived   = m.lookup("derived-a-c");
    auto derivedcd = m.lookup("derived-cd");
    auto derivedce = m.lookup("derived-ce");
    REQUIRE(derived != ts::Metrics::NOT_FOUND);
    REQUIRE(m.type(derived) == Metrics::MetricType::COUNTER);

    REQUIRE(m[derived].load() == 0);

    a->increment(1);
    b->increment(1);
    b->increment(1);
    c->increment(1);
    d->increment(4);
    e->increment(5);

    ts::Metrics::Derived::update_derived();

    REQUIRE(m[derived].load() == 4);
    REQUIRE(m[derivedcd].load() == 5);
    REQUIRE(m[derivedce].load() == 10);
  }
}
