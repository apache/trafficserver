/** @file

  Localized version of Boost.String_Ref

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

#ifndef TS_STRING_REF_H
#define TS_STRING_REF_H

#include <algorithm>
#include <cstddef>
#include <iosfwd>
#include <iterator>
#include <stdexcept>
#include <string>

namespace ts
{
template <typename charT, typename traits> class GenericStringRef
{
public:
  // types
  typedef charT value_type;
  typedef const charT *pointer;
  typedef const charT &reference;
  typedef const charT &const_reference;
  typedef pointer const_iterator; // impl-defined
  typedef const_iterator iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
  typedef const_reverse_iterator reverse_iterator;
  typedef std::size_t size_type;
  typedef std::ptrdiff_t difference_type;
  static constexpr size_type npos = static_cast<size_type>(-1);

  // construct/copy
  constexpr GenericStringRef() : ptr_(nullptr), len_(0) {}
  constexpr GenericStringRef(const GenericStringRef &rhs) : ptr_(rhs.ptr_), len_(rhs.len_) {}
  GenericStringRef &
  operator=(const GenericStringRef &rhs)
  {
    ptr_ = rhs.ptr_;
    len_ = rhs.len_;
    return *this;
  }

  GenericStringRef(const charT *str) : ptr_(str), len_(traits::length(str)) {}
  template <typename Allocator>
  GenericStringRef(const std::basic_string<charT, traits, Allocator> &str) : ptr_(str.data()), len_(str.length())
  {
  }

  constexpr GenericStringRef(const charT *str, size_type len) : ptr_(str), len_(len) {}
  template <typename Allocator> explicit operator std::basic_string<charT, traits, Allocator>() const
  {
    return std::basic_string<charT, traits, Allocator>(begin(), end());
  }

  std::basic_string<charT, traits>
  to_string() const
  {
    return std::basic_string<charT, traits>(begin(), end());
  }

  // iterators
  constexpr const_iterator
  begin() const
  {
    return ptr_;
  }
  constexpr const_iterator
  cbegin() const
  {
    return ptr_;
  }
  constexpr const_iterator
  end() const
  {
    return ptr_ + len_;
  }
  constexpr const_iterator
  cend() const
  {
    return ptr_ + len_;
  }
  const_reverse_iterator
  rbegin() const
  {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator
  crbegin() const
  {
    return const_reverse_iterator(end());
  }
  const_reverse_iterator
  rend() const
  {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator
  crend() const
  {
    return const_reverse_iterator(begin());
  }

  // capacity
  constexpr size_type
  size() const
  {
    return len_;
  }
  constexpr size_type
  length() const
  {
    return len_;
  }
  constexpr size_type
  max_size() const
  {
    return len_;
  }
  constexpr bool
  empty() const
  {
    return len_ == 0;
  }

  // element access
  constexpr const charT &operator[](size_type pos) const { return ptr_[pos]; }
  const charT &
  at(size_t pos) const
  {
    if (pos >= len_)
      throw(std::out_of_range("ts::StringRef::at"));
    return ptr_[pos];
  }

  constexpr const charT &
  front() const
  {
    return ptr_[0];
  }
  constexpr const charT &
  back() const
  {
    return ptr_[len_ - 1];
  }
  constexpr const charT *
  data() const
  {
    return ptr_;
  }

  // modifiers
  void
  clear()
  {
    len_ = 0;
  }
  void
  remove_prefix(size_type n)
  {
    if (n > len_)
      n = len_;
    ptr_ += n;
    len_ -= n;
  }

  void
  remove_suffix(size_type n)
  {
    if (n > len_)
      n = len_;
    len_ -= n;
  }

  // GenericStringRef string operations
  GenericStringRef
  substr(size_type pos, size_type n = npos) const
  {
    if (pos > size())
      throw(std::out_of_range("string_ref::substr"));
    if (n == npos || pos + n > size())
      n = size() - pos;
    return GenericStringRef(data() + pos, n);
  }

  int
  compare(GenericStringRef x) const
  {
    const int cmp = traits::compare(ptr_, x.ptr_, (std::min)(len_, x.len_));
    return cmp != 0 ? cmp : (len_ == x.len_ ? 0 : len_ < x.len_ ? -1 : 1);
  }

  bool
  starts_with(charT c) const
  {
    return !empty() && traits::eq(c, front());
  }
  bool
  starts_with(GenericStringRef x) const
  {
    return len_ >= x.len_ && traits::compare(ptr_, x.ptr_, x.len_) == 0;
  }

  bool
  ends_with(charT c) const
  {
    return !empty() && traits::eq(c, back());
  }
  bool
  ends_with(GenericStringRef x) const
  {
    return len_ >= x.len_ && traits::compare(ptr_ + len_ - x.len_, x.ptr_, x.len_) == 0;
  }

  size_type
  find(GenericStringRef s) const
  {
    const_iterator iter = std::search(this->cbegin(), this->cend(), s.cbegin(), s.cend(), traits::eq);
    return iter == this->cend() ? npos : std::distance(this->cbegin(), iter);
  }

  size_type
  find(charT c) const
  {
    const_iterator iter = std::find_if(this->cbegin(), this->cend(), [c](charT val) { return traits::eq(c, val); });
    return iter == this->cend() ? npos : std::distance(this->cbegin(), iter);
  }

  size_type
  rfind(GenericStringRef s) const
  {
    const_reverse_iterator iter = std::search(this->crbegin(), this->crend(), s.crbegin(), s.crend(), traits::eq);
    return iter == this->crend() ? npos : reverse_distance(this->crbegin(), iter);
  }

  size_type
  rfind(charT c) const
  {
    const_reverse_iterator iter = std::find_if(this->crbegin(), this->crend(), [c](charT val) { return traits::eq(c, val); });
    return iter == this->crend() ? npos : reverse_distance(this->crbegin(), iter);
  }

  size_type
  find_first_of(charT c) const
  {
    return find(c);
  }
  size_type
  find_last_of(charT c) const
  {
    return rfind(c);
  }

  size_type
  find_first_of(GenericStringRef s) const
  {
    const_iterator iter = std::find_first_of(this->cbegin(), this->cend(), s.cbegin(), s.cend(), traits::eq);
    return iter == this->cend() ? npos : std::distance(this->cbegin(), iter);
  }

  size_type
  find_last_of(GenericStringRef s) const
  {
    const_reverse_iterator iter = std::find_first_of(this->crbegin(), this->crend(), s.cbegin(), s.cend(), traits::eq);
    return iter == this->crend() ? npos : reverse_distance(this->crbegin(), iter);
  }

  size_type
  find_first_not_of(GenericStringRef s) const
  {
    const_iterator iter = find_not_of(this->cbegin(), this->cend(), s);
    return iter == this->cend() ? npos : std::distance(this->cbegin(), iter);
  }

  size_type
  find_first_not_of(charT c) const
  {
    for (const_iterator iter = this->cbegin(); iter != this->cend(); ++iter)
      if (!traits::eq(c, *iter))
        return std::distance(this->cbegin(), iter);
    return npos;
  }

  size_type
  find_last_not_of(GenericStringRef s) const
  {
    const_reverse_iterator iter = find_not_of(this->crbegin(), this->crend(), s);
    return iter == this->crend() ? npos : reverse_distance(this->crbegin(), iter);
  }

  size_type
  find_last_not_of(charT c) const
  {
    for (const_reverse_iterator iter = this->crbegin(); iter != this->crend(); ++iter)
      if (!traits::eq(c, *iter))
        return reverse_distance(this->crbegin(), iter);
    return npos;
  }

private:
  template <typename r_iter>
  size_type
  reverse_distance(r_iter first, r_iter last) const
  {
    return len_ - 1 - std::distance(first, last);
  }

  template <typename Iterator>
  Iterator
  find_not_of(Iterator first, Iterator last, GenericStringRef s) const
  {
    for (; first != last; ++first)
      if (0 == traits::find(s.ptr_, s.len_, *first))
        return first;
    return last;
  }

  const charT *ptr_;
  std::size_t len_;
};

//  Comparison operators
//  Equality
template <typename charT, typename traits>
inline bool
operator==(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  if (x.size() != y.size())
    return false;
  return x.compare(y) == 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator==(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x == GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator==(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) == y;
}

template <typename charT, typename traits>
inline bool
operator==(GenericStringRef<charT, traits> x, const charT *y)
{
  return x == GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator==(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) == y;
}

//  Inequality
template <typename charT, typename traits>
inline bool
operator!=(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  if (x.size() != y.size())
    return true;
  return x.compare(y) != 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator!=(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x != GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator!=(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) != y;
}

template <typename charT, typename traits>
inline bool
operator!=(GenericStringRef<charT, traits> x, const charT *y)
{
  return x != GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator!=(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) != y;
}

//  Less than
template <typename charT, typename traits>
inline bool
operator<(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  return x.compare(y) < 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator<(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x < GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator<(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) < y;
}

template <typename charT, typename traits>
inline bool
operator<(GenericStringRef<charT, traits> x, const charT *y)
{
  return x < GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator<(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) < y;
}

//  Greater than
template <typename charT, typename traits>
inline bool
operator>(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  return x.compare(y) > 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator>(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x > GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator>(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) > y;
}

template <typename charT, typename traits>
inline bool
operator>(GenericStringRef<charT, traits> x, const charT *y)
{
  return x > GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator>(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) > y;
}

//  Less than or equal to
template <typename charT, typename traits>
inline bool
operator<=(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  return x.compare(y) <= 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator<=(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x <= GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator<=(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) <= y;
}

template <typename charT, typename traits>
inline bool
operator<=(GenericStringRef<charT, traits> x, const charT *y)
{
  return x <= GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator<=(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) <= y;
}

//  Greater than or equal to
template <typename charT, typename traits>
inline bool
operator>=(GenericStringRef<charT, traits> x, GenericStringRef<charT, traits> y)
{
  return x.compare(y) >= 0;
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator>=(GenericStringRef<charT, traits> x, const std::basic_string<charT, traits, Allocator> &y)
{
  return x >= GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits, typename Allocator>
inline bool
operator>=(const std::basic_string<charT, traits, Allocator> &x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) >= y;
}

template <typename charT, typename traits>
inline bool
operator>=(GenericStringRef<charT, traits> x, const charT *y)
{
  return x >= GenericStringRef<charT, traits>(y);
}

template <typename charT, typename traits>
inline bool
operator>=(const charT *x, GenericStringRef<charT, traits> y)
{
  return GenericStringRef<charT, traits>(x) >= y;
}

namespace detail
{
  template <class charT, class traits>
  inline void
  insert_fill_chars(std::basic_ostream<charT, traits> &os, std::size_t n)
  {
    enum { chunk_size = 8 };
    charT fill_chars[chunk_size];
    std::fill_n(fill_chars, static_cast<std::size_t>(chunk_size), os.fill());
    for (; n >= chunk_size && os.good(); n -= chunk_size)
      os.write(fill_chars, static_cast<std::size_t>(chunk_size));
    if (n > 0 && os.good())
      os.write(fill_chars, n);
  }

  template <class charT, class traits>
  void
  insert_aligned(std::basic_ostream<charT, traits> &os, const GenericStringRef<charT, traits> &str)
  {
    const std::size_t size           = str.size();
    const std::size_t alignment_size = static_cast<std::size_t>(os.width()) - size;
    const bool align_left =
      (os.flags() & std::basic_ostream<charT, traits>::adjustfield) == std::basic_ostream<charT, traits>::left;
    if (!align_left) {
      detail::insert_fill_chars(os, alignment_size);
      if (os.good())
        os.write(str.data(), size);
    } else {
      os.write(str.data(), size);
      if (os.good())
        detail::insert_fill_chars(os, alignment_size);
    }
  }

} // namespace detail

// Inserter
template <class charT, class traits>
inline std::basic_ostream<charT, traits> &
operator<<(std::basic_ostream<charT, traits> &os, const GenericStringRef<charT, traits> &str)
{
  if (os.good()) {
    const std::size_t size = str.size();
    const std::size_t w    = static_cast<std::size_t>(os.width());
    if (w <= size)
      os.write(str.data(), size);
    else
      detail::insert_aligned(os, str);
    os.width(0);
  }
  return os;
}

typedef GenericStringRef<char, std::char_traits<char>> StringRef;

} // namespace ts

#if 0
// Need to update these for ATS hash support.
namespace std {
    // Hashing
    template<> struct hash<boost::string_ref>;
    template<> struct hash<boost::u16string_ref>;
    template<> struct hash<boost::u32string_ref>;
    template<> struct hash<boost::wstring_ref>;
}
#endif

#endif
