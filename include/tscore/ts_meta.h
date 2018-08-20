/** @file

  Meta programming support utilities.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#pragma once

namespace ts
{
namespace meta
{
  /** This creates an ordered series of meta template cases that can be used to select one of a set
   * of functions in a priority ordering. A set of templated overloads take an (extra) argument of
   * the case structures, each a different one. Calling the function invokes the highest case that
   * is valid. Because of SFINAE the templates can have errors, as long as at least one doesn't.
   * The root technique is to use @c decltype to check an expression for the overload to be valid.
   * Because the compiler will evaluate everything it can while parsing the template this
   * expression must be delayed until the template is instantiated. This is done by making the
   * return type @c auto and making the @c decltype dependent on the template parameter. In
   * addition, the comma operator can be used to force a specific return type while also checking
   * the expression for validity. E.g.
   *
   * @code
   * template <typename T> auto func(T && t, CaseArg_0 const&) -> decltype(t.item, int()) { }
   * @endcode
   *
   * The comma operator discards the type and value of the left operand therefore the return type of
   * the function is @c int but this overload will not be available if @c t.item does not compile
   * (e.g., there is no such member). The presence of @c t.item also prevents this compilation check
   * from happening until overload selection is needed. Therefore if the goal was a function that
   * would return the value of the @c T::count member if present and 0 if not, the code would be
   *
   * @code
   * template <typename T> auto Get_Count(T && t, CaseTag<0>)
   *   -> int
   * { return 0; }
   * template <typename T> auto Get_Count(T && t, CaseTag<1>)
   *   -> decltype(t.count, int())
   * { return t.count; }
   * int Get_Count(Thing& t) { return GetCount(t, CaseArg); }
   * @endcode
   *
   * Note the overloads will be checked from the highest case to the lowest and the first one that
   * is valid (via SFINAE) is used. This is the point of using the case arguments, to force an
   * ordering on the overload selection. Unfortunately this means the functions @b must be
   * templated, even if there's no other reason for it, because it depends on SFINAE which doesn't
   * apply to normal overloads.
   *
   * The key point is the expression in the @c decltype should be the same expression used in the
   * method to verify it will compile. It is annoying to type it twice but there's not a better
   * option.
   *
   * Note @c decltype does not accept explicit types - to have the type of "int" an @c int must be
   * constructed. This is easy for builtin types except @c void. @c CaseVoidFunc is provided for that
   * situation, e.g. <tt>decltype(CaseVoidFunc())</tt> provides @c void via @c decltype.
   */

  /// Case hierarchy.
  template <unsigned N> struct CaseTag : public CaseTag<N - 1> {
    constexpr CaseTag() {}
    static constexpr unsigned value = N;
  };

  /// Anchor the hierarchy.
  template <> struct CaseTag<0> {
    constexpr CaseTag() {}
    static constexpr unsigned value = 0;
  };

  /** This is the final case - it forces the super class hierarchy.
   * After defining the cases using the indexed case arguments, this is used to to perform the call.
   * To increase the hierarchy depth, change the template argument to a larger number.
   */
  static constexpr CaseTag<9> CaseArg{};

  // A function to provide a @c void type for use in cases.
  // Other types can use the default constructor, e.g. "int()" for @c int.
  inline void
  CaseVoidFunc()
  {
  }
} // namespace meta
} // namespace ts
