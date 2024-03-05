/*
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

#include <vector>
#include <string>
#include <string_view>

// #include "ts/ts.h"
// #include "ts/apidefs.h"
#include "tsutil/Metrics.h"

#include "cripts/Lulu.hpp"

namespace detail
{

using MetricID = ts::Metrics::IdType;

class BaseMetrics
{
  using self_type = detail::BaseMetrics;

public:
  BaseMetrics()                     = delete;
  BaseMetrics(const self_type &)    = delete;
  void operator=(const self_type &) = delete;
  virtual ~BaseMetrics()            = default;

  BaseMetrics(const Cript::string_view &name) : _name(name) {}

  [[nodiscard]] Cript::string_view
  name() const
  {
    return {_name};
  }

  [[nodiscard]] detail::MetricID
  id() const
  {
    return _id;
  }

  void
  increment(int64_t inc) const
  {
    TSReleaseAssert(_id != ts::Metrics::NOT_FOUND);
    _metric->increment(inc);
  }

  void
  increment() const
  {
    increment(1);
  }

  void
  decrement(int64_t dec) const
  {
    TSReleaseAssert(_id != ts::Metrics::NOT_FOUND);
    _metric->decrement(dec);
  }

  void
  decrement() const
  {
    decrement(1);
  }

  [[nodiscard]] int64_t
  get() const
  {
    TSReleaseAssert(_id != TS_ERROR);
    return _metric->load();
  }

  void
  set(int64_t val)
  {
    TSReleaseAssert(_id != TS_ERROR);
    _metric->store(val);
  }

protected:
  void
  _initialize(detail::MetricID id)
  {
    static auto &instance = ts::Metrics::instance();

    _id     = id;
    _metric = instance.lookup(id);
  }

private:
  ts::Metrics::AtomicType *_metric = nullptr;
  Cript::string _name              = "unknown";
  detail::MetricID _id             = ts::Metrics::NOT_FOUND;
}; // class BaseMetrics

} // namespace detail

namespace Metrics
{
class Counter : public detail::BaseMetrics
{
  using super_type = detail::BaseMetrics;
  using self_type  = Counter;

public:
  Counter(const Cript::string_view &name) : super_type(name) { _initialize(ts::Metrics::Counter::create(name)); }

  // Counters can only increment, so lets produce some nice compile time erorrs too
  void decrement(int64_t) = delete;
  void set(int64_t val)   = delete;

  template <typename T>
  void
  decrement(T)
  {
    static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can only use decrement(), consider Metric::Gauge instead?");
  }

  template <typename T>
  void
  set(T)
  {
    static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can not be set to a value), consider Metric::Gauge instead?");
  }

  static self_type *
  create(const Cript::string_view &name)
  {
    auto *ret = new self_type(name);
    auto id   = ts::Metrics::Counter::create(name);

    ret->_initialize(id);

    return ret;
  }

}; // class Counter

class Gauge : public detail::BaseMetrics
{
  using super_type = detail::BaseMetrics;
  using self_type  = Gauge;

public:
  Gauge(const Cript::string_view &name) : super_type(name) { _initialize(ts::Metrics::Gauge::create(name)); }

  static self_type *
  create(const Cript::string_view &name)
  {
    auto *ret = new self_type(name);
    auto id   = ts::Metrics::Gauge::create(name);

    ret->_initialize(id);

    return ret;
  }

}; // class Counter

} // namespace Metrics

namespace Cript
{
class MetricStorage
{
  using self_type = MetricStorage;

public:
  MetricStorage()                   = delete;
  MetricStorage(const self_type &)  = delete;
  void operator=(const self_type &) = delete;

  MetricStorage(size_t size) : _metrics(size) {}

  ~MetricStorage()
  {
    for (auto &metric : _metrics) {
      delete metric;
    }
  }

  detail::BaseMetrics *&
  operator[](std::size_t index)
  {
    if (_metrics.size() <= index) {
      _metrics.resize(index + 8); // 8 at a time.
    }

    return _metrics[index];
  }

private:
  std::vector<detail::BaseMetrics *> _metrics;

}; // class MetricStorage

} // namespace Cript
