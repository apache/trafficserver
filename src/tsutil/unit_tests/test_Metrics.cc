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

#include <algorithm>
#include <array>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

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

TEST_CASE("Metrics hidden store", "[libtsapi][Metrics]")
{
  auto &m = Metrics::instance();
  auto &h = Metrics::hidden_instance();

  SECTION("stores are separate")
  {
    REQUIRE(std::addressof(m) != std::addressof(h));

    auto hp = Metrics::Counter::createHiddenPtr("hidden.only");
    REQUIRE(hp != nullptr);

    // Not visible in the published store, by name or by iteration.
    REQUIRE(m.lookup("hidden.only") == Metrics::NOT_FOUND);
    for (auto &&[name, type, value] : m) {
      REQUIRE(name != "hidden.only");
    }

    // Visible in the hidden store.
    REQUIRE(h.lookup("hidden.only") != Metrics::NOT_FOUND);
    bool found = false;
    for (auto &&[name, type, value] : h) {
      if (name == "hidden.only") {
        found = true;
      }
    }
    REQUIRE(found);
  }

  SECTION("same name in both stores is independent")
  {
    auto pub = Metrics::Counter::createPtr("dual.name");
    auto hid = Metrics::Counter::createHiddenPtr("dual.name");
    REQUIRE(pub != hid);

    // Exercised through the typed facade, which is what proves no cast is needed at a call site
    // holding a Counter::AtomicType *.
    Metrics::Counter::increment(pub, 3);
    Metrics::Counter::increment(hid, 7);
    REQUIRE(Metrics::Counter::load(pub) == 3);
    REQUIRE(Metrics::Counter::load(hid) == 7);
  }

  SECTION("createHiddenPtr is idempotent by name")
  {
    auto a = Metrics::Counter::createHiddenPtr("hidden.idem");
    auto b = Metrics::Counter::createHiddenPtr("hidden.idem");
    REQUIRE(a == b);
  }

  SECTION("prefixed create")
  {
    auto p = Metrics::Counter::createHiddenPtr("pfx.", "suffix");
    REQUIRE(p != nullptr);
    REQUIRE(h.lookup("pfx.suffix") != Metrics::NOT_FOUND);
  }

  SECTION("the metric type is recorded in the hidden store")
  {
    Metrics::IdType cid{}, gid{};

    REQUIRE(Metrics::Counter::createHiddenPtr("hidden.typed.counter") != nullptr);
    REQUIRE(Metrics::Gauge::createHiddenPtr("hidden.typed.gauge") != nullptr);

    cid = h.lookup("hidden.typed.counter");
    gid = h.lookup("hidden.typed.gauge");
    REQUIRE(cid != Metrics::NOT_FOUND);
    REQUIRE(gid != Metrics::NOT_FOUND);

    REQUIRE(h.type(cid) == Metrics::MetricType::COUNTER);
    REQUIRE(h.type(gid) == Metrics::MetricType::GAUGE);
  }

  SECTION("hidden gauge works with the typed Gauge API")
  {
    auto g = Metrics::Gauge::createHiddenPtr("hidden.gauge.", "one");
    REQUIRE(g != nullptr);
    Metrics::Gauge::store(g, 42);
    REQUIRE(Metrics::Gauge::load(g) == 42);
    Metrics::Gauge::increment(g);
    REQUIRE(Metrics::Gauge::load(g) == 43);
    Metrics::Gauge::decrement(g);
    REQUIRE(Metrics::Gauge::load(g) == 42);
  }

  SECTION("hidden metrics are shared across threads")
  {
    // Metrics is thread_local but Storage is shared, so the same name must resolve to the same
    // atomic on every thread. This is how per-group counters are created from many event threads.
    constexpr int                                         N_THREADS = 4;
    std::vector<std::thread>                              threads;
    std::array<Metrics::Counter::AtomicType *, N_THREADS> ptrs{};

    for (int i = 0; i < N_THREADS; ++i) {
      threads.emplace_back([i, &ptrs]() {
        auto p  = Metrics::Counter::createHiddenPtr("hidden.threaded");
        ptrs[i] = p;
        Metrics::Counter::increment(p, 10);
      });
    }
    for (auto &t : threads) {
      t.join();
    }

    for (int i = 1; i < N_THREADS; ++i) {
      REQUIRE(ptrs[i] == ptrs[0]);
    }
    REQUIRE(Metrics::Counter::load(ptrs[0]) == N_THREADS * 10);
  }
}

TEST_CASE("Metrics blob growth boundary", "[libtsapi][Metrics]")
{
  // Storage packs metrics into fixed-size blobs (MAX_SIZE entries each). Creating more than
  // MAX_SIZE metrics forces at least one new blob to be allocated, which is exactly where an
  // off-by-one in the blob/offset bookkeeping would corrupt or orphan entries. Use the hidden
  // store so this doesn't dump thousands of names into the published store that other test cases
  // iterate over.
  auto                                       &h     = Metrics::hidden_instance();
  constexpr int                               COUNT = Metrics::MAX_SIZE + 100;
  std::vector<Metrics::Counter::AtomicType *> ptrs;
  std::vector<std::string>                    names;

  ptrs.reserve(COUNT);
  names.reserve(COUNT);

  for (int i = 0; i < COUNT; ++i) {
    names.push_back("blob.growth." + std::to_string(i));
    auto p = Metrics::Counter::createHiddenPtr(names[i]);
    REQUIRE(p != nullptr);
    ptrs.push_back(p);
    Metrics::Counter::increment(p, i);
  }

  for (int i = 0; i < COUNT; ++i) {
    auto id = h.lookup(names[i]);
    REQUIRE(id != Metrics::NOT_FOUND);
    REQUIRE(h.valid(id));

    // Re-creating by name must be idempotent and resolve to the exact same atomic: a blob
    // boundary bug that aliases two entries onto the same slot, or orphans one behind the
    // boundary, would fail this.
    auto p2 = Metrics::Counter::createHiddenPtr(names[i]);
    REQUIRE(p2 == ptrs[i]);

    // Distinct values catch aliasing: if two logically distinct entries were mapped to the same
    // underlying atomic, this readback would not match the index written above.
    REQUIRE(Metrics::Counter::load(ptrs[i]) == i);
  }

  // Every pointer must be distinct: no two names should have been aliased onto the same atomic.
  std::vector<Metrics::Counter::AtomicType *> sorted_ptrs = ptrs;
  std::sort(sorted_ptrs.begin(), sorted_ptrs.end());
  REQUIRE(std::adjacent_find(sorted_ptrs.begin(), sorted_ptrs.end()) == sorted_ptrs.end());
}
