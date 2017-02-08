/** @file

  Scaled integral values.

  In many situations it is desirable to define scaling factors or base units (a "metric"). This template
  enables this to be done in a type and scaling safe manner where the defined factors carry their scaling
  information as part of the type.

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

#if !defined(TS_SCALAR_H)
#define TS_SCALAR_H

#include <cstdint>
#include <ratio>
#include <ostream>

namespace tag
{
struct generic;
}

namespace ApacheTrafficServer
{
template <intmax_t N, typename C, typename T> class Scalar;

namespace detail
{
  // Internal class to deal with operator overload issues.
  // Because the type of integers with no explicit type is (int) that type is special in terms of overloads.
  // To be convienet @c Scalar should support operators for its internal declared counter type and (int).
  // This creates ambiguous overloads when C is (int). This class lets the (int) overloads be moved to a super
  // class so conflict causes overridding rather than ambiguity.
  template <intmax_t N, typename C, typename T> struct ScalarArithmetics {
    typedef ApacheTrafficServer::Scalar<N, C, T> S;
    S &operator+=(int);
    S &operator-=(int);
    S &operator*=(int);
    S &operator/=(int);

  protected:
    // Only let subclasses construct, as this class only makes sense as an abstract superclass.
    ScalarArithmetics();
  };

  template <intmax_t N, typename C, typename T> ScalarArithmetics<N, C, T>::ScalarArithmetics() {}
  template <intmax_t N, typename C, typename T>
  auto
  ScalarArithmetics<N, C, T>::operator+=(int n) -> S &
  {
    return static_cast<S *>(this).operator+=(static_cast<C>(n));
  }
  template <intmax_t N, typename C, typename T>
  auto
  ScalarArithmetics<N, C, T>::operator-=(int n) -> S &
  {
    return static_cast<S *>(this).operator-=(static_cast<C>(n));
  }
  template <intmax_t N, typename C, typename T>
  auto
  ScalarArithmetics<N, C, T>::operator*=(int n) -> S &
  {
    return static_cast<S *>(this).operator*=(static_cast<C>(n));
  }
  template <intmax_t N, typename C, typename T>
  auto
  ScalarArithmetics<N, C, T>::operator/=(int n) -> S &
  {
    return static_cast<S *>(this).operator/=(static_cast<C>(n));
  }
}

/** A class to hold scaled values.

    Instances of this class have a @a count and a @a scale. The "value" of the instance is @a
    count * @a scale.  The scale is stored in the compiler in the class symbol table and so only
    the count is a run time value. An instance with a large scale can be assign to an instance
    with a smaller scale and the conversion is done automatically. Conversions from a smaller to
    larger scale must be explicit using @c scaled_up and @c scaled_down. This prevents
    inadvertent changes in value. Because the scales are not the same these conversions can be
    lossy and the two conversions determine whether, in such a case, the result should be rounded
    up or down to the nearest scale value.

    @a N sets the scale. @a C is the type used to hold the count, which is in units of @a N.

    @a T is a "tag" type which is used only to distinguish the base metric for the scale. Scalar
    types that have different tags are not interoperable although they can be converted manually by
    converting to units and then explicitly constructing a new Scalar instance. This is by
    design. This can be ignored - if not specified then it defaults to a "generic" tag. The type can
    be (and usually is) defined in name only).

    @note This is modeled somewhat on @c std::chrono and serves a similar function for different
    and simpler cases (where the ratio is always an integer, never a fraction).

    @see scaled_up
    @see scaled_down
 */
template <intmax_t N, typename C = int, typename T = tag::generic> class Scalar : public detail::ScalarArithmetics<N, C, T>
{
  typedef Scalar self; ///< Self reference type.

public:
  /// Scaling factor for instances.
  /// Make it externally accessible.
  constexpr static intmax_t SCALE = N;
  typedef C Count; ///< Type used to hold the count.
  typedef T Tag;   ///< Make tag accessible.

  constexpr Scalar(); ///< Default contructor.
  ///< Construct to have @a n scaled units.
  constexpr Scalar(Count n);

  /// Copy constructor for same scale.
  template <typename I> Scalar(Scalar<N, I, T> const &that);

  /// Copy / conversion constructor.
  /// @note Requires that @c S be an integer multiple of @c SCALE.
  template <intmax_t S, typename I> Scalar(Scalar<S, I, T> const &that);

  /// Direct assignment.
  /// The count is set to @a n.
  self &operator=(Count n);

  /// The number of scale units.
  constexpr Count count() const;
  /// The absolute value, scaled up.
  constexpr Count units() const;

  /// Assignment operator.
  /// The value is scaled appropriately.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this isn't the case then
  /// the @c scale_up or @c scale_down casts must be used to indicate the rounding direction.
  template <intmax_t S, typename I> self &operator=(Scalar<S, I, T> const &that);
  /// Assignment from same scale.
  self &operator=(self const &that);

  /// Addition operator.
  /// The value is scaled from @a that to @a this.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this isn't the case then
  /// the @c scale_up or @c scale_down casts must be used to indicate the rounding direction.
  template <intmax_t S, typename I> self &operator+=(Scalar<S, I, T> const &that);
  /// Addition - add @a n as a number of scaled units.
  self &operator+=(C n);
  /// Addition - add @a n as a number of scaled units.
  self &operator+=(self const &that);

  /// Increment - increase count by 1.
  self &operator++();
  /// Increment - increase count by 1.
  self operator++(int);
  /// Decrement - decrease count by 1.
  self &operator--();
  /// Decrement - decrease count by 1.
  self operator--(int);

  /// Subtraction operator.
  /// The value is scaled from @a that to @a this.
  /// @note Requires the scale of @a that be an integer multiple of the scale of @a this. If this isn't the case then
  /// the @c scale_up or @c scale_down casts must be used to indicate the rounding direction.
  template <intmax_t S, typename I> self &operator-=(Scalar<S, I, T> const &that);
  /// Subtraction - subtract @a n as a number of scaled units.
  self &operator-=(C n);
  /// Subtraction - subtract @a n as a number of scaled units.
  self &operator-=(self const &that);

  /// Multiplication - multiple the count by @a n.
  self &operator*=(C n);

  /// Division - divide (rounding down) the count by @a n.
  self &operator/=(C n);

  /// Scale value @a x to this type, rounding up.
  template <intmax_t S, typename I> self scale_up(Scalar<S, I, T> const &x);

  /// Scale value @a x to this type, rounding down.
  template <intmax_t S, typename I> self scale_down(Scalar<S, I, T> const &x);

  /// Run time access to the scale (template arg @a N).
  static constexpr intmax_t scale();

protected:
  Count _n; ///< Number of scale units.
};

template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::Scalar() : _n()
{
}
template <intmax_t N, typename C, typename T> constexpr Scalar<N, C, T>::Scalar(Count n) : _n(n)
{
}
template <intmax_t N, typename C, typename T>
constexpr auto
Scalar<N, C, T>::count() const -> Count
{
  return _n;
}
template <intmax_t N, typename C, typename T>
constexpr auto
Scalar<N, C, T>::units() const -> Count
{
  return _n * SCALE;
}
template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::operator=(Count n) -> self &
{
  _n = n;
  return *this;
}
template <intmax_t N, typename C, typename T>
inline auto
Scalar<N, C, T>::operator=(self const &that) -> self &
{
  _n = that._n;
  return *this;
}
template <intmax_t N, typename C, typename T>
constexpr inline intmax_t
Scalar<N, C, T>::scale()
{
  return SCALE;
}

template <intmax_t N, typename C, typename T>
template <typename I>
Scalar<N, C, T>::Scalar(Scalar<N, I, T> const &that) : _n(static_cast<C>(that.count()))
{
}

template <intmax_t N, typename C, typename T> template <intmax_t S, typename I> Scalar<N, C, T>::Scalar(Scalar<S, I, T> const &that)
{
  typedef std::ratio<S, N> R;
  static_assert(R::den == 1, "Construction not permitted - target scale is not an integral multiple of source scale.");
  _n = that.count() * R::num;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::operator=(Scalar<S, I, T> const &that) -> self &
{
  typedef std::ratio<S, N> R;
  static_assert(R::den == 1, "Assignment not permitted - target scale is not an integral multiple of source scale.");
  _n = that.count() * R::num;
  return *this;
}

// -- Free Functions --

/** Convert a metric @a src to a different scale, keeping the unit value as close as possible, rounding up.
    The resulting count in the return value will be the smallest count that is not smaller than the unit
    value of @a src.

    @code
    typedef Scalar<16> Paragraphs;
    typedef Scalar<1024> KiloBytes;

    Paragraphs src(37459);
    auto size = scale_up<KiloBytes>(src); // size.count() == 586
    @endcode
 */
template <typename M, intmax_t S, typename I>
M
scale_up(Scalar<S, I, typename M::Tag> const &src)
{
  typedef std::ratio<M::SCALE, S> R;
  auto c = src.count();

  if (M::SCALE == S) {
    return c;
  } else if (R::den == 1) {
    return c / R::num + (0 != c % R::num); // N is a multiple of S.
  } else if (R::num == 1) {
    return c * R::den; // S is a multiple of N.
  } else {
    return (c / R::num) * R::den + ((c % R::num) * R::den) / R::num + (0 != (c % R::num));
  }
}

/** Convert a metric @a src to a different scale, keeping the unit value as close as possible, rounding down.
    The resulting count in the return value will be the largest count that is not larger than the unit
    value of @a src.

    @code
    typedef Scalar<16> Paragraphs;
    typedef Scalar<1024> KiloBytes;

    Paragraphs src(37459);
    auto size = scale_up<KiloBytes>(src); // size.count() == 585
    @endcode
 */
template <typename M, intmax_t S, typename I>
M
scale_down(Scalar<S, I, typename M::Tag> const &src)
{
  typedef std::ratio<M::SCALE, S> R;
  auto c = src.count();

  if (R::den == 1) {
    return c / R::num; // S is a multiple of N.
  } else if (R::num == 1) {
    return c * R::den; // N is a multiple of S.
  } else {
    // General case where neither N nor S are a multiple of the other.
    // Yes, a bit odd, but this minimizes the risk of integer overflow.
    // I need to validate that under -O2 the compiler will only do 1 division to get
    // both the quotient and remainder for (n/N) and (n%N). In cases where N,S are
    // powers of 2 I have verified recent GNU compilers will optimize to bit operations.
    return (c / R::num) * R::den + ((c % R::num) * R::den) / R::num;
  }
}

/// Convert a unit value @a n to a Scalar, rounding down.
template <typename M>
M
scale_down(intmax_t n)
{
  return n / M::SCALE; // assuming compiler will optimize out dividing by 1 if needed.
}

/// Convert a unit value @a n to a Scalar, rounding up.
template <typename M>
M
scale_up(intmax_t n)
{
  return M::SCALE == 1 ? n : (n / M::SCALE + (0 != (n % M::SCALE)));
}

// --- Compare operators

// Try for a bit of performance boost - if the metrics have the same scale
// just comparing the counts is sufficient and scaling conversion is avoided.
template <intmax_t N, typename C1, typename C2, typename T>
bool
operator<(Scalar<N, C1, T> const &lhs, Scalar<N, C2, T> const &rhs)
{
  return lhs.count() < rhs.count();
}

template <intmax_t N, typename C1, typename C2, typename T>
bool
operator==(Scalar<N, C1, T> const &lhs, Scalar<N, C2, T> const &rhs)
{
  return lhs.count() == rhs.count();
}

// Could be derived but if we're optimizing let's avoid the extra negation.
// Or we could check if the compiler can optimize that out anyway.
template <intmax_t N, typename C1, typename C2, typename T>
bool
operator<=(Scalar<N, C1, T> const &lhs, Scalar<N, C2, T> const &rhs)
{
  return lhs.count() <= rhs.count();
}

// General base cases.

template <intmax_t N1, typename C1, intmax_t N2, typename C2, typename T>
bool
operator<(Scalar<N1, C1, T> const &lhs, Scalar<N2, C2, T> const &rhs)
{
  typedef std::ratio<N1, N2> R;
  // Based on tests with the GNU compiler, the fact that the conditionals are compile time
  // constant causes the never taken paths to be dropped so there are no runtime conditional
  // checks, even with no optimization at all.
  if (R::den == 1) {
    return lhs.count() < rhs.count() * R::num;
  } else if (R::num == 1) {
    return lhs.count() * R::den < rhs.count();
  } else
    return lhs.units() < rhs.units();
}

template <intmax_t N1, typename C1, intmax_t N2, typename C2, typename T>
bool
operator==(Scalar<N1, C1, T> const &lhs, Scalar<N2, C2, T> const &rhs)
{
  typedef std::ratio<N1, N2> R;
  if (R::den == 1) {
    return lhs.count() == rhs.count() * R::num;
  } else if (R::num == 1) {
    return lhs.count() * R::den == rhs.count();
  } else
    return lhs.units() == rhs.units();
}

template <intmax_t N1, typename C1, intmax_t N2, typename C2, typename T>
bool
operator<=(Scalar<N1, C1, T> const &lhs, Scalar<N2, C2, T> const &rhs)
{
  typedef std::ratio<N1, N2> R;
  if (R::den == 1) {
    return lhs.count() <= rhs.count() * R::num;
  } else if (R::num == 1) {
    return lhs.count() * R::den <= rhs.count();
  } else
    return lhs.units() <= rhs.units();
}

// Derived compares. No narrowing optimization needed because if the scales
// are the same the nested call with be optimized.

template <intmax_t N1, typename C1, intmax_t N2, typename C2, typename T>
bool
operator>(Scalar<N1, C1, T> const &lhs, Scalar<N2, C2, T> const &rhs)
{
  return rhs < lhs;
}

template <intmax_t N1, typename C1, intmax_t N2, typename C2, typename T>
bool
operator>=(Scalar<N1, C1, T> const &lhs, Scalar<N2, C2, T> const &rhs)
{
  return rhs <= lhs;
}

// Do the integer compares.
// A bit ugly to handle the issue that integers without explicit type are 'int'. Therefore suppport must be provided
// for comparison not just the counter type C but also explicitly 'int'. That makes the operators ambiguous if C is
// 'int'. The specializations for 'int' resolve this as their presence "covers" the generic cases.

template <intmax_t N, typename C, typename T>
bool
operator<(Scalar<N, C, T> const &lhs, C n)
{
  return lhs.count() < n;
}
template <intmax_t N, typename C, typename T>
bool
operator<(C n, Scalar<N, C, T> const &rhs)
{
  return n < rhs.count();
}
template <intmax_t N, typename C, typename T>
bool
operator<(Scalar<N, C, T> const &lhs, int n)
{
  return lhs.count() < static_cast<C>(n);
}
template <intmax_t N, typename C, typename T>
bool
operator<(int n, Scalar<N, C, T> const &rhs)
{
  return static_cast<C>(n) < rhs.count();
}
template <intmax_t N>
bool
operator<(Scalar<N, int> const &lhs, int n)
{
  return lhs.count() < n;
}
template <intmax_t N>
bool
operator<(int n, Scalar<N, int> const &rhs)
{
  return n < rhs.count();
}

template <intmax_t N, typename C, typename T>
bool
operator==(Scalar<N, C, T> const &lhs, C n)
{
  return lhs.count() == n;
}
template <intmax_t N, typename C, typename T>
bool
operator==(C n, Scalar<N, C, T> const &rhs)
{
  return n == rhs.count();
}
template <intmax_t N, typename C, typename T>
bool
operator==(Scalar<N, C, T> const &lhs, int n)
{
  return lhs.count() == static_cast<C>(n);
}
template <intmax_t N, typename C, typename T>
bool
operator==(int n, Scalar<N, C, T> const &rhs)
{
  return static_cast<C>(n) == rhs.count();
}
template <intmax_t N>
bool
operator==(Scalar<N, int> const &lhs, int n)
{
  return lhs.count() == n;
}
template <intmax_t N>
bool
operator==(int n, Scalar<N, int> const &rhs)
{
  return n == rhs.count();
}

template <intmax_t N, typename C, typename T>
bool
operator>(Scalar<N, C, T> const &lhs, C n)
{
  return lhs.count() > n;
}
template <intmax_t N, typename C, typename T>
bool
operator>(C n, Scalar<N, C, T> const &rhs)
{
  return n > rhs.count();
}
template <intmax_t N, typename C, typename T>
bool
operator>(Scalar<N, C, T> const &lhs, int n)
{
  return lhs.count() > static_cast<C>(n);
}
template <intmax_t N, typename C, typename T>
bool
operator>(int n, Scalar<N, C, T> const &rhs)
{
  return static_cast<C>(n) > rhs.count();
}
template <intmax_t N>
bool
operator>(Scalar<N, int> const &lhs, int n)
{
  return lhs.count() > n;
}
template <intmax_t N>
bool
operator>(int n, Scalar<N, int> const &rhs)
{
  return n > rhs.count();
}

template <intmax_t N, typename C, typename T>
bool
operator<=(Scalar<N, C, T> const &lhs, C n)
{
  return lhs.count() <= n;
}
template <intmax_t N, typename C, typename T>
bool
operator<=(C n, Scalar<N, C, T> const &rhs)
{
  return n <= rhs.count();
}
template <intmax_t N, typename C, typename T>
bool
operator<=(Scalar<N, C, T> const &lhs, int n)
{
  return lhs.count() <= static_cast<C>(n);
}
template <intmax_t N, typename C, typename T>
bool
operator<=(int n, Scalar<N, C, T> const &rhs)
{
  return static_cast<C>(n) <= rhs.count();
}
template <intmax_t N>
bool
operator<=(Scalar<N, int> const &lhs, int n)
{
  return lhs.count() <= n;
}
template <intmax_t N>
bool
operator<=(int n, Scalar<N, int> const &rhs)
{
  return n <= rhs.count();
}

template <intmax_t N, typename C, typename T>
bool
operator>=(Scalar<N, C, T> const &lhs, C n)
{
  return lhs.count() >= n;
}
template <intmax_t N, typename C, typename T>
bool
operator>=(C n, Scalar<N, C, T> const &rhs)
{
  return n >= rhs.count();
}
template <intmax_t N, typename C, typename T>
bool
operator>=(Scalar<N, C, T> const &lhs, int n)
{
  return lhs.count() >= static_cast<C>(n);
}
template <intmax_t N, typename C, typename T>
bool
operator>=(int n, Scalar<N, C, T> const &rhs)
{
  return static_cast<C>(n) >= rhs.count();
}
template <intmax_t N>
bool
operator>=(Scalar<N, int> const &lhs, int n)
{
  return lhs.count() >= n;
}
template <intmax_t N>
bool
operator>=(int n, Scalar<N, int> const &rhs)
{
  return n >= rhs.count();
}

// Arithmetic operators
template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
  Scalar<N, C, T>::operator+=(Scalar<S, I, T> const &that) -> self &
{
  typedef std::ratio<S, N> R;
  static_assert(R::den == 1, "Addition not permitted - target scale is not an integral multiple of source scale.");
  _n += that.count() * R::num;
  return *this;
}
template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator+=(self const &that) -> self &
{
  _n += that._n;
  return *this;
}
template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator+=(C n) -> self &
{
  _n += n;
  return *this;
}

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
auto
operator + (Scalar<N,C,T> lhs, Scalar<S, I, T> const &rhs) -> typename std::common_type<Scalar<N,C,T>,Scalar<S,I,T>>::type
{
  return typename std::common_type<Scalar<N,C,T>,Scalar<S,I,T>>::type(lhs) += rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(lhs) += rhs;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, C n)
{
  return Scalar<N, C, T>(lhs) += n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(C n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) += n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(Scalar<N, C, T> const &lhs, int n)
{
  return Scalar<N, C, T>(lhs) += n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator+(int n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) += n;
}
template <intmax_t N>
Scalar<N, int>
operator+(Scalar<N, int> const &lhs, int n)
{
  return Scalar<N, int>(lhs) += n;
}
template <intmax_t N>
Scalar<N, int>
operator+(int n, Scalar<N, int> const &rhs)
{
  return Scalar<N, int>(rhs) += n;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::operator-=(Scalar<S, I, T> const &that) -> self &
{
  typedef std::ratio<S, N> R;
  static_assert(R::den == 1, "Subtraction not permitted - target scale is not an integral multiple of source scale.");
  _n -= that.count() * R::num;
  return *this;
}
template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator-=(self const &that) -> self &
{
  _n -= that._n;
  return *this;
}
template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator-=(C n) -> self &
{
  _n -= n;
  return *this;
}

template <intmax_t N, typename C, intmax_t S, typename I, typename T>
auto
operator - (Scalar<N,C,T> lhs, Scalar<S, I, T> const &rhs) -> typename std::common_type<Scalar<N,C,T>,Scalar<S,I,T>>::type
{
  return typename std::common_type<Scalar<N,C,T>,Scalar<S,I,T>>::type(lhs) -= rhs;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(lhs) -= rhs;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, C n)
{
  return Scalar<N, C, T>(lhs) -= n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(C n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) -= n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(Scalar<N, C, T> const &lhs, int n)
{
  return Scalar<N, C, T>(lhs) -= n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator-(int n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) -= n;
}
template <intmax_t N>
Scalar<N, int>
operator-(Scalar<N, int> const &lhs, int n)
{
  return Scalar<N, int>(lhs) -= n;
}
template <intmax_t N>
Scalar<N, int>
operator-(int n, Scalar<N, int> const &rhs)
{
  return Scalar<N, int>(rhs) -= n;
}

template <intmax_t N, typename C, typename T> auto Scalar<N, C, T>::operator++() -> self &
{
  ++_n;
  return *this;
}

template <intmax_t N, typename C, typename T> auto Scalar<N, C, T>::operator++(int) -> self
{
  self zret(*this);
  ++_n;
  return zret;
}

template <intmax_t N, typename C, typename T> auto Scalar<N, C, T>::operator--() -> self &
{
  --_n;
  return *this;
}

template <intmax_t N, typename C, typename T> auto Scalar<N, C, T>::operator--(int) -> self
{
  self zret(*this);
  --_n;
  return zret;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator*=(C n) -> self &
{
  _n *= n;
  return *this;
}

template <intmax_t N, typename C, typename T> Scalar<N, C, T> operator*(Scalar<N, C, T> const &lhs, C n)
{
  return Scalar<N, C, T>(lhs) *= n;
}
template <intmax_t N, typename C, typename T> Scalar<N, C, T> operator*(C n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) *= n;
}
template <intmax_t N, typename C, typename T> Scalar<N, C, T> operator*(Scalar<N, C, T> const &lhs, int n)
{
  return Scalar<N, C, T>(lhs) *= n;
}
template <intmax_t N, typename C, typename T> Scalar<N, C, T> operator*(int n, Scalar<N, C, T> const &rhs)
{
  return Scalar<N, C, T>(rhs) *= n;
}
template <intmax_t N> Scalar<N, int> operator*(Scalar<N, int> const &lhs, int n)
{
  return Scalar<N, int>(lhs) *= n;
}
template <intmax_t N> Scalar<N, int> operator*(int n, Scalar<N, int> const &rhs)
{
  return Scalar<N, int>(rhs) *= n;
}

template <intmax_t N, typename C, typename T>
auto
Scalar<N, C, T>::operator/=(C n) -> self &
{
  _n /= n;
  return *this;
}

template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator/(Scalar<N, C, T> const &lhs, C n)
{
  return Scalar<N, C, T>(lhs) /= n;
}
template <intmax_t N, typename C, typename T>
Scalar<N, C, T>
operator/(Scalar<N, C, T> const &lhs, int n)
{
  return Scalar<N, C, T>(lhs) /= n;
}
template <intmax_t N>
Scalar<N, int>
operator/(Scalar<N, int> const &lhs, int n)
{
  return Scalar<N, int>(lhs) /= n;
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::scale_up(Scalar<S, I, T> const &that) -> self
{
  return ApacheTrafficServer::scale_up<self>(that);
}

template <intmax_t N, typename C, typename T>
template <intmax_t S, typename I>
auto
Scalar<N, C, T>::scale_down(Scalar<S, I, T> const &that) -> self
{
  return ApacheTrafficServer::scale_down<self>(that);
}

namespace detail
{
  // These classes exist only to create distinguishable overloads.
  struct tag_label_A {
  };
  struct tag_label_B : public tag_label_A {
  };
  // The purpose is to print a label for a tagged type only if the tag class defines a member that
  // is the label.  This creates a base function that always works and does nothing. The second
  // function creates an overload if the tag class has a member named 'label' that has an stream IO
  // output operator. When invoked with a second argument of B then the second overload exists and
  // is used, otherwise only the first exists and that is used. The critical technology is the use
  // of 'auto' and 'decltype' which effectively checks if the code inside 'decltype' compiles.
  template <typename T>
  inline std::ostream &
  tag_label(std::ostream &s, tag_label_A const &)
  {
    return s;
  }
  template <typename T>
  inline auto
  tag_label(std::ostream &s, tag_label_B const &) -> decltype(s << T::label, s)
  {
    return s << T::label;
  }
} // detail

} // namespace

namespace std
{
template <intmax_t N, typename C, typename T>
ostream &
operator<<(ostream &s, ApacheTrafficServer::Scalar<N, C, T> const &x)
{
  static ApacheTrafficServer::detail::tag_label_B const b;
  s << x.units();
  return ApacheTrafficServer::detail::tag_label<T>(s, b);
}


/// Compute common type of two scalars.
/// In `std` to overload the base definition. This yields a type that has the common type of the
/// counter type and a scale that is the GCF of the input scales.
template < intmax_t N, typename C, intmax_t S, typename I, typename T >
struct common_type<ApacheTrafficServer::Scalar<N,C,T>, ApacheTrafficServer::Scalar<S,I,T>>
{
  typedef std::ratio<N, S> R;
  typedef ApacheTrafficServer::Scalar<N/R::num, typename common_type<C,I>::type, T> type;
};
}
#endif // TS_SCALAR_H
