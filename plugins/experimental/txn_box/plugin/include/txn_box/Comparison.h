/**
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

  Copyright 2019, Oath Inc.
*/

#pragma once

#include <memory>
#include <unordered_map>
#include <functional>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"
#include "txn_box/Accelerator.h"
#include "txn_box/yaml_util.h"

/** Base class for comparisons.
 *
 */
class Comparison
{
  using self_type = Comparison;

public:
  /// Handle type for local instances.
  using Handle = std::unique_ptr<self_type>;

  /** Factory functor that creates an instance from a configuration node.
   *
   * @param cfg Configuration.
   * @param cmp_node Comparison node.
   * @param key Key identifying the comparison.
   * @param arg Argument, if any.
   * @param value_node The value node for the @a key.
   */
  using Loader = std::function<swoc::Rv<Handle>(Config &cfg, YAML::Node const &cmp_node, swoc::TextView const &key,
                                                swoc::TextView const &arg, YAML::Node const &value_node)>;

  // Factory that maps from names to assemblers.
  using Factory = std::unordered_map<swoc::TextView, std::tuple<Loader, ActiveType>, std::hash<std::string_view>>;

  virtual ~Comparison() = default;

  /** Number of regular expression capture groups provided by a match.
   *
   * @return The number of capture groups, or 0 if it is not a regular expression.
   *
   * The default implementation returns @c 0, regular expression based comparisons must
   * override to return the appropriate number for the regular expression.
   */
  virtual unsigned rxp_group_count() const;

  /// @defgroup Comparison overloads.
  /// These must match the set of types in @c FeatureTypes.
  /// Subclasses (specific comparisons) should override these as appropriate for its supported types.
  /// Context state updates are done through the @c Context argument.
  /// @{
  virtual bool
  operator()(Context &, std::monostate) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, nil_value) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<STRING> const &) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<INTEGER>) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<BOOLEAN>) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<FLOAT>) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<IP_ADDR> const &) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<DURATION>) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<TIMEPOINT>) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, Cons const *) const
  {
    return false;
  }
  virtual bool
  operator()(Context &, feature_type_for<TUPLE> const &) const
  {
    return false;
  }
  virtual bool operator()(Context &, Generic const *) const;
  /// @}

  /** External comparison entry.
   *
   * @param ctx Runtime context.
   * @param feature Feature to compare.
   * @return @c true if matched, @c false if not.
   *
   * Subclasses should override this method only if they will handle all feature types. If the
   * comparison is limited to a few or a single feature type, it is better to overload the
   * type specific comparisons.
   */
  virtual bool
  operator()(Context &ctx, Feature const &feature) const
  {
    auto visitor = [&](auto &&value) { return (*this)(ctx, value); };
    return std::visit(visitor, feature);
  }

  /** Accelerator candidate marking.
   *
   * @param counts  Array of acceleration counters.
   *
   * If a comparison can be accelerated, it is required to override this method. The implementation
   * must increment the counter(s) corresponding to the accelerators that can be used by this
   * comparison. By default a comparison cannot be accelerated and in that case nothing should be
   * done.
   *
   * If the counter is bumped for specific accelerator, the comparison must also override the
   * corresponding overload of @c accelerate to register itself if the framework decides there
   * are enough acceleratable comparisons to make it useful.
   *
   * @see accelerate
   */
  virtual void can_accelerate(Accelerator::Counters &counters) const;

  /** String acceleration.
   *
   * @param str_accel An accelerator instance.
   *
   * If a comparison supports string acceleration, it must override this method and register with
   * @a str_accel.
   *
   * @note The comparison must also override @c can_accelerate to bump the strint accelerator
   * counter.
   *
   * @see can_accelerate
   */
  virtual void accelerate(StringAccelerator *str_accel) const;

  /** Define a comparison.
   *
   * @param name Name for key node to indicate this comparison.
   * @param types Mask of types that are supported by this comparison.
   * @param worker Assembler to construct instance from configuration node.
   * @return A handle to a constructed instance on success, errors on failure.
   */
  static swoc::Errata define(swoc::TextView name, ActiveType const &types, Loader &&worker);

  /** Load a comparison from a YAML @a node.
   *
   * @param cfg Configuration object.
   * @param node Node with comparison config.
   * @return A constructed instance or errors on failure.
   */
  static swoc::Rv<Handle> load(Config &cfg, YAML::Node node);

protected:
  /// The assemblers.
  static Factory _factory;
};

class ComparisonGroupBase
{
  using self_type = ComparisonGroupBase;
  using Errata    = swoc::Errata;

public:
  virtual ~ComparisonGroupBase() = default;
  virtual Errata load(Config &cfg, YAML::Node node);

protected:
  virtual Errata               load_case(Config &cfg, YAML::Node node) = 0;
  swoc::Rv<Comparison::Handle> load_cmp(Config &cfg, YAML::Node node);
};

/** Container for an ordered list of Comparisons.
 *
 * @tparam W Wrapper class for comparisons.
 *
 * It is assumed additional information needs to be associated with each @c Comparison and
 * therefore each @c Comparison will be stored in a wrapper class @a W which holds the
 * ancillary data.
 */
template <typename W> class ComparisonGroup : protected ComparisonGroupBase
{
  using self_type  = ComparisonGroup;
  using super_type = ComparisonGroupBase;

  using container = std::vector<W>;

  using Errata = swoc::Errata;

public:
  using value_type = W; ///< Export template parameter.

  using iterator       = typename container::iterator;
  using const_iterator = typename container::const_iterator;

  ComparisonGroup() = default;

  /** Load the group from the value in @a node.
   *
   * @param cfg Configuration context.
   * @param node The value for the comparisons.
   * @return Errors, if any.
   *
   * @a node can be an object, in which case it is treated as a list of length 1 containing that
   * object. Otherwise @a node must be a list of objects.
   */
  Errata load(Config &cfg, YAML::Node node) override;

  /** Invoke the comparisons.
   *
   * @param ctx Transaction context.
   * @param feature Active feature to compare.
   * @return The iterator for the successful comparison, or the end iterator if none succeeded.
   */
  iterator operator()(Context &ctx, Feature const &feature);

  /// @return The location of the first comparison.
  iterator
  begin()
  {
    return _cmps.begin();
  }
  /// @return Location past the last comparison.
  iterator
  end()
  {
    return _cmps.end();
  }
  const_iterator
  begin() const
  {
    return _cmps.begin();
  }
  const_iterator
  end() const
  {
    return _cmps.end();
  }

protected:
  /// The comparisons.
  std::vector<W> _cmps;

  /** Load comparison case.
   *
   * @param cfg Configuration context.
   * @param node Value node containing the case.
   * @return Errors, if any.
   */
  Errata load_case(Config &cfg, YAML::Node node) override;
};

template <typename W>
auto
ComparisonGroup<W>::load(Config &cfg, YAML::Node node) -> Errata
{
  if (node.IsSequence()) {
    _cmps.reserve(node.size());
  }
  return this->super_type::load(cfg, node);
}

template <typename W>
auto
ComparisonGroup<W>::load_case(Config &cfg, YAML::Node node) -> Errata
{
  W w;
  if (auto errata = w.pre_load(cfg, node); !errata.is_ok()) {
    return errata;
  }

  // It is permitted to have an empty comparison, which always matches and is marked by a
  // nil handle.
  if (node.size() >= 1) {
    auto &&[handle, errata] = this->load_cmp(cfg, node);
    if (!errata.is_ok()) {
      return std::move(errata);
    }
    w.assign(std::move(handle));
  }

  _cmps.emplace_back(std::move(w));
  return {};
}

template <typename W>
auto
ComparisonGroup<W>::operator()(Context &ctx, Feature const &feature) -> iterator
{
  for (auto spot = _cmps.begin(), limit = _cmps.end(); spot != limit; ++spot) {
    if ((*spot)(ctx, feature)) {
      return spot;
    }
  }
  return _cmps.end();
}
