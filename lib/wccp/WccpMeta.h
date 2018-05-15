/** @file
    Meta programming support for WCCP.

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

#pragma once

#include <algorithm>

// Various meta programming efforts. Experimental at present.

namespace ts
{
// Some support templates so we can fail to compile if the
// compile time check fails.

// This creates the actual error, depending on whether X has a valid
// nest type Result.
template <typename X> struct TEST_RESULT {
  typedef typename X::Result type;
};

// Bool checking - a base template then specializations to succeed or
// fail.
template <bool VALUE> struct TEST_BOOL {
};
// Successful test defines Result.
template <> struct TEST_BOOL<true> {
  typedef int Result;
};
// Failing test does not define Result.
template <> struct TEST_BOOL<false> {
};

// Fail to compile if VALUE is not true.
template <bool VALUE> struct TEST_IF_TRUE : public TEST_RESULT<TEST_BOOL<VALUE>> {
};

// Helper for assigning a value to all instances in a container.
template <typename T, typename R, typename A1> struct TsAssignMember : public std::binary_function<T, A1, R> {
  R T::*_m;
  A1 _arg1;
  TsAssignMember(R T::*m, A1 const &arg1) : _m(m), _arg1(arg1) {}
  R
  operator()(T &t) const
  {
    return t.*_m = _arg1;
  }
};

// Helper function to compute types for TsAssignMember.
template <typename T, typename R, typename A1>
struct TsAssignMember<T, R, A1>
assign_member(R T::*m, A1 const &arg1) {
  return TsAssignMember<T, R, A1>(m, arg1);
}

// Overload for_each to operate on a container.
template <typename C, typename F>
void
for_each(C &c, F const &f)
{
  std::for_each(c.begin(), c.end(), f);
}

/** Calc minimal value over a direct type container.
    This handles an accessor that takes a argument.
*/
template <typename C, typename V, typename ARG1>
V
minima(C const &c, V (C::value_type::*ex)(ARG1) const, ARG1 const &arg1)
{
  V v = std::numeric_limits<V>::max();
  for (typename C::const_iterator spot = c.begin(), limit = c.end(); spot != limit; ++spot) {
    v = std::min(v, ((*spot).*ex)(arg1));
  }
  return v;
}

/** Calc minimal value over a paired type container.
    This handles an accessor that takes a argument.
*/
template <typename C, typename V, typename ARG1>
V
minima(C const &c, V (C::value_type::second_type::*ex)(ARG1) const, ARG1 const &arg1)
{
  V v = std::numeric_limits<V>::max();
  for (typename C::const_iterator spot = c.begin(), limit = c.end(); spot != limit; ++spot) {
    v = std::min(v, ((spot->second).*ex)(arg1));
  }
  return v;
}

/** Apply a unary method to every object in a direct container.
 */
template <typename C, typename V, typename ARG1>
void
for_each(C &c, V (C::value_type::*ex)(ARG1), ARG1 const &arg1)
{
  for (typename C::iterator spot = c.begin(), limit = c.end(); spot != limit; ++spot)
    ((*spot).*ex)(arg1);
}

/** Apply a unary method to every object in a paired container.
 */
template <typename C, typename V, typename ARG1>
void
for_each(C &c, V (C::value_type::second_type::*ex)(ARG1) const, ARG1 const &arg1)
{
  for (typename C::iterator spot = c.begin(), limit = c.end(); spot != limit; ++spot)
    ((spot->second).*ex)(arg1);
}

template <typename Elt,  ///< Element type.
          typename Value ///< Member value type.
          >
struct MemberPredicate {
  Value const &m_value; ///< Value to test against.
  Value Elt::*m_mptr;   ///< Pointer to member to test.
  MemberPredicate(Value Elt::*mptr, Value const &v) : m_value(v), m_mptr(mptr) {}
  bool
  operator()(Elt const &elt) const
  {
    return elt.*m_mptr == m_value;
  }
};

template <typename T, typename V>
MemberPredicate<T, V>
predicate(V T::*m, V const &v)
{
  return MemberPredicate<T, V>(m, v);
}

template <typename Elt,  ///< Element type.
          typename Value ///< Member value type.
          >
struct MethodPredicate {
  typedef Value (Elt::*MethodPtr)() const;
  Value const &m_value; ///< Value to test against.
  MethodPtr m_mptr;     ///< Pointer to method returning value.
  MethodPredicate(MethodPtr mptr, Value const &v) : m_value(v), m_mptr(mptr) {}
  bool
  operator()(Elt const &elt) const
  {
    return (elt.*m_mptr)() == m_value;
  }
};

template <typename T, typename V>
MethodPredicate<T, V>
predicate(V (T::*m)() const, V const &v)
{
  return MethodPredicate<T, V>(m, v);
}

#if 0

/// Accumulate a minimum value when called repeated on different objects.
template <
  typename V, ///< Value type.
  typename T, ///< Object type
  typename F ///< Extractor type.
> struct MinimaFunctor :  public std::unary_function<T, void> {
  V& m_result; ///< Result value.
  F m_extractor; ///< Extraction functor.
  /// Constructor.
MinimaFunctor(F const& f, V& v) : m_result(v), m_extractor(f) { }
  /// Extract a value and accumulate the minimum.
  void operator () (T const& obj) const {
    m_result = std::min(m_result, m_extractor(obj));
  }
};

// Working on a more general mechanism by starting with more specific
// ones to see the pattern.

template <
  typename R, ///< Return type.
  typename T, ///< Object type.
  typename ARG1 ///< Bound argument type.
> struct BinderConstMethodArg1ToNullary : std::unary_function<T, R> {
  typedef R (T::*F)(ARG1) const; /// Method type.
  F m_method; ///< The actual method.
  ARG1 m_arg1; ///< Bound argument.
  /// Constructor.
  BinderConstMethodArg1ToNullary(
    F const& f, ///< Pointer to  method.
    ARG1 const& arg1 ///< Argument to bind.
  ) : m_method(f), m_arg1(arg1) {
  }
  /// Call the method.
  R operator () (T const& obj) const {
    return (obj.*m_method)(m_arg1);
  }
};

#endif

} // namespace ts
