/** @file

   Given a class T with a compare function that returns ordering information, create the standard
   comparison operators.

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#pragma once

#include <type_traits>
#include <utility>
#include "tscpp/util/ts_meta.h"

namespace ts
{
/** This is a manual override for comparison.
 *
 * @tparam T Left hand side operand type.
 * @tparam U Right hand side operand type.
 *
 * Specialize this for @a T and @a U if the normal mechanisms yield bad results. This has the highest
 * priority for selecting a compare function.
 */
template <typename T, typename U = T> struct ComparablePolicy {
};

namespace detail
{
  // This is an ordered set of ways to call the compare function for two types, @a T and @a U.
  // One of these is called only if the operator overload detects at least one operand that
  // is a @c Comparable. Therefore no checks for appropriate types are needed, and this sequence
  // only probes the target types for supported free functions and methods.

  /// Use @c ComparePolicy if it has been specialized to @a T and @a U.
  template <typename T, typename U>
  auto
  ComparableFunction(T const &lhs, U const &rhs, meta::CaseTag<4> const &) -> decltype(ComparablePolicy<T, U>()(lhs, rhs), int())
  {
    return static_cast<int>(ComparablePolicy<T, U>()(lhs, rhs));
  }

  /// Use the free function @c cmp(T,U).
  template <typename T, typename U>
  auto
  ComparableFunction(T const &lhs, U const &rhs, meta::CaseTag<3> const &) -> decltype(cmp(lhs, rhs), int())
  {
    return static_cast<int>(cmp(lhs, rhs));
  }

  /// Use the free function @c cmp(U,T) and flip the result.
  template <typename T, typename U>
  auto
  ComparableFunction(T const &lhs, U const &rhs, meta::CaseTag<2> const &) -> decltype(cmp(rhs, lhs), int())
  {
    return static_cast<int>(cmp(rhs, lhs)) * -1;
  }

  /// Use the @c T::cmp method.
  template <typename T, typename U>
  auto
  ComparableFunction(T const &lhs, U const &rhs, meta::CaseTag<1> const &) -> decltype(lhs.cmp(rhs), int())
  {
    return static_cast<int>(lhs.cmp(rhs));
  }

  /// Use @c U::cmp and flip the result.
  template <typename T, typename U>
  auto
  ComparableFunction(T const &lhs, U const &rhs, meta::CaseTag<0> const &) -> decltype(rhs.cmp(lhs), int())
  {
    return static_cast<int>(rhs.cmp(lhs)) * -1;
  }

} // namespace detail

/** Create standard comparison operators given a compare function.
 *
 * The operators supported are @c == @c != @c \< @c \> @c \<= @c \>=
 *
 * To successfully use this mixin, there are two requirements.
 * - There must be a ternary comparison that returns an int in the standard ternary compare style.
 * - The class must inherit from this mixin.
 *
 * The standard ternary compare must return an @c int which is
 * - negative if @a lhs is smaller than @a rhs
 * - 0 if @a lhs is equal to @a rhs
 * - positive if @a lhs is greater than @a rhs
 *
 * If a comparison operator is used and at least one of the operands inherits from @c Comparable
 * then the types are probed for ternary comparisons. The order is
 *
 * - @c ts::ComparablePolicy<lhs,rhs> specialization
 * - function @c cmp(lhs,rhs)
 * - function @c cmp(rhs,lhs)
 * - method @c lhs::cmp(rhs)
 * - method @c rhs::cmp(lhs)
 *
 * Once a class inherits from this mixin, then comparisons are supported against any type for
 * which a ternary compare can be found.
 *
 * @code
 * class T : public ts::Comparable
 * @endcode
 *
 * To provide self comparison operators, it would suffice to have
 *
 * @code
 *   int cmp(T const& that) const;
 * @endcode
 *
 * If comparison operators against @c std::string_view should supported, this could be done by adding
 *
 * @code
 *   int cmp(std::string_view that) { return strcmp(text, that); }
 * @endcode
 *
 * If both of these are present, then the following are now valid
 * @code
 * T t, t1, t2;
 * if ("walt"sv == t) {...}
 * if ("walt" == t) {...} // because string literals convert to string_view
 * if (t1 < t2) {...}
 * @endcode
 */
struct Comparable {
};

// ---
// For each comparison operator, a template overload is provided. These overloads are enabled for
// overload resolution iff at least one operand inherits from @c ts::Comparable. In that case the
// @c ComparableFunction machinery is engaged to probe for a ternary comparison. All of this should
// optimize away after compilation.

template <typename T, typename U>
auto
operator==(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return 0 == detail::ComparableFunction(lhs, rhs, meta::CaseArg);
}

template <typename T, typename U>
auto
operator!=(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return 0 != detail::ComparableFunction(lhs, rhs, meta::CaseArg);
}

template <typename T, typename U>
auto
operator<(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return detail::ComparableFunction(lhs, rhs, meta::CaseArg) < 0;
}

template <typename T, typename U>
auto
operator<=(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return detail::ComparableFunction(lhs, rhs, meta::CaseArg) <= 0;
}

template <typename T, typename U>
auto
operator>(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return detail::ComparableFunction(lhs, rhs, meta::CaseArg) > 0;
}

template <typename T, typename U>
auto
operator>=(T const &lhs, U const &rhs) ->
  typename std::enable_if<std::is_base_of<Comparable, T>::value || std::is_base_of<Comparable, U>::value, bool>::type
{
  return detail::ComparableFunction(lhs, rhs, meta::CaseArg) >= 0;
}

} // end namespace ts
