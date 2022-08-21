// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

  Scaled integral values.

  In many situations it is desirable to define scaling factors or base units (a "metric"). This template
  enables this to be done in a type and scaling safe manner where the defined factors carry their scaling
  information as part of the type.
*/
#pragma once

#include <cstdint>
#include <ratio>
#include <ostream>
#include <type_traits>

#include "swoc/swoc_version.h"
#include "swoc/swoc_meta.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

namespace tag {
/// A generic tag for @c Scalar types, used as the default.
struct generic;
} // namespace tag

template <intmax_t N, typename C, typename T> class Scalar;

namespace detail {
/// @cond INTERNAL_DETAIL

// @internal - although these conversion methods look bulky, in practice they compile down to
// very small amounts of code due to the conditions being compile time constant - the non-taken
// clauses are dead code and eliminated by the compiler.

// The general case where neither N nor S are a multiple of the other seems a bit long but this
// minimizes the risk of integer overflow.  I need to validate that under -O2 the compiler will
// only do 1 division to get both the quotient and remainder for (n/N) and (n%N). In cases where
// N,S are powers of 2 I have verified recent GNU compilers will optimize to bit operations.

// Convert a count @a c that is scale @a S to scale @c N
template <intmax_t N, intmax_t S, typename C>
C
scale_conversion_round_up(C c) {
  using R = std::ratio<N, S>;
  if constexpr (N == S) {
    return c;
  } else if constexpr (R::den == 1) {
    return c / R::num + (0 != c % R::num); // N is a multiple of S.
  } else if constexpr (R::num == 1) {
    return c * R::den; // S is a multiple of N.
  }
  return (c / R::num) * R::den + ((c % R::num) * R::den) / R::num + (0 != (c % R::num));
}

// Convert a count @a c that is scale @a S to scale @c N
template <intmax_t N, intmax_t S, typename C>
C
scale_conversion_round_down(C c) {
  using R = std::ratio<N, S>;
  if constexpr (N == S) {
    return c;
  } else if constexpr (R::den == 1) {
    return c / R::num; // N = k S
  } else if constexpr (R::num == 1) {
    return c * R::den; // S = k N
  }
  return (c / R::num) * R::den + ((c % R::num) * R::den) / R::num;
}

/* Helper classes for @c Scalar

   These wrap values to capture extra information for @c Scalar methods. This includes whether to
   round up or down when converting and, when the wrapped data is also a @c Scalar, the scale.

   These are not intended for direct use but by the @c round_up and @c round_down free functions
   which capture the information about the argument and construct an instance of one of these
   classes to pass it on to a @c Scalar method.

   Scale conversions between @c Scalar instances are handled in these classes via the templated
   methods @c scale_conversion_round_up and @c scale_conversion_round_down.

   Conversions between scales and types for the scalar helpers is done inside the helper classes
   and a user type conversion operator exists so the helper can be converted by the compiler to
   the correct type. For the units base conversion this is done in @c Scalar because the
   generality of the needed conversion is too broad to be easily used. It can be done but there is
   some ugliness due to the fact that in some cases two user conversions are needed, which is
   difficult to deal with. I have tried it both ways and overall this seems a cleaner
   implementation.

   Much of this is driven by the fact that the assignment operator, in some cases, can not be
   templated and therefore to have a nice interface for assignment this split is needed.

   Note - the key point is the actual conversion is not done when the wrapper instance is created
   but when the wrapper instance is assigned. That is what enables the conversion to be done in
   the context of the destination, which is not otherwise possible.
 */

// Unit value, to be rounded up.
template <typename C> struct scalar_unit_round_up_t {
  C _n;

  template <intmax_t N, typename I>
  constexpr I
  scale() const {
    return static_cast<I>(_n / N + (0 != (_n % N)));
  }
};

// Unit value, to be rounded down.
template <typename C> struct scalar_unit_round_down_t {
  C _n;

  template <intmax_t N, typename I>
  constexpr I
  scale() const {
    return static_cast<I>(_n / N);
  }
};

// Scalar value, to be rounded up.
template <intmax_t N, typename C, typename T> struct scalar_round_up_t {
  C _n;

  template <intmax_t S, typename I> constexpr operator Scalar<S, I, T>() const {
    return Scalar<S, I, T>(scale_conversion_round_up<S, N>(_n));
  }
};

// Scalar value, to be rounded down.
template <intmax_t N, typename C, typename T> struct scalar_round_down_t {
  C _n;

  template <intmax_t S, typename I> constexpr operator Scalar<S, I, T>() const {
    return Scalar<S, I, T>(scale_conversion_round_down<S, N>(_n));
  }
};

/// @endcond
} // namespace detail

/** A class to hold scaled values.

    Instances of this class have a @a count and a @a scale. The "value" of the instance is @a
    count * @a scale.  The scale is stored in the compiler in the class symbol table and so only
    the count is a run time value. An instance with a large scale can be assigned to an instance
    with a smaller scale and the conversion is done automatically. Conversions from a smaller to
    larger scale must be explicit using @c round_up and @c round_down. This prevents
    inadvertent changes in value. Because the scales are not the same these conversions can be
    lossy and the two conversions determine whether, in such a case, the result should be rounded
    up or down to the nearest scale value.

    @a N sets the scale. @a C is the type used to hold the count, which is in units of @a N.

    @a T is a "tag" type which is used only to distinguish the base metric for the scale. Scalar
    types that have different tags are not interoperable although they can be converted manually by
    converting to units and then explicitly constructing a new Scalar instance. This is by design.
    This can be ignored - if not specified then it defaults to a "generic" tag. The type can be (and
    usually is) defined in name only).

    @note This is modeled somewhat on @c std::chrono and serves a similar function for different
    and simpler cases (where the ratio is always an integer, never a fraction).

    @see round_up
    @see round_down
 */
template <intmax_t N, typename C = int, typename T = tag::generic> class Scalar {
  using self_type = Scalar; ///< Self reference type.

public:
  /// Scaling factor - make it external accessible.
  constexpr static intmax_t SCALE = N;
  using Counter                   = C; ///< Type used to hold the count.
  using Tag                       = T; ///< Tag for the scalar.

  static_assert(N > 0, "The scaling factor (1st template argument) must be a positive integer");
  static_assert(std::is_integral<C>::value, "The counter type (2nd template argument) must be an integral type");

  constexpr Scalar(); ///< Default constructor.

  /// Construct to have value that is @a n scaled units.
  explicit constexpr Scalar(Counter n);

  /// Copy constructor.
  constexpr Scalar(self_type const &that); /// Copy constructor.

  /// Copy constructor for same scale.
  template <typename I> constexpr Scalar(Scalar<N, I, T> const &that);

  /// Direct conversion constructor.
  /// @note Requires that @c S be an integer multiple of @c SCALE.
  template <intmax_t S, typename I> constexpr Scalar(Scalar<S, I, T> const &that);

  /// @cond INTERNAL_DETAIL
  // Assignment from internal rounding structures.
  // Conversion constructor.
  constexpr Scalar(detail::scalar_round_up_t<N, C, T> const &v);

  // Conversion constructor.
  constexpr Scalar(detail::scalar_round_down_t<N, C, T> const &that);

  // Conversion constructor.
  template <typename I> constexpr Scalar(detail::scalar_unit_round_up_t<I> v);

  // Conversion constructor.
  template <typename I> constexpr Scalar(detail::scalar_unit_round_down_t<I> v);
  /// @endcond

  /** Assign value from @a that.
   *
   * @tparam S Scale.
   * @tparam I Integral type.
   * @param that Source value.
   * @return @a this.
   *
   * @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this
   * isn't the case then the @c round_up or @c round_down must be used to indicate the rounding
   * direction.
   */
  template <intmax_t S, typename I> self_type &operator=(Scalar<S, I, T> const &that);

  /// Self type assignment.
  self_type &operator=(self_type const &that);

  /// @cond INTERNAL_DETAIL
  /** Internal method to assign a unit value to be rounded up to the internal @c SCALE.
   *
   * @tparam I The underlying scale type.
   * @param n The value to be scaled.
   * @return @a this
   */
  template <typename I> self_type &operator=(detail::scalar_unit_round_up_t<I> n);

  /** Internal method to assign a unit value to be rounded down to the internal @c SCALE.
   *
   * @tparam I The underlying scale type.
   * @param n The value to be scaled.
   * @return @a this
   */
  template <typename I> self_type &operator=(detail::scalar_unit_round_down_t<I> n);

  /** Internal method to assign a differently scaled SCALAR to be rounded up.
   *
   * @param v The embedding of the value to be scaled.
   * @return @a this
   *
   * The template parameters of @a v carry the static information needed to correctly scale
   * it to the local scale.
   */
  self_type &operator=(detail::scalar_round_up_t<N, C, T> v);

  /** Internal method to assign a differently scaled SCALAR to be rounded down.
   *
   * @param v The embedding of the value to be scaled.
   * @return @a this
   *
   * The template parameters of @a v carry the static information needed to correctly scale
   * it to the local scale.
   */
  self_type &operator=(detail::scalar_round_down_t<N, C, T> v);
  /// @endcond

  /** Set the scaled count to @a n.
   *
   * @param n The number of scale units.
   * @return @a this
   *
   * This sets the count to be @a n, making the effective value @c SCALE*n. This is frequently useful
   * for parsing, when the input is also in scaled units (e.g., the number of megabytes to use for
   * a file).
   */
  self_type &assign(Counter n);

  /// The value @a that is scaled appropriately.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this
  /// isn't the case then the @c round_up or @c round_down must be used to indicate the rounding
  /// direction.
  template <intmax_t S, typename I> self_type &assign(Scalar<S, I, T> const &that);

  /// @cond INTERNAL_DETAIL
  template <typename I> self_type &assign(detail::scalar_unit_round_up_t<I> n);

  template <typename I> self_type &assign(detail::scalar_unit_round_down_t<I> n);

  self_type &assign(detail::scalar_round_up_t<N, C, T> v);

  self_type &assign(detail::scalar_round_down_t<N, C, T> v);
  /// @endcond

  /// The number of scale units.
  constexpr Counter count() const;

  /// The scaled value.
  constexpr Counter value() const;

  /// User conversion to scaled value.
  constexpr operator Counter() const;

  /// Addition operator.
  /// The value is scaled from @a that to @a this.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this isn't the case then
  /// the @c scale_up or @c scale_down casts must be used to indicate the rounding direction.
  self_type &operator+=(self_type const &that);

  /** Cross scale addition.
   *
   * @tparam S Source scale.
   * @tparam I Source raw type.
   * @param that Value.
   * @return @a this
   *
   * @a that is scaled as needed to be added to @a this.
   */
  template <intmax_t S, typename I> self_type &operator+=(Scalar<S, I, T> const &that);

  /// @cond INTERNAL_DETAIL
  template <typename I> self_type &operator+=(detail::scalar_unit_round_up_t<I> n);

  template <typename I> self_type &operator+=(detail::scalar_unit_round_down_t<I> n);

  self_type &operator+=(detail::scalar_round_up_t<N, C, T> v);

  self_type &operator+=(detail::scalar_round_down_t<N, C, T> v);
  /// @endcond

  /// Increment - increase count by 1.
  self_type &operator++();

  /// Increment - increase count by 1.
  self_type operator++(int);

  /// Decrement - decrease count by 1.
  self_type &operator--();

  /// Decrement - decrease count by 1.
  self_type operator--(int);

  /// Increment by @a n.
  self_type &inc(Counter n);

  /// Decrement by @a n.
  self_type &dec(Counter n);

  /// Subtraction operator.
  /// The value is scaled from @a that to @a this.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this isn't the case then
  /// the @c scale_up or @c scale_down casts must be used to indicate the rounding direction.
  self_type &operator-=(self_type const &that);

  /** Subtraction.
   *
   * @tparam S Scale.
   * @tparam I Raw type.
   * @param that Value to subtract.
   * @return @a this
   *
   * The value of @a that is subtracted from the value of @a this.
   */
  template <intmax_t S, typename I> self_type &operator-=(Scalar<S, I, T> const &that);

  /// @cond INTERNAL_DETAIL
  template <typename I> self_type &operator-=(detail::scalar_unit_round_up_t<I> n);

  template <typename I> self_type &operator-=(detail::scalar_unit_round_down_t<I> n);

  self_type &operator-=(detail::scalar_round_up_t<N, C, T> v);

  self_type &operator-=(detail::scalar_round_down_t<N, C, T> v);
  /// @endcond

  /// Multiplication - multiple the count by @a n.
  self_type &operator*=(C n);

  /// Division - divide (rounding down) the count by @a n.
  self_type &operator/=(C n);

  /// Utility overload of the function operator to create instances at the same scale.
  self_type operator()(Counter n) const;

  /// Return a value at the same scale with a count increased by @a n.
  self_type plus(Counter n) const;

  /// Return a value at the same scale with a count decreased by @a n.
  self_type minus(Counter n) const;

  /// Run time access to the scale (template arg @a N).
  static constexpr intmax_t scale();

protected:
  Counter _n; ///< Number of scale units.
};

/// @cond Scalar_INTERNAL
// Avoid issues with doxygen matching externally defined methods.
template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::Scalar() : _n() {}

template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::Scalar(Counter n) : _n(n) {}

template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::Scalar(self_type const &that) : _n(that._n) {}

template <intmax_t N, typename C, typename T>
template <typename I>
constexpr Scalar<N, C, T>::Scalar(Scalar<N, I, T> const &that) : _n(static_cast<C>(that.count())) {}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
constexpr Scalar<N, C, T>::Scalar(Scalar<S, I, T> const &that) : _n(std::ratio<S, N>::num * that.count()) {
  static_assert(std::ratio<S, N>::den == 1,
                "Construction not permitted - target scale is not an integral multiple of source scale.");
}

template <intmax_t N, typename C, typename T>
constexpr Scalar<N, C, T>::Scalar(detail::scalar_round_up_t<N, C, T> const &v) : _n(v._n) {}

template <intmax_t N, typename C, typename T>
constexpr Scalar<N, C, T>::Scalar(detail::scalar_round_down_t<N, C, T> const &v) : _n(v._n) {}

template <intmax_t N, typename C, typename T>
template <typename I>
constexpr Scalar<N, C, T>::Scalar(detail::scalar_unit_round_up_t<I> v) : _n(v.template scale<N, C>()) {}

template <intmax_t N, typename C, typename T>
template <typename I>
constexpr Scalar<N, C, T>::Scalar(detail::scalar_unit_round_down_t<I> v) : _n(v.template scale<N, C>()) {}

template <intmax_t N, typename C, typename T>
constexpr auto
Scalar<N, C, T>::count() const -> Counter {
  return _n;
}

template <intmax_t N, typename C, typename T>
constexpr C
Scalar<N, C, T>::value() const {
  return _n * SCALE;
}

template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::operator Counter() const {
  return _n * SCALE;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::assign(Counter n) -> self_type & {
  _n = n;
  return *this;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::operator=(self_type const &that) -> self_type & {
  _n = that._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::operator=(detail::scalar_round_up_t<N, C, T> v) -> self_type &{
  _n = v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::assign(detail::scalar_round_up_t<N, C, T> v) -> self_type & {
  _n = v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::operator=(detail::scalar_round_down_t<N, C, T> v) -> self_type & {
  _n = v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::assign(detail::scalar_round_down_t<N, C, T> v) -> self_type & {
  _n = v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
inline auto
Scalar<N, C, T>::operator=(detail::scalar_unit_round_up_t<I> v) -> self_type & {
  _n = v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
inline auto
Scalar<N, C, T>::assign(detail::scalar_unit_round_up_t<I> v) -> self_type & {
  _n = v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
inline auto
Scalar<N, C, T>::operator=(detail::scalar_unit_round_down_t<I> v) -> self_type & {
  _n = v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
inline auto
Scalar<N, C, T>::assign(detail::scalar_unit_round_down_t<I> v) -> self_type & {
  _n = v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::operator=(Scalar<S, I, T> const &that) -> self_type & {
  using R = std::ratio<S, N>;
  static_assert(R::den == 1, "Assignment not permitted - target scale is not an integral multiple of source scale.");
  _n = that.count() * R::num;
  return *this;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::assign(Scalar<S, I, T> const &that) -> self_type & {
  using R = std::ratio<S, N>;
  static_assert(R::den == 1, "Assignment not permitted - target scale is not an integral multiple of source scale.");
  _n = that.count() * R::num;
  return *this;
}

template <intmax_t N, typename C, typename T>
constexpr inline intmax_t
Scalar<N, C, T>::scale() {
  return SCALE;
}

/// @endcond Scalar_INTERNAL

// --- Functions ---

/** Prepare units @a n to be assigned to a @c Scalar, rounding up as needed.
 *
 * @tparam C The type of the value.
 * @param n The value.
 * @return An unspecified type suitable to be assigned to a @c Scalar.
 */
template <typename C>
constexpr detail::scalar_unit_round_up_t<C>
round_up(C n) {
  return {n};
}

/** Prepare a @c Scalar instance to be assigned to another @c Scalar, rounding up as needed.
 *
 * @tparam N @c Scalar scale value.
 * @tparam C @c Scalar internal storage type.
 * @tparam T @c Scalar tag.
 * @param v The @c Scalar value.
 * @return
 */
template <intmax_t N, typename C, typename T>
constexpr detail::scalar_round_up_t<N, C, T>
round_up(Scalar<N, C, T> v) {
  return {v.count()};
}

/** Prepare units @a n to be assigned to a @c Scalar, rounding down as needed.
 *
 * @tparam C The type of the value.
 * @param n The value.
 * @return An unspecified type suitable to be assigned to a @c Scalar.
 */
template <typename C>
constexpr detail::scalar_unit_round_down_t<C>
round_down(C n) {
  return {n};
}

/** Prepare a @c Scalar instance to be assigned to another @c Scalar, rounding down as needed.
 *
 * @tparam N @c Scalar scale value.
 * @tparam C @c Scalar internal storage type.
 * @tparam T @c Scalar tag.
 * @param v The @c Scalar value.
 * @return
 */
template <intmax_t N, typename C, typename T>
constexpr detail::scalar_round_down_t<N, C, T>
round_down(Scalar<N, C, T> v) {
  return {v.count()};
}

// --- Compare operators
// These optimize nicely because if R::num or R::den is 1 the compiler will drop it.

template <intmax_t N, typename C1, intmax_t S, typename I, typename T>
bool
operator<(Scalar<N, C1, T> const &lhs, Scalar<S, I, T> const &rhs) {
  using R = std::ratio<N, S>;
  return lhs.count() * R::num < rhs.count() * R::den;
}

template <intmax_t N, typename C1, intmax_t S, typename I, typename T>
bool
operator==(Scalar<N, C1, T> const &lhs, Scalar<S, I, T> const &rhs) {
  using R = std::ratio<N, S>;
  return lhs.count() * R::num == rhs.count() * R::den;
}

template <intmax_t N, typename C1, intmax_t S, typename I, typename T>
bool
operator<=(Scalar<N, C1, T> const &lhs, Scalar<S, I, T> const &rhs) {
  using R = std::ratio<N, S>;
  return lhs.count() * R::num <= rhs.count() * R::den;
}

// Derived compares.
template <intmax_t N, typename C, intmax_t S, typename I, typename T>
bool
operator>(Scalar<N, C, T> const &lhs, Scalar<S, I, T> const &rhs) {
  return rhs < lhs;
}

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
bool
operator>=(Scalar<N, C, T> const &lhs, Scalar<S, I, T> const &rhs) {
  return rhs <= lhs;
}

// Arithmetic operators
template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator+=(self_type const &that) -> self_type & {
  _n += that._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::operator+=(Scalar<S, I, T> const &that) -> self_type & {
  using R = std::ratio<S, N>;
  static_assert(R::den == 1, "Addition not permitted - target scale is not an integral multiple of source scale.");
  _n += that.count() * R::num;
  return *this;
}

/// @cond INTERNAL_DETAIL
template <intmax_t N, typename C, typename T>
template <typename I>
auto
Scalar<N, C, T>::operator+=(detail::scalar_unit_round_up_t<I> v) -> self_type & {
  _n += v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
auto
Scalar<N, C, T>::operator+=(detail::scalar_unit_round_down_t<I> v) -> self_type & {
  _n += v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator+=(detail::scalar_round_up_t<N, C, T> v) -> self_type & {
  _n += v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator+=(detail::scalar_round_down_t<N, C, T> v) -> self_type & {
  _n += v._n;
  return *this;
}

/// @endcond

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
auto
operator+(Scalar<N, C, T> lhs, Scalar<S, I, T> const &rhs) -> typename std::common_type<Scalar<N, C, T>, Scalar<S, I, T>>::type {
  return typename std::common_type<Scalar<N, C, T>, Scalar<S, I, T>>::type(lhs) += rhs;
}

/** Add a two scalars of the same type.
 * @return A scalar of the same type holding the sum.
 */
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs) += rhs;
}

/// @cond INTERNAL_DETAIL
// These handle adding a wrapper and a scalar.
template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator+(detail::scalar_unit_round_up_t<I> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) += lhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, detail::scalar_unit_round_up_t<I> rhs) {
  return Scalar<N, C, T>(lhs) += rhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator+(detail::scalar_unit_round_down_t<I> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) += lhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, detail::scalar_unit_round_down_t<I> rhs) {
  return Scalar<N, C, T>(lhs) += rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(detail::scalar_round_up_t<N, C, T> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) += lhs._n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, detail::scalar_round_up_t<N, C, T> rhs) {
  return Scalar<N, C, T>(lhs) += rhs._n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(detail::scalar_round_down_t<N, C, T> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) += lhs._n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, detail::scalar_round_down_t<N, C, T> rhs) {
  return Scalar<N, C, T>(lhs) += rhs._n;
}
/// @endcond

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator-=(self_type const &that) -> self_type & {
  _n -= that._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::operator-=(Scalar<S, I, T> const &that) -> self_type & {
  using R = std::ratio<S, N>;
  static_assert(R::den == 1, "Subtraction not permitted - target scale is not an integral multiple of source scale.");
  _n -= that.count() * R::num;
  return *this;
}

/// @cond INTERNAL_DETAIL

template <intmax_t N, typename C, typename T>
template <typename I>
auto
Scalar<N, C, T>::operator-=(detail::scalar_unit_round_up_t<I> v) -> self_type & {
  _n -= v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
template <typename I>
auto
Scalar<N, C, T>::operator-=(detail::scalar_unit_round_down_t<I> v) -> self_type & {
  _n -= v.template scale<N, C>();
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator-=(detail::scalar_round_up_t<N, C, T> v) -> self_type & {
  _n -= v._n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator-=(detail::scalar_round_down_t<N, C, T> v) -> self_type & {
  _n -= v._n;
  return *this;
}

/// @endcond

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
auto
operator-(Scalar<N, C, T> lhs, Scalar<S, I, T> const &rhs) -> typename std::common_type<Scalar<N, C, T>, Scalar<S, I, T>>::type {
  return typename std::common_type<Scalar<N, C, T>, Scalar<S, I, T>>::type(lhs) -= rhs;
}

/** Subtract scalars.
 * @return A scalar of the same type holding the difference @a lhs - @a rhs
 */
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs) -= rhs;
}

/// @cond INTERNAL_DETAIL
// Handle subtraction for intermediate wrappers.
template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator-(detail::scalar_unit_round_up_t<I> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs.template scale<N, C>()) -= rhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, detail::scalar_unit_round_up_t<I> rhs) {
  return Scalar<N, C, T>(lhs) -= rhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator-(detail::scalar_unit_round_down_t<I> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs.template scale<N, C>()) -= rhs;
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, detail::scalar_unit_round_down_t<I> rhs) {
  return Scalar<N, C, T>(lhs) -= rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(detail::scalar_round_up_t<N, C, T> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs._n) -= rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, detail::scalar_round_up_t<N, C, T> rhs) {
  return Scalar<N, C, T>(lhs) -= rhs._n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(detail::scalar_round_down_t<N, C, T> lhs, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(lhs._n) -= rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, detail::scalar_round_down_t<N, C, T> rhs) {
  return Scalar<N, C, T>(lhs) -= rhs._n;
}
/// @endcond

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator++() -> self_type & {
  ++_n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator++(int) -> self_type {
  self_type zret(*this);
  ++_n;
  return zret;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator--() -> self_type & {
  --_n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator--(int) -> self_type {
  self_type zret(*this);
  --_n;
  return zret;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::inc(Counter n) -> self_type & {
  _n += n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::dec(Counter n) -> self_type & {
  _n -= n;
  return *this;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator*=(C n) -> self_type & {
  _n *= n;
  return *this;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator*(Scalar<N, C, T> const &lhs, C n) {
  return Scalar<N, C, T>(lhs) *= n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator*(C n, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) *= n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator*(Scalar<N, C, T> const &lhs, int n) {
  return Scalar<N, C, T>(lhs) *= n;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator*(int n, Scalar<N, C, T> const &rhs) {
  return Scalar<N, C, T>(rhs) *= n;
}

template <intmax_t N>
Scalar<N, int>
operator*(Scalar<N, int> const &lhs, int n) {
  return Scalar<N, int>(lhs) *= n;
}

template <intmax_t N>
Scalar<N, int>
operator*(int n, Scalar<N, int> const &rhs) {
  return Scalar<N, int>(rhs) *= n;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator/=(C n) -> self_type & {
  _n /= n;
  return *this;
}

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
auto
operator/(Scalar<N, C, T> lhs, Scalar<S, I, T> rhs) -> typename std::common_type<C, I>::type {
  using R = std::ratio<N, S>;
  return (lhs.count() * R::num) / (rhs.count() * R::den);
}

template <intmax_t N, typename C, typename T, typename I>
Scalar<N, C, T>
operator/(Scalar<N, C, T> lhs, I n) {
  static_assert(std::is_integral<I>::value, "Scalar divsion only support integral types.");
  return Scalar<N, C, T>(lhs) /= n;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator()(Counter n) const -> self_type {
  return self_type{n};
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::plus(Counter n) const -> self_type {
  return {_n + n};
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::minus(Counter n) const -> self_type {
  return {_n - n};
}

/** Explicitly round @c value up to a multiple of @a N.
 *
 * @tparam N [explicit] Rounding unit.
 * @tparam C [deduced] Base type of @a value.
 * @param value Value to round.
 * @return The smallest multiple of @a N that is at least as large as @a value.
 *
 * @code
 * int r = swoc::round_up<10>(119); // r becomes 120.
 * int r = swoc::round_up<10>(120); // r becomes 120.
 * int r = swoc::round_up<10>(121); // r becomes 130.
 * @endcode
 */
template <intmax_t N, typename C>
C
round_up(C value) {
  return N * detail::scale_conversion_round_up<N, 1>(value);
}

/** Explicitly round @a value to a multiple of @a N.
 *
 * @tparam N [explicit] Rounding unit.
 * @tparam C [deduced] Base type of @a value.
 * @param value Value to round.
 * @return The largest multiple of @a N that is not greater than @a value.
 *
 * @code
 * int r = swoc::round_down<10>(119); // r becomes 110.
 * int r = swoc::round_down<10>(120); // r becomes 120.
 * int r = swoc::round_down<10>(121); // r becomes 120.
 * @endcode
 */
template <intmax_t N, typename C>
C
round_down(C value) {
  return N * detail::scale_conversion_round_down<N, 1>(value);
}

namespace detail {
template <typename T>
auto
tag_label(std::ostream &, const meta::CaseTag<0> &) -> void {}

template <typename T>
auto
tag_label(std::ostream &w, const meta::CaseTag<1> &) -> decltype(T::label, meta::TypeFunc<void>()) {
  w << T::label;
}

template <typename T>
inline std::ostream &
tag_label(std::ostream &w) {
  tag_label<T>(w, meta::CaseArg);
  return w;
}
} // namespace detail
}} // namespace swoc::SWOC_VERSION_NS

namespace std {
/// Compute common type of two scalars.
/// This is in `std` to overload the base definition. This yields a type that has the common type of
/// the counter type and a scale that is the GCF of the input scales.
template <intmax_t N, typename C, intmax_t S, typename I, typename T>
struct common_type<swoc::Scalar<N, C, T>, swoc::Scalar<S, I, T>> {
  using R    = std::ratio<N, S>;
  using type = swoc::Scalar<N / R::num, typename common_type<C, I>::type, T>;
};

template <intmax_t N, typename C, typename T>
ostream &
operator<<(ostream &s, swoc::Scalar<N, C, T> const &x) {
  s << x.value();
  return swoc::detail::tag_label<T>(s);
}

} // namespace std
