/** @file

   Spans of memory. This is similar but independently developed from @c std::span. The goal is
   to provide convenient handling for chunks of memory. These chunks can be treated as arrays
   of arbitrary types via template methods.


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
#include <cstring>
#include <iosfwd>
#include <iostream>
#include <cstddef>
#include <string_view>

/// Apache Traffic Server commons.
namespace ts
{
/** A span of contiguous piece of memory.

    A @c MemSpan does not own the memory to which it refers, it is simply a span of part of some
    (presumably) larger memory object. The purpose is that frequently code needs to work on a specific
    part of the memory. This can avoid copying or allocation by allocating all needed memory at once
    and then working with it via instances of this class.
 */
class MemSpan
{
  using self_type = MemSpan; ///< Self reference type.

protected:
  void *_data     = nullptr; ///< Pointer to base of memory chunk.
  ptrdiff_t _size = 0;       ///< Size of memory chunk.

public:
  /// Default constructor (empty buffer).
  constexpr MemSpan();

  /** Construct explicitly with a pointer and size.
   */
  constexpr MemSpan(void *ptr,  ///< Pointer to buffer.
                    ptrdiff_t n ///< Size of buffer.
  );

  /** Construct from a half open range of two pointers.
      @note The instance at @start is in the span but the instance at @a end is not.
  */
  template <typename T>
  constexpr MemSpan(T *start, ///< First byte in the span.
                    T *end    ///< First byte not in the span.
  );

  /** Construct from a half open range of two pointers.
      @note The instance at @start is in the span but the instance at @a end is not.
  */
  MemSpan(void *start, ///< First byte in the span.
          void *end    ///< First byte not in the span.
  );

  /** Construct to cover an array.
   *
   * @tparam T Array element type.
   * @tparam N Number of elements in the array.
   * @param a The array.
   */
  template <typename T, size_t N> MemSpan(T (&a)[N]);

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr MemSpan(std::nullptr_t);

  /** Equality.

      Compare the span contents.

      @return @c true if the contents of @a that are the same as the content of @a this,
      @c false otherwise.
   */
  bool operator==(self_type const &that) const;

  /** Identical.

      Check if the spans refer to the same span of memory.
      @return @c true if @a this and @a that refer to the same span, @c false if not.
   */
  bool is_same(self_type const &that) const;

  /** Inequality.
      @return @c true if @a that does not refer to the same span as @a this,
      @c false otherwise.
   */
  bool operator!=(self_type const &that) const;

  /// Assignment - the span is copied, not the content.
  self_type &operator=(self_type const &that);

  /** Shift the span to discard the first byte.
      @return @a this.
  */
  self_type &operator++();

  /** Shift the span to discard the leading @a n bytes.
      @return @a this
  */
  self_type &operator+=(ptrdiff_t n);

  /// Check for empty span.
  /// @return @c true if the span is empty (no contents), @c false otherwise.
  bool operator!() const;

  /// Check for non-empty span.
  /// @return @c true if the span contains bytes.
  explicit operator bool() const;

  /// Check for empty span (no content).
  /// @see operator bool
  bool empty() const;

  /// @name Accessors.
  //@{
  /// Pointer to the first byte in the span.
  char *begin();
  const char *begin() const;

  /// Pointer to first byte not in the span.
  char *end();
  const char *end() const;

  /// Number of bytes in the span.
  constexpr ptrdiff_t ssize() const;
  size_t size() const;

  /// Pointer to memory in the span.
  void *data();

  /// Pointer to memory in the span.
  const void *data() const;

  /// Memory pointer, one past the last element of the span.
  void *data_end();
  const void *data_end() const;

  /// @return the @a V value at index @a n.
  template <typename V> V at(ptrdiff_t n) const;

  /// @return a pointer to the @a V value at index @a n.
  template <typename V> V const *ptr(ptrdiff_t n) const;
  //@}

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(void *ptr,      ///< Buffer address.
                    ptrdiff_t n = 0 ///< Buffer size.
  );

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(void *start,    ///< First valid character.
                    void const *end ///< First invalid character.
  );

  /// Clear the span (become an empty span).
  self_type &clear();

  /// @return @c true if the byte at @a *p is in the span.
  bool contains(const void *p) const;

  /** Find a value.
      The memory is searched as if it were an array of the value type @a V.

      @return A pointer to the first occurrence of @a v in @a this
      or @c nullptr if @a v is not found.
  */
  template <typename V> V *find(V v) const;

  /** Find a value.
      The memory is searched as if it were an array of type @a V.

      @return A pointer to the first value for which @a pred is @c true otherwise
      @c nullptr.
  */
  template <typename V, typename F> V *find_if(F const &pred);

  /** Get the initial segment of the span before @a p.

      The byte at @a p is not included. If @a p is not in the span an empty span
      is returned.

      @return A buffer that contains all data before @a p.
  */
  self_type prefix(const void *p) const;

  /** Get the first @a n bytes of the span.

      @return A span with the first @a n bytes of this span.
  */
  self_type prefix(ptrdiff_t n) const;

  /** Shrink the span from the front.
   *
   * @param p The limit of the removed span.
   * @return @c *this
   *
   * The byte at @a p is not removed.
   */

  self_type remove_prefix(void const *p);
  /** Shringt the span from the front.
   *
   * @param n The number of bytes to remove.
   * @return @c *this
   */
  self_type &remove_prefix(ptrdiff_t n);

  /** Get the trailing segment of the span after @a p.

      The byte at @a p is not included. If @a p is not in the span an empty span is returned.

      @return A buffer that contains all data after @a p.
  */
  self_type suffix(const void *p) const;

  /** Get the trailing @a n bytes.

      @return A span with @a n bytes of the current span.
  */
  self_type suffix(ptrdiff_t p) const;

  /** Shrink the span from the back.
   *
   * @param p The limit of the removed span.
   * @return @c *this
   *
   * The byte at @a p is not removed.
   */
  self_type &remove_suffix(void const *p);

  /** Shringt the span from the back.
   *
   * @param n The number of bytes to remove.
   * @return @c *this
   */
  self_type &remove_suffix(ptrdiff_t n);

  /** Return a view of the memory.
   *
   * @return A @c string_view covering the span contents.
   */
  std::string_view view() const;

  /** Support automatic conversion to string_view.
   *
   * @return A view of the memory in this span.
   */
  operator std::string_view() const;

  /// Internal utility for computing the difference of two void pointers.
  /// @return the byte (char) difference between the pointers, @a lhs - @a rhs
  static ptrdiff_t distance(void const *lhs, void const *rhs);
};

// -- Implementation --

inline int
memcmp(MemSpan const &lhs, MemSpan const &rhs)
{
  int zret    = 0;
  ptrdiff_t n = lhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1;
  } else if (lhs.size() > rhs.size()) {
    zret = -1;
    n    = rhs.size();
  }
  // else the sizes are equal therefore @a n and @a zret are already correct.

  int r = std::memcmp(lhs.data(), rhs.data(), n);
  if (0 != r) { // If we got a not-equal, override the size based result.
    zret = r;
  }

  return zret;
}
// need to bring memcmp in so this is an overload, not an override.
using std::memcmp;

inline constexpr MemSpan::MemSpan() {}

inline constexpr MemSpan::MemSpan(void *ptr, ptrdiff_t n) : _data(ptr), _size(n) {}

template <typename T> constexpr MemSpan::MemSpan(T *start, T *end) : _data(start), _size((end - start) * sizeof(T)) {}

// <void*> is magic, handle that specially.
// No constexpr because the spec specifically forbids casting from <void*> to a typed pointer.
inline MemSpan::MemSpan(void *start, void *end) : _data(start), _size(static_cast<char *>(end) - static_cast<char *>(start)) {}

template <typename T, size_t N> MemSpan::MemSpan(T (&a)[N]) : _data(a), _size(N * sizeof(T)) {}

inline constexpr MemSpan::MemSpan(std::nullptr_t) {}

inline ptrdiff_t
MemSpan::distance(void const *lhs, void const *rhs)
{
  return static_cast<const char *>(lhs) - static_cast<const char *>(rhs);
}

inline MemSpan &
MemSpan::assign(void *ptr, ptrdiff_t n)
{
  _data = ptr;
  _size = n;
  return *this;
}

inline MemSpan &
MemSpan::assign(void *ptr, void const *limit)
{
  _data = ptr;
  _size = static_cast<const char *>(limit) - static_cast<const char *>(ptr);
  return *this;
}

inline MemSpan &
MemSpan::clear()
{
  _data = nullptr;
  _size = 0;
  return *this;
}

inline bool
MemSpan::is_same(self_type const &that) const
{
  return _data == that._data && _size == that._size;
}

inline bool
MemSpan::operator==(self_type const &that) const
{
  return _size == that._size && (_data == that._data || 0 == memcmp(this->data(), that.data(), _size));
}

inline bool
MemSpan::operator!=(self_type const &that) const
{
  return !(*this == that);
}

inline bool MemSpan::operator!() const
{
  return _size == 0;
}

inline MemSpan::operator bool() const
{
  return _size != 0;
}

inline bool
MemSpan::empty() const
{
  return _size == 0;
}

inline MemSpan &
MemSpan::operator++()
{
  _data = static_cast<char *>(_data) + 1;
  --_size;
  return *this;
}

inline MemSpan &
MemSpan::operator+=(ptrdiff_t n)
{
  if (n > _size) {
    this->clear();
  } else {
    _data = static_cast<char *>(_data) + n;
    _size -= n;
  }
  return *this;
}

inline char *
MemSpan::begin()
{
  return static_cast<char *>(_data);
}

inline const char *
MemSpan::begin() const
{
  return static_cast<const char *>(_data);
}

inline void *
MemSpan::data()
{
  return _data;
}

inline const void *
MemSpan::data() const
{
  return _data;
}

inline char *
MemSpan::end()
{
  return static_cast<char *>(_data) + _size;
}

inline const char *
MemSpan::end() const
{
  return static_cast<const char *>(_data) + _size;
}

inline void *
MemSpan::data_end()
{
  return static_cast<char *>(_data) + _size;
}

inline const void *
MemSpan::data_end() const
{
  return static_cast<char *>(_data) + _size;
}

inline constexpr ptrdiff_t
MemSpan::ssize() const
{
  return _size;
}

inline size_t
MemSpan::size() const
{
  return static_cast<size_t>(_size);
}

inline MemSpan &
MemSpan::operator=(MemSpan const &that)
{
  _data = that._data;
  _size = that._size;
  return *this;
}

inline bool
MemSpan::contains(const void *p) const
{
  return !this->empty() && _data <= p && p < this->data_end();
}

inline MemSpan
MemSpan::prefix(const void *p) const
{
  self_type zret;
  if (_data <= p && p <= this->data_end())
    zret.assign(_data, p);
  return zret;
}

inline MemSpan
MemSpan::prefix(ptrdiff_t n) const
{
  return {_data, std::min(n, _size)};
}

inline MemSpan &
MemSpan::remove_prefix(ptrdiff_t n)
{
  if (n < 0) {
  } else if (n <= _size) {
    _size -= n;
    _data = static_cast<char *>(_data) + n;
  } else {
    this->clear();
  }
  return *this;
}

inline MemSpan
MemSpan::suffix(void const *p) const
{
  self_type zret;
  if (_data <= p && p <= this->data_end()) {
    zret.assign(const_cast<void *>(p), this->data_end());
  }
  return zret;
}

inline MemSpan
MemSpan::suffix(ptrdiff_t n) const
{
  self_type zret;
  if (n < 0) {
    n = std::max(ptrdiff_t{0}, n + _size);
  }
  if (n <= _size) {
    zret.assign(static_cast<char *>(_data) + n, _size - n);
  }
  return zret;
}

inline MemSpan &
MemSpan::remove_suffix(void const *p)
{
  if (_data <= p && p <= this->data_end()) {
    _size -= distance(this->data_end(), p);
  }
  return *this;
}

inline MemSpan &
MemSpan::remove_suffix(ptrdiff_t n)
{
  if (n < 0) {
    n = std::max(ptrdiff_t{0}, n + _size);
  }
  if (n <= _size) {
    _size -= n;
    _data = static_cast<char *>(_data) + n;
  }
  return *this;
}

template <typename V>
inline V
MemSpan::at(ptrdiff_t n) const
{
  return static_cast<V *>(_data)[n];
}

template <typename V>
inline V const *
MemSpan::ptr(ptrdiff_t n) const
{
  return static_cast<V const *>(_data) + n;
}

template <typename V>
inline V *
MemSpan::find(V v) const
{
  for (V *spot = static_cast<V *>(_data), *limit = spot + (_size / sizeof(V)); spot < limit; ++spot)
    if (v == *spot)
      return spot;
  return nullptr;
}

// Specialize char for performance.
template <>
inline char *
MemSpan::find(char v) const
{
  return static_cast<char *>(memchr(_data, v, _size));
}

template <typename V, typename F>
inline V *
MemSpan::find_if(F const &pred)
{
  for (V *p = static_cast<V *>(_data), *limit = p + (_size / sizeof(V)); p < limit; ++p)
    if (pred(*p))
      return p;
  return nullptr;
}

inline std::string_view
MemSpan::view() const
{
  return {static_cast<const char *>(_data), static_cast<size_t>(_size)};
}

inline MemSpan::operator std::string_view() const
{
  return this->view();
}

} // namespace ts

namespace std
{
inline ostream &
operator<<(ostream &os, const ts::MemSpan &b)
{
  if (os.good()) {
    os << b.size() << '@' << hex << b.data();
  }
  return os;
}
} // namespace std
