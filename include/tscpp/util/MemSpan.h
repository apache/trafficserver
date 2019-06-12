/** @file

   Spans of writable memory. This is similar but independently developed from @c std::span. The goal
   is to provide convenient handling for chunks of memory. These chunks can be treated as arrays of
   arbitrary types via template methods.
*/

/* Licensed to the Apache Software Foundation (ASF) under one or more contributor license
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
#include <type_traits>
#include <ratio>
#include <exception>

namespace ts
{
/** A span of contiguous piece of memory.

    A @c MemSpan does not own the memory to which it refers, it is simply a span of part of some
    (presumably) larger memory object. It acts as a pointer, not a container - copy and assignment
    change the span, not the memory to which the span refers.

    The purpose is that frequently code needs to work on a specific part of the memory. This can
    avoid copying or allocation by allocating all needed memory at once and then working with it via
    instances of this class.

 */
template <typename T> class MemSpan
{
  using self_type = MemSpan; ///< Self reference type.

protected:
  T *_ptr       = nullptr; ///< Pointer to base of memory chunk.
  size_t _count = 0;       ///< Number of elements.

public:
  using value_type = T;

  /// Default constructor (empty buffer).
  constexpr MemSpan() = default;

  /// Copy constructor.
  constexpr MemSpan(self_type const &that) = default;

  /** Construct from a first element @a start and a @a count of elements.
   *
   * @param start First element.
   * @param count Total number of elements.
   */
  constexpr MemSpan(value_type *start, size_t count);

  /** Construct from a half open range [start, last).
   *
   * @param start Start of range.
   * @param last Past end of range.
   */
  constexpr MemSpan(value_type *start, value_type *last);

  /** Construct to cover an array.
   *
   * @tparam N Number of elements in the array.
   * @param a The array.
   */
  template <size_t N> MemSpan(T (&a)[N]);

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
  self_type &operator=(self_type const &that) = default;

  /// Access element at index @a idx.
  T &operator[](size_t idx) const;

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
  /// Pointer to the first element in the span.
  T *begin() const;

  /// Pointer to first element not in the span.
  T *end() const;

  /// Number of elements in the span
  size_t count() const;

  /// Number of bytes in the span.
  size_t size() const;

  /// Pointer to memory in the span.
  T *data() const;

  /** Make a copy of @a this span on the same memory but of type @a U.
   *
   * @tparam U Type for the created span.
   * @return A @c MemSpan which contains the same memory as instances of @a U.
   */
  template <typename U = void> MemSpan<U> rebind() const;

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(T *ptr,      ///< Buffer start.
                    size_t count ///< # of elements.
  );

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(T *first,     ///< First valid element.
                    T const *last ///< First invalid element.
  );

  /// Clear the span (become an empty span).
  self_type &clear();

  /// @return @c true if the byte at @a *p is in the span.
  bool contains(value_type const *p) const;

  /** Get the initial segment of @a count elements.

      @return An instance that contains the leading @a count elements of @a this.
  */
  self_type prefix(size_t count) const;

  /** Shrink the span by removing @a count leading elements.
   *
   * @param count The number of elements to remove.
   * @return @c *this
   */
  self_type &remove_prefix(size_t count);

  /** Get the trailing segment of @a count elements.
   *
   * @param count Number of elements to retrieve.
   * @return An instance that contains the trailing @a count elements of @a this.
   */
  self_type suffix(size_t count) const;

  /** Shrink the span by removing @a count trailing elements.
   *
   * @param count Number of elements to remove.
   * @return @c *this
   */
  self_type &remove_suffix(size_t count);

  /** Return a view of the memory.
   *
   * @return A @c string_view covering the span contents.
   */
  std::string_view view() const;

  template <typename U> friend class MemSpan;
};

/** Specialization for void pointers.
 *
 * Key differences:
 *
 * - No subscript operator.
 * - No array initialization.
 * - All other @c MemSpan types implicitly convert to this type.
 *
 * @internal I tried to be clever about the base template but there were too many differences
 * One major issue was the array initialization did not work at all if the @c void case didn't
 * exclude that. Once separate there are a number of useful tweaks available.
 */
template <> class MemSpan<void>
{
  using self_type = MemSpan; ///< Self reference type.
  template <typename U> friend class MemSpan;

public:
  using value_type = void; /// Export base type.

protected:
  value_type *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Number of elements.

public:
  /// Default constructor (empty buffer).
  constexpr MemSpan() = default;

  /// Copy constructor.
  constexpr MemSpan(self_type const &that) = default;

  /** Cross type copy constructor.
   *
   * @tparam U Type for source span.
   * @param that Source span.
   *
   * This enables any @c MemSpan to be automatically converted to a void span, just as any pointer
   * can convert to a void pointer.
   */
  template <typename U> constexpr MemSpan(MemSpan<U> const &that);

  /** Construct from a pointer @a start and a size @a n bytes.
   *
   * @param start Start of the span.
   * @param n # of bytes in the span.
   */
  constexpr MemSpan(value_type *start, size_t n);

  /** Construct from a half open range of [start, last).
   *
   * @param start Start of the range.
   * @param last Past end of range.
   */
  MemSpan(value_type *start, value_type *last);

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr MemSpan(std::nullptr_t);

  /** Equality.

      Compare the span contents.

      @return @c true if the contents of @a that are bytewise the same as the content of @a this,
      @c false otherwise.
   */
  bool operator==(self_type const &that) const;

  /** Identical.

      Check if the spans refer to the same span of memory.

      @return @c true if @a this and @a that refer to the same memory, @c false if not.
   */
  bool is_same(self_type const &that) const;

  /** Inequality.
      @return @c true if @a that does not refer to the same span as @a this,
      @c false otherwise.
   */
  bool operator!=(self_type const &that) const;

  /// Assignment - the span is copied, not the content.
  /// Any type of @c MemSpan can be assigned to @c MemSpan<void>.
  template <typename U> self_type &operator=(MemSpan<U> const &that);

  /// Check for empty span.
  /// @return @c true if the span is empty (no contents), @c false otherwise.
  bool operator!() const;

  /// Check for non-empty span.
  /// @return @c true if the span contains bytes.
  explicit operator bool() const;

  /// Check for empty span (no content).
  /// @see operator bool
  bool empty() const;

  /// Number of bytes in the span.
  size_t size() const;

  /// Pointer to memory in the span.
  value_type *data() const;

  /// Pointer to memory in the span.
  value_type *data_end() const;

  /** Create a new span for a different type @a V on the same memory.
   *
   * @tparam V Type for the created span.
   * @return A @c MemSpan which contains the same memory as instances of @a V.
   */
  template <typename U> MemSpan<U> rebind() const;

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(value_type *ptr, ///< Buffer start.
                    size_t n         ///< # of bytes
  );

  /// Set the span.
  /// This is faster but equivalent to constructing a new span with the same
  /// arguments and assigning it.
  /// @return @c this.
  self_type &assign(value_type *first,     ///< First valid element.
                    value_type const *last ///< First invalid element.
  );

  /// Clear the span (become an empty span).
  self_type &clear();

  /// @return @c true if the byte at @a *ptr is in the span.
  bool contains(value_type const *ptr) const;

  /** Get the initial segment of @a n bytes.

      @return An instance that contains the leading @a n bytes of @a this.
  */
  self_type prefix(size_t n) const;

  /** Shrink the span by removing @a n leading bytes.
   *
   * @param count The number of elements to remove.
   * @return @c *this
   */

  self_type &remove_prefix(size_t count);

  /** Get the trailing segment of @a n bytes.
   *
   * @param n Number of bytes to retrieve.
   * @return An instance that contains the trailing @a count elements of @a this.
   */
  self_type suffix(size_t n) const;

  /** Shrink the span by removing @a n bytes.
   *
   * @param n Number of bytes to remove.
   * @return @c *this
   */
  self_type &remove_suffix(size_t n);

  /** Return a view of the memory.
   *
   * @return A @c string_view covering the span contents.
   */
  std::string_view view() const;
};

// -- Implementation --

namespace detail
{
  /// Suport pointer distance calculations for all types, @b include @c <void*>.
  /// This is useful in templates.
  inline size_t
  ptr_distance(void const *first, void const *last)
  {
    return static_cast<const char *>(last) - static_cast<const char *>(first);
  }

  template <typename T>
  size_t
  ptr_distance(T const *first, T const *last)
  {
    return last - first;
  }

  /** Functor to convert span types.
   *
   * @tparam T Source span type.
   * @tparam U Destination span type.
   *
   * @internal More void handling. This can't go in @c MemSpan because template specialization is
   * invalid in class scope and this needs to be specialized for @c void.
   */
  template <typename T, typename U> struct is_span_compatible {
    /// @c true if the size of @a T is an integral multiple of the size of @a U or vice versa.
    static constexpr bool value = std::ratio<sizeof(T), sizeof(U)>::num == 1 || std::ratio<sizeof(U), sizeof(T)>::num == 1;
    /** Compute the new size in units of @c sizeof(U).
     *
     * @param size Size in bytes.
     * @return Size in units of @c sizeof(U).
     *
     * The critical part of this is the @c static_assert that guarantees the result is an integral
     * number of instances of @a U.
     */
    static size_t count(size_t size);
  };

  template <typename T, typename U>
  size_t
  is_span_compatible<T, U>::count(size_t size)
  {
    if (size % sizeof(U)) {
      throw std::invalid_argument("MemSpan rebind where span size is not a multiple of the element size");
    }
    return size / sizeof(U);
  }

  /// @cond INTERNAL_DETAIL
  // Must specialize for rebinding to @c void because @c sizeof doesn't work. Rebinding from @c void
  // is handled by the @c MemSpan<void>::rebind specialization and doesn't use this mechanism.
  template <typename T> struct is_span_compatible<T, void> {
    static constexpr bool value = true;
    static size_t count(size_t size);
  };

  template <typename T>
  size_t
  is_span_compatible<T, void>::count(size_t size)
  {
    return size;
  }
  /// @endcond

} // namespace detail

// --- Standard memory operations ---

template <typename T>
int
memcmp(MemSpan<T> const &lhs, MemSpan<T> const &rhs)
{
  int zret = 0;
  size_t n = lhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.count() < rhs.count()) {
    zret = 1;
  } else if (lhs.count() > rhs.count()) {
    zret = -1;
    n    = rhs.size();
  }
  // else the counts are equal therefore @a n and @a zret are already correct.

  int r = std::memcmp(lhs.data(), rhs.data(), n);
  if (0 != r) { // If we got a not-equal, override the size based result.
    zret = r;
  }

  return zret;
}

using std::memcmp;

template <typename T>
T *
memcpy(MemSpan<T> &dst, MemSpan<T> const &src)
{
  return static_cast<T *>(std::memcpy(dst.data(), src.data(), std::min(dst.size(), src.size())));
}

template <typename T>
T *
memcpy(MemSpan<T> &dst, T *src)
{
  return static_cast<T *>(std::memcpy(dst.data(), src, dst.size()));
}

template <typename T>
T *
memcpy(T *dst, MemSpan<T> &src)
{
  return static_cast<T *>(std::memcpy(dst, src.data(), src.size()));
}

inline char *
memcpy(MemSpan<char> &span, std::string_view view)
{
  return static_cast<char *>(std::memcpy(span.data(), view.data(), std::min(view.size(), view.size())));
}

inline void *
memcpy(MemSpan<void> &span, std::string_view view)
{
  return std::memcpy(span.data(), view.data(), std::min(view.size(), view.size()));
}

using std::memcpy;
using std::memcpy;

template <typename T>
inline MemSpan<T> const &
memset(MemSpan<T> const &dst, T const &t)
{
  for (auto &e : dst) {
    e = t;
  }
  return dst;
}

inline MemSpan<char> const &
memset(MemSpan<char> const &dst, char c)
{
  std::memset(dst.data(), c, dst.size());
  return dst;
}

inline MemSpan<unsigned char> const &
memset(MemSpan<unsigned char> const &dst, unsigned char c)
{
  std::memset(dst.data(), c, dst.size());
  return dst;
}

inline MemSpan<void> const &
memset(MemSpan<void> const &dst, char c)
{
  std::memset(dst.data(), c, dst.size());
  return dst;
}

using std::memset;

// --- MemSpan<T> ---

template <typename T> constexpr MemSpan<T>::MemSpan(T *ptr, size_t count) : _ptr{ptr}, _count{count} {}

template <typename T> constexpr MemSpan<T>::MemSpan(T *first, T *last) : _ptr{first}, _count{detail::ptr_distance(first, last)} {}

template <typename T> template <size_t N> MemSpan<T>::MemSpan(T (&a)[N]) : _ptr{a}, _count{N} {}

template <typename T> constexpr MemSpan<T>::MemSpan(std::nullptr_t) {}

template <typename T>
MemSpan<T> &
MemSpan<T>::assign(T *ptr, size_t count)
{
  _ptr   = ptr;
  _count = count;
  return *this;
}

template <typename T>
MemSpan<T> &
MemSpan<T>::assign(T *first, T const *last)
{
  _ptr   = first;
  _count = detail::ptr_distance(first, last);
  return *this;
}

template <typename T>
MemSpan<T> &
MemSpan<T>::clear()
{
  _ptr   = nullptr;
  _count = 0;
  return *this;
}

template <typename T>
bool
MemSpan<T>::is_same(self_type const &that) const
{
  return _ptr == that._ptr && _count == that._count;
}

template <typename T>
bool
MemSpan<T>::operator==(self_type const &that) const
{
  return _count == that._count && (_ptr == that._ptr || 0 == memcmp(_ptr, that._ptr, this->size()));
}

template <typename T>
bool
MemSpan<T>::operator!=(self_type const &that) const
{
  return !(*this == that);
}

template <typename T> bool MemSpan<T>::operator!() const
{
  return _count == 0;
}

template <typename T> MemSpan<T>::operator bool() const
{
  return _count != 0;
}

template <typename T>
bool
MemSpan<T>::empty() const
{
  return _count == 0;
}

template <typename T>
T *
MemSpan<T>::begin() const
{
  return _ptr;
}

template <typename T>
T *
MemSpan<T>::data() const
{
  return _ptr;
}

template <typename T>
T *
MemSpan<T>::end() const
{
  return _ptr + _count;
}

template <typename T> T &MemSpan<T>::operator[](size_t idx) const
{
  return _ptr[idx];
}

template <typename T>
size_t
MemSpan<T>::count() const
{
  return _count;
}

template <typename T>
size_t
MemSpan<T>::size() const
{
  return _count * sizeof(T);
}

template <typename T>
bool
MemSpan<T>::contains(T const *ptr) const
{
  return _ptr <= ptr && ptr < _ptr + _count;
}

template <typename T>
auto
MemSpan<T>::prefix(size_t count) const -> self_type
{
  return {_ptr, std::min(count, _count)};
}

template <typename T>
auto
MemSpan<T>::remove_prefix(size_t count) -> self_type &
{
  count = std::min(_count, count);
  _count -= count;
  _ptr += count;
  return *this;
}

template <typename T>
auto
MemSpan<T>::suffix(size_t count) const -> self_type
{
  count = std::min(_count, count);
  return {(_ptr + _count) - count, count};
}

template <typename T>
MemSpan<T> &
MemSpan<T>::remove_suffix(size_t count)
{
  _count -= std::min(count, _count);
  return *this;
}

template <typename T>
template <typename U>
MemSpan<U>
MemSpan<T>::rebind() const
{
  static_assert(detail::is_span_compatible<T, U>::value,
                "MemSpan only allows rebinding between types who sizes are integral multiples.");
  return {static_cast<U *>(static_cast<void *>(_ptr)), detail::is_span_compatible<T, U>::count(this->size())};
}

template <typename T>
std::string_view
MemSpan<T>::view() const
{
  return {static_cast<const char *>(_ptr), this->size()};
}

// --- void specialization ---

template <typename U> constexpr MemSpan<void>::MemSpan(MemSpan<U> const &that) : _ptr(that._ptr), _size(that.size()) {}

inline constexpr MemSpan<void>::MemSpan(value_type *ptr, size_t n) : _ptr{ptr}, _size{n} {}

inline MemSpan<void>::MemSpan(value_type *first, value_type *last) : _ptr{first}, _size{detail::ptr_distance(first, last)} {}

inline constexpr MemSpan<void>::MemSpan(std::nullptr_t) {}

inline MemSpan<void> &
MemSpan<void>::assign(value_type *ptr, size_t n)
{
  _ptr  = ptr;
  _size = n;
  return *this;
}

inline MemSpan<void> &
MemSpan<void>::assign(value_type *first, value_type const *last)
{
  _ptr  = first;
  _size = detail::ptr_distance(first, last);
  return *this;
}

inline MemSpan<void> &
MemSpan<void>::clear()
{
  _ptr  = nullptr;
  _size = 0;
  return *this;
}

inline bool
MemSpan<void>::is_same(self_type const &that) const
{
  return _ptr == that._ptr && _size == that._size;
}

inline bool
MemSpan<void>::operator==(self_type const &that) const
{
  return _size == that._size && (_ptr == that._ptr || 0 == memcmp(_ptr, that._ptr, _size));
}

inline bool
MemSpan<void>::operator!=(self_type const &that) const
{
  return !(*this == that);
}

inline bool MemSpan<void>::operator!() const
{
  return _size == 0;
}

inline MemSpan<void>::operator bool() const
{
  return _size != 0;
}

inline bool
MemSpan<void>::empty() const
{
  return _size == 0;
}

inline void *
MemSpan<void>::data() const
{
  return _ptr;
}

inline void *
MemSpan<void>::data_end() const
{
  return static_cast<char *>(_ptr) + _size;
}

inline size_t
MemSpan<void>::size() const
{
  return _size;
}

template <typename U>
auto
MemSpan<void>::operator=(MemSpan<U> const &that) -> self_type &
{
  _ptr  = that._ptr;
  _size = that.size();
  return *this;
}

inline bool
MemSpan<void>::contains(value_type const *ptr) const
{
  return _ptr <= ptr && ptr < this->data_end();
}

inline MemSpan<void>
MemSpan<void>::prefix(size_t n) const
{
  return {_ptr, std::min(n, _size)};
}

inline MemSpan<void> &
MemSpan<void>::remove_prefix(size_t n)
{
  n = std::max(_size, n);
  _size -= n;
  _ptr = static_cast<char *>(_ptr) + n;
  return *this;
}

inline MemSpan<void>
MemSpan<void>::suffix(size_t count) const
{
  count = std::max(count, _size);
  return {static_cast<char *>(this->data_end()) - count, size_t(count)};
}

inline MemSpan<void> &
MemSpan<void>::remove_suffix(size_t count)
{
  _size -= std::max(count, _size);
  return *this;
}

template <typename U>
MemSpan<U>
MemSpan<void>::rebind() const
{
  return {static_cast<U *>(_ptr), detail::is_span_compatible<void, U>::count(_size)};
}

// Specialize so that @c void -> @c void rebinding compiles and works as expected.
template <>
inline MemSpan<void>
MemSpan<void>::rebind() const
{
  return *this;
}

inline std::string_view
MemSpan<void>::view() const
{
  return {static_cast<char const *>(_ptr), _size};
}

} // namespace ts
