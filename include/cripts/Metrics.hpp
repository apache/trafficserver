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

  BaseMetrics(const cripts::string_view &name) : _name(name) {}

  void
  operator=(int64_t val)
  {
    CAssert(_id != TS_ERROR);
    _metric->store(val);
  }

  operator int64_t() const
  {
    CAssert(_id != TS_ERROR);
    return _metric->load();
  }

  [[nodiscard]] cripts::string_view
  Name() const
  {
    return {_name};
  }

  [[nodiscard]] detail::MetricID
  Id() const
  {
    return _id;
  }

  void
  Increment(int64_t inc) const
  {
    CAssert(_id != ts::Metrics::NOT_FOUND);
    _metric->increment(inc);
  }

  void
  Increment() const
  {
    Increment(1);
  }

  void
  Decrement(int64_t dec) const
  {
    CAssert(_id != ts::Metrics::NOT_FOUND);
    _metric->decrement(dec);
  }

  void
  Decrement() const
  {
    Decrement(1);
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
  cripts::string           _name   = "unknown";
  detail::MetricID         _id     = ts::Metrics::NOT_FOUND;
}; // class BaseMetrics

} // namespace detail

namespace cripts
{

namespace Metrics
{
  class Counter : public detail::BaseMetrics
  {
    using super_type = detail::BaseMetrics;
    using self_type  = Counter;

  public:
    Counter(const cripts::string_view &name) : super_type(name) { _initialize(ts::Metrics::Counter::create(name)); }

    // Counters can only increment, so lets produce some nice compile time erorrs too
    void Decrement(int64_t)  = delete;
    void Setter(int64_t val) = delete;

    template <typename T>
    void
    Decrement(T)
    {
      static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can only use Increment(), consider Metric::Gauge instead?");
    }

    template <typename T>
    void
    Setter(T)
    {
      static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can not be set to a value), consider Metric::Gauge instead?");
    }

    static self_type *
    Create(const cripts::string_view &name)
    {
      auto *ret = new self_type(name);
      auto  id  = ts::Metrics::Counter::create(name);

      ret->_initialize(id);

      return ret;
    }

  }; // class Counter

  class Gauge : public detail::BaseMetrics
  {
    using super_type = detail::BaseMetrics;
    using self_type  = Gauge;

  public:
    Gauge(const cripts::string_view &name) : super_type(name) { _initialize(ts::Metrics::Gauge::create(name)); }

    static self_type *
    Create(const cripts::string_view &name)
    {
      auto *ret = new self_type(name);
      auto  id  = ts::Metrics::Gauge::create(name);

      ret->_initialize(id);

      return ret;
    }

  }; // class Counter

} // namespace Metrics

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

} // namespace cripts
