// SPDX-License-Identifier: Apache-2.0
// Copyright Verizon Media 2020
/** @file

   Spans of writable memory. This is similar but independently developed from @c std::span. The goal
   is to provide convenient handling for chunks of memory. These chunks can be treated as arrays of
   arbitrary types via template methods.
*/

#pragma once

#include "swoc/swoc_version.h"
#include "swoc/Scalar.h"

#include <cstring>
#include <memory>
#include <type_traits>
#include <ratio>
#include <tuple>
#include <exception>

/// For template deduction guides
#include <array>
#include <vector>
#include <string_view>

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
  /// Copy constructor.
  constexpr MemSpan(self_type & that) = default;

  /** Construct from a first element @a start and a @a count of elements.
   *
   * @param start First element.
   * @param count Total number of elements.
   */
  constexpr MemSpan(value_type *ptr, size_t count);

  /** Construct from a half open range [start, last).
   *
   * @param begin Start of range.
   * @param end Past end of range.
   */
  constexpr MemSpan(value_type *begin, value_type *end);

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


  /** Construct from any vector like container.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to the pointer type for @a T and @c size_t respectively.
   *
   * @internal A non-const variant of this is needed because passing by CR means imposing constness
   * on the container which can then undesirably propagate that to the element type. Best example -
   * consstructing from @c std::string. Without this variant it's not possible to construct a @c char
   * span vs. a @c char @c const.
   */
  template < typename C
            , typename = std::enable_if_t<
              std::is_convertible_v<decltype(std::declval<C>().data()), T *> &&
                std::is_convertible_v<decltype(std::declval<C>().size()), size_t>
              , void
              >
            > constexpr MemSpan(C & c);

  /** Construct from any vector like container.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to the pointer type for @a T and @c size_t respectively.
   *
   * @note Because the container is passed as a constant reference, this may cause the span type to
   * also be a constant element type.
   */
  template < typename C
            , typename = std::enable_if_t<
              std::is_convertible_v<decltype(std::declval<C>().data()), T *> &&
                std::is_convertible_v<decltype(std::declval<C>().size()), size_t>
              , void
              >
            > constexpr MemSpan(C const & c);

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
  constexpr size_t size() const;

  /// Number of elements in the span
  /// @note Deprecate for 1.5.0.
  constexpr size_t count() const;

  /// Number of elements in the span
  constexpr size_t length() const;

  /// Number of bytes in the span.
  constexpr size_t data_size() const;

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

  /** Adjust the span.
   *
   * @param first Starting point of the span.
   * @param last Past the end of the span.
   * @return @a this
   */
  self_type &assign(T *first, T const *last);

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

  /** Construct all elements in the span.
   *
   * For each element in the span, construct an instance of the span type using the @a args. If the
   * instances need destruction this must be done explicitly.
   */
  template <typename... Args> self_type & make(Args &&... args);

  /// Destruct all elements in the span.
  void destroy();

  template <typename U> friend class MemSpan;
};

/** Specialization for void pointers.
 *
 * Key differences:
 *
 * - No subscript operator.
 * - No array initialization.
 * - Other non-const @c MemSpan types implicitly convert to this type.
 *
 * @internal I tried to be clever about the base template but there were too many differences
 * One major issue was the array initialization did not work at all if the @c void case didn't
 * exclude that. Once separate there are a number of useful tweaks available.
 */
template <> class MemSpan<void const> {
  using self_type = MemSpan; ///< Self reference type.
  template <typename U> friend class MemSpan;

public:
  using value_type = void const; /// Export base type.

protected:
  void *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Number of bytes in the chunk.

public:
  /// Default constructor (empty buffer).
  constexpr MemSpan() = default;

  /// Copy constructor.
  constexpr MemSpan(self_type const &that) = default;

  /// Copy assignment
  constexpr self_type & operator = (self_type const& that) = default;

  /// Special constructor for @c void
  constexpr MemSpan(MemSpan<void> const& that);

  /** Cross type copy constructor.
   *
   * @tparam U Type for source span.
   * @param that Source span.
   *`
   * This enables any @c MemSpan to be automatically converted to a void span, just as any pointer
   * can convert to a void pointer.
   */
  template <typename U> constexpr MemSpan(MemSpan<U> const &that);

  /** Construct from a pointer @a start and a size @a n bytes.
   *
   * @param start Start of the span.
   * @param n # of bytes in the span.
   */
  constexpr MemSpan(value_type *ptr, size_t n);

  /** Construct from a half open range of [start, last).
   *
   * @param start Start of the range.
   * @param last Past end of range.
   */
  MemSpan(value_type *begin, value_type *end);

  /** Construct from any vector like container.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to @c void* and @c size_t respectively.
   */
  template < typename C
            , typename = std::enable_if_t<
                std::is_convertible_v<decltype(std::declval<C>().size()), size_t>
              , void
              >
            > constexpr MemSpan(C const& c);

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

  /** Equivalent.

      Check if the spans refer to the same span of memory.

      @return @c true if @a this and @a that refer to the same memory, @c false if not.
   */
  bool is_same(self_type const &that) const;

  /** Inequality.
      @return @c true if @a that does not refer to the same span as @a this,
      @c false otherwise.
   */
  bool operator!=(self_type const &that) const;

  /// Check for non-empty span.
  /// @return @c true if the span contains bytes.
  explicit operator bool() const;

  /// Check for empty span.
  /// @return @c true if the span is empty (no contents), @c false otherwise.
  bool operator!() const;

  /// Check for empty span (no content).
  /// @see operator bool
  constexpr bool empty() const;

  /// @return Number of bytes in the span.
  constexpr size_t size() const;

  /// @return Number of bytes in the span.
  /// @note Template compatibility.
  constexpr size_t count() const;

  /// @return Number of bytes in the span.
  /// @note Template compatibility.
  constexpr size_t length() const;

  /// Number of bytes in the span.
  constexpr size_t data_size() const;

  /// Pointer to memory in the span.
  constexpr value_type *data() const;

  /// Pointer to just after memory in the span.
  value_type *data_end() const;

  /// Assignment - special handling for @c void.
  constexpr self_type &operator=(MemSpan<void> const &that);

  /// Assignment - the span is copied, not the content.
  /// Any type of @c MemSpan can be assigned to @c MemSpan<void>.
  template <typename U> self_type &operator=(MemSpan<U> const &that);

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

  /** Create a new span for a different type @a V on the same memory.
   *
   * @tparam V Type for the created span.
   * @return A @c MemSpan which contains the same memory as instances of @a V.
   */
  template <typename U> MemSpan<U> rebind() const;

  /** Cast the span as a the instance of a type.
   *
   * @tparam U Target type.
   * @return A pointer to the span as a constant instance of @a U.
   *
   * @note This throws if the size is not a match for @a U.
   */
  template <typename U> U const * as_ptr() const;

  /// Clear the span (become an empty span).
  self_type &clear();

  /// @return @c true if the byte at @a *ptr is in the span.
  bool contains(void const *ptr) const;

  /** Get the initial segment of @a n bytes.

      @return An instance that contains the leading @a n bytes of @a this.
  */
  self_type prefix(size_t n) const;

  /** Shrink the span by removing @a n leading bytes.
   *
   * @param count The number of elements to remove.
   * @return @c *this
   */
  self_type &remove_prefix(size_t n);

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
   * @param n Number of elements.
   * @return The span starting at @a offset for @a count elements in @a this.
   *
   * The result is clipped by @a this - if @a offset is out of range an empty span is returned. Otherwise @c count is clipped by the
   * number of elements available in @a this. In effect the intersection of the span described by ( @a offset , @a count ) and @a
   * this span is returned, which may be the empty span.
   */
  constexpr self_type subspan(size_t offset, size_t n) const;

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
   * @param alignment Alignment size (must be power of 2).
   * @return An aligned span.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty).
   */
  self_type align(size_t alignment) const;

  /** Force memory alignment.
   *
   * @param alignment Alignment size (must be power of 2).
   * @param obj_size Size of instances requiring alignment.
   * @return An aligned span.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty). Trailing space
   * is discarded such that the resulting memory space is a multiple of @a size.
   *
   * @note @a obj_size should be a multiple of @a alignment. This happens naturally if @c sizeof is used.
   */
  self_type align(size_t alignment, size_t obj_size) const;

};

template <> class MemSpan<void> : public MemSpan<void const> {
  using self_type = MemSpan;
  using super_type = MemSpan<void const>;
  template <typename U> friend class MemSpan;
public:
  using value_type = void;

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
   *`
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
   * @param begin Start of the range.
   * @param end Past end of range.
   */
  MemSpan(value_type *begin, value_type *end);

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr MemSpan(std::nullptr_t);

  /// Pointer to memory in the span.
  constexpr value_type *data() const;

  /// Pointer to just after memory in the span.
  value_type *data_end() const;

  /// Assignment - the span is copied, not the content.
  /// Any type of @c MemSpan can be assigned to @c MemSpan<void>.
  template <typename U> self_type &operator=(MemSpan<U> const &that);

  /** Construct from any vector like container.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to @c void* and @c size_t respectively.
   */
  template < typename C
            , typename = std::enable_if_t<
              ! std::is_const_v<decltype(std::declval<C>().data()[0])> &&
                std::is_convertible_v<decltype(std::declval<C>().size()), size_t>
              , void
              >
            > constexpr MemSpan(C const& c);

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

   /// @return An instance that contains the leading @a n bytes of @a this.
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
   * @param n Number of elements.
   * @return The span starting at @a offset for @a count elements in @a this.
   *
   * The result is clipped by @a this - if @a offset is out of range an empty span is returned. Otherwise @c count is clipped by the
   * number of elements available in @a this. In effect the intersection of the span described by ( @a offset , @a count ) and @a
   * this span is returned, which may be the empty span.
   */
  constexpr self_type subspan(size_t offset, size_t n) const;

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
   * @param alignment Alignment size (must be power of 2).
   * @return An aligned span.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty).
   */
  self_type align(size_t alignment) const;

  /** Force memory alignment.
   *
   * @param alignment Alignment size (must be power of 2).
   * @param obj_size Size of instances requiring alignment.
   * @return An aligned span.
   *
   * The minimum amount of space is removed from the front to yield an aligned span. If the span is not large
   * enough to perform the alignment, the pointer is aligned and the size reduced to zero (empty). Trailing space
   * is discarded such that the resulting memory space is a multiple of @a size.
   *
   * @note @a obj_size should be a multiple of @a alignment. This happens naturally if @c sizeof is used.
   */
  self_type align(size_t alignment, size_t obj_size) const;

  /** Create a new span for a different type @a V on the same memory.
   *
   * @tparam V Type for the created span.
   * @return A @c MemSpan which contains the same memory as instances of @a V.
   */
  template <typename U> MemSpan<U> rebind() const;

  /** Cast the span as a the instance of a type.
   *
   * @tparam U Target type.
   * @return A pointer to the span as a constant instance of @a U.
   *
   * @note This throws if the size is not a match for @a U.
   */
  template <typename U> U * as_ptr() const;

  /// Clear the span (become an empty span).
  self_type &clear();

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

inline void const *
ptr_add(void const * ptr, size_t count) {
  return static_cast<char const *>(ptr) + count;
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
  return sizeof(T) * size;
}

template <typename T> struct is_span_compatible<T, void const> {
  static constexpr bool value = true;
  static size_t count(size_t size);
};

template <typename T>
size_t
is_span_compatible<T, void const>::count(size_t size) {
  return sizeof(T) * size;
}
/// @endcond

} // namespace detail

// --- Standard memory operations ---
template class MemSpan<unsigned char>;

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

/** Specialized @c memset.
 *
 * @tparam D Target span type.
 * @tparam S Source value type.
 * @param dst Target span.
 * @param c Source data.
 * @return @a dst
 *
 * If @a D and @a S are size 1 and instances of @a S can convert to @a D this directly calls @c memset.
 * This handles the various synonyms for a single byte, such as @c char, @c unsigned @c char, @c uint8_t, etc.
 */
template < typename D, typename S >
auto memset(MemSpan<D> const& dst, S c) -> std::enable_if_t<sizeof(D) == 1 && sizeof(S) == 1 && std::is_convertible_v<S,D>, MemSpan<D>>
{
  D d = c;
  std::memset(dst.data(), d, dst.size());
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

template <typename T> constexpr MemSpan<T>::MemSpan(T *begin, T *end) : _ptr{begin}, _count{detail::ptr_distance(begin, end)} {}

template <typename T> template <auto N> constexpr MemSpan<T>::MemSpan(T (&a)[N]) : _ptr{a}, _count{N} {}

template <typename T> constexpr MemSpan<T>::MemSpan(std::nullptr_t) {}

template <typename T> template <auto N, typename U, typename META> constexpr MemSpan<T>::MemSpan(std::array<U,N> const& a) : _ptr{a.data()} , _count{a.size()} {}
template <typename T> template <auto N> constexpr MemSpan<T>::MemSpan(std::array<T,N> & a) : _ptr{a.data()} , _count{a.size()} {}
template <typename T> template <typename C, typename> constexpr MemSpan<T>::MemSpan(C & c) : _ptr(c.data()), _count(c.size()) {}
template <typename T> template <typename C, typename> constexpr MemSpan<T>::MemSpan(C const& c) : _ptr(c.data()), _count(c.size()) {}

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
MemSpan<T>::size() const {
  return _count;
}

template <typename T> constexpr
  size_t
  MemSpan<T>::count() const {
  return _count;
}

template <typename T> constexpr
  size_t
  MemSpan<T>::length() const {
  return _count;
}

template <typename T>
constexpr size_t
MemSpan<T>::data_size() const {
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
  return {static_cast<U *>(static_cast<VOID_PTR>(_ptr)), detail::is_span_compatible<T, U>::count(this->data_size())};
}

template <typename T>
template <typename... Args>
auto
MemSpan<T>::make(Args &&...args) -> self_type & {
  for ( T * elt = this->data(), * limit = this->data_end() ; elt < limit ; ++elt ) {
    new (elt) T(std::forward<Args>(args)...);
  }
  return *this;
}

template <typename T>
void
MemSpan<T>::destroy() {
  for ( T * elt = this->data(), * limit = this->data_end() ; elt < limit ; ++elt ) {
    std::destroy_at(elt);
  }
}

// --- void specializations ---

template <typename U> constexpr MemSpan<void const>::MemSpan(MemSpan<U> const &that) : _ptr(const_cast<std::remove_const_t<U> *>(that._ptr)), _size(sizeof(U) * that.size()) {}
template <typename U> constexpr MemSpan<void>::MemSpan(MemSpan<U> const &that) : super_type(that) { static_assert(!std::is_const_v<U>, "MemSpan<void> does not support constant memory."); }

inline constexpr MemSpan<void const>::MemSpan(MemSpan<void> const &that) : _ptr(that._ptr), _size(that.size()) {}

inline constexpr MemSpan<void const>::MemSpan(value_type *ptr, size_t n) : _ptr{const_cast<void*>(ptr)}, _size{n} {}
inline constexpr MemSpan<void>::MemSpan(value_type *ptr, size_t n) : super_type(ptr, n) {}

inline MemSpan<void const>::MemSpan(value_type *begin, value_type *end) : _ptr{const_cast<void*>(begin)}, _size{detail::ptr_distance(begin, end)} {}
inline MemSpan<void >::MemSpan(value_type *begin, value_type *end) : super_type(begin, end) {}

template <typename C, typename>
constexpr MemSpan<void const>::MemSpan(C const &c)
  : _ptr(const_cast<std::remove_const_t<std::remove_reference_t<decltype(*(std::declval<C>().data()))>> *>(c.data()))
    , _size(c.size() * sizeof(*(std::declval<C>().data()))) {}
template <typename C, typename>
constexpr MemSpan<void>::MemSpan(C const &c) : super_type(c) {}

inline constexpr MemSpan<void const>::MemSpan(std::nullptr_t) {}
inline constexpr MemSpan<void>::MemSpan(std::nullptr_t) {}

inline bool
MemSpan<void const>::is_same(self_type const &that) const {
  return _ptr == that._ptr && _size == that._size;
}

inline bool
MemSpan<void const>::operator==(self_type const &that) const {
  return _size == that._size && (_ptr == that._ptr || 0 == memcmp(_ptr, that._ptr, _size));
}

inline bool
MemSpan<void const>::operator!=(self_type const &that) const {
  return !(*this == that);
}

inline MemSpan<void const>::operator bool() const {
  return _size != 0;
}

inline bool
MemSpan<void const>::operator!() const {
  return _size == 0;
}

inline constexpr bool
MemSpan<void const>::empty() const {
  return _size == 0;
}

template <typename U>
auto
MemSpan<void const>::operator=(MemSpan<U> const &that) -> self_type & {
  _ptr  = that._ptr;
  _size = sizeof(U) * that.size();
  return *this;
}

inline constexpr auto
MemSpan<void const>::operator=(MemSpan<void> const &that) -> self_type & {
  _ptr  = that._ptr;
  _size = that.size();
  return *this;
}

template <typename U>
auto
MemSpan<void>::operator=(MemSpan<U> const &that) -> self_type & {
  static_assert(! std::is_const_v<U>, "Cannot assign constant pointer to MemSpan<void>");
  this->super_type::operator=(that);
  return *this;
}

inline auto
MemSpan<void const>::assign(value_type *ptr, size_t n) -> self_type & {
  _ptr  = const_cast<void*>(ptr);
  _size = n;
  return *this;
}

inline auto
MemSpan<void>::assign(value_type * ptr, size_t n) -> self_type & {
  super_type::assign(ptr, n);
  return *this;
}

inline auto
MemSpan<void const>::assign(value_type *first, value_type const *last) -> self_type & {
  _ptr  = const_cast<void*>(first);
  _size = detail::ptr_distance(first, last);
  return *this;
}

inline auto
MemSpan<void>::assign(value_type *first, value_type const *last) -> self_type & {
  super_type::assign(first, last);
  return *this;
}

inline auto
MemSpan<void const>::clear() -> self_type & {
  _ptr  = nullptr;
  _size = 0;
  return *this;
}

inline auto
MemSpan<void>::clear() -> self_type & {
  super_type::clear();
  return *this;
}

inline constexpr void const *
MemSpan<void const>::data() const {
  return _ptr;
}

inline constexpr void *
MemSpan<void>::data() const {
  return _ptr;
}

inline void const *
MemSpan<void const>::data_end() const {
  return detail::ptr_add(_ptr, _size);
}

inline void *
MemSpan<void>::data_end() const {
  return detail::ptr_add(_ptr, _size);
}

inline constexpr size_t
MemSpan<void const>::size() const {
  return _size;
}

inline constexpr size_t
MemSpan<void const>::count() const {
  return _size;
}

inline constexpr size_t
MemSpan<void const>::length() const {
  return _size;
}

inline constexpr size_t
MemSpan<void const>::data_size() const {
  return _size;
}

inline bool
MemSpan<void const>::contains(value_type const *ptr) const {
  return _ptr <= ptr && ptr < this->data_end();
}

inline auto
MemSpan<void const>::prefix(size_t n) const -> self_type {
  return {_ptr, std::min(n, _size)};
}

inline auto
MemSpan<void>::prefix(size_t n) const -> self_type {
  return {_ptr, std::min(n, _size)};
}

inline auto
MemSpan<void const>::remove_prefix(size_t n) -> self_type & {
  n = std::min(_size, n);
  _size -= n;
  _ptr = detail::ptr_add(_ptr, n);
  return *this;
}

inline auto
MemSpan<void>::remove_prefix(size_t n) -> self_type & {
  super_type::remove_prefix(n);
  return *this;
}

inline auto
MemSpan<void const>::suffix(size_t n) const -> self_type {
  n = std::min(n, _size);
  return {detail::ptr_add(this->data_end(), -n), n};
}

inline auto
MemSpan<void>::suffix(size_t n) const -> self_type {
  n = std::min(n, _size);
  return {detail::ptr_add(this->data_end(), -n), n};
}

inline auto
MemSpan<void const>::remove_suffix(size_t n) -> self_type & {
  _size -= std::min(n, _size);
  return *this;
}

inline auto
MemSpan<void>::remove_suffix(size_t n) -> self_type & {
  this->super_type::remove_suffix(n);
  return *this;
}

inline constexpr auto
MemSpan<void const>::subspan(size_t offset, size_t n) const -> self_type {
  return offset <= _size ? self_type{detail::ptr_add(this->data(), offset), std::min(n, _size - offset)} : self_type{};
}

inline constexpr auto
MemSpan<void>::subspan(size_t offset, size_t n) const -> self_type {
  return offset <= _size ? self_type{detail::ptr_add(this->data(), offset), std::min(n, _size - offset)} : self_type{};
}

template <typename T> auto
MemSpan<void const>::align() const -> self_type { return this->align(alignof(T), sizeof(T)); }

template <typename T> auto
MemSpan<void>::align() const -> self_type { return this->align(alignof(T), sizeof(T)); }

inline auto
MemSpan<void const>::align(size_t alignment) const -> self_type {
  auto p = uintptr_t(_ptr);
  auto padding = p & (alignment - 1);
  size_t size = 0;
  if (_size > padding) { // if there's not enough to pad, result is zero size.
    size = _size - padding;
  }
  return { reinterpret_cast<void*>(p + padding), size };
}

inline auto
MemSpan<void>::align(size_t alignment) const -> self_type {
  auto && [ ptr, size ] = super_type::align(alignment);
  return { ptr, size };
}

inline auto
MemSpan<void const>::align(size_t alignment, size_t obj_size) const -> self_type {
  auto p = uintptr_t(_ptr);
  auto padding = p & (alignment - 1);
  size_t size = 0;
  if (_size > padding) { // if there's not enough to pad, result is zero size.
    size = ((_size - padding) / obj_size ) * obj_size;
  }
  return { reinterpret_cast<void*>(p + padding), size };
}

inline auto
MemSpan<void>::align(size_t alignment, size_t obj_size) const -> self_type {
  auto && [ ptr, n ] = super_type::align(alignment, obj_size);
  return { ptr, n };
}

template < typename U > MemSpan<U>
MemSpan<void const>::rebind() const {
  static_assert(std::is_const_v<U>, "Cannot rebind MemSpan<const void> to non-const type.");
  return {static_cast<U *>(_ptr), detail::is_span_compatible<value_type, U>::count(_size)};
}

template < typename U > MemSpan<U>
MemSpan<void>::rebind() const {
  return {static_cast<U *>(_ptr), detail::is_span_compatible<value_type, U>::count(_size)};
}

// Specialize so that @c void -> @c void rebinding compiles and works as expected.
template <>
inline auto
MemSpan<void const>::rebind() const -> self_type {
  return *this;
}

template <>
inline auto
MemSpan<void>::rebind() const -> self_type {
  return *this;
}

template <typename U> U *
MemSpan<void>::as_ptr() const {
  if (_size != sizeof(U)) {
    throw std::invalid_argument("MemSpan::as size is not compatible with target type.");
  }
  return static_cast<U *>(_ptr);
}

template <typename U> U const *
MemSpan<void const>::as_ptr() const {
  if (_size != sizeof(U)) {
    throw std::invalid_argument("MemSpan::as size is not compatible with target type.");
  }
  return static_cast<U const *>(_ptr);
}

/// Deduction guides
template<typename T, size_t N> MemSpan(std::array<T,N> &) -> MemSpan<T>;
template<typename T, size_t N> MemSpan(std::array<T,N> const &) -> MemSpan<T const>;
template<size_t N> MemSpan(char (&)[N]) -> MemSpan<char>;
template<size_t N> MemSpan(char const (&)[N]) -> MemSpan<char const>;
template<typename T> MemSpan(std::vector<T> &) -> MemSpan<T>;
template<typename T> MemSpan(std::vector<T> const&) -> MemSpan<T const>;
MemSpan(std::string_view const&) -> MemSpan<char const>;
MemSpan(std::string &) -> MemSpan<char>;
MemSpan(std::string const&) -> MemSpan<char const>;

namespace detail {
struct malloc_liberator {
  void operator()(void * ptr) { ::free(ptr); }
};
} // namespace detail.

/// A variant of @c unique_ptr that handles memory from @c malloc.
template<typename T> using unique_malloc = std::unique_ptr<T, detail::malloc_liberator>;
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
