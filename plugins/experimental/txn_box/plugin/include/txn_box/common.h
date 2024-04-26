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

  Copyright 2019, Oath Inc
*/

#pragma once

#include <tuple>
#include <variant>
#include <chrono>
#include <functional>

#include <swoc/swoc_meta.h>
#include <swoc/TextView.h>
#include <swoc/MemSpan.h>
#include <swoc/Errata.h>
#include <swoc/swoc_ip.h>
#include <swoc/Lexicon.h>
#include <swoc/bwf_fwd.h>

constexpr swoc::Errata::Severity S_DEBUG{1};
constexpr swoc::Errata::Severity S_INFO{2};
constexpr swoc::Errata::Severity S_WARN{3};
constexpr swoc::Errata::Severity S_ERROR{4};

// Forward declares
class Config;
class Context;

namespace YAML
{
class Node;
}

/// Separate a name and argument for a directive or extractor.
extern swoc::Rv<swoc::TextView> parse_arg(swoc::TextView &key);

/** Data for a feature that is a full / string.
 *
 * This is a @c TextView with a couple of extra flags to indicate the semantic location of the
 * string memory. If neither flag is set, the string data should be presumed to exist in transient
 * transaction memory and is therefore subject to overwriting.
 *
 * This is used by the @c Context to represent string features in order to minimize string
 * copying while providing safe access to (possibly) transient data. This enables copy on use
 * rather than always copying.
 */
class FeatureView : public swoc::TextView
{
  using self_type  = FeatureView;
  using super_type = swoc::TextView;

public:
  bool _direct_p  = false; ///< String is in externally controlled memory.
  bool _literal_p = false; ///< String is in transaction static memory.
  bool _cstr_p    = false; ///< There is a null char immediately after the view.

  using super_type::super_type; ///< Import constructors.
  using super_type::operator=;  ///< Import assignment.

  /** Return a literal feature view.
   *
   * @param view Text of the literal.
   * @return A @c FeatureView marked as a literal.
   */
  static self_type Literal(TextView const &view);

  /** Create a direct feature view.
   *
   * @param view Base view.
   * @return A @c FeatureView for @a view that is direct.
   */
  static self_type Direct(TextView const &view);
};

/// YAML tag for literal (no feature extraction).
static constexpr swoc::TextView LITERAL_TAG{"!literal"};
/// YAML tag for duration.
static constexpr swoc::TextView DURATION_TAG{"!duration"};

// Self referential types, forward declared.
struct Cons;
struct Feature;

/// Compact tuple representation, via a @c Memspan.
/// Tuples have a fixed size.
using FeatureTuple = swoc::MemSpan<Feature>;

/** Generic data.
 * Two uses:
 * - Very specialized types that are not general enough to warrant a top level feature type.
 * - Extension types such that non-framework code can have its own feature (sub) type.
 * This should be subclassed.
 */
class Generic
{
public:
  swoc::TextView _tag; ///< Sub type identifier.

  Generic(swoc::TextView const &tag) : _tag(tag) {}
  virtual ~Generic() {}
  virtual swoc::TextView
  description() const
  {
    return _tag;
  }

  /** Extract a non-Generic feature from @a this.
   *
   * @return The equivalent non-Generic feature, or @c NIL_FEATURE if there is no conversion.
   *
   * @note The base implementation returns @c NIL_FEATURE therefore unless conversion is supported,
   * this does not need to be overridden.
   */
  virtual Feature extract() const;

  virtual bool
  is_nil() const
  {
    return false;
  }
};

/// Enumeration of types of values, e.g. those returned by a feature string or extractor.
/// This includes all of the types of features, plus some "meta" types to describe situations
/// in which the type may not be known at configuration load time.
/// @note Changes here must be synchronized with changes in
/// - @c FeatureTypelist
/// - @c FeatureIndexToValue
enum ValueType : int8_t {
  NO_VALUE = 0, ///< No value, uninitialized
  NIL,          ///< Config level no data.
  STRING,       ///< View of a string.
  INTEGER,      ///< Integer.
  BOOLEAN,      ///< Boolean.
  FLOAT,        ///< Floating point.
  IP_ADDR,      ///< IP Address
  DURATION,     ///< Duration (time span).
  TIMEPOINT,    ///< Timestamp, specific moment on a clock.
  CONS,         ///< Pointer to cons cell.
  TUPLE,        ///< Array of features (@c FeatureTuple)
  GENERIC,      ///< Extended type.
};

/// Empty struct to represent a NIL / NULL runtime value.
struct nil_value {
};

class ActiveType; // Forward declare

namespace std
{
template <> struct tuple_size<ValueType> : public std::integral_constant<size_t, static_cast<size_t>(ValueType::GENERIC) + 1> {
};
}; // namespace std

// *** @c FeatureTypeList, @c ValueType, and @c FeatureIndexToValue must be kept in synchronization! ***
/// Type list of feature types.
using FeatureTypeList =
  swoc::meta::type_list<std::monostate, nil_value, FeatureView, intmax_t, bool, double, swoc::IPAddr, std::chrono::nanoseconds,
                        std::chrono::system_clock::time_point, Cons *, FeatureTuple, Generic *>;

// *** @c FeatureTypeList, @c ValueType, and @c FeatureIndexToValue must be kept in synchronization! ***
/// Variant index to feature type.
constexpr std::array<ValueType, FeatureTypeList::size> FeatureIndexToValue{
  NO_VALUE, NIL, STRING, INTEGER, BOOLEAN, FLOAT, IP_ADDR, DURATION, TIMEPOINT, CONS, TUPLE, GENERIC};

namespace detail
{
template <typename GENERATOR, size_t... IDX>
constexpr std::initializer_list<std::result_of_t<GENERATOR(size_t)>>
indexed_init_list(GENERATOR &&g, std::index_sequence<IDX...> &&)
{
  return {g(IDX)...};
}
template <size_t N, typename GENERATOR>
constexpr std::initializer_list<std::result_of_t<GENERATOR(size_t)>>
indexed_init_list(GENERATOR &&g)
{
  return indexed_init_list(std::forward<GENERATOR>(g), std::make_index_sequence<N>());
}

template <typename GENERATOR, size_t... IDX>
constexpr std::array<std::invoke_result_t<GENERATOR, size_t>, sizeof...(IDX)>
indexed_array(GENERATOR &&g, std::index_sequence<IDX...> &&)
{
  return std::array<std::invoke_result_t<GENERATOR, size_t>, sizeof...(IDX)>{g(IDX)...};
}
template <size_t N, typename GENERATOR>
constexpr std::array<std::result_of_t<GENERATOR(size_t)>, N>
indexed_array(GENERATOR &&g)
{
  return indexed_array(std::forward<GENERATOR>(g), std::make_index_sequence<N>());
}

} // namespace detail

/** Convert a value enumeration to a variant index.
 *
 * @param type Value type (enumeration).
 * @return Index in @c FeatureData for that feature type.
 */
inline constexpr unsigned
IndexFor(ValueType type)
{
  auto IDX = detail::indexed_array<std::tuple_size<ValueType>::value>([](unsigned idx) { return idx; });
  return IDX[static_cast<unsigned>(type)];
};

/** Helper template for handling overloads for a specific set of types.
 * @tparam L The type list.
 * @tparam T The type to check against the type list.
 * @tparam R The return type of the function / method.
 *
 * This can be used to enable a template method for only the types in @a L.
 * @code
 *   template < typename T >
 *   auto operator() (T & t) ->
 *     EnableForTypes<swoc::meta::type_list<T0, T1, T2>, T, void>
 *   { ... }
 * @endcode
 * The return type @a R can be fixed (as in this case, it is always @c void ) or it can be dependent
 * on @a T (e.g., @c T& ). This will set a class such that the function operator works for any
 * type in the Feature variant, but not other types. Note that overloads for specific Feature
 * types can be defined before such a template. This is generally done when those types are
 * usable types, with the template for a generic failure response for non-usable types.
 */
template <typename L, typename T, typename R>
using EnableForTypes = std::enable_if_t<L::template contains<typename std::decay<T>::type>, R>;

/** Helper template for handling @c Feature variants.
 * @tparam T The type to check against the variant type list.
 * @tparam R The return type of the function / method.
 *
 * This can be used to enable a template method for only the types in the variant.
 * @code
 *   template < typename T > auto operator() (T & t) -> EnableForFeatureTypes<T, void> { ... }
 * @endcode
 * The return type @a R can be fixed (as in this case, it is always @c void ) or it can be dependent
 * on @a T (e.g., @c T& ). This will set a class such that the function operator works for any
 * type in the Feature variant, but not other types. Note that overloads for specific Feature
 * types can be defined before such a template. This is generally done when those types are
 * usable types, with the template for a generic failure response for non-usable types.
 */
template <typename T, typename R>
using EnableForFeatureTypes = std::enable_if_t<FeatureTypeList::contains<typename std::decay<T>::type>, R>;

/** Feature.
 * This is a wrapper on the variant type containing all the distinct feature types.
 * All of these are small and fixed size, any external storage (e.g. the text for a full)
 * is stored separately.
 *
 * @internal This is needed to deal with self-reference in the underlying variant. Some nested types
 * need to refer to @c Feature but the variant itself can't be forward declared. Instead this struct
 * is declared as an empty wrapper on the actual variant which can be forward declared.
 */
struct Feature : public FeatureTypeList::template apply<std::variant> {
  using self_type    = Feature;                                       ///< Self reference type.
  using super_type   = FeatureTypeList::template apply<std::variant>; ///< Parent type.
  using variant_type = super_type;                                    ///< The base variant type.

  /// Convenience meta-function to convert a @c FeatureData index to the specific feature type.
  /// @tparam F ValueType enumeration value.
  template <ValueType F> using type_for = std::variant_alternative_t<IndexFor(F), variant_type>;

  // Inherit variant constructors.
  using super_type::super_type;

  /** The value type of @a this.
   *
   * @return The type of @a this feature as a value type.
   *
   * This is the direct type of the feature, without regard to containment.
   */
  ValueType
  value_type() const
  {
    return FeatureIndexToValue[this->index()];
  }

  /** The active type of @a this.
   *
   * @return The feature type as an active type.
   */
  ActiveType active_type() const;

  /** Check if @a this feature contains other features.
   *
   * @return @c true if @a this feature contains other features, @c false if not.
   */
  bool is_list() const;

  /** Force feature to @c bool.
   *
   * @return @c true if equivalent to @c true, @c false otherwise.
   */
  type_for<BOOLEAN> as_bool() const;

  /** Coerce feature to integer.
   *
   * @param invalid Invalid value.
   * @return The feature as an integer, or @a invalid if it cannot be coerced along with errors.
   */
  swoc::Rv<type_for<INTEGER>> as_integer(type_for<INTEGER> invalid = 0) const;

  /** Coerce feature to a duration.
   *
   * @param invalid Invalid value.
   * @return The feature as a duration, or @a invalid if it cannot be coerced along with errors.
   */
  swoc::Rv<type_for<DURATION>> as_duration(type_for<DURATION> invalid = std::chrono::seconds(0)) const;

  /** Create a string feature by combining this feature.
   *
   * @param ctx Runtime context.
   * @param glue Separate between features.
   * @return A string feature containing this feature.
   *
   * This is simply a string rendering if @a this is a singleton. If it is a list form then the
   * list elements are rendered, separated by the @a glue. The primary use of this is to force
   * an arbitrary feature to be a string.
   */
  self_type join(Context &ctx, swoc::TextView const &glue) const;
};

bool operator==(Feature const &lhs, Feature const &rhs);
inline bool
operator!=(Feature const &lhs, Feature const &rhs)
{
  return !(lhs == rhs);
}
bool operator<(Feature const &lhs, Feature const &rhs);
inline bool
operator>(Feature const &lhs, Feature const &rhs)
{
  return rhs < lhs;
}
bool operator<=(Feature const &lhs, Feature const &rhs);
inline bool
operator>=(Feature const &lhs, Feature const &rhs)
{
  return rhs <= lhs;
}

/// @cond NO_DOXYGEN
// These are overloads for variant visitors so that other call sites can use @c Feature
// directly without having to reach in to the @c variant_type.
namespace std
{
template <typename VISITOR>
auto
visit(VISITOR &&visitor,
      Feature  &feature) -> decltype(visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type &>(feature)))
{
  return visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type &>(feature));
}

template <typename VISITOR>
auto
visit(VISITOR &&visitor, Feature const &feature) -> decltype(visit(std::forward<VISITOR>(visitor),
                                                                   static_cast<Feature::variant_type const &>(feature)))
{
  return visit(std::forward<VISITOR>(visitor), static_cast<Feature::variant_type const &>(feature));
}
} // namespace std
/// @endcond

namespace detail
{
// Need to document this, and then move it in to libswoc.
// This walks a typelist and compute the index of a given type @c F in the typelist.
// The utility is to convert from a feature type in the feature variant to the index in the variant.
// This is used almost entirely for error reporting.
template <typename T, typename... Rest> struct TypeListIndex;

template <typename T, typename... Rest> struct TypeListIndex<T, T, Rest...> : std::integral_constant<std::size_t, 0> {
};

template <typename T, typename U, typename... Rest>
struct TypeListIndex<T, U, Rest...> : std::integral_constant<std::size_t, 1 + TypeListIndex<T, Rest...>::value> {
};

template <typename F> struct TypeListIndexWrapper {
  template <typename... Args> struct IDX : std::integral_constant<size_t, TypeListIndex<F, Args...>::value> {
  };
};

template <typename F> using TypeListIndexer = FeatureTypeList::template apply<detail::TypeListIndexWrapper<F>::template IDX>;

} // namespace detail

/** Convert a feature type to a feature index.
 *
 * @tparam F Feature type.
 */
template <typename F> static constexpr size_t index_for_type = detail::TypeListIndexer<F>::value;

/** Convert a feature type to a @c ValueType value.
 *
 * @tparam F Feature type.
 */
template <typename F> static constexpr ValueType value_type_of = ValueType(index_for_type<F>);

/// Nil value feature.
/// @internal Default constructor doesn't work in the Intel compiler, must be explicit.
static constexpr Feature NIL_FEATURE{nil_value{}};

/** Standard cons cell.
 *
 * Used to build up structures that have indeterminate length in the standard cons cell way.
 */
struct Cons {
  Feature _car; ///< Immediate feature.
  Feature _cdr; ///< Next feature.
};

/// A mask indicating a set of @c ValueType.
using FeatureMask = std::bitset<FeatureTypeList::size>;
/// A mask indicating a set of @c ValueType.
using ValueMask = std::bitset<std::tuple_size<ValueType>::value>;

/** The active type.
 * This is a mask of feature types, representing the possible types of the active feature.
 */
class ActiveType
{
  using self_type = ActiveType;

public:
  struct TupleOf {
    ValueMask _mask;
    TupleOf() = default;
    explicit TupleOf(ValueMask mask) : _mask(mask) {}
    template <typename... Rest> TupleOf(ValueType vt, Rest &&...rest) : TupleOf(rest...) { _mask[vt] = true; }
  };
  ActiveType()                                = default;
  ActiveType(self_type const &that)           = default;
  self_type &operator=(self_type const &that) = default;

  ActiveType(ValueMask vtypes) : _base_type(vtypes){};
  template <typename... Rest> ActiveType(ValueType vt, Rest &&...rest);
  template <typename... Rest> ActiveType(TupleOf const &tt, Rest &&...rest);

  self_type &operator=(ValueType vt);
  self_type &operator=(TupleOf const &tt);
  self_type &operator|=(ValueType vt);
  self_type &
  operator|=(ValueMask vtypes)
  {
    _base_type |= vtypes;
    return *this;
  }
  self_type &operator|=(TupleOf const &tt);

  bool
  operator==(self_type const &that)
  {
    return _base_type == that._base_type && _tuple_type == that._tuple_type;
  }
  bool
  operator!=(self_type const &that)
  {
    return !(*this == that);
  }

  /// Check if this is any type and therefore has a value.
  bool
  has_value() const
  {
    return _base_type.any();
  }

  bool
  can_satisfy(ValueType vt) const
  {
    return _base_type[vt];
  }
  bool
  can_satisfy(ValueMask vmask) const
  {
    return (_base_type & vmask).any();
  }
  bool
  can_satisfy(self_type const &that) const
  {
    auto c = _base_type & that._base_type;
    // TUPLE is a common type if the tuple element types have a common type or no type is specified
    // for the TUPLE, which means just check for being a TUPLE.
    if (c[TUPLE] && that._tuple_type.any() && (that._tuple_type & _tuple_type).none()) {
      c[TUPLE] = false;
    }
    return c.any();
  }

  ValueMask
  base_types() const
  {
    return _base_type;
  }
  ValueMask
  tuple_types() const
  {
    return _tuple_type;
  }

  self_type &
  mark_cfg_const()
  {
    _cfg_const_p = true;
    return *this;
  }
  bool
  is_cfg_const() const
  {
    return _cfg_const_p;
  }

  static self_type
  any_type()
  {
    self_type zret;
    zret._base_type.set();
    zret._tuple_type.set();
    return zret;
  }

protected:
  ValueMask                  _base_type;           ///< Base type of the feature.
  ValueMask                  _tuple_type;          ///< Types of the elements of a tuple.
  bool                       _cfg_const_p = false; ///< Config time constant.
  friend swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, ActiveType const &type);
};

template <typename... Rest> ActiveType::ActiveType(ValueType vt, Rest &&...rest) : ActiveType(rest...)
{
  _base_type[vt] = true;
}

template <typename... Rest> ActiveType::ActiveType(TupleOf const &tt, Rest &&...rest) : ActiveType(rest...)
{
  _tuple_type       |= tt._mask;
  _base_type[TUPLE]  = true;
}

inline auto
ActiveType::operator=(ValueType vt) -> self_type &
{
  _base_type.reset();
  _base_type[vt] = true;
  return *this;
}

inline auto
ActiveType::operator=(TupleOf const &tt) -> self_type &
{
  _base_type.reset();
  _base_type[TUPLE] = true;
  _tuple_type       = tt._mask;
  return *this;
}

inline auto
ActiveType::operator|=(ValueType vt) -> self_type &
{
  _base_type[vt] = true;
  return *this;
}

inline auto
ActiveType::operator|=(TupleOf const &tt) -> self_type &
{
  _base_type[TUPLE]  = true;
  _tuple_type       |= tt._mask;
  return *this;
}

/** Create a @c ValueMask containing a single @a type.
 *
 * @param type Type to put in the mask.
 * @return A mask with only @a type set.
 *
 * This is useful for initializing @c const instance of @c FeatureMask. For example, if the mask
 * should be for @c STRING it would be
 *
 * @code
 *   static const FeatureMask Mask { MaskFor(STRING) };
 * @endcode
 *
 * @see ValueType
 * @see FeatureMask
 */
inline ValueMask
MaskFor(ValueType type)
{
  ValueMask mask;
  mask[IndexFor(type)] = true;
  return mask;
}

/** Create a @c ValueMask for a list of value types.
 *
 * @tparam P Argument types.
 * @param parms Value type enumeration values.
 * @return A mask
 *
 * This is enabled only if all types in @a P are @c ValueType.
 */
template <typename... P>
auto
MaskFor(P... parms) -> std::enable_if_t<std::conjunction_v<std::is_same<ValueType, P>...>, ValueMask>
{
  ValueMask mask;
  ((mask[IndexFor(parms)] = true), ...);
  return mask;
}

/** Create a @c FeatureMask containing @a types.
 *
 * @param types List of types to put in the mask.
 * @return A mask with the specified @a types set.
 *
 * @a types is an initializer list. For example, if the mask should have @c STRING and @c INTEGER
 * set, it would be
 *
 * @code
 *   static const FeatureMask Mask { MaskFor({ STRING, INTEGER}) };
 * @endcode
 *
 * This is useful for initializing @c const instance of @c FeatureMask.
 *
 * @see ValueType
 * @see FeatureMask
 */
inline ValueMask
MaskFor(std::initializer_list<ValueType> const &types)
{
  ValueMask mask;
  for (auto type : types) {
    mask[IndexFor(type)] = true;
  }
  return mask;
}

/// Convenience meta-function to convert a @c FeatureData index to the specific feature type.
/// @tparam F ValueType enumeration value.
template <ValueType F> using feature_type_for = Feature::type_for<F>;

/** Compute a feature mask from a list of types.
 *
 * @tparam F List of feature types.
 * @return A mask for the feature types in @a F.
 *
 * @internal This can't be @c constexpr because the underlying type @c std::bitset doesn't have a
 * @c constexpr constructor.
 */
template <typename... F>
ValueMask
MaskFor()
{
  ValueMask mask;
  ((mask[index_for_type<F>] = true), ...);
  return mask;
}

template <typename... F> struct ValueMaskFor {
  static inline const ValueMask value{MaskFor<F...>()};
};

/// Check if @a feature is nil.
inline bool
is_nil(Feature const &feature)
{
  if (auto gf = std::get_if<GENERIC>(&feature)) {
    return (*gf)->is_nil();
  }
  return feature.index() == IndexFor(NIL);
}
/// Check if @a feature is empty (nil or an empty string).
inline bool
is_empty(Feature const &feature)
{
  return IndexFor(NIL) == feature.index() || (IndexFor(STRING) == feature.index() && std::get<IndexFor(STRING)>(feature).empty());
}

/** Get the first element for @a feature.
 *
 * @param feature Feature from which to extract.
 * @return If @a feature is not a sequence, @a feature. Otherwise return the first feature in the
 * sequence.
 *
 */
Feature car(Feature const &feature);

/** Drop the first element in @a feature.
 *
 * @param feature Feature sequence.
 * @return If @a feature is not a sequence, or there are no more elements in @a feature, the @c NIL feature.
 * Otherwise a sequence not containing the first element of @a feature.
 *
 */
Feature &cdr(Feature &feature);

inline void
clear(Feature &feature)
{
  if (auto gf = std::get_if<GENERIC>(&feature); gf) {
    (*gf)->~Generic();
  }
  feature = NIL_FEATURE;
}

static constexpr swoc::TextView ACTIVE_FEATURE_KEY{"..."};
static constexpr swoc::TextView UNMATCHED_FEATURE_KEY{"*"};

/// Conversion between @c ValueType and printable names.
extern swoc::Lexicon<ValueType> const ValueTypeNames;

/// Supported hooks.
/// @internal These must be in order because that is used to check if the value for the _when_
/// directive is valid from the current hook. Special hooks that can't be scheduled from a
/// transaction go before @c TXN_START so they are always "in the past".
enum class Hook {
  INVALID,     ///< Invalid hook (default initialization value).
  POST_LOAD,   ///< After configuration has been loaded.
  POST_ACTIVE, ///< After the configuration has become active.
  MSG,         ///< During plugin message handling (implicit).
  TXN_START,   ///< Transaction start.
  CREQ,        ///< Read Request from user agent.
  PRE_REMAP,   ///< Before remap.
  REMAP,       ///< Remap (implicit).
  POST_REMAP,  ///< After remap.
  PREQ,        ///< Send request from proxy to upstream.
  URSP,        ///< Read response from upstream.
  PRSP,        ///< Send response to user agent from proxy.
  TXN_CLOSE    ///< Transaction close.
};

/// Make @c tuple_size work for the @c Hook enum.
namespace std
{
template <> struct tuple_size<Hook> : public std::integral_constant<size_t, static_cast<size_t>(Hook::TXN_CLOSE) + 1> {
};
}; // namespace std

/** Convert a @c Hook enumeration to an unsigned value.
 *
 * @param id Enumeration to convert.
 * @return Numeric value of @a id.
 */
inline constexpr unsigned
IndexFor(Hook id)
{
  return static_cast<unsigned>(id);
}

/// Set of enabled hooks.
using HookMask = std::bitset<std::tuple_size<Hook>::value>;

/** Create a @c HookMask containing a single @a type.
 *
 * @param hook Enumeration value for the hook to mark.
 * @return A mask with only @a type set.
 *
 * This is useful for initializing @c const instance of @c HookMask. For example, if the mask
 * should be for @c PRE_REMAP it would be
 *
 * @code
 *   static const HookMask Mask { MaskFor(Hook::PRE_REMAP) };
 * @endcode
 */
inline HookMask
MaskFor(Hook hook)
{
  HookMask mask;
  mask[IndexFor(hook)] = true;
  return mask;
}

/** Create a @c HookMask containing @a types.
 *
 * @param hooks Enumeration values to hooks to mark.
 * @return A mask with the specified @a types set.
 *
 * @a types is an initializer list. For example, if the mask should have @c CREQ and @c PREQ
 * set, it would be
 *
 * @code
 *   static const HookMask Mask { MaskFor({ Hook::CREQ, Hook::PREQ}) };
 * @endcode
 *
 * This is useful for initializing @c const instance of @c HookMask.
 *
 * @see ValueType
 * @see HookMask
 */
inline HookMask
MaskFor(std::initializer_list<Hook> const &hooks)
{
  HookMask mask;
  for (auto hook : hooks) {
    mask[IndexFor(hook)] = true;
  }
  return mask;
}

template <typename E> struct MaskTypeFor {
};
template <> struct MaskTypeFor<Hook> {
  using type = HookMask;
};
template <> struct MaskTypeFor<ValueType> {
  using type = ValueMask;
};

template <typename... P>
auto
MaskFor(P... parms) -> std::enable_if_t<std::conjunction_v<std::is_same<Hook, P>...>, HookMask>
{
  HookMask mask;
  ((mask[IndexFor(parms)] = true), ...);
  return mask;
}

/// Name lookup for hook values.
extern swoc::Lexicon<Hook> HookName;

inline FeatureView
FeatureView::Literal(TextView const &view)
{
  self_type zret{view};
  zret._literal_p = true;
  return zret;
}

inline FeatureView
FeatureView::Direct(TextView const &view)
{
  self_type zret{view};
  zret._direct_p = true;
  return zret;
}

/// Conversion enumeration for checking boolean strings.
enum BoolTag {
  INVALID = -1,
  False   = 0,
  True    = 1,
};
/// Mapping of strings to boolean values.
/// This is for handling various synonymns in a consistent manner.
extern swoc::Lexicon<BoolTag> const BoolNames;

/// Container for global data.
struct Global {
  swoc::Errata             _preload_errata;
  int                      TxnArgIdx = -1;
  std::vector<std::string> _args; ///< Global configuration arguments.
  /// Amount of reserved storage requested by remap directives.
  /// This is not always correct, @c Context must handle overflows gracefully.
  std::atomic<size_t> _remap_ctx_storage_required{0};

  void reserve_txn_arg();

  // -- Reserved keys -- //
  /// Standard name for nested directives and therefore reserved globally.
  static constexpr swoc::TextView DO_KEY = "do";
};

/// Global data.
extern Global G;

/// Reserved storage descriptor.
struct ReservedSpan {
  size_t offset = 0; ///< Offset for start of storage.
  size_t n      = 0; ///< Storage size;
};

/// Used for clean up in @c Config and @c Context.
/// A list of these is used to perform additional cleanup for extensions to the basic object.
struct Finalizer {
  using self_type                  = Finalizer; ///< Self reference type.
  void                       *_ptr = nullptr;   ///< Pointer to object to destroy.
  std::function<void(void *)> _f;               ///< Functor to destroy @a _ptr.

  self_type *_prev = nullptr;                           ///< List support.
  self_type *_next = nullptr;                           ///< List support.
  using Linkage    = swoc::IntrusiveLinkage<Finalizer>; ///< For @c IntrusiveDList

  Finalizer(void *ptr, std::function<void(void *)> &&f);
};

inline Finalizer::Finalizer(void *ptr, std::function<void(void *)> &&f) : _ptr(ptr), _f(std::move(f)) {}

/** Scoping value change.
 *
 * @tparam T Type of variable to scope.
 */
template <typename T> struct let {
  T &_var;   ///< Reference to scoped variable.
  T  _value; ///< Original value.

  /** Construct a scope.
   *
   * @param var Variable to scope.
   * @param value temporary value to assign.
   */
  let(T &var, T const &value);

  /** Construct a scope.
   *
   * @param var Variable to scope.
   * @param value temporary value to assign.
   */
  let(T &var, T &&value);

  ~let();
};

template <typename T> let<T>::let(T &var, T const &value) : _var(var), _value(var)
{
  _var = value;
}
template <typename T> let<T>::let(T &var, T &&value) : _var(var), _value(std::move(var))
{
  _var = std::move(value);
}

template <typename T> let<T>::~let()
{
  _var = _value;
}

// BufferWriter support.
namespace swoc
{
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, feature_type_for<NO_VALUE>);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, feature_type_for<NIL>);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ValueType type);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, FeatureTuple const &t);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Feature const &feature);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, ValueMask const &mask);
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, feature_type_for<DURATION> const &d);
} // namespace swoc
swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, Hook hook);
swoc::BufferWriter &bwformat(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, ActiveType const &type);

namespace std::chrono
{
using days  = duration<hours::rep, ratio<86400>>;
using weeks = duration<hours::rep, ratio<86400 * 7>>;
} // namespace std::chrono
