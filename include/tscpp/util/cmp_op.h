/** @file

   Convenient definition of comparison operators for user-defined types.

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

/*
Suppose that, for types T1 and T2, there exists a compare function with return type int.  The two parameters to the
function have types const T1 & and const T2 &.  The function returns a negative value if the first parameter is less
than the second parameter, 0 if the first parameter is equal to the second parameter, and a positive value if the first
parameter is greater than the second parameter.  This include file allows the definition of the comparison operators
'operator R (const T1 &, const T2 &)' for R in ==, !=, <, <=, >, >= .  If T1 and T2 are not the same type, the
comparison operators 'operator R (const T2 &, const T1 &)' will also be defined.  To define the comparison operators,
provide a specialization of the ts::cmp_op::Enable like this:

namespace ts
{
namespace cmp_op
{
template <>
struct Enable<T1, T2> : public Yes<T1, T2, compare_function> {};
}
}

'compare_function' stands for the name of the compare function for T1 and T2 mentioned above.
*/

#pragma once

#include <type_traits>

namespace ts
{
namespace cmp_op
{
  template <typename T1, typename T2> struct Enable {
  };

  template <typename T1, typename T2, int (*Cmp)(const std::remove_reference_t<T1> &, const std::remove_reference_t<T2> &)>
  struct Yes {
    struct one_op_type_return_type {
      using type = bool;
    };
    struct two_op_types_return_type {
      using type = bool;
    };
    static int
    cmp(const std::remove_reference_t<T1> &op1, const std::remove_reference_t<T2> &op2)
    {
      return Cmp(op1, op2);
    }
  };

  // Partial specialization for when T1 and T2 are the same type.
  //
  template <typename T, int (*Cmp)(const std::remove_reference_t<T> &, const std::remove_reference_t<T> &)> struct Yes<T, T, Cmp> {
    struct one_op_type_return_type {
      using type = bool;
    };
    static int
    cmp(const std::remove_reference_t<T> &op1, const std::remove_reference_t<T> &op2)
    {
      return Cmp(op1, op2);
    }
  };

} // end namespace cmp_op
} // end namespace ts

template <typename T1, typename T2>
auto
operator==(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) == 0;
}
template <typename T1, typename T2>
auto
operator!=(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) != 0;
}
template <typename T1, typename T2>
auto
operator>(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) > 0;
}
template <typename T1, typename T2>
auto
operator>=(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) >= 0;
}
template <typename T1, typename T2>
auto
operator<(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) < 0;
}
template <typename T1, typename T2>
auto
operator<=(const T1 &op1, const T2 &op2) -> typename ts::cmp_op::Enable<T1, T2>::one_op_type_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) <= 0;
}

// If T1 and T2 are not the same types, define the operators for the reverse order

template <typename T1, typename T2>
auto
operator==(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) == 0;
}
template <typename T1, typename T2>
auto
operator!=(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) != 0;
}
template <typename T1, typename T2>
auto
operator>(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) < 0;
}
template <typename T1, typename T2>
auto
operator>=(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) <= 0;
}
template <typename T1, typename T2>
auto
operator<(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) > 0;
}
template <typename T1, typename T2>
auto
operator<=(const T2 &op2, const T1 &op1) -> typename ts::cmp_op::Enable<T1, T2>::two_op_types_return_type::type
{
  return ts::cmp_op::Enable<T1, T2>::cmp(op1, op2) >= 0;
}
