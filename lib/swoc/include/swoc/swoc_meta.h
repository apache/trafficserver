// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

  Meta programming support utilities.
*/

#pragma once

#include <type_traits>

#include "swoc/swoc_version.h"

namespace swoc { inline namespace SWOC_VERSION_NS { namespace meta {
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
 * template <typename T> auto func(T && t, CaseTag<0>) -> decltype(t.item, int()) { }
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
 * Note @c decltype does not accept explicit types - to have the type of "int" a function returning
 * @c int must be provided.  This is easy for simple builtin types such as @c int - use the
 * constructor @c int(). For @c void and non-simple types (such as @c int* ) this is a bit more
 * challenging. A general utility is provided for this - @c TypeFunc. For the @c void case this
 * would be <tt>decltype(TypeFunc<void>())</tt>. For @c int* it would be
 * <tt>decltype(TypeFunc<int *>())</tt>.
 */

/// Case hierarchy.
template <unsigned N> struct CaseTag : /** @cond DOXYGEN_FAIL */ public CaseTag<N - 1> /** @endcond */ {
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

/** A typed function for use in @c decltype.
 *
 * @tparam T The desired type.
 * @return @a T
 *
 * This function has no implementation. It should be used only inside @c decltype when a specific
 * type (rather than the type of an expression) is needed. For a type @a T that has a expression
 * default constructor this can be used.
 *
 * @code
 * decltype(T())
 * @endcode
 *
 * But if there is no default constructor this will not compile. This is a work around, so the
 * previous expression would be
 *
 * @code
 * decltype(meta::TypeFunc<T>())
 * @endcode
 *
 * Note this can also be a problem for even built in types like @c unsigned @c long for which
 * the expression
 *
 * @code
 * decltype(unsigned long())
 * @endcode
 *
 * does not compile.
 */
template <typename T> T TypeFunc();

/** Support for variable parameter lambdas.
 *
 * @tparam Args Lambdas to combine.
 *
 * This creates a class that has an overloaded function operator, with each overload corresponding
 * to one of the provided lambdas. The original use case is with @c std::visit where this can be
 * used to construct the @a visitor from a collection of lambdas.
 *
 * @code
 * std::variant<int, bool> v;
 * std::visit(swoc::meta::vary{
 *   [] (int & i) { ... },
 *   [] (bool & b) { ... }
 *   }, v);
 * @endcode
 */
template <typename... Args> struct vary : public Args... { using Args::operator()...; };
/// Template argument deduction guide (C++17 required).
template <typename... Args> vary(Args...) -> vary<Args...>;

/** Check if a type is any of a set of types.
 *
 * @tparam T Type to check.
 * @tparam Types Type list to match against.
 *
 * @a value is @c true if @a T is the same as any of @a Types. This is commonly used with
 * @c enable_if to enable a function / method only if the argument type is one of a fixed set.
 * @code
 *   template < typename T > auto f(T const& t)
 *     -> std::enable_if_t<swoc::meta::is_any_of<T, int, float, bool>::value, void>
 *   { ... }
 */
template <typename T, typename... Types> struct is_any_of {
  static constexpr bool value = std::disjunction<std::is_same<T, Types>...>::value;
};

template <typename T, typename... Types> struct is_homogenous {
  static constexpr bool value = std::conjunction<std::is_same<T, Types>...>::value;
  using type                  = T;
};

/// Helper variable template for is_any_of
template <typename T, typename... Types> inline constexpr bool is_any_of_v = is_any_of<T, Types...>::value;

/** Type list support class.
 *
 * @tparam Types List of types.
 *
 * The examples for the nested meta functions presume a type list has been defined like
 * @code
 * using TL = swoc::meta::type_list<int, bool, float, double>;
 * @endcode
 */
template <typename... Types> struct type_list {
  /// Length of the type list.
  static constexpr size_t size = sizeof...(Types);

  /** Define a type using the types in the list.
   * Give a type list, defining a variant that has those types would be
   * @code
   * using v = TL::template apply<std::variant>;
   * @endcode
   *
   * The primary reason to do this is if the type list is needed elsewhere (e.g. to enable
   * methods only for variant types).
   *
   * @see contains
   */
  template <template <typename... Pack> typename T> using apply = T<Types...>;

  /** Determine if a type @a T is a member of the type list.
   *
   * @tparam T Type to check.
   *
   * Frequently used with @c enable_if. This is handy for variants, if the list of variant
   * types is encoded in the type list.
   *
   * @code
   * template < typename T > auto f(T const& t)
   *   -> std::enable_if_t<TL::template contains<T>::value, void>
   * { ... }
   * @endcode
   */
  template <typename T> static constexpr bool contains = is_any_of<T, Types...>::value;
};

}}} // namespace swoc::SWOC_VERSION_NS::meta
