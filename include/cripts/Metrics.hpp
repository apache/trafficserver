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

#include <ts/ts.h>
#include <ts/apidefs.h>

#include <cripts/Lulu.hpp>

namespace detail
{

using MetricID = int;

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
    TSReleaseAssert(_id != TS_ERROR);
    TSStatIntIncrement(_id, inc);
  }

  void
  increment() const
  {
    increment(1);
  }

  void
  decrement(int64_t dec) const
  {
    TSReleaseAssert(_id != TS_ERROR);
    TSStatIntIncrement(_id, -dec);
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
    return TSStatIntGet(_id);
  }

  void
  set(int64_t val)
  {
    TSReleaseAssert(_id != TS_ERROR);
    TSStatIntSet(_id, val);
  }

  [[nodiscard]] virtual TSStatSync sync() const = 0;

protected:
  bool initialize();

private:
  Cript::string _name  = "unknown";
  detail::MetricID _id = TS_ERROR;
}; // class BaseMetrics

} // namespace detail

namespace Metrics
{
class Counter : public detail::BaseMetrics
{
  using super_type = detail::BaseMetrics;
  using self_type  = Counter;

public:
  Counter(const Cript::string_view &name) : super_type(name) { super_type::initialize(); }

  using super_type::increment;
  using super_type::decrement;
  // Counters can only increment /decrement by 1
  void increment(int64_t) = delete;
  void decrement(int64_t) = delete;

  template <typename T>
  void
  increment(T)
  {
    static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can only use increment(), consider Metric::Sum instead?");
  }

  template <typename T>
  void
  decrement(T)
  {
    static_assert(std::is_same_v<T, int64_t>, "A Metric::Counter can only use decrement(), consider Metric::Sum instead?");
  }

  [[nodiscard]] TSStatSync
  sync() const override
  {
    return TSStatSync::TS_STAT_SYNC_COUNT;
  }

  static self_type *
  create(const Cript::string_view &name)
  {
    auto *ret = new self_type(name);

    TSReleaseAssert(ret->id() != TS_ERROR);

    return ret;
  }

}; // class Counter

class Sum : public detail::BaseMetrics
{
  using super_type = detail::BaseMetrics;
  using self_type  = Sum;

public:
  Sum(const Cript::string_view &name) : super_type(name) { super_type::initialize(); }

  [[nodiscard]] TSStatSync
  sync() const override
  {
    return TSStatSync::TS_STAT_SYNC_SUM;
  }

  static self_type *
  create(const Cript::string_view &name)
  {
    auto *ret = new self_type(name);

    TSReleaseAssert(ret->id() != TS_ERROR);

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
