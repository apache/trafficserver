// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

   Spans of writable memory. This is similar but independently developed from @c std::span. The goal
   is to provide convenient handling for chunks of memory. These chunks can be treated as arrays of
   arbitrary types via template methods.
*/

#pragma once

#include <cstring>
#include <iosfwd>
#include <iostream>
#include <cstddef>
#include <array>
#include <string_view>
#include <type_traits>
#include <ratio>
#include <tuple>
#include <exception>

#include "swoc/swoc_version.h"
#include "swoc/Scalar.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
/** A span of contiguous piece of memory.

    A @c MemSpan does not own the memory to which it refers, it is simply a span of part of some
    (presumably) larger memory object. It acts as a pointer, not a container - copy and assignment
    change the span, not the memory to which the span refers.

    The purpose is that frequently code needs to work on a specific part of the memory. This can
    avoid copying or allocation by allocating all needed memory at once and then working with it via
    instances of this class.

    @note The issue of @c const correctness is tricky. Because this class is intended as a "smart"
    pointer, its constancy does not carry over to its elements, just as a constant pointer doesn't
    make its target constant. This makes it different than containers such as @c std::array or
    @c std::vector. This means when creating an instance based on such containers the constancy of
    the container affects the element type of the span. E.g.

    - A @c std::array<T,N> maps to a @c MemSpan<T>
    - A @c const @c std::array<T,N> maps to a @c MemSpan<const T>

    For convenience a @c MemSpan<const T> can be constructed from a @c MemSpan<T> because this maintains
    @c const correctness and models how a @c T @c const* can be constructed from a @c T* .
 */
template <typename T> class MemSpan {
  using self_type = MemSpan; ///< Self reference type.

protected:
  T *_ptr       = nullptr; ///< Pointer to base of memory chunk.
  size_t _count = 0;       ///< Number of elements.

public:
  using value_type     = T; ///< Element type for span.
  using iterator       = T *; ///< Iterator.
  using const_iterator = T const *; ///< Constant iterator.

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
  template <auto N> constexpr MemSpan(T (&a)[N]);

  /** Construct from constant @c std::array.
   *
   * @tparam N Array size.
   * @param a Array instance.
   *
   * @note Because the elements in a constant array are constant the span value type must be constant.
   */
  template < auto N, typename U
           , typename META = std::enable_if_t<
               std::conjunction_v<
                 std::is_const<T>
               , std::is_same<std::remove_const_t<U>, std::remove_const_t<T>>
               >
          >
    > constexpr MemSpan(std::array<U, N> const& a);

  /** Construct from a @c std::array.
   *
   * @tparam N Array size.
   * @param a Array instance.
   */
  template <auto N> constexpr MemSpan(std::array<T, N> & a);

  /** Construct a span of constant values from a span of non-constant.
   *
   * @tparam U Span types.
   * @tparam META Metaprogramming type to control conversion existence.
   * @param that Source span.
   *
   * This enables the standard conversion from non-const to const.
   */
  template < typename U
           , typename META = std::enable_if_t<
             std::conjunction_v<
               std::is_const<T>
               , std::is_same<U, std::remove_const_t<T>>
   >>>
   constexpr MemSpan(MemSpan<U> const& that) : _ptr(that.data()), _count(that.count()) {}


  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr MemSpan(std::nullptr_t);

  /** Equality.

      Compare the span contents.

      @return @c true if the contents of @a that are the same as the content of @a this,
      @c false otherwise.
   */
  constexpr bool operator==(self_type const &that) const;

  /** Identical.

      Check if the spans refer to the same span of memory.
      @return @c true if @a this and @a that refer to the same span, @c false if not.
   */
  bool is_same(self_type const &that) const;

  /** Inequality.
      @return @c true if @a that does not refer to the same span as @a this,
      @c false otherwise.
   */
  constexpr bool operator!=(self_type const &that) const;

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
  constexpr bool empty() const;

  /// @name Accessors.
  //@{
  /// Pointer to the first element in the span.
  constexpr T *begin() const;

  /// Pointer to first element not in the span.
  constexpr T *end() const;

  /// Number of elements in the span
  constexpr size_t count() const;

  /// Number of bytes in the span.
  size_t size() const;

  /// @return Pointer to memory in the span.
  T * data() const;

  /// @return Pointer to immediate after memory in the span.
  constexpr T *data_end() const;

  /** Access the first element in the span.
   *
   * @return A reference to the first element in the span.
   */
  T &front();

  /** Access the last element in the span.
   *
   * @return A reference to the last element in the span.
   */
  T &back();

  /** Apply a function @a f to every element of the span.
   *
   * @tparam F Functor type.
   * @param f Functor instance.
   * @return @a this
   */
  template <typename F> self_type &apply(F &&f);

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
  constexpr self_type prefix(size_t count) const;

  /** Get the initial segment of @a count elements.

      @return An instance that contains the leading @a count elements of @a this.

     @note Synonymn for @c prefix for STL compatibility.
  */
  constexpr self_type first(size_t count) const;

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
  constexpr self_type suffix(size_t count) const;

  /** Get the trailing segment of @a count elements.
   *
   * @param count Number of elements to retrieve.
   * @return An instance that contains the trailing @a count elements of @a this.
   *
   * @note Synonymn for @c suffix for STL compatibility.
   */
  constexpr self_type last(size_t count) const;

  /** Shrink the span by removing @a count trailing elements.
   *
   * @param count Number of elements to remove.
   * @return @c *this
   */
  self_type &remove_suffix(size_t count);

  /** Return a sub span of @a this span.
   *
   * @param offset Offset (index) of first element in subspan.
   * @param count Number of elements in the subspan.
   * @return A subspan starting at @a offset for @a count elements.
   *
   * The result is clipped by @a this - if @a offset is out of range an empty span is returned.
   * Otherwise @c count is clipped by the number of elements available in @a this.
   */
  constexpr self_type subspan(size_t offset, size_t count) const;

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
template <> class MemSpan<void> {
  using self_type = MemSpan; ///< Self reference type.
  template <typename U> friend class MemSpan;

public:
  using value_type = void; /// Export base type.

protected:
  value_type *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Number of bytes in the chunk.

public:
  /// Default constructor (empty buffer).
  constexpr MemSpan() = default;

  /// Copy constructor.
  constexpr MemSpan(self_type const &that) = default;

  /// Copy assignment
  constexpr self_type & operator = (self_type const& that) = default;

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
  constexpr value_type *data() const;

  /// Pointer to just after memory in the span.
  value_type *data_end() const;

  /** Create a new span for a different type @a V on the same memory.
   *
   * @tparam V Type for the created span.
   * @return A @c MemSpan which contains the same memory as instances of @a V.
   */
  template <typename U> MemSpan<U> rebind() const;

  /** Update the span.
   *
   * @param ptr Start of span memory.
   * @param n Number of elements in the span.
   * @return @a this
   */
  self_type &assign(value_type *ptr, size_t n);

  /** Update the span.
   *
   * @param first First element in the span.
   * @param last One past the last element in the span.
   * @return @a this
   */
  self_type &assign(value_type *first, value_type const *last);

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

  /** Return a sub span of @a this span.
   *
   * @param offset Offset (index) of first element.
   * @param count Number of elements.
   * @return The span starting at @a offset for @a count elements in @a this.
   *
   * The result is clipped by @a this - if @a offset is out of range an empty span is returned. Otherwise @c count is clipped by the
   * number of elements available in @a this. In effect the intersection of the span described by ( @a offset , @a count ) and @a
   * this span is returned, which may be the empty span.
   */
  constexpr self_type subspan(size_t offset, size_t count) const;

  /** Align span for a type.
   *
   * @tparam T Alignment type.
   * @return A suffix of the span suitably aligned for @a T.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty).
   */
  template <typename T> self_type align() const;

  /** Force memory alignment.
   *
   * @param n Alignment size (must be power of 2).
   * @return An aligned span.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty).
   */
  self_type align(size_t n) const;

  /** Return a view of the memory.
   *
   * @return A @c string_view covering the span contents.
   */
  std::string_view view() const;
};

// -- Implementation --

namespace detail {
/// @cond INTERNAL_DETAIL
inline size_t
ptr_distance(void const *first, void const *last) {
  return static_cast<const char *>(last) - static_cast<const char *>(first);
}

template <typename T>
size_t
ptr_distance(T const *first, T const *last) {
  return last - first;
}

inline void *
ptr_add(void *ptr, size_t count) {
  return static_cast<char *>(ptr) + count;
}
/// @endcond

/** Meta Function to check the type compatibility of two spans..
 *
 * @tparam T Source span type.
 * @tparam U Destination span type.
 *
 * The types are compatible if one is an integral multiple of the other, so the span divides evenly.
 *
 * @a U must not lose constancy compared to @a T.
 *
 * @internal More void handling. This can't go in @c MemSpan because template specialization is
 * invalid in class scope and this needs to be specialized for @c void.
 */
template <typename T, typename U> struct is_span_compatible {
  /// @c true if the size of @a T is an integral multiple of the size of @a U or vice versa.
  static constexpr bool value =
    (std::ratio<sizeof(T), sizeof(U)>::num == 1 || std::ratio<sizeof(U), sizeof(T)>::num == 1) &&
    (std::is_const_v<U> || ! std::is_const_v<T>); // can't lose constancy.
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
is_span_compatible<T, U>::count(size_t size) {
  if (size % sizeof(U)) {
    throw std::invalid_argument("MemSpan rebind where span size is not a multiple of the element size");
  }
  return size / sizeof(U);
}

/// @cond INTERNAL_DETAIL
// Must specialize for rebinding to @c void because @c sizeof doesn't work. Rebinding from @c void
// is handled by the @c MemSpan<void>::rebind specialization and doesn't use this mechanism.
template <typename T> struct is_span_compatible<T, void> {
  static constexpr bool value = ! std::is_const_v<T>;
  static size_t count(size_t size);
};

template <typename T>
size_t
is_span_compatible<T, void>::count(size_t size) {
  return size;
}
/// @endcond

} // namespace detail

// --- Standard memory operations ---

template <typename T>
int
memcmp(MemSpan<T> const &lhs, MemSpan<T> const &rhs) {
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
memcpy(MemSpan<T> &dst, MemSpan<T> const &src) {
  return static_cast<T *>(std::memcpy(dst.data(), src.data(), std::min(dst.size(), src.size())));
}

template <typename T>
T *
memcpy(MemSpan<T> &dst, T *src) {
  return static_cast<T *>(std::memcpy(dst.data(), src, dst.size()));
}

template <typename T>
T *
memcpy(T *dst, MemSpan<T> &src) {
  return static_cast<T *>(std::memcpy(dst, src.data(), src.size()));
}

inline char *
memcpy(MemSpan<char> &span, std::string_view view) {
  return static_cast<char *>(std::memcpy(span.data(), view.data(), std::min(view.size(), view.size())));
}

inline void *
memcpy(MemSpan<void> &span, std::string_view view) {
  return std::memcpy(span.data(), view.data(), std::min(view.size(), view.size()));
}

using std::memcpy;

/** Set contents of a span to a fixed @a value.
 *
 * @tparam T Span type.
 * @param dst Span to change.
 * @param value Source value.
 * @return
 */
template <typename T>
inline MemSpan<T> const &
memset(MemSpan<T> const &dst, T const &value) {
  for (auto &e : dst) {
    e = value;
  }
  return dst;
}

/// @cond INTERNAL_DETAIL

// Optimization for @c char.
inline MemSpan<char> const &
memset(MemSpan<char> const &dst, char c) {
  std::memset(dst.data(), c, dst.size());
  return dst;
}

// Optimization for @c unsigned @c char
inline MemSpan<unsigned char> const &
memset(MemSpan<unsigned char> const &dst, unsigned char c) {
  std::memset(dst.data(), c, dst.size());
  return dst;
}

// Optimization for @c char.
inline MemSpan<void> const &
memset(MemSpan<void> const &dst, char c) {
  std::memset(dst.data(), c, dst.size());
  return dst;
}

/// @endcond

using std::memset;

// --- MemSpan<T> ---

template <typename T> constexpr MemSpan<T>::MemSpan(T *ptr, size_t count) : _ptr{ptr}, _count{count} {}

template <typename T> constexpr MemSpan<T>::MemSpan(T *first, T *last) : _ptr{first}, _count{detail::ptr_distance(first, last)} {}

template <typename T> template <auto N> constexpr MemSpan<T>::MemSpan(T (&a)[N]) : _ptr{a}, _count{N} {}

template <typename T> constexpr MemSpan<T>::MemSpan(std::nullptr_t) {}

template <typename T> template <auto N, typename U, typename META> constexpr MemSpan<T>::MemSpan(std::array<U,N> const& a) : _ptr{a.data()} , _count{a.size()} {}
template <typename T> template <auto N> constexpr MemSpan<T>::MemSpan(std::array<T,N> & a) : _ptr{a.data()} , _count{a.size()} {}

template <typename T>
MemSpan<T> &
MemSpan<T>::assign(T *ptr, size_t count) {
  _ptr   = ptr;
  _count = count;
  return *this;
}

template <typename T>
MemSpan<T> &
MemSpan<T>::assign(T *first, T const *last) {
  _ptr   = first;
  _count = detail::ptr_distance(first, last);
  return *this;
}

template <typename T>
MemSpan<T> &
MemSpan<T>::clear() {
  _ptr   = nullptr;
  _count = 0;
  return *this;
}

template <typename T>
bool
MemSpan<T>::is_same(self_type const &that) const {
  return _ptr == that._ptr && _count == that._count;
}

template <typename T>
constexpr bool
MemSpan<T>::operator==(self_type const &that) const {
  return _count == that._count && (_ptr == that._ptr || 0 == memcmp(_ptr, that._ptr, this->size()));
}

template <typename T>
constexpr bool
MemSpan<T>::operator!=(self_type const &that) const {
  return !(*this == that);
}

template <typename T>
bool
MemSpan<T>::operator!() const {
  return _count == 0;
}

template <typename T> MemSpan<T>::operator bool() const {
  return _count != 0;
}

template <typename T> constexpr
bool
MemSpan<T>::empty() const {
  return _count == 0;
}

template <typename T> constexpr
T *
MemSpan<T>::begin() const {
  return _ptr;
}

template <typename T>
T *
MemSpan<T>::data() const {
  return _ptr;
}

template <typename T> constexpr
T *
MemSpan<T>::data_end() const {
  return _ptr + _count;
}

template <typename T> constexpr
T *
MemSpan<T>::end() const {
  return _ptr + _count;
}

template <typename T>
T &
MemSpan<T>::operator[](size_t idx) const {
  return _ptr[idx];
}

template <typename T> constexpr
size_t
MemSpan<T>::count() const {
  return _count;
}

template <typename T>
size_t
MemSpan<T>::size() const {
  return _count * sizeof(T);
}

template <typename T>
bool
MemSpan<T>::contains(T const *ptr) const {
  return _ptr <= ptr && ptr < _ptr + _count;
}

template <typename T> constexpr
auto
MemSpan<T>::prefix(size_t count) const -> self_type {
  return {_ptr, std::min(count, _count)};
}

template <typename T> constexpr
auto
MemSpan<T>::first(size_t count) const -> self_type {
  return this->prefix(count);
}

template <typename T>
auto
MemSpan<T>::remove_prefix(size_t count) -> self_type & {
  count = std::min(_count, count);
  _count -= count;
  _ptr += count;
  return *this;
}

template <typename T> constexpr
auto
MemSpan<T>::suffix(size_t count) const -> self_type {
  count = std::min(_count, count);
  return {(_ptr + _count) - count, count};
}

template <typename T>
constexpr MemSpan<T>
MemSpan<T>::last(size_t count) const {
  return this->suffix(count);
}

template <typename T>
MemSpan<T> &
MemSpan<T>::remove_suffix(size_t count) {
  _count -= std::min(count, _count);
  return *this;
}

template <typename T>
constexpr MemSpan<T>
MemSpan<T>::subspan(size_t offset, size_t count) const {
  return offset < _count ? self_type{this->data() + offset, std::min(count, _count - offset)} : self_type{};
}

template <typename T>
T &
MemSpan<T>::front() {
  return *_ptr;
}

template <typename T>
T &
MemSpan<T>::back() {
  return _ptr[_count - 1];
}

template <typename T>
template <typename F>
typename MemSpan<T>::self_type &
MemSpan<T>::apply(F &&f) {
  for (auto &item : *this) {
    f(item);
  }
  return *this;
}

template <typename T>
template <typename U>
MemSpan<U>
MemSpan<T>::rebind() const {
  static_assert(detail::is_span_compatible<T, U>::value,
                "MemSpan only allows rebinding between types where the sizes are such that one is an integral multiple of the other.");
  using VOID_PTR = std::conditional_t<std::is_const_v<U>, const void *, void*>;
  return {static_cast<U *>(static_cast<VOID_PTR>(_ptr)), detail::is_span_compatible<T, U>::count(this->size())};
}

template <typename T>
std::string_view
MemSpan<T>::view() const {
  return {static_cast<const char *>(_ptr), this->size()};
}

// --- void specialization ---

template <typename U> constexpr MemSpan<void>::MemSpan(MemSpan<U> const &that) : _ptr(that._ptr), _size(that.size()) {}

inline constexpr MemSpan<void>::MemSpan(value_type *ptr, size_t n) : _ptr{ptr}, _size{n} {}

inline MemSpan<void>::MemSpan(value_type *first, value_type *last) : _ptr{first}, _size{detail::ptr_distance(first, last)} {}

inline constexpr MemSpan<void>::MemSpan(std::nullptr_t) {}

inline MemSpan<void> &
MemSpan<void>::assign(value_type *ptr, size_t n) {
  _ptr  = ptr;
  _size = n;
  return *this;
}

inline MemSpan<void> &
MemSpan<void>::assign(value_type *first, value_type const *last) {
  _ptr  = first;
  _size = detail::ptr_distance(first, last);
  return *this;
}

inline MemSpan<void> &
MemSpan<void>::clear() {
  _ptr  = nullptr;
  _size = 0;
  return *this;
}

inline bool
MemSpan<void>::is_same(self_type const &that) const {
  return _ptr == that._ptr && _size == that._size;
}

inline bool
MemSpan<void>::operator==(self_type const &that) const {
  return _size == that._size && (_ptr == that._ptr || 0 == memcmp(_ptr, that._ptr, _size));
}

inline bool
MemSpan<void>::operator!=(self_type const &that) const {
  return !(*this == that);
}

inline bool
MemSpan<void>::operator!() const {
  return _size == 0;
}

inline MemSpan<void>::operator bool() const {
  return _size != 0;
}

inline bool
MemSpan<void>::empty() const {
  return _size == 0;
}

inline constexpr void *
MemSpan<void>::data() const {
  return _ptr;
}

inline void *
MemSpan<void>::data_end() const {
  return detail::ptr_add(_ptr, _size);
}

inline size_t
MemSpan<void>::size() const {
  return _size;
}

template <typename U>
auto
MemSpan<void>::operator=(MemSpan<U> const &that) -> self_type & {
  _ptr  = that._ptr;
  _size = that.size();
  return *this;
}

inline constexpr MemSpan<void>
MemSpan<void>::subspan(size_t offset, size_t count) const {
  return offset <= _size ? self_type{detail::ptr_add(this->data(), offset), std::min(count, _size - offset)} : self_type{};
}

inline bool
MemSpan<void>::contains(value_type const *ptr) const {
  return _ptr <= ptr && ptr < this->data_end();
}

inline MemSpan<void>
MemSpan<void>::prefix(size_t n) const {
  return {_ptr, std::min(n, _size)};
}

inline MemSpan<void> &
MemSpan<void>::remove_prefix(size_t n) {
  n = std::min(_size, n);
  _size -= n;
  _ptr = static_cast<char *>(_ptr) + n;
  return *this;
}

inline MemSpan<void>
MemSpan<void>::suffix(size_t count) const {
  count = std::min(count, _size);
  return {static_cast<char *>(this->data_end()) - count, count};
}

inline MemSpan<void> &
MemSpan<void>::remove_suffix(size_t count) {
  _size -= std::min(count, _size);
  return *this;
}

template <typename T>
MemSpan<void>::self_type
MemSpan<void>::align() const { return this->align(alignof(T)); }

inline MemSpan<void>::self_type
MemSpan<void>::align(size_t n) const {
  auto p = uintptr_t(_ptr);
  auto padding = p & (n - 1);
  return { reinterpret_cast<void*>(p + padding), _size - std::min<uintptr_t>(_size, padding) };
}

template <typename U>
MemSpan<U>
MemSpan<void>::rebind() const {
  return {static_cast<U *>(_ptr), detail::is_span_compatible<void, U>::count(_size)};
}

// Specialize so that @c void -> @c void rebinding compiles and works as expected.
template <>
inline MemSpan<void>
MemSpan<void>::rebind() const {
  return *this;
}

inline std::string_view
MemSpan<void>::view() const {
  return {static_cast<char const *>(_ptr), _size};
}

/// Deduction guide for constructing from a @c std::array.
template<typename T, size_t N> MemSpan(std::array<T,N> &) -> MemSpan<T>;
template<typename T, size_t N> MemSpan(std::array<T,N> const &) -> MemSpan<T const>;

}} // namespace swoc::SWOC_VERSION_NS

/// @cond NO_DOXYGEN
// STL tuple support - this allows the @c MemSpan to be used as a tuple of a pointer
// and size.
namespace std {
template <size_t IDX, typename R> class tuple_element<IDX, swoc::MemSpan<R>> {
  static_assert("swoc::MemSpan tuple index out of range");
};

template <typename R> class tuple_element<0, swoc::MemSpan<R>> {
public:
  using type = R *;
};

template <typename R> class tuple_element<1, swoc::MemSpan<R>> {
public:
  using type = size_t;
};

template <typename R> class tuple_size<swoc::MemSpan<R>> : public std::integral_constant<size_t, 2> {};

} // namespace std

/// @endcond
