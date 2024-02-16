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

/** Feature modification / transformation.
 *
 */
class Modifier
{
  using self_type = Modifier; ///< Self reference type.

public:
  /// Handle for instances.
  using Handle = std::unique_ptr<self_type>;

  /** Function to create an instance from YAML configuration.
   * @param cfg The configuration state object.
   * @param mod_node The YAML node for the feature modifier.
   * @param key The name of @a mod_node, the key name matched to select this modifier.
   * @param arg The argument in the key, if any.
   * @param key_value The YAML node in @a mod_node that identified the modifier.
   * @return A handle for the new instance, or errors if any.
   */
  using Worker =
    std::function<swoc::Rv<Handle>(Config &cfg, YAML::Node mod_node, swoc::TextView key, swoc::TextView arg, YAML::Node key_value)>;

  virtual ~Modifier() = default;

  /** Modification operator.
   *
   * @param ctx Runtime transaction context.
   * @param feature Feature to modify.
   * @return Modified feature, or errors.
   *
   * The input feature is never modified - the modified feature is returned. If there is no
   * modification it is acceptable to return the input feature.
   */
  virtual swoc::Rv<Feature>
  operator()(Context &ctx, Feature &feature)
  {
    auto visitor = [&](auto &&value) { return (*this)(ctx, value); };
    return std::visit(visitor, feature);
  }

  /// Feature type specific overloads as a convenience, so modifiers that support few types
  /// can overload these instead of the general case.
  /// @{
  // Do-nothing base implementations - subclasses should override methods for supported types.
  virtual swoc::Rv<Feature> operator()(Context &ctx, feature_type_for<NIL>);
  virtual swoc::Rv<Feature> operator()(Context &ctx, feature_type_for<STRING> feature);
  virtual swoc::Rv<Feature> operator()(Context &ctx, feature_type_for<IP_ADDR> feature);
  /// @}

  // At least prevent compilation if a non-feature type is used.
  template <typename T>
  auto
  operator()(Context &, T const &) -> EnableForFeatureTypes<T, swoc::Rv<Feature>>
  {
    return {};
  }

  /** Check if the comparison is valid for @a type.
   *
   * @param type Type of feature to compare.
   * @return @c true if this comparison can compare to that feature type, @c false otherwise.
   */
  virtual bool is_valid_for(ActiveType const &type) const = 0;

  /** Output type of the modifier.
   *
   * @param in The input type for the modifier.
   * @return The type of the modified feature.
   */
  virtual ActiveType result_type(ActiveType const &ex_type) const = 0;

  /** Define a mod for @a name.
   *
   * @param name Name of the mode.
   * @param f Instance constructor.
   * @return Errors, if any.
   *
   */
  static swoc::Errata define(swoc::TextView name, Worker const &f);

  /** Define a standard layout modifier.
   *
   * @tparam M The Modifier class.
   * @return Errors, if any.
   *
   * This is used when the modifier class layout is completely standard. The template picks out those pieces and passes them to the
   * argument based @c define.
   */
  template <typename M>
  static swoc::Errata
  define()
  {
    return self_type::define(M::KEY, &M::load);
  }

  /** Load an instance from YAML.
   *
   * @param cfg Config state object.
   * @param node Node containing the modifier.
   * @param ex_type Feature type to modify.
   * @return
   */
  static swoc::Rv<Handle> load(Config &cfg, YAML::Node const &node, ActiveType ex_type);

protected:
  /// Set of defined modifiers.
  using Factory = std::unordered_map<swoc::TextView, Worker, std::hash<std::string_view>>;
  /// Storage for set of modifiers.
  static Factory _factory;
};

// ---
/// Base class for various filter modifiers.
class FilterMod : public Modifier
{
  using self_type  = FilterMod;
  using super_type = Modifier;

public:
  static inline const std::string ACTION_REPLACE = "replace"; ///< Replace element.
  static inline const std::string ACTION_DROP    = "drop";    ///< Drop / remove element.
  static inline const std::string ACTION_PASS    = "pass";    ///< Pass unaltered.
  static inline const std::string ACTION_OPT     = "option";  ///< Options
protected:
  /// Action to take for an element.
  enum Action {
    PASS = 0, ///< No action
    DROP,     ///< Remove element from result.
    REPLACE   ///< Replace element in result.
  };
};
