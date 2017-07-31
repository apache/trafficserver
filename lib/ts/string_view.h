/** @file

  This is an implemetation of the std::string_view class for us to use
  with c++11 and 14 until we can use the c++ 17 standard. This has a few overloads
  to deal with some ats objects to help with migration work

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

#include <cstddef>
#include <type_traits>
#include <iterator>
#include <stdexcept>
#include <algorithm>
#include <utility>
#include <string>
#include <ostream>

#if __cplusplus < 201402
#define CONSTEXPR14 inline
#else
#define CONSTEXPR14 constexpr
#endif

namespace ts
{
// forward declare class for iterator friend relationship
template <typename _Type, typename _CharTraits = std::char_traits<_Type>> class basic_string_view;

/////////////////////
// the iterator

namespace _private_
{
  template <typename _CharTraits> class string_view_iterator
  { // iterator for character buffer wrapper
  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type        = typename _CharTraits::char_type;
    using difference_type   = std::ptrdiff_t;
    using pointer           = const value_type *;
    using reference         = const value_type &;

    constexpr string_view_iterator() noexcept = default;

  private:
    friend basic_string_view<value_type, _CharTraits>;

    constexpr explicit string_view_iterator(const pointer rhs) noexcept : m_ptr(rhs) {}

  public:
    constexpr reference operator*() const noexcept
    { // return designated object
      return *m_ptr;
    }

    constexpr pointer operator->() const noexcept
    { // return pointer to class object
      return m_ptr;
    }

    CONSTEXPR14 string_view_iterator &operator++() noexcept
    { // preincrement
      ++m_ptr;
      return *this;
    }

    CONSTEXPR14 string_view_iterator operator++(int)noexcept
    { // postincrement
      string_view_iterator tmp{*this};
      ++*this;
      return tmp;
    }

    CONSTEXPR14 string_view_iterator &operator--() noexcept
    { // predecrement
      --m_ptr;
      return *this;
    }

    CONSTEXPR14 string_view_iterator operator--(int)noexcept
    { // postdecrement
      string_view_iterator tmp{*this};
      --*this;
      return tmp;
    }

    CONSTEXPR14 string_view_iterator &
    operator+=(const difference_type offset) noexcept
    { // increment by integer
      m_ptr += offset;
      return *this;
    }

    CONSTEXPR14 string_view_iterator
    operator+(const difference_type offset) const noexcept
    { // return this + integer
      string_view_iterator tmp{*this};
      tmp += offset;
      return tmp;
    }

    CONSTEXPR14 string_view_iterator &
    operator-=(const difference_type offset) noexcept
    { // decrement by integer
      m_ptr -= offset;
      return *this;
    }

    CONSTEXPR14 string_view_iterator
    operator-(const difference_type offset) const noexcept
    { // return this - integer
      string_view_iterator tmp{*this};
      tmp -= offset;
      return tmp;
    }

    constexpr difference_type
    operator-(const string_view_iterator &rhs) const noexcept
    { // return difference of iterators
      return m_ptr - rhs.m_ptr;
    }

    constexpr reference operator[](const difference_type offset) const noexcept
    { // subscript
      return *(*this) + offset;
    }

    constexpr bool
    operator==(const string_view_iterator &rhs) const noexcept
    { // test for iterator equality
      return m_ptr == rhs.m_ptr;
    }

    constexpr bool
    operator!=(const string_view_iterator &rhs) const noexcept
    { // test for iterator inequality
      return !(*this == rhs);
    }

    constexpr bool
    operator<(const string_view_iterator &rhs) const noexcept
    { // test if this < rhs
      return m_ptr < rhs.m_ptr;
    }

    constexpr bool
    operator>(const string_view_iterator &rhs) const noexcept
    { // test if this > rhs
      return rhs < *this;
    }

    constexpr bool
    operator<=(const string_view_iterator &rhs) const noexcept
    { // test if this <= rhs
      return !(rhs < *this);
    }

    constexpr bool
    operator>=(const string_view_iterator &rhs) const noexcept
    { // test if this >= rhs
      return !(*this < rhs);
    }

  private:
    pointer m_ptr = nullptr;
  };
}

template <typename _CharTraits>
CONSTEXPR14 _private_::string_view_iterator<_CharTraits>
operator+(const typename _private_::string_view_iterator<_CharTraits>::difference_type offset,
          _private_::string_view_iterator<_CharTraits> rhs) noexcept
{ // return integer + _Right
  rhs += offset;
  return rhs;
}

/// the main class

template <typename _Type, typename _CharTraits> class basic_string_view
{ // wrapper for any kind of contiguous character buffer
public:
  // some standard junk to say hey.. you are messing up something important.. stop it
  static_assert(std::is_same<_Type, typename _CharTraits::char_type>::value,
                "Bad char_traits for basic_string_view; "
                "N4606 21.4.2 [string.view.template] \"the type traits::char_type shall name the same type as charT.\"");

  using traits_type            = _CharTraits;
  using value_type             = _Type;
  using pointer                = _Type *;
  using const_pointer          = const _Type *;
  using reference              = _Type &;
  using const_reference        = const _Type &;
  using const_iterator         = _private_::string_view_iterator<_CharTraits>;
  using iterator               = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator       = const_reverse_iterator;
  using size_type              = std::size_t;
  using difference_type        = ptrdiff_t;
  using self_type              = basic_string_view<value_type, traits_type>;
  static constexpr size_type npos{~0ULL};

  constexpr basic_string_view() noexcept {}

  constexpr basic_string_view(basic_string_view const &) noexcept = default;
  CONSTEXPR14 basic_string_view &operator=(basic_string_view const &) noexcept = default;

  /* implicit */
  // Note that with constexpr enabled and modern g++ and clang ( icpc)
  // there is a optmization to strlen and traits_type::length for
  // literals in which the compiler will replace it with the size
  // at compile time. With c++ 17 the constexpr guarrentee
  // that the literal length is optimized out
  constexpr basic_string_view(const_pointer rhs) noexcept : m_data(rhs), m_size(traits_type::length(rhs)) {}

  constexpr basic_string_view(const_pointer rhs, const size_type length) noexcept // strengthened
    : m_data(rhs), m_size(length)
  {
  }

  // std::string constructor
  constexpr basic_string_view(std::string const &rhs) noexcept : m_data(rhs.data()), m_size(rhs.size()) {}

  // For iterator on string_view we don't need to deal with const and non-const as different types
  // they are all const iterators as the string values are immutable
  // keep in mind that the string view is mutable in what it points to

  constexpr const_iterator
  begin() const noexcept
  { // get the beginning of the range
    return const_iterator(m_data);
  }

  constexpr const_iterator
  end() const noexcept
  { // get the end of the range
    return const_iterator(m_data + m_size);
  }

  constexpr const_iterator
  cbegin() const noexcept
  { // get the beginning of the range
    return begin();
  }

  constexpr const_iterator
  cend() const noexcept
  { // get the end of the range
    return end();
  }

  constexpr const_reverse_iterator
  rbegin() const noexcept
  { // get the beginning of the reversed range
    return const_reverse_iterator{end()};
  }

  constexpr const_reverse_iterator
  rend() const noexcept
  { // get the end of the reversed range
    return const_reverse_iterator{begin()};
  }

  constexpr const_reverse_iterator
  crbegin() const noexcept
  { // get the beginning of the reversed range
    return rbegin();
  }

  constexpr const_reverse_iterator
  crend() const noexcept
  { // get the end of the reversed range
    return rend();
  }

  constexpr size_type
  size() const noexcept
  { // get the size of this basic_string_view
    return m_size;
  }

  constexpr size_type
  length() const noexcept
  { // get the size of this basic_string_view
    return m_size;
  }

  constexpr bool
  empty() const noexcept
  { // check whether this basic_string_view is empty
    return m_size == 0;
  }

  constexpr const_pointer
  data() const noexcept
  { // get the base pointer of this basic_string_view
    // std says data does not have to have a NULL terminator
    return m_data;
  }

  constexpr size_type
  max_size() const noexcept
  {
    // max size of byte we could make - 1 for npos divided my size of char
    // to return the number of "character sized bytes" we can allocate
    return (UINTPTR_MAX - 1) / sizeof(_Type);
  }

  CONSTEXPR14 const_reference operator[](const size_type index) const // strengthened
  {
// get character at offset
// assume index is in range
#ifdef _DEBUG
    check_index_bound(index); // for debug
#endif
    return m_data[index];
  }

  CONSTEXPR14 const_reference
  at(const size_type index) const
  {                           // get the character at offset or throw if that is out of range
    check_index_bound(index); // add bound check
    return m_data[index];
  }

  constexpr const_reference front() const noexcept // strengthened
  {
    // std is undefined for empty string
    return m_data[0];
  }

  constexpr const_reference
  back() const
  {
    // std is undefined for empty string
    return m_data[m_size - 1];
  }

  CONSTEXPR14 void remove_prefix(const size_type length) noexcept // strengthened
  {                                                               // chop off the beginning
    auto tmp = std::min(length, m_size);
    m_data += tmp;
    m_size -= tmp;
  }

  CONSTEXPR14 void remove_suffix(const size_type length) noexcept // strengthened
  {                                                               // chop off the end
    m_size -= std::min(length, m_size);
  }

  CONSTEXPR14 void
  swap(basic_string_view &rhs) noexcept
  {                                   // swap contents
    const basic_string_view tmp{rhs}; // note: std::swap is not constexpr
    rhs   = *this;
    *this = tmp;
  }

  // possibly unsafe.. look for linux equal to a windows copy_s version if it exists
  CONSTEXPR14 size_type
  copy(_Type *const rhs, size_type length, const size_type offset = 0) const
  {
    check_offset_bound(offset);
    length = std::min(length, m_size - offset);
    traits_type::copy(rhs, m_data + offset, length);
    return length;
  }

  CONSTEXPR14 basic_string_view
  substr(const size_type offset = 0, size_type length = npos) const
  {
    check_offset_bound(offset);
    length = std::min(length, m_size - offset);
    return basic_string_view(m_data + offset, length);
  }

  // this is not a std function.. but is needed in some form for ==, != operators
  // I could make this _equal to say don't call it.. but I won't
  constexpr bool
  _equal(const basic_string_view rhs) const noexcept
  {
    return m_size == rhs.m_size && traits_type::compare(m_data, rhs.m_data, m_size) == 0;
  }

  constexpr bool
  _equal(std::string const &rhs) const noexcept
  {
    return m_size == rhs.size() && traits_type::compare(m_data, rhs.data(), m_size) == 0;
  }

  CONSTEXPR14 bool
  _equal(value_type const *const rhs) const noexcept
  {
    self_type tmp(rhs);
    return m_size == tmp.size() && traits_type::compare(m_data, tmp.data(), m_size) == 0;
  }

  //////////////////////////////////////////////
  // Compare

  CONSTEXPR14 int
  compare(const basic_string_view rhs) const noexcept
  {
    const int result = traits_type::compare(m_data, rhs.m_data, std::min(m_size, rhs.size()));

    if (result != 0) {
      return result;
    }

    if (m_size < rhs.m_size) {
      return -1;
    }

    if (m_size > rhs.m_size) {
      return 1;
    }

    return 0;
  }

  constexpr int
  compare(const size_type offset, const size_type length, const basic_string_view rhs) const
  {
    return substr(offset, length).compare(rhs);
  }

  constexpr int
  compare(const size_type offset, const size_type length, const basic_string_view rhs, const size_type rhsoffsetset,
          const size_type rhs_length) const
  {
    return substr(offset, length).compare(rhs.substr(rhsoffsetset, rhs_length));
  }

  constexpr int
  compare(const _Type *const rhs) const
  {
    return compare(basic_string_view(rhs));
  }

  constexpr int
  compare(const size_type offset, const size_type length, const _Type *const rhs) const
  {
    return substr(offset, length).compare(basic_string_view(rhs));
  }

  constexpr int
  compare(const size_type offset, const size_type length, const _Type *const rhs, const size_type rhs_length) const
  {
    return substr(offset, length).compare(basic_string_view(rhs, rhs_length));
  }

  //////////////////////
  // find -- string

  CONSTEXPR14 size_type find(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    // do we have space to find the string given the offset
    if (rhs_length > m_size || offset > m_size - rhs_length) {
      // no match found
      return npos;
    }

    if (rhs_length == 0) { // empty string always matches to offset
      return offset;
    }

    // this is the point in which we can stop checking
    const auto end_ptr = m_data + (m_size - rhs_length) + 1;
    // start looking
    for (auto curr = m_data + offset; curr < end_ptr; ++curr) {
      // try to first char
      curr = traits_type::find(curr, end_ptr - curr, *rhs);
      if (!curr) { // didn't find first character; report failure
        return npos;
      }
      // given we found it, see if it compares ( matches)
      if (traits_type::compare(curr, rhs, rhs_length) == 0) { // found match
        return curr - m_data;
      }
      // else try again till we run out of string space
    }
    // no match found
    return npos;
  }

  constexpr size_type
  find(const basic_string_view rhs, const size_type offset = 0) const noexcept
  {
    return find(rhs.m_data, offset, rhs.size());
  }

  constexpr size_type find(const _Type *const rhs, const size_type offset = 0) const noexcept // strengthened
  {
    return find(rhs, offset, traits_type::length(rhs));
  }

  // character form

  CONSTEXPR14 size_type
  find(const _Type c, const size_type offset = 0) const noexcept
  {
    if (offset < m_size) {
      const auto found_at = traits_type::find(m_data + offset, m_size - offset, c);
      if (found_at) {
        return found_at - m_data;
      }
    }
    // no match found
    return npos;
  }

  /////////////////////////////
  // rfind -- string

  CONSTEXPR14 size_type rfind(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    if (rhs_length == 0) {
      return std::min(offset, m_size); // empty string always matches
    }

    if (rhs_length <= m_size) {
      // room for match, look for it
      for (auto curr = m_data + std::min(offset, m_size - rhs_length);; --curr) {
        // do we have a match
        if (traits_type::eq(*curr, *rhs) && traits_type::compare(curr, rhs, rhs_length) == 0) {
          // found a match
          return curr - m_data;
        }

        // at beginning, no more chance for match?
        if (curr == m_data) {
          break;
        }
      }
    }

    return npos;
  }

  constexpr size_type rfind(const _Type *const rhs, const size_type offset = npos) const noexcept // strengthened
  {
    return rfind(rhs, offset, traits_type::length(rhs));
  }

  constexpr size_type
  rfind(const basic_string_view rhs, const size_type offset = npos) const noexcept
  {
    return rfind(rhs.m_data, offset, rhs.m_size);
  }

  // character version

  CONSTEXPR14 size_type
  rfind(const _Type c, const size_type offset = npos) const noexcept
  {
    if (m_data != 0) {
      for (auto curr = m_data + std::min(offset, m_size - 1);; --curr) {
        if (traits_type::eq(*curr, c)) {
          // found a match
          return curr - m_data;
        }

        if (curr == m_data) {
          // at beginning, no more chances for match
          break;
        }
      }
    }
    // no match found
    return npos;
  }

  //////////////////////////////////////
  // find_first_of

  CONSTEXPR14 size_type find_first_of(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    if (rhs_length != 0 && offset < m_size) {
      const auto _End = m_data + m_size;
      for (auto curr = m_data + offset; curr < _End; ++curr) {
        if (traits_type::find(rhs, rhs_length, *curr)) {
          // found a match
          return curr - m_data;
        }
      }
    }

    // no match found
    return npos;
  }

  constexpr size_type find_first_of(const _Type *const rhs, const size_type offset = 0) const noexcept // strengthened
  {
    return find_first_of(rhs, offset, traits_type::length(rhs));
  }

  constexpr size_type
  find_first_of(const basic_string_view rhs, const size_type offset = 0) const noexcept
  {
    return find_first_of(rhs.m_data, offset, rhs.m_size);
  }

  // character version

  CONSTEXPR14 size_type
  find_first_of(const _Type c, const size_type offset = 0) const noexcept
  {
    if (offset < m_size) {
      const auto found_at = traits_type::find(m_data + offset, m_size - offset, c);
      if (found_at) {
        return found_at - m_data;
      }
    }

    // no match found
    return npos;
  }

  //////////////////////////////
  // find_last_of

  CONSTEXPR14 size_type find_last_of(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    if (rhs_length != 0 && m_size != 0) {
      for (auto curr = m_data + std::min(offset, m_size - 1);; --curr) {
        if (traits_type::find(rhs, rhs_length, *curr)) {
          // found a match
          return curr - m_data;
        }

        if (curr == m_data) {
          // at beginning, no more chances for match
          break;
        }
      }
    }

    // no match found
    return npos;
  }

  constexpr size_type find_last_of(const _Type *const rhs, const size_type offset = npos) const noexcept // strengthened
  {
    return find_last_of(rhs, offset, traits_type::length(rhs));
  }

  constexpr size_type
  find_last_of(const basic_string_view rhs, const size_type offset = npos) const noexcept
  {
    return find_last_of(rhs.m_data, offset, rhs.m_size);
  }

  // character version

  CONSTEXPR14 size_type
  find_last_of(const _Type c, const size_type offset = npos) const noexcept
  {
    if (m_size != 0) { // room for match, look for it
      for (auto curr = m_data + std::min(offset, m_size - 1);; --curr) {
        if (traits_type::eq(*curr, c)) {
          // found a match
          return curr - m_data;
        }

        if (curr == m_data) {
          // at beginning, no more chances for match
          break;
        }
      }
    }

    // no match found
    return npos;
  }

  //////////////////////////////
  // find_first_not_of

  CONSTEXPR14 size_type find_first_not_of(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    if (offset < m_size) {
      const auto _End = m_data + m_size;
      for (auto curr = m_data + offset; curr < _End; ++curr) {
        if (!traits_type::find(rhs, rhs_length, *curr)) {
          // found a match
          return curr - m_data;
        }
      }
    }

    // no match found
    return npos;
  }

  constexpr size_type find_first_not_of(const _Type *const rhs, const size_type offset = 0) const noexcept // strengthened
  {
    return find_first_not_of(rhs, offset, traits_type::length(rhs));
  }

  constexpr size_type
  find_first_not_of(const basic_string_view rhs, const size_type offset = 0) const noexcept
  {
    return find_first_not_of(rhs.m_data, offset, rhs.m_size);
  }

  // character version

  CONSTEXPR14 size_type
  find_first_not_of(const _Type c, const size_type offset = 0) const noexcept
  {
    if (offset < m_size) {
      const auto _End = m_data + m_size;
      for (auto curr = m_data + offset; curr < _End; ++curr) {
        if (!traits_type::eq(*curr, c)) {
          // found a match
          return curr - m_data;
        }
      }
    }

    // no match found
    return npos;
  }

  //////////////////////////////
  // find_last_not_of

  CONSTEXPR14 size_type find_last_not_of(const _Type *const rhs, const size_type offset, const size_type rhs_length) const
    noexcept // strengthened
  {
    if (m_size != 0) {
      for (auto curr = m_data + std::min(offset, m_size - 1);; --curr) {
        if (!traits_type::find(rhs, rhs_length, *curr)) {
          // found a match
          return curr - m_data;
        }

        if (curr == m_data) {
          // at beginning, no more chances for match
          break;
        }
      }
    }

    // no match found
    return npos;
  }

  constexpr size_type find_last_not_of(const _Type *const rhs, const size_type offset = npos) const noexcept // strengthened
  {
    return find_last_not_of(rhs, offset, traits_type::length(rhs));
  }

  constexpr size_type
  find_last_not_of(const basic_string_view rhs, const size_type offset = npos) const noexcept
  {
    return find_last_not_of(rhs.m_data, offset, rhs.m_size);
  }

  // character version

  CONSTEXPR14 size_type
  find_last_not_of(const _Type c, const size_type offset = npos) const noexcept
  {
    if (m_size != 0) { // room for match, look for it
      for (auto curr = m_data + std::min(offset, m_size - 1);; --curr) {
        if (!traits_type::eq(*curr, c)) {
          // found a match
          return curr - m_data;
        }

        if (curr == m_data) {
          // at beginning, no more chances for match
          break;
        }
      }
    }
    // no match found
    return npos;
  }

private:
  CONSTEXPR14 void
  check_offset_bound(size_type offset) const
  {
    // check that the offset is not greater than the size
    if (offset > m_size) {
      throw std::out_of_range("invalid string_view position");
    }
  }

  CONSTEXPR14 void
  check_index_bound(size_type index) const
  {
    // check that the offset is not greater than the size
    if (index >= m_size) {
      throw std::out_of_range("invalid string_view position");
    }
  }

private:
  const_pointer m_data = nullptr;
  size_type m_size     = 0;
};

// operators for basic_string_view<>
// this has a c++11 compat version and a c++14/17 version that uses enable_if_t and some newer stuff to reduce the code
// Ideally we use the old version for c++11 compilers, newer version for c++14 compiler. For c++17 we should not need this file
// as we can use the builtin version then
#if __cplusplus < 201402

////////////////////////////
// ==
template <typename _Type, typename _Traits>
inline bool
operator==(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs._equal(rhs);
}
template <typename _Type, typename _Traits>
inline bool
operator==(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs._equal(lhs);
}
template <typename _Type, typename _Traits>
inline bool
operator==(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return lhs._equal(rhs);
}
template <typename _Type, typename _Traits>
inline bool
operator==(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) == 0;
}
template <typename _Type, typename _Traits>
inline bool
operator==(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) == 0;
}
////////////////////////////
// !=
template <typename _Type, typename _Traits>
inline bool
operator!=(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return !lhs._equal(rhs);
}
template <typename _Type, typename _Traits>
inline bool
operator!=(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return !rhs._equal(lhs);
}
template <typename _Type, typename _Traits>
inline bool
operator!=(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return !lhs._equal(rhs);
}
template <typename _Type, typename _Traits>
inline bool
operator!=(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) != 0;
}
template <typename _Type, typename _Traits>
inline bool
operator!=(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) != 0;
}
////////////////////////////
// <
template <typename _Type, typename _Traits>
inline bool
operator<(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) < 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) < 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return lhs.compare(rhs) < 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) < 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) < 0;
}
////////////////////////////
// >

template <typename _Type, typename _Traits>
inline bool
operator>(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) > 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) > 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return lhs.compare(rhs) > 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) > 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) < 0;
}

////////////////////////////
// <=
template <typename _Type, typename _Traits>
inline bool
operator<=(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) <= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<=(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) <= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<=(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return lhs.compare(rhs) <= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<=(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) <= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator<=(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) <= 0;
}
////////////////////////////
// >=
template <typename _Type, typename _Traits>
inline bool
operator>=(basic_string_view<_Type, _Traits> const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) >= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>=(std::string const &lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return lhs.compare(rhs) >= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>=(basic_string_view<_Type, _Traits> const lhs, std::string const &rhs)
{
  return lhs.compare(rhs) >= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>=(basic_string_view<_Type, _Traits> const lhs, char const *const rhs)
{
  return lhs.compare(rhs) >= 0;
}
template <typename _Type, typename _Traits>
inline bool
operator>=(char const *const lhs, basic_string_view<_Type, _Traits> const rhs)
{
  return rhs.compare(lhs) >= 0;
}

#else
/////////////////////////////////////////////////
// this form is more functional than the above case which only deals with char* and std::string
// this form deal with convertable type correctly as is less code :)

////////////////////////////
// ==
template <typename _Type, typename _Traits>
constexpr bool
operator==(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return lhs._equal(rhs);
}

// user conversion for stuff like std::string or char*, literals

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator==(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept(basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs))))
{
  return rhs._equal(std::forward<_OtherType>(lhs));
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator==(const basic_string_view<_Type, _Traits> lhs,
           _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return lhs._equal(std::forward<_OtherType>(rhs));
}

///////////////////////////////
// !=
template <typename _Type, typename _Traits>
constexpr bool
operator!=(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return !lhs._equal(rhs);
}

// user conversion for stuff like std::string or char*, literals
template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator!=(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)))))
{
  return !rhs._equal(std::forward<_OtherType>(lhs));
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator!=(const basic_string_view<_Type, _Traits> lhs,
           _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return !lhs._equal(std::forward<_OtherType>(rhs));
}

///////////////////////////////
// <
template <typename _Type, typename _Traits>
constexpr bool
operator<(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return lhs.compare(rhs) < 0;
}

// user conversion for stuff like std::string or char*, literals
template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator<(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)))))
{ // less-than compare objects convertible to basic_string_view instances
  return basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)).compare(rhs) < 0;
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator<(const basic_string_view<_Type, _Traits> lhs,
          _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return lhs.compare(std::forward<_OtherType>(rhs)) < 0;
}

///////////////////////////////
// >
template <typename _Type, typename _Traits>
constexpr bool
operator>(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return lhs.compare(rhs) > 0;
}
// user conversion for stuff like std::string or char*, literals
template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator>(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)))))
{
  return basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)).compare(rhs) > 0;
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator>(const basic_string_view<_Type, _Traits> lhs,
          _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return lhs.compare(std::forward<_OtherType>(rhs)) > 0;
}

///////////////////////////////
// <=
template <typename _Type, typename _Traits>
constexpr bool
operator<=(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return lhs.compare(rhs) <= 0;
}
// user conversion for stuff like std::string or char*, literals
template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator<=(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)))))
{
  return basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)).compare(rhs) <= 0;
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator<=(const basic_string_view<_Type, _Traits> lhs,
           _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return lhs.compare(std::forward<_OtherType>(rhs)) <= 0;
}

///////////////////////////////
// >=
template <typename _Type, typename _Traits>
constexpr bool
operator>=(const basic_string_view<_Type, _Traits> lhs, const basic_string_view<_Type, _Traits> rhs) noexcept
{
  return lhs.compare(rhs) >= 0;
}
// user conversion for stuff like std::string or char*, literals
template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator>=(_OtherType &&lhs, const basic_string_view<_Type, _Traits> rhs) noexcept(
  noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)))))
{
  return basic_string_view<_Type, _Traits>(std::forward<_OtherType>(lhs)).compare(rhs) >= 0;
}

template <typename _Type, typename _Traits, typename _OtherType,
          typename = std::enable_if_t<std::is_convertible<_OtherType, basic_string_view<_Type, _Traits>>::value>>
constexpr bool
operator>=(const basic_string_view<_Type, _Traits> lhs,
           _OtherType &&rhs) noexcept(noexcept((basic_string_view<_Type, _Traits>(std::forward<_OtherType>(rhs)))))
{
  return lhs.compare(std::forward<_OtherType>(rhs)) >= 0;
}
#endif
// stream operator

template <typename _Type, typename _Traits>
inline std::basic_ostream<_Type, _Traits> &
operator<<(std::basic_ostream<_Type, _Traits> &os, const basic_string_view<_Type, _Traits> lhs)
{
  return os.write(lhs.data(), lhs.size());
}
using string_view = basic_string_view<char>;

} // namespace ts
