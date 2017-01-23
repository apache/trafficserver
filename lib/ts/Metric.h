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

#if !defined(TS_METRIC_H)
#define TS_METRIC_H

#include <cstdint>
#include <ratio>

namespace ApacheTrafficServer
{
/** A class to hold scaled values.

    Instances of this class have a @a count and a @a scale. The "value" of the instance is @a
    count * @a scale.  The scale is stored in the compiler in the class symbol table and so only
    the count is a run time value. An instance with a large scale can be assign to an instance
    with a smaller scale and the conversion is done automatically. Conversions from a smaller to
    larger scale must be explicit using @c metric_round_up and @c metric_round_down. This prevents
    inadvertent changes in value. Because the scales are not the same these conversions can be
    lossy and the two conversions determine whether, in such a case, the result should be rounded
    up or down to the nearest scale value.

    @a N sets the scale. @a T is the type used to hold the count, which is in units of @a N.

    @note This is modeled somewhat on @c std::chrono and serves a similar function for different
    and simpler cases (where the ratio is always an integer, never a fraction).

    @see metric_round_up
    @see metric_round_down
 */
template <intmax_t N, typename T = int> class Metric
{
  typedef Metric self; ///< Self reference type.

public:
  /// Scaling factor for instances.
  /// Make it externally accessible.
  constexpr static intmax_t SCALE = N;
  typedef T Count; ///< Type used to hold the count.

  constexpr Metric(); ///< Default contructor.
  ///< Construct to have @a n scaled units.
  constexpr Metric(Count n);

  /// Copy constructor for same scale.
  template <typename C> Metric(Metric<N, C> const &that);

  /// Copy / conversion constructor.
  /// @note Requires that @c S be an integer multiple of @c SCALE.
  template <intmax_t S, typename I> Metric(Metric<S, I> const &that);

  /// Direct assignment.
  /// The count is set to @a n.
  self &operator=(Count n);

  /// The number of scale units.
  constexpr Count count() const;
  /// The absolute value, scaled up.
  constexpr Count units() const;

  /// Assignment operator.
  /// @note Requires the scale of @c S be an integer multiple of the scale of this.
  template <intmax_t S, typename I> self &operator=(Metric<S, I> const &that);
  /// Assignment from same scale.
  self &operator=(self const &that);

  /// Run time access to the scale of this metric (template arg @a N).
  static constexpr intmax_t scale();

protected:
  Count _n; ///< Number of scale units.
};

template <intmax_t N, typename C> constexpr Metric<N, C>::Metric() : _n()
{
}
template <intmax_t N, typename C> constexpr Metric<N, C>::Metric(Count n) : _n(n)
{
}
template <intmax_t N, typename C>
constexpr auto
Metric<N, C>::count() const -> Count
{
  return _n;
}
template <intmax_t N, typename C>
constexpr auto
Metric<N, C>::units() const -> Count
{
  return _n * SCALE;
}
template <intmax_t N, typename C>
inline auto
Metric<N, C>::operator=(Count n) -> self &
{
  _n = n;
  return *this;
}
template <intmax_t N, typename C>
inline auto
Metric<N, C>::operator=(self const &that) -> self &
{
  _n = that._n;
  return *this;
}
template <intmax_t N, typename C>
constexpr inline intmax_t
Metric<N, C>::scale()
{
  return SCALE;
}

template <intmax_t N, typename C> template <typename I> Metric<N, C>::Metric(Metric<N, I> const &that) : _n(static_cast<C>(that._n))
{
}

template <intmax_t N, typename C> template <intmax_t S, typename I> Metric<N, C>::Metric(Metric<S, I> const &that)
{
  typedef std::ratio<S, N> R;
  static_assert(R::den == 1, "Construction not permitted - target scale is not an integral multiple of source scale.");
  _n = that.count() * R::num;
}

template <intmax_t N, typename C>
template <intmax_t S, typename I>
auto
Metric<N, C>::operator=(Metric<S, I> const &that) -> self &
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
    typedef Metric<16> Paragraphs;
    typedef Metric<1024> KiloBytes;

    Paragraphs src(37459);
    auto size = metric_round_up<KiloBytes>(src); // size.count() == 586
    @endcode
 */
template <typename M, intmax_t S, typename I>
M
metric_round_up(Metric<S, I> const &src)
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
    typedef Metric<16> Paragraphs;
    typedef Metric<1024> KiloBytes;

    Paragraphs src(37459);
    auto size = metric_round_up<KiloBytes>(src); // size.count() == 585
    @endcode
 */
template <typename M, intmax_t S, typename I>
M
metric_round_down(Metric<S, I> const &src)
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

/// Convert a unit value @a n to a Metric, rounding down.
template <typename M>
M
metric_round_down(intmax_t n)
{
  return n / M::SCALE; // assuming compiler will optimize out dividing by 1 if needed.
}

/// Convert a unit value @a n to a Metric, rounding up.
template <typename M>
M
metric_round_up(intmax_t n)
{
  return M::SCALE == 1 ? n : (n / M::SCALE + (0 != (n % M::SCALE)));
}

// --- Compare operators

// Try for a bit of performance boost - if the metrics have the same scale
// just comparing the counts is sufficient and scaling conversion is avoided.
template <intmax_t N, typename C1, typename C2>
bool
operator<(Metric<N, C1> const &lhs, Metric<N, C2> const &rhs)
{
  return lhs.count() < rhs.count();
}

template <intmax_t N, typename C>
bool
operator<(Metric<N, C> const &lhs, C n)
{
  return lhs.count() < n;
}

template <intmax_t N, typename C>
bool
operator<(C n, Metric<N, C> const &rhs)
{
  return n < rhs.count();
}

template <intmax_t N, typename C1, typename C2>
bool
operator==(Metric<N, C1> const &lhs, Metric<N, C2> const &rhs)
{
  return lhs.count() == rhs.count();
}

template <intmax_t N, typename C>
bool
operator==(Metric<N, C> const &lhs, C n)
{
  return lhs.count() == n;
}

template <intmax_t N, typename C>
bool
operator==(C n, Metric<N, C> const &rhs)
{
  return n == rhs.count();
}

// Could be derived but if we're optimizing let's avoid the extra negation.
// Or we could check if the compiler can optimize that out anyway.
template <intmax_t N, typename C1, typename C2>
bool
operator<=(Metric<N, C1> const &lhs, Metric<N, C2> const &rhs)
{
  return lhs.count() <= rhs.count();
}

template <intmax_t N, typename C>
bool
operator<=(Metric<N, C> const &lhs, C n)
{
  return lhs.count() <= n;
}

template <intmax_t N, typename C>
bool
operator<=(C n, Metric<N, C> const &rhs)
{
  return n <= rhs.count();
}

// Do the integer compares.

template <intmax_t N, typename C>
bool
operator>(Metric<N, C> const &lhs, C n)
{
  return lhs.count() > n;
}

template <intmax_t N, typename C>
bool
operator>(C n, Metric<N, C> const &rhs)
{
  return n > rhs.count();
}

template <intmax_t N, typename C>
bool
operator>=(Metric<N, C> const &lhs, C n)
{
  return lhs.count() >= n;
}

template <intmax_t N, typename C>
bool
operator>=(C n, Metric<N, C> const &rhs)
{
  return n >= rhs.count();
}

// General base cases.

template <intmax_t N1, typename C1, intmax_t N2, typename C2>
bool
operator<(Metric<N1, C1> const &lhs, Metric<N2, C2> const &rhs)
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

template <intmax_t N1, typename C1, intmax_t N2, typename C2>
bool
operator==(Metric<N1, C1> const &lhs, Metric<N2, C2> const &rhs)
{
  typedef std::ratio<N1, N2> R;
  if (R::den == 1) {
    return lhs.count() == rhs.count() * R::num;
  } else if (R::num == 1) {
    return lhs.count() * R::den == rhs.count();
  } else
    return lhs.units() == rhs.units();
}

template <intmax_t N1, typename C1, intmax_t N2, typename C2>
bool
operator<=(Metric<N1, C1> const &lhs, Metric<N2, C2> const &rhs)
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

template <intmax_t N1, typename C1, intmax_t N2, typename C2>
bool
operator>(Metric<N1, C1> const &lhs, Metric<N2, C2> const &rhs)
{
  return rhs < lhs;
}

template <intmax_t N1, typename C1, intmax_t N2, typename C2>
bool
operator>=(Metric<N1, C1> const &lhs, Metric<N2, C2> const &rhs)
{
  return rhs <= lhs;
}
}

#endif // TS_METRIC_H
