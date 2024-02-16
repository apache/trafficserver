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

#include "txn_box/common.h"
#include "txn_box/Expr.h"

/* ---------------------------------------------------------------------------------------------- */
/** Handle a group of features that can cross reference.
 * Support a map or list of features as a group.
 */
class FeatureGroup
{
  using self_type = FeatureGroup; ///< Self reference type.
public:
  /// Index type for the various indices.
  using index_type = unsigned short;
  /// Value to mark uninitialized / invalid index.
  static constexpr index_type INVALID_IDX = std::numeric_limits<index_type>::max();

  /// Initialization flags.
  enum Flag : int8_t {
    NONE     = -1, ///< No flags
    REQUIRED = 0,  ///< Key must exist and have a valid expression.
  };

  /// Description of a key with a feature to extract.
  struct Descriptor {
    using self_type = Descriptor; ///< Self reference type.
    swoc::TextView _name;         ///< Key name
    std::bitset<2> _flags;        ///< Flags.

    // Convenience constructors.
    /// Construct with only a name, no flags.
    Descriptor(swoc::TextView const &name);
    /// Construct with a name and a single flag.
    Descriptor(swoc::TextView const &name, Flag flag);
    ;
    /// Construct with a name and a list of flags.
    Descriptor(swoc::TextView const &name, std::initializer_list<Flag> const &flags);
  };

  /// Information about an expression.
  /// This is per configuration data.
  struct ExprInfo {
    Expr _expr;           ///< The feature expression.
    swoc::TextView _name; ///< Key name.
    /// Extracted feature index. For each referenced key, a slot is allocated for caching the
    /// extracted feature. @c INVALID_IDX indicates the feature isn't a dependency target
    /// and is therefore not cached.
    index_type _exf_idx = INVALID_IDX;
    bool _dependent_p   = false; ///< Is expression dependent on another key in the group?
  };

  /** Store invocation state for extracting features.
   *
   * Features which are dependency targets are stored here per transaction / context. The goal is
   * to
   * - prevent extracting the feature multiple times
   * - avoid nested extractions, so that when any feature in the group is extracted any
   *   dependencies have already been extracted and are available as literals.
   */
  struct State {
    swoc::MemSpan<Feature> _features; ///< Cached features from expression evaluation.
  };

  ~FeatureGroup();

  /** Load the feature expressions from @a node.
   *
   * @param cfg Configuration context.
   * @param node Map with keys that have expressions.
   * @param ex_keys Keys expected to have expressions.
   * @return Errors, if any.
   *
   * The @a ex_keys are loaded and if those refer to other keys, those other keys are transitively
   * loaded. The loading order is a linear ordering of the dependencies between keys. A circular
   * dependency is an error and reported.
   *
   * @internal The restriction on dependencies on multi-valued keys is a performance issue. I
   * currently do not know how to support that and allow lazy extraction for multi-valued keys. It's
   * not even clear what that would mean in practice - is the entire multi-value extracted? Obscure
   * enough I'll leave that for when a use case becomes known.
   */
  swoc::Errata load(Config &cfg, YAML::Node const &node, std::initializer_list<Descriptor> const &ex_keys);

  /** Load the expressions from @a node
   *
   * @param cfg Configuration context.
   * @param node Node - must be a scalar or sequence.
   * @param ex_keys Elements to extract
   * @return Errors, if any.
   *
   * @a node is required to be a sequence, or a scalar which is treated as a sequence of length 1.
   * The formats are extracted in order. If any format is @c REQUIRED then all preceding ones are
   * also required, even if not marked as such.
   */
  swoc::Errata load_as_tuple(Config &cfg, YAML::Node const &node, std::initializer_list<Descriptor> const &ex_keys);

  /** Load the expression from a scalar @a value.
   *
   * @param cfg Configuration instance.
   * @param value Scalar value to load.
   * @param name Name of the key to load.
   * @return Errors, if any.
   */
  swoc::Errata load_as_scalar(Config &cfg, YAML::Node const &value, swoc::TextView const &name);

  /** Get the index of extraction information for @a name.
   *
   * @param name Name of the key.
   * @return The index for the extraction info for @a name or @c INVALID_IDX if not found.
   */
  index_type index_of(swoc::TextView const &name);

  /** Get the extraction information for @a idx.
   *
   * @param idx Key index.
   * @return The extraction information.
   */
  ExprInfo &operator[](index_type idx);

  /** Extract the feature.
   *
   * @param ctx Transaction context.
   * @param state Group state for extraction.
   * @param name Name of the feature key.
   * @return The extracted data.
   */
  Feature extract(Context &ctx, swoc::TextView const &name);

  /** Extract the feature.
   *
   * @param ctx Transaction context.
   * @param state Group state for extraction.
   * @param idx Index of the feature key.
   * @return The extracted data.
   */
  Feature extract(Context &ctx, index_type idx);

protected:
  static constexpr uint8_t DONE    = 1; ///< All dependencies computed.
  static constexpr uint8_t IN_PLAY = 2; ///< Dependencies currently being computed.

  /** Wrapper for tracking array.
   * This wraps a stack allocated variable sized array, which is otherwise inconvenient to use.
   * @note It is assumed the total number of keys is small enough that linear searching is overall
   * faster compared to better search structures that require memory allocation.
   *
   * Essentially this serves as yet another context object, which tracks the reference context
   * as the dependency chains are traced during format loading, to avoid methods with massive
   * and identical parameter lists.
   *
   * This is a temporary data structure used only during configuration load. The data that needs
   * to be persisted is copied to member variables at the end of parsing when all the sizes and
   * info are known.
   *
   * @note - This is a specialized internal class and much care should be used by any sublcass
   * touching it.
   *
   * @internal Move @c Config in here as well?
   */
  struct Tracking {
    /// Per tracked item information.
    struct Info : public ExprInfo {
      int8_t _mark = NONE; ///< Ordering search march.

      /// This is not part of the base data, but is really a parallel array that contains a
      /// linear ordering of the target dependencies. Only keys that are targets are in this
      /// array, and the values are the index in the overall @a info array.
      index_type _order_idx = INVALID_IDX;
    };

    /// External provided array used to track the keys.
    /// Generally stack allocated, it should be the number of keys in the node as this is an
    /// upper bound of the amount of elements needed.
    swoc::MemSpan<Info> _info;

    /// The number of valid elements in the @a _info array.
    index_type _count = 0;

    YAML::Node const &_node; ///< Node containing the keys.

    /** Construct a wrapper on a tracking array.
     *
     * @param node Node containing keys.
     * @param info Array.
     * @param n # of elements in @a info.
     */
    Tracking(YAML::Node const &node, Info *info, unsigned n) : _info(info, n), _node(node) {}

    /// Allocate an entry and return a pointer to it.
    Info *
    alloc()
    {
      return &_info[_count++];
    }

    /// Find the array element with @a name.
    /// @return A pointer to the element, or @c nullptr if not found.
    Info *find(swoc::TextView const &name) const;

    /// Obtain an array element for @a name.
    /// @return A pointer to the element.
    /// If @a name is not in the array, an element is allocated and set to @a name.
    Info *obtain(swoc::TextView const &name);
  };

  index_type _ref_count = 0; ///< Number of edge targets.

  swoc::MemSpan<ExprInfo> _expr_info;  ///< Info for the key expression by key.
  swoc::MemSpan<index_type> _ordering; ///< Extraction ordering for dependency targets.

  /// Context storage to store a @c State instance across feature extraction.
  ReservedSpan _ctx_state_span;

  /// Extractor specialized for this feature group.
  Ex_this _ex_this{*this};

  /** Load an extractor format.
   *
   * @param cfg Configuration state.
   * @param tracking Overall tracking info.
   * @param info Tracking info for @a node.
   * @param node Node that has the expression as a value.
   * @return Errors, if any.
   */
  swoc::Errata load_expr(Config &cfg, Tracking &tracking, Tracking::Info *info, YAML::Node const &node);

  /** Load the format at key @a name from the tracking node.
   *
   * @param cfg Configuration state.
   * @param tracking Tracking info.
   * @param name Key name.
   * @return The tracking info entry for the key.
   *
   * The base node is contained in @a tracking. The key for @a name is selected and the
   * format there loaded.
   */
  swoc::Rv<Tracking::Info *> load_key(Config &cfg, Tracking &tracking, swoc::TextView name);
};

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name) : _name(name) {}

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name, FeatureGroup::Flag flag) : _name(name)
{
  if (flag != NONE) {
    _flags[flag] = true;
  }
}

inline FeatureGroup::Descriptor::Descriptor(swoc::TextView const &name, std::initializer_list<FeatureGroup::Flag> const &flags)
  : _name(name)
{
  for (auto f : flags) {
    if (f != NONE) {
      _flags[f] = true;
    }
  }
}

inline FeatureGroup::ExprInfo &
FeatureGroup::operator[](FeatureGroup::index_type idx)
{
  return _expr_info[idx];
}
/* ---------------------------------------------------------------------------------------------- */
