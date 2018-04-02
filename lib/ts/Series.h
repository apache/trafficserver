/** @file

    Define classes that comply with TS Series concept for integral types and string_view.

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

#include <type_traits>
#include <limits>
#include <string>

#include <string_view.h>

namespace ts
{
namespace _private_
{
  template <typename Accum, typename Type, bool IsIntegralType, bool IsUnsignedElement> struct Series_;

  template <typename Accum, typename Type> struct Series_<Accum, Type, true, true> {
    static void
    visit(Accum &a, Type x_)
    {
      using UType   = typename std::make_unsigned<Type>::type;
      using Element = typename Accum::Element;

      const int EBits = std::numeric_limits<Element>::digits;
      const int TBits = std::numeric_limits<UType>::digits;

      if (TBits > EBits) {
        UType x = static_cast<UType>(x_);

        for (int i = TBits; i > 0; i -= EBits) {
          a(static_cast<typename Accum::Element>(x));

          // Note that this statement would never actually be executed if EBites >= TBits.  The case where the shift is 0 is
          // needed only to prevent a compilation error.
          //
          x >>= EBits < TBits ? EBits : 0;
        }
      } else {
        a(static_cast<typename Accum::Element>(x_));
      }
    }
  };
} // end namespace _private_

/*
The first type parameter to the Series template must comply with the TS Accumulator concept.  An accumultor has a public type
Element which is an unsigned integral type.  It has a function call operator that takes on parameter of type Element.  (The
return type of the function call operator, if any, does not matter.)

All instantiations of the class template Series<Accum, Type> must provide a static member function called 'visit'.  The first
parameter to 'visit' should be a (non-const) reference to an instance of Accum.  The second paramter to 'visit' should be of type
Type or const reference to Type.  'visit' should slice up the data in the Type instance with each slice being an instance of
Accum::Element.  It should call the function call operator of the accumulator object (passed as the first parameter) once with
each slice, in a consistent order.

This header provides partial specializations of Series for Type being any integral type.  It also provides partial specializations
where Type is ts::string_view or std::string, or where Type is const char *, which is presumed to be a pointer to a nul-
terminated C string.

All specializations of Series must be in the ts namespace.

For this class:

struct B
{
  ts::string_view sv;
  std::string str;
  const char *cStr;
  int i;
};

an example partial specialization of Series is:

namespace ts
{
template <typename Accum>
struct Series<Accum, B>
{
  static void
  visit(Accum &acc, const B &b)
  {
    Series<Accum, string_view>::visit(acc, b.sv);
    Series<Accum, std::string>::visit(acc, b.str);
    Series<Accum, const char *>::visit(acc, b.cStr);
    Series<Accum, int>::visit(acc, b.i);
  }
};
}

*/

template <typename Accum, typename Type>
struct Series
  : public _private_::Series_<Accum, Type, std::is_integral<Type>::value, std::is_unsigned<typename Accum::Element>::value> {
};

template <typename Accum, typename ValueType = char> struct StringViewSeries {
  static void
  visit(Accum &a, ts::basic_string_view<ValueType> sv)
  {
    for (unsigned i = 0; i < sv.size(); ++i) {
      Series<Accum, ValueType>::visit(a, static_cast<typename Accum::Element>(sv[i]));
    }
  }
};

template <typename Accum, typename ValueType>
struct Series<Accum, ts::basic_string_view<ValueType>> : public StringViewSeries<Accum, ValueType> {
};

template <typename Accum> struct NulTermStringSeries {
  static void
  visit(Accum &a, const char *str)
  {
    for (; *str; ++str) {
      Series<Accum, char>::visit(a, static_cast<typename Accum::Element>(*str));
    }
  }
};

template <typename Accum> struct Series<Accum, const char *> : public NulTermStringSeries<Accum> {
};

template <typename Accum, typename ValueType = char> struct StringSeries {
  static void
  visit(Accum &a, const std::basic_string<ValueType> &str)
  {
    for (unsigned i = 0; i < str.size(); ++i) {
      Series<Accum, ValueType>::visit(a, static_cast<typename Accum::Element>(str[i]));
    }
  }
};

template <typename Accum, typename ValueType>
struct Series<Accum, std::basic_string<ValueType>> : public StringSeries<Accum, ValueType> {
};

} // end namespace ts
