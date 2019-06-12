/** @file

    Create a distinct type from a builtin numeric type.

    This template class converts a basic type into a class, so that
    instances of the class act like the basic type in normal use but
    as a distinct type when evaluating overloads. This is very handy
    when one has several distinct value types that map to the same
    basic type. That means we can have overloads based on the type
    even though the underlying basic type is the same. The second
    template argument, X, is used only for distinguishing
    instantiations of the template with the same base type. It doesn't
    have to exist. One can declare an instantiation like

    @code
    typedef NumericType<int, struct some_random_tag_name> some_random_type;
    @endcode

    It is not necessary to ever mention some_random_tag_name
    again. All we need is the entry in the symbol table.

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

#include <limits>

namespace ts
{
// Forward declare.
template <typename T, typename X> class NumericType;

/// @cond NOT_DOCUMENTED
/** Support template for resolving operator ambiguity.

    Not for client use.

    @internal This resolves a problem when @a T is not @c int.  In
    that case, because raw numbers are @c int the overloading rule
    changes creates an ambiguity - gcc won't distinguish between class
    methods and builtins because @c NumericType has a user conversion
    to @a T. So signature <tt>(NumericType, T)</tt> and <tt>(T,
    int)</tt> are considered equivalent. This defines the @c int
    operators explicitly. We inherit it so if @a T is @c int, these
    are silently overridden.

    @internal Note that we don't have to provide an actual implementation
    for these operators. Funky, isn't it?
*/
template <typename T, ///< Base numeric type.
          typename X  ///< Distinguishing tag type.
          >
class NumericTypeIntOperators
{
public:
  NumericType<T, X> &operator+=(int t);
  NumericType<T, X> &operator-=(int t);

  // Must have const and non-const versions.
  NumericType<T, X> operator+(int t);
  NumericType<T, X> operator-(int t);
  NumericType<T, X> operator+(int t) const;
  NumericType<T, X> operator-(int t) const;
};

template <typename T, typename X> NumericType<T, X> operator+(int t, NumericTypeIntOperators<T, X> const &);

template <typename T, typename X> NumericType<T, X> operator-(int t, NumericTypeIntOperators<T, X> const &);

/// @endcond

/** Numeric type template.

    @internal One issue is that this is not a POD and so doesn't work
    with @c printf. I will need to investigate what that would take.
 */
template <typename T, ///< Base numeric type.
          typename X  ///< Distinguishing tag type.
          >
class NumericType : public NumericTypeIntOperators<T, X>
{
public:
  using raw_type  = T;           ///< Base type.
  using self_type = NumericType; ///< Self reference type.

  /// @cond NOT_DOCUMENTED
  // Need to import these to avoid compiler problems.
  using NumericTypeIntOperators<T, X>::operator+=;
  using NumericTypeIntOperators<T, X>::operator-=;
  using NumericTypeIntOperators<T, X>::operator+;
  using NumericTypeIntOperators<T, X>::operator-;
  /// @endcond

  /// Default constructor, uninitialized.
  NumericType();
  //! Construct from implementation type.
  constexpr NumericType(raw_type const t ///< Initialized value.
  );
  /// Copy constructor.
  constexpr NumericType(self_type const &that) = default;

  //! Assignment from implementation type.
  NumericType &operator=(raw_type const t);
  //! Self assignment.
  NumericType &operator=(self_type const &that);

  /// User conversion to implementation type.
  /// @internal If we have just a single const method conversion to a copy
  /// of the @c raw_type then the stream operators don't work. Only a CR
  /// conversion operator satisifies the argument matching.
  operator raw_type const &() const { return _t; }
  /// User conversion to implementation type.
  operator raw_type &() { return _t; }
  /// Explicit conversion to host type
  raw_type
  raw() const
  {
    return _t;
  }

  // User conversions to raw type provide the standard comparison operators.
  self_type &operator+=(self_type const &that);
  self_type &operator-=(self_type const &that);

  self_type &operator+=(raw_type t);
  self_type &operator-=(raw_type t);

  self_type operator+(self_type const &that);
  self_type operator-(self_type const &that);

  self_type operator+(raw_type t);
  self_type operator-(raw_type t);

  self_type &operator++();
  self_type operator++(int);
  self_type &operator--();
  self_type operator--(int);

private:
  raw_type _t;
};

// Method definitions.
// coverity[uninit_ctor]
template <typename T, typename X> NumericType<T, X>::NumericType() {}
template <typename T, typename X> constexpr NumericType<T, X>::NumericType(raw_type const t) : _t(t) {}
template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator=(raw_type const t)
{
  _t = t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator=(self_type const &that)
{
  _t = that._t;
  return *this;
}

template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator+=(self_type const &that)
{
  _t += that._t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator-=(self_type const &that)
{
  _t -= that._t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator+(self_type const &that)
{
  return self_type(_t + that._t);
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator-(self_type const &that)
{
  return self_type(_t - that._t);
}

template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator+=(raw_type t)
{
  _t += t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator-=(raw_type t)
{
  _t -= t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator+(raw_type t)
{
  return self_type(_t + t);
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator-(raw_type t)
{
  return self_type(_t - t);
}

template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator++()
{
  ++_t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X> &
NumericType<T, X>::operator--()
{
  --_t;
  return *this;
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator++(int)
{
  self_type tmp(*this);
  ++_t;
  return tmp;
}
template <typename T, typename X>
NumericType<T, X>
NumericType<T, X>::operator--(int)
{
  self_type tmp(*this);
  --_t;
  return tmp;
}

template <typename T, typename X>
NumericType<T, X>
operator+(T const &lhs, NumericType<T, X> const &rhs)
{
  return rhs + lhs;
}
template <typename T, typename X>
NumericType<T, X>
operator-(T const &lhs, NumericType<T, X> const &rhs)
{
  return NumericType<T, X>(lhs - rhs.raw());
}

/* ----------------------------------------------------------------------- */
} // namespace ts
/* ----------------------------------------------------------------------- */
