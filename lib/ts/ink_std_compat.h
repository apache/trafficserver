/** @file

  Compatibility with future versions of the C++ standard library

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

#ifdef __cplusplus
//
#if __cplusplus < 201402L
//
// C++ 14 compatibility
//
#include <memory>
#include <type_traits>

namespace std
{
template <typename T, typename... Args>
std::unique_ptr<T>
make_unique(Args &&... args)
{
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
// Local implementation of integer sequence templates from <utility> in C++14.
// Drop once we move to C++14.

template <typename T, T... N> struct integer_sequence {
  typedef T value_type;
  static_assert(std::is_integral<T>::value, "std::integer_sequence requires an integral type");

  static inline std::size_t
  size()
  {
    return (sizeof...(N));
  }
};

template <std::size_t... N> using index_sequence = integer_sequence<std::size_t, N...>;

namespace sequence_expander_detail
{
  // Expand a sequence (4 ways)
  template <typename T, std::size_t... _Extra> struct seq_expand;

  template <typename T, T... N, std::size_t... _Extra> struct seq_expand<integer_sequence<T, N...>, _Extra...> {
    typedef integer_sequence<T, N..., 1 * sizeof...(N) + N..., 2 * sizeof...(N) + N..., 3 * sizeof...(N) + N..., _Extra...> type;
  };

  template <std::size_t N> struct modulus;
  template <std::size_t N> struct construct : modulus<N % 4>::template modular_construct<N> {
  };

  // 4 base cases (e.g. modulo 4)
  template <> struct construct<0> {
    typedef integer_sequence<std::size_t> type;
  };
  template <> struct construct<1> {
    typedef integer_sequence<std::size_t, 0> type;
  };
  template <> struct construct<2> {
    typedef integer_sequence<std::size_t, 0, 1> type;
  };
  template <> struct construct<3> {
    typedef integer_sequence<std::size_t, 0, 1, 2> type;
  };

  // Modulus cases - split 4 ways and pick up the remainder explicitly.
  template <> struct modulus<0> {
    template <std::size_t N> struct modular_construct : seq_expand<typename construct<N / 4>::type> {
    };
  };
  template <> struct modulus<1> {
    template <std::size_t N> struct modular_construct : seq_expand<typename construct<N / 4>::type, N - 1> {
    };
  };
  template <> struct modulus<2> {
    template <std::size_t N> struct modular_construct : seq_expand<typename construct<N / 4>::type, N - 2, N - 1> {
    };
  };
  template <> struct modulus<3> {
    template <std::size_t N> struct modular_construct : seq_expand<typename construct<N / 4>::type, N - 3, N - 2, N - 1> {
    };
  };

  template <typename T, typename U> struct convert {
    template <typename> struct result;

    template <T... N> struct result<integer_sequence<T, N...>> {
      typedef integer_sequence<U, N...> type;
    };
  };

  template <typename T> struct convert<T, T> {
    template <typename U> struct result {
      typedef U type;
    };
  };

  template <typename T, T N>
  using make_integer_sequence_unchecked = typename convert<std::size_t, T>::template result<typename construct<N>::type>::type;

  template <typename T, T N> struct make_integer_sequence {
    static_assert(std::is_integral<T>::value, "std::make_integer_sequence can only be instantiated with an integral type");
    static_assert(0 <= N, "std::make_integer_sequence input shall not be negative");

    typedef make_integer_sequence_unchecked<T, N> type;
  };

} // namespace sequence_expander_detail

template <typename T, T N> using make_integer_sequence = typename sequence_expander_detail::make_integer_sequence<T, N>::type;

template <std::size_t N> using make_index_sequence = make_integer_sequence<std::size_t, N>;

template <typename... T> using index_sequence_for = make_index_sequence<sizeof...(T)>;

} // namespace std
#endif // C++ 14 compatibility

//
#if __cplusplus < 201700L
//
// C++ 17 compatibility
//
#include <cassert>
namespace std
{
template <typename T>
inline const T &
clamp(const T &v, const T &lo, const T &hi)
{
  assert(lo <= hi);
  return (v < lo) ? lo : ((hi < v) ? hi : v);
}

} // namespace std
#endif // C++ 17 compatibility
#endif // __cplusplus
