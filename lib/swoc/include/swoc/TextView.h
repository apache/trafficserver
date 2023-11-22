// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

   Class for handling "views" of text. Views presume the memory for the buffer is managed
   elsewhere and allow efficient access to segments of the buffer without copies. Views are read
   only as the view doesn't own the memory. Along with generic buffer methods are specialized
   methods to support better string parsing, particularly token based parsing.

   This class is based on @c std::string_view and is easily and cheaply converted to and from that
   class.
*/

#pragma once
#include <bitset>
#include <cstdint>
#include <iosfwd>
#include <memory.h>
#include <string>
#include <string_view>
#include <limits>
#include <type_traits>

#include "swoc/swoc_version.h"
#include "swoc/string_view_util.h"

// For no apparent reason, g++ 11 complains about array bound violations with either suffix_at or
// assign, the error message is too vague for me to be sure - it doesn't even provide the location of
// the method invocation. I've been using g++ 12 for development and I don't see that error.
#if __GNUC__ == 11
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
#endif

namespace swoc { inline namespace SWOC_VERSION_NS {

class TextView;

/** A set of characters.
 *
 */
class CharSet {
  using self_type = CharSet;

public:
  constexpr CharSet(TextView const &chars);
  bool
  operator()(u_char idx) const {
    return _chars[idx];
  }

protected:
  std::bitset<256> _chars;
};

/** A read only view of a contiguous piece of memory.

    A @c TextView does not own the memory to which it refers, it is simply a view of part of some
    (presumably) larger memory object. The purpose is to allow working in a read only way a specific
    part of the memory. A classic example for ATS is working with HTTP header fields and values
    which need to be accessed independently but preferably without copying. A @c TextView supports
    this style.

    @note To simplify the interface there is no constructor taking only a character pointer.
    Constructors require either a literal string or an explicit length. This avoid ambiguities which
    are much more annoying that explicitly calling @c strlen on a character pointer.

    @internal For construction, assignment operator, and @c assign method, there are a lot of overloads
    because users would like to be able to use the same sort of arguments for all of these. This includes
    - self / parent type
    - @c std::string
    - literal string
    - C-string pointer
    - pointer and count
    - begin/end style pointers.
    - character containers that have the STL standard @c size and @c data methods.
 */
class TextView : public std::string_view {
  using self_type  = TextView;         ///< Self reference type.
  using super_type = std::string_view; ///< Parent type.

public:
  /// Default constructor (empty buffer).
  constexpr TextView() noexcept = default;

  /// Construct from a @c std::string_view or @c TextView
  /// @note This provides an user defined conversion from @c std::string_view to @c TextView. The
  /// reverse conversion is implicit in @c TextView being a subclass of @c std::string_view.
  constexpr TextView(super_type const &that) noexcept;

  /** Construct from pointer and size.
   *
   * @param ptr Pointer to first character.
   * @param n Number of characters.
   *
   * If @a n is @c npos then @c ptr is presumed to be a C string and checked for length. If @c ptr
   * is @c nullptr the length is 0. Otherwise @c strlen is used to calculate the length.
   */
  constexpr TextView(char const *ptr, size_t n) noexcept;

  /** Construct from pointer and size.
   *
   * @param ptr Pointer to first character.
   * @param n Number of characters.
   */
  constexpr TextView(char const *ptr, unsigned n) noexcept;

  /** Construct from pointer and size.
   *
   * @param ptr Pointer to first character.
   * @param n Number of characters.
   *
   * If @a n is negative then @c ptr is presumed to be a C string and checked for length. If @c ptr
   * is @c nullptr the length is 0. Otherwise @c strlen is used to calculate the length.
   */
  constexpr TextView(char const *ptr, ssize_t n) noexcept;

  /** Construct from pointer and size.
   *
   * @param ptr Pointer to first character.
   * @param n Number of characters.
   *
   * If @a n is negative then @c ptr is presumed to be a C string and checked for length. If @c ptr
   * is @c nullptr the length is 0. Otherwise @c strlen is used to calculate the length.
   */
  constexpr TextView(char const *ptr, int n) noexcept;

  /** Construct from a half open range [first, last).
   *
   * @param first Start of half open range.
   * @param last End of half open range.
   *
   * The character at @a first will be in the view, but the character at @a last will not.
   *
   * @note @c explicit to avoid interpreting a string initializer list as a view.
   *
   * @internal For the love of Turing, WHY DID YOU DO THIS?
   *
   * Well, estemed reader, because the C++ standard doesn't have a better way to support overloads
   * that handle character pointers and literal strings differently. If the parameters were simply
   * <tt>(char const *, char const *)</tt> then a construct like <tt>{ "really", "broken" }</tt> can
   * be interpreted as a @c TextView because the elements implicitly convert to <tt>char const
   * *</tt>. This makes no sense and creates some @b very annoying ambiguities for lists of strings
   * if there are exactly two in the list. See @c Lexicon for an example.
   *
   * The template itself does the check to make sure it's a character @b pointer and not an array. Arrays
   * are handled by a different constructor so this only disables constructing from two char arrays
   * which IMHO makes no sense and should be forbidden.
   */
  template <typename T>
  explicit TextView(
    T first,
    std::enable_if_t<!std::is_array_v<T> && std::is_pointer_v<T> && std::is_convertible_v<T, char const *>, T> last) noexcept
    : super_type(first, last - first) {}

  /** Construct from any character container following STL standards.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to @c char @c const @c * and @c size_t respectively.
   */
  template <typename C, typename = std::enable_if_t<std::is_convertible_v<decltype(std::declval<C>().data()), char const *> &&
                                                      std::is_convertible_v<decltype(std::declval<C>().size()), size_t>,
                                                    void>>
  constexpr TextView(C const &c);

  /** Construct from literal string or array.

      All elements of the array are included in the view unless the last element is nul, in which case it is elided.
      If this is inappropriate then a constructor with an explicit size should be used.

      @code
        TextView a("A literal string");
      @endcode
      The last character in @a a will be 'g'.
   */
  template <size_t N> constexpr TextView(const char (&s)[N]) noexcept;

  /** Construct from a C-string.
   *
   * @param src A pointer to a C-string.
   *
   * The view does not include the terminating nul.
   *
   * @internal @a src a reference because it is otherwise ambiguous with the literal constructor.
   */
  TextView(char *&src) : super_type(src, src ? strlen(src) : 0) {}

  /** Construct from a const C-string.
   *
   * @param src Pointer to a const C-string.
   *
   * The view does not include the terminating nul.
   *
   * @internal @a src a reference because it is otherwise ambiguous with the literal constructor.
   */
  TextView(char const *&src) : super_type(src, src ? strlen(src) : 0) {}

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr TextView(std::nullptr_t) noexcept;

  /// Construct from @c std::string, referencing the entire string contents.
  /// @internal This can't be @c constexpr because this uses methods in @c std::string that may
  /// not be @c constexpr.
  TextView(std::string const &str) noexcept;

  /// Assign a super class instance, @c std::string_view  to @a this.
  self_type &operator=(super_type const &that);

  /// Assign a constant array to @a this.
  /// @note If the last character of @a s is a nul byte, it is not included in the view.
  template <size_t N> self_type &operator=(const char (&s)[N]);

  /// Assign from C-string @a s.
  self_type &operator=(char *&s);
  /// Assign from C-string @a s.
  self_type &operator=(char const *&s);

  /// Assign from a @c std::string.
  self_type &operator=(const std::string &s);

  /** Assign a view of the @a c_str
   *
   * @param c_str Pointer to C string.
   * @return @a this
   *
   * @note @c c_str must be a null terminated string. The null byte is not included in the view.
   */
  self_type &assign(char *&c_str);

  /** Assign a view of the @a c_str
   *
   * @param c_str Pointer to C string.
   * @return @a this
   *
   * @note @c c_str must be a null terminated string. The null byte is not included in the view.
   */
  self_type &assign(char const *&c_str);

  /** Assign from a pointer and size.
   *
   * @param ptr Pointer to first character of the view.
   * @param n Length of the view.
   * @return @a this
   *
   * if @a n is @a npos then @c strlen is used determine the size of the view.
   */
  self_type &assign(char const *ptr, size_t n);

  /** Assign the half open view [ @a b , @a e ) to @a this
   *
   * @param b First character in the view.
   * @param e One character after the last character in the view.
   * @return @a this
   */
  self_type &assign(char const *b, char const *e);

  /// Explicitly set the view from a @c std::string
  self_type &assign(std::string const &s);

  /** Assign literal string or array.

   * All elements of the array are included in the view unless the last element is nul, in which case it is elided.
   * If this is inappropriate then a constructor with an explicit size should be used.
   *
   * @code
   *   tv.assign("A literal string");
   * @endcode
   * The last character in @a tv will be 'g'.
  */
  template <size_t N> self_type &assign(const char (&s)[N]) noexcept;

  /** Assign from any character container following STL standards.
   *
   * @tparam C Container type.
   * @param c container
   *
   * The container type must have the methods @c data and @c size which must return values convertible
   * to @c char @c const @c * and @c size_t respectively.
   */
  template <typename C, typename = std::enable_if_t<std::is_convertible_v<decltype(std::declval<C>().data()), char const *> &&
                                                      std::is_convertible_v<decltype(std::declval<C>().size()), size_t>,
                                                    void>>
  constexpr self_type &
  assign(C const &c) {
    return this->assign(c.data(), c.size());
  }

  /** Dereference operator.

      @note This allows the view to be used as if it were a character iterator to a null terminated
      string which is handy for several other STL interfaces.

      @return The first byte in the view, or a nul character if the view is empty.
  */
  /// @return The first byte in the view.
  constexpr char operator*() const;

  /** Discard the first byte of the view.
   *
   *  @return @a this.
   */
  self_type &operator++();

  /** Discard the first byte of the view.
   *
   * @return The view before discarding the byte.
   */
  self_type operator++(int);

  /** Discard the first @a n bytes of the view.
   *
   *  Equivalent to @c remove_prefix(n).
   *  @return @a this
   */
  self_type &operator+=(size_t n);

  /// Check for empty view.
  /// @return @c true if the view has a nullptr @b or zero size.
  constexpr bool operator!() const noexcept;

  /// Check for non-empty view.
  /// @return @c true if the view refers to a non-empty range of bytes.
  explicit constexpr operator bool() const noexcept;

  /// Clear the view (become an empty view).
  self_type &clear();

  /// Get the offset of the first character for which @a pred is @c true.
  template <typename F> size_t find_if(F const &pred) const;
  /// Get the offset of the last character for which @a pred is @c true.
  template <typename F> size_t rfind_if(F const &pred) const;

  /** Remove bytes that match @a c from the start of the view.
   *
   * @return @a this
   */
  self_type &ltrim(char c);

  /** Remove bytes from the start of the view that are in @a delimiters.
   *
   * @return @a this
   */
  self_type &ltrim(CharSet const &delimiters);

  /** Remove bytes from the start of the view that are in @a delimiters.
   *
   * @return @a this
   */
  self_type &ltrim(std::string_view const &delimiters);

  /** Remove bytes from the start of the view that are in @a delimiters.
   *
   * @internal This is needed to avoid collisions with the templated predicate style.
   *
   * @return @c *this
   */
  self_type &ltrim(const char *delimiters);

  /** Remove bytes from the start of the view for which @a pred is @c true.
      @a pred must be a functor taking a @c char argument and returning @c bool.
      @return @c *this
  */
  template <typename F> self_type &ltrim_if(F const &pred);

  /** Remove bytes that match @a c from the end of the view.
   *
   * @return @a this
   */
  self_type &rtrim(char c);

  /** Remove bytes from the end of the view that are in @a delimiters.
   *
   * @return @a this
   */
  self_type &rtrim(CharSet const &delimiters);

  /** Remove bytes from the end of the view that are in @a delimiters.
   * @return @a this
   */
  self_type &rtrim(std::string_view const &delimiters);

  /** Remove bytes from the end of the view for which @a pred is @c true.
   *
   * @a pred must be a functor taking a @c char argument and returning @c bool.
   *
   * @return @c *this
   */
  template <typename F> self_type &rtrim_if(F const &pred);

  /** Remove bytes that match @a c from the start and end of this view.
   *
   * @return @a this
   */
  self_type &trim(char c);

  /** Remove bytes from the start and end of the view that are in @a delimiters.
   * @return @a this
   */
  self_type &trim(CharSet const &delimiters);

  /** Remove bytes from the start and end of the view that are in @a delimiters.
   * @return @a this
   */
  self_type &trim(std::string_view const &delimiters);

  /** Remove bytes from the start and end of the view that are in @a delimiters.
      @internal This is needed to avoid collisions with the templated predicate style.
      @return @c *this
  */
  self_type &trim(const char *delimiters);

  /** Remove bytes from the start and end of the view for which @a pred is @c true.
      @a pred must be a functor taking a @c char argument and returning @c bool.
      @return @c *this
  */
  template <typename F> self_type &trim_if(F const &pred);

  /** Get a view of the first @a n bytes.
   *
   * @param n Number of chars in the prefix.
   * @return A view of the first @a n characters in @a this, bounded by the size of @a this.
   */
  constexpr self_type prefix(size_t n) const noexcept;

  /** Get a view of a prefix bounded by @a c.
   *
   * @param c Delimiter character.
   * @return A view of the prefix bounded by @a c, or all of @a this if @a c is not found.
   * @note The character @a c is not included in the returned view.
   */
  self_type prefix_at(char c) const;

  /** Get a view of a prefix bounded by a character in @a delimiters.
   *
   * @param delimiters A set of characters.
   *
   * @return A view of the prefix bounded by any character in @a delimiters, or empty if none are
   * found.
   *
   * @note The delimiter character is not included in the returned view.
   */
  self_type prefix_at(std::string_view const &delimiters) const;

  /** Get a view of a prefix bounded by a character predicate @a pred.
   *
   * @a pred must be a functor which takes a @c char argument and returns @c bool. Each character in
   * @a this is tested by @a pred and the prefix is delimited by the first character for which @a
   * pred is @c true.
   *
   * @param pred A character predicate.
   *
   * @return A view of the prefix bounded by @a pred or empty if @a pred is not @c true for any
   * characer.
   *
   * @note The deliminting character is not included in the returned view.
   */
  template <typename F> self_type prefix_if(F const &pred) const;

  /** Remove bytes from the start of the view.
   *
   * @param n Number of bytes to remove.
   * @return @a this.
   */
  self_type &remove_prefix(size_t n);

  /** Remove bytes from the end of the view.
   *
   * @param n Number of bytes to remove.
   * @return @a this.
   */
  self_type &remove_suffix(size_t n);

  /** Remove the leading characters of @a this up to and including @a c.
   *
   * @param c Delimiter character.
   * @return @a this.
   * @note The first occurrence of character @a c is removed along with all preceding characters, or
   * the view is cleared if @a c is not found.
   */
  self_type &remove_prefix_at(char c);

  /** Remove the leading characters of @a this up to and including the first character matching @a delimiters.
   *
   * @param delimiters Characters to match.
   * @return @a this.
   * @note The first occurrence of any character in @a delimiters is removed along with all preceding
   * characters, or the view is cleared if none are found.
   */
  self_type &remove_prefix_at(std::string_view const &delimiters);

  /** Remove the leading characters up to and including the character selected by @a pred.
   *
   * @tparam F Predicate function type.
   * @param pred The predicate instance.
   * @return @a this.
   *
   * Characters are removed until @a pred returns @c true. The matching character is also removed.
   */
  template <typename F> self_type &remove_prefix_if(F const &pred);

  /** Remove and return a prefix of size @a n.
   *
   * @param n Size of the prefix.
   * @return The first @a n bytes of @a this if @a n is in @a this, otherwise an empty view.
   *
   * The prefix is removed and returned if the requested prefix is no larger than @a this,
   * otherwise @a this is not modified.
   *
   * @note The character at offset @a n is discarded if @a this is modified.
   *
   * @see @c take_prefix
   */
  self_type split_prefix(size_t n);

  /** Remove and return a prefix bounded by the first occurrence of @a c.
   *
   * @param c The character to match.
   * @return The prefix bounded by @a c if @a c is found, an empty view if not.
   *
   * The prefix is removed and returned if @a c is found, otherwise @a this is not modified.
   *
   * @note The delimiter character is discarded if @a this is modified.
   *
   * @see @c take_prefix
   */
  self_type split_prefix_at(char c);

  /** Remove and return a prefix bounded by the first occurrence of any of @a delimiters.
   *
   * @param delimiters The characters to match.
   * @return The prefix bounded by a delimiter if one is found, otherwise an empty view.
   *
   * The prefix is removed and returned if a @a delimiter is found, otherwise @a this is not modified.
   *
   * @note The matching character is discarded if @a this is modified.
   *
   * @see @c take_prefix_at
   */
  self_type split_prefix_at(std::string_view const &delimiters);

  /** Remove and return a prefix bounded by the first character that satisfies @a pred.
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The prefix bounded by the first character satisfying @a pred.
   *
   * The prefix is removed and returned if a character satisfying @a pred is found, otherwise
   * @a this is not modified.
   *
   * @note The matching character is discarded if @a this is modified.
   *
   * @see @c take_prefix_if
   */
  template <typename F> self_type split_prefix_if(F const &pred);

  /** Remove and return the first @a n characters.
   *
   * @param n Size of the return prefix.
   * @return The first @a n bytes of @a this if @a n is in @a this, otherwise all of @a this.
   *
   * The prefix is removed and returned if the requested prefix is no larger than @a this,
   * otherwise all of @a this is removed and returned.
   *
   * @note The character at offset @a n is discarded if @a n is within the bounds of @a this.
   *
   * @see @c split_prefix
   */
  self_type take_prefix(size_t n);

  /** Remove and return a prefix bounded by the first occurrence of @a c.
   *
   * @param c The character to match.
   * @return The prefix bounded by @a c if @a c is found, all of @a this if not.
   *
   * The prefix is removed and returned if @a c is found, otherwise all of @a this is removed and
   * returned.
   *
   * @note The character at offset @a n is discarded if found.
   *
   * @see @c split_prefix_at
   */
  self_type take_prefix_at(char c);

  /** Remove and return a prefix bounded by the first occurrence of any of @a delimiters.
   *
   * @param delimiters The characters to match.
   * @return The prefix bounded by a delimiter if one is found, otherwise all of @a this.
   *
   * The prefix is removed and returned if a @a delimiter is found, otherwise all of @a this is
   * removed and returned.
   *
   * @note The matching character is discarded if found.
   *
   * @see @c split_prefix_at
   */
  self_type take_prefix_at(std::string_view const &delimiters);

  /** Remove and return a prefix bounded by the first character that satisfies @a pred.
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The prefix bounded by the first character satisfying @a pred, or all of @a this if none
   * is found.
   *
   * The prefix is removed and returned if a character satisfying @a pred is found, otherwise
   * all of @a this is removed and returned.
   *
   * @note The matching character is discarded if found.
   *
   * @see @c split_prefix_if
   */
  template <typename F> self_type take_prefix_if(F const &pred);

  /** Remove and return a prefix of characters satisfying @a pred
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The prefix of characters that satisfy @a pred.
   *
   * The returned prefix is removed from @a this. That prefix may be empty if the first character
   * does not satisfy @a pred.
   *
   * @note This is very similar to @c ltrim_if but returns the removed text instead of the modified
   * view.
   */
  template <typename F> self_type clip_prefix_of(F const &pred);

  /** Get a view of the last @a n bytes.
   *
   * @param n Number of chars in the suffix.
   * @return A view of the last @a n characters in @a this, bounded by the size of @a this.
   */
  constexpr self_type suffix(size_t n) const noexcept;

  /** Get a view of a suffix bounded by @a c.
   *
   * @param c Delimiter character.
   * @return A view of the suffix bounded by @a c, or all of @a this if @a c is not found.
   * @note The character @a c is not included in the returned view.
   */
  self_type suffix_at(char c) const;

  /** Get a view of a suffix bounded by a character in @a delimiters.
   *
   * @param delimiters A set of characters.
   *
   * @return A view of the suffix bounded by any character in @a delimiters, or mepty if none are
   * found.
   *
   * @note The delimiter character is not included in the returned view.
   */
  self_type suffix_at(std::string_view const &delimiters) const;

  /** Get a view of a suffix bounded by a character predicate @a pred.
   *
   * @a pred must be a functor which takes a @c char argument and returns @c bool. Each character in
   * @a this is tested by @a pred and the suffix is delimited by the last character for which @a
   * pred is @c true.
   *
   * @param pred A character predicate.
   *
   * @return A view of the suffix bounded by @a pred or empty if @a pred is not @c true for any
   * character.
   *
   * @note The delimiting character is not included in the returned view.
   */
  template <typename F> self_type suffix_if(F const &pred) const;

  /** Remove the trailing characters of @a this up to and including @a c.
   *
   * @param c Delimiter character.
   * @return @a this.
   *
   * @note The last occurrence of character @a c is removed along with all succeeding characters, or
   * the view is cleared if @a c is not found.
   */
  self_type &remove_suffix_at(char c);

  /** Remove the trailing characters of @a this up to and including the last character matching @a delimiters.
   *
   * @param delimiters Characters to match.
   * @return @a this.
   * @note The first occurrence of any character in @a delimiters is removed along with all preceding
   * characters, or the view is cleared if none are found.
   */
  self_type &remove_suffix_at(std::string_view const &delimiters);

  /** Remove the trailing characters up to and including the character selected by @a pred.
   *
   * @tparam F Predicate function type.
   * @param pred The predicate instance.
   * @return @a this.
   *
   * If predicate is never true the view is cleared.
   */
  template <typename F> self_type &remove_suffix_if(F const &pred);

  /** Remove and return a suffix of size @a n.
   *
   * @param n Size of the suffix.
   * @return The first @a n bytes of @a this if @a n is in @a this, otherwise an empty view.
   *
   * The prefix is removed and returned if the requested suffix is no larger than @a this,
   * otherwise @a this is not modified.
   *
   * @note The character at offset @a n is discarded if @a this is modified.
   *
   * @see @c take_suffix
   */
  self_type split_suffix(size_t n);

  /** Remove and return a suffix bounded by the last occurrence of @a c.
   *
   * @param c The character to match.
   * @return The suffix bounded by @a c if @a c is found, an empty view if not.
   *
   * The suffix is removed and returned if @a c is found, otherwise @a this is not modified.
   *
   * @note The character at offset @a n is discarded if @a this is modified.
   *
   * @see @c take_suffix_at
   */
  self_type split_suffix_at(char c);

  /** Remove and return a suffix bounded by the last occurrence of any of @a delimiters.
   *
   * @param delimiters The characters to match.
   * @return The suffix bounded by a delimiter if found, an empty view if none found.
   *
   * The suffix is removed and returned if delimiter is found, otherwise @a this is not modified.
   *
   * @note The delimiter character is discarded if @a this is modified.
   *
   * @see @c take_suffix_at
   */
  self_type split_suffix_at(std::string_view const &delimiters);

  /** Remove and return a suffix bounded by the last character that satisfies @a pred.
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The suffix bounded by the first character satisfying @a pred if found, otherwise @a this
   * is not modified.
   *
   * The prefix is removed and returned if a character satisfying @a pred if found, otherwise
   * @a this is not modified.
   *
   * @note The matching character is discarded if @a this is modified.
   *
   * @see @c take_suffix_if
   */
  template <typename F> self_type split_suffix_if(F const &pred);

  /** Remove and return a suffix of size @a n.
   *
   * @param n Size of the suffix.
   * @return The first @a n bytes of @a this if @a n is in @a this, otherwise all of @a this.
   *
   * The returned suffix is removed from @a this, along with the character at offset @a n if present.
   *
   * @see @c split_suffix
   */
  self_type take_suffix(size_t n);

  /** Remove and return a suffix bounded by the last occurrence of @a c.
   *
   * @param c The character to match.
   * @return The suffix bounded by @a c if @a c is found, all of @a this if not.
   *
   * The returned suffix is removed from @a this, along with the delimiter character if found.
   *
   * @see @c split_suffix_at
   */
  self_type take_suffix_at(char c);

  /** Remove and return a suffix bounded by the last occurrence of any of @a delimiters.
   *
   * @param delimiters The characters to match.
   * @return The suffix bounded by a delimiter if @a c is found, all of @a this if not.
   *
   * The returned suffix is removed from @a this, along with the delimiter character if found.
   *
   * @see @c split_suffix_at
   */
  self_type take_suffix_at(std::string_view const &delimiters);

  /** Remove and return a suffix bounded by the last character that satisfies @a pred.
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The suffix bounded by the first character satisfying @a pred if found, otherwise all of @a this.
   *
   * @note The matching character is discarded if found.
   *
   * @see @c split_suffix_if
   */
  template <typename F> self_type take_suffix_if(F const &pred);

  /** Remove and return a suffix of characters satisfying @a pred
   *
   * @tparam F Predicate functor type.
   * @param pred A function taking @c char and returning @c bool.
   * @return The suffix of characters that satisfy @a pred.
   *
   * The returned suffix is removed from @a this. That suffix may be empty if the last character
   * does not satisfy @a pred.
   *
   * @note This is very similar to @c rtrim_if but returns the removed text instead of the modified
   * view.
   */
  template <typename F> self_type clip_suffix_of(F const &pred);

  /** Get a view of part of this view.
   *
   * @param pos Offset of first byte in the new view.
   * @param count Number of bytes in the view.
   * @return The view starting at @a pos for @a count bytes.
   *
   * The returned view is clipped by @a this - that is, it will not extend beyond the original view.
   * @a count is reduced such that it covers only data in @a this.
   *
   * @note This is provided primarily for co-variance, i.e. the returned view is a @c TextView
   * instead of a @c std::string_view.
   */
  constexpr self_type substr(size_type pos = 0, size_type count = npos) const noexcept;

  /** Check if the view begins with a specific @a prefix.
   *
   * @param prefix String to check against @a this.
   * @return @c true if <tt>this->prefix(prefix.size()) == prefix</tt>, @c false otherwise.
   * @internal C++20 preview.
   */
  bool starts_with(std::string_view const &prefix) const noexcept;

  /** Check if the view begins with a specific @a prefix.
   *
   * @param prefix String to check against @a this.
   * @return @c true if <tt>this->prefix(prefix.size()) == prefix</tt>, @c false otherwise.
   * @internal C++20 preview.
   */
  bool starts_with(char const *prefix) const;

  /** Check if the view begins with the character @c c.
   *
   * @param c Character to check.
   * @return @c true if the string is non-empty and the first character is @c c.
   * @internal C++20 preview.
   */
  bool starts_with(char c) const noexcept;

  /** Check if the view begins with a specific @a prefix, ignoring case.
   *
   * @param prefix String to check against @a this.
   * @return @c true if <tt>this->prefix(prefix.size()) == prefix</tt> without regard to case, @c false otherwise.
   * @internal C++20 preview.
   */
  bool starts_with_nocase(std::string_view const &prefix) const noexcept;

  /** Check if the view begins with a specific @a prefix.
   *
   * @param prefix String to check against @a this.
   * @return @c true if <tt>this->prefix(prefix.size()) == prefix</tt>, @c false otherwise.
   * @internal C++20 preview.
   */
  bool starts_with_nocase(char const *prefix) const;

  /** Check if the view begins with the character @c c, ignoring case.
   *
   * @param c Character to check.
   * @return @c true if the string is non-empty and the first character is @c c.
   * @internal C++20 preview.
   */
  bool starts_with_nocase(char c) const noexcept;

  /** Check if the view ends with a specific @a suffix.
   *
   * @param suffix String to check against @a this.
   * @return @c true if <tt>this->suffix(suffix.size()) == suffix</tt>, @c false otherwise.
   * @internal C++20 preview.
   */
  bool ends_with(std::string_view const &suffix) const noexcept;

  /** Check if the view ends with a specific @a suffix.
   *
   * @param suffix String to check against @a this.
   * @return @c true if <tt>this->suffix(suffix.size()) == suffix</tt>, @c false otherwise.
   * @internal C++20 preview.
   */
  bool ends_with(char const *suffix) const;

  /** Check the view ends with the character @c c.
   *
   * @param c Character to check.
   * @return @c true if the string is non-empty and the last character is @c c.
   * @internal C++20 preview.
   */
  bool ends_with(char c) const noexcept;

  /** Check if the view starts with a specific @a suffix, ignoring case.
   *
   * @param suffix String to check against @a this.
   * @return @c true if <tt>this->suffix(suffix.size()) == suffix</tt> without regard to case, @c false otherwise.
   * @internal C++20 preview.
   */
  bool ends_with_nocase(std::string_view const &suffix) const noexcept;

  /** Check if the view starts with a specific @a suffix, ignoring case.
   *
   * @param suffix String to check against @a this.
   * @return @c true if <tt>this->suffix(suffix.size()) == suffix</tt> without regard to case, @c false otherwise.
   * @internal C++20 preview.
   */
  bool ends_with_nocase(char const *suffix) const;

  /** Check the view ends with the character @c c, ignoring case.
   *
   * @param c Character to check.
   * @return @c true if the string is non-empty and the last character is @c c.
   * @internal C++20 preview.
   */
  bool ends_with_nocase(char c) const noexcept;

  // Functors for using this class in STL containers.
  /// Ordering functor, lexicographic comparison.
  struct LessThan {
    /// @return Case sensitive ordering.
    bool
    operator()(self_type const &lhs, self_type const &rhs) const noexcept {
      return -1 == strcmp(lhs, rhs);
    }
  };

  /// Ordering functor, case ignoring lexicographic comparison.
  struct LessThanNoCase {
    /// @return Case insensitive ordering.
    bool
    operator()(self_type const &lhs, self_type const &rhs) const noexcept {
      return -1 == strcasecmp(lhs, rhs);
    }
  };

  /// Support for containers that need case insensitive comparisons between views.
  struct CaselessEqual {
    /// @return @c true if the view contants are equal when compared without regard to case.
    bool
    operator()(self_type const &lhs, self_type const &rhs) const noexcept {
      return lhs.size() == rhs.size() && 0 == strcasecmp(lhs, rhs);
    }
  };

  /** A pointer to the first byte.
   *
   * @return Address of the first byte of the view.
   *
   * @internal This fixes an error in @c std::string_view where this method is declared to return
   * a template parameter instead of the correct @c value_type. The effect is @c string_view::data
   * is not considered by the compiler to return <tt>char const *</tt> which makes meta-programming
   * painful.
   */
  constexpr value_type const *data() const noexcept;

  /** A pointer to past the last byte.
   *
   * @return Address of the first byte past the end of the view.
   *
   * This is effectively @c std::string_view::end() except it explicit returns a pointer and not
   * (potentially) an iterator class, to match up with @c data().
   */
  constexpr value_type const *data_end() const noexcept;

  /// Specialized stream operator implementation.
  /// @note Use the standard stream operator unless there is a specific need for this, which is unlikely.
  /// @return The stream @a os.
  /// @internal Needed because @c std::ostream::write must be used and
  /// so alignment / fill have to be explicitly handled.
  template <typename Stream> Stream &stream_write(Stream &os, const TextView &b) const;

  /// @cond OVERLOAD
  // These methods are all overloads of other methods, defined in order to make the API more
  // convenient to use. Mostly these overload @c int for @c size_t so naked numbers work as expected.
  constexpr self_type prefix(int n) const noexcept;
  self_type take_suffix(int n);
  self_type split_prefix(int n);
  constexpr self_type suffix(int n) const noexcept;
  self_type split_suffix(int n);
  /// @endcond

protected:
  /// Initialize a bit mask to mark which characters are in this view.
  static void init_delimiter_set(std::string_view const &delimiters, std::bitset<256> &set);
};

/// Internal table of digit values for characters.
/// This is -1 for characters that are not valid digits.
extern const int8_t svtoi_convert[256];

/** Convert the text in @c TextView @a src to a signed numeric value.

    If @a parsed is non-null then the part of the string actually parsed is placed there.
    @a base sets the conversion base. If not set base 10 is used with two special cases:

    - If the number starts with a literal '0' then it is treated as base 8.
    - If the number starts with the literal characters '0x' or '0X' then it is treated as base 16.

    If @a base is explicitly set then any leading radix indicator is not supported.
*/
intmax_t svtoi(TextView src, TextView *parsed = nullptr, int base = 0);

/** Convert the text in @c TextView @a src to an unsigned numeric value.

    If @a parsed is non-null then the part of the string actually parsed is placed there.
    @a base sets the conversion base. If not set base 10 is used with two special cases:

    - If the number starts with a literal '0' then it is treated as base 8.
    - If the number starts with the literal characters '0x' or '0X' then it is treated as base 16.

    If @a base is explicitly set then any leading radix indicator is not supported.
*/
uintmax_t svtou(TextView src, TextView *parsed = nullptr, int base = 0);

/** Convert the text in @c src to an unsigned numeric value.
 *
 * @tparam N The radix (must be  1..36)
 * @param src The source text. Updated during parsing.
 * @return The converted numeric value.
 *
 * This is a specialized function useful only where conversion performance is critical. It is used
 * inside @c svtoi and @a svtou for the common cases of 8, 10, and 16, therefore normally this isn't much more
 * performant in those cases than just @c svtoi. Because of this only positive values are parsed.
 * If determining the radix from the text or signed value parsing is needed, used @c svtoi.
 *
 * @a src is updated in place to indicate what characters were parsed by removing them from the view
 * Parsing stops on the first invalid digit, so any leading non-digit characters (e.g. whitespace)
 * must already be removed. For overflow, all valid digits are consumed and the maximum value returned.
 */
template <int RADIX>
uintmax_t
svto_radix(TextView &src) {
  static_assert(0 < RADIX && RADIX <= 36, "Radix must be in the range 1..36");
  static constexpr auto MAX            = std::numeric_limits<uintmax_t>::max();
  static constexpr auto OVERFLOW_LIMIT = MAX / RADIX;
  uintmax_t zret                       = 0;
  int8_t v;
  while (src.size() && (0 <= (v = swoc::svtoi_convert[uint8_t(*src)])) && v < RADIX) {
    // Tweaked for performance - need to check range after @a RADIX multiply.
    ++src; // Update view iff the character is parsed.
    if (zret <= OVERFLOW_LIMIT && uintmax_t(v) <= MAX - (zret *= RADIX)) {
      zret += v;
    } else {
      zret = MAX; // clamp to max - once set will always hit this case for subsequent input.
    }
  }
  return zret;
}

/// Convenience overload.
/// @see svto_radix(swoc::TextView &src)
template <int N>
uintmax_t
svto_radix(TextView &&src) {
  return svto_radix<N>(src);
}

/** Parse @a text as a floating point number.
 *
 * @param text The input text.
 * @param parsed Parsed text [out]
 * @return The floating point value, or 0.0 if invalid input.
 *
 * If @a parsed is not @a nullptr then the span of characters parsed is put there. This can be
 * used to check if the parse was scuccesful - on a failed parse, it will be empty.
 *
 * @note This should be within 1 epsilon of correct, although it doesn't guarantee picking
 * the closest epsilon. It's more than sufficient for use in configurations, but possibly
 * not for high precision work.
 */
double svtod(TextView text, TextView *parsed = nullptr);
// ----------------------------------------------------------
// Inline implementations.
// Note: Why, you may ask, do I use @c TextView::self_type for return type instead of the
// simpler plain @c TextView ? Because otherwise Doxygen can't match up the declaration and
// definition and the reference documentation is messed up. Sigh.

inline constexpr CharSet::CharSet(TextView const &chars) {
  for (auto c : chars) {
    _chars[u_char(c)] = true;
  }
}

// === TextView Implementation ===
/// @cond TextView_INTERNAL
// Doxygen doesn't match these up well due to various type and template issues.
// @internal If there is more than one overload for numeric types, it's easy to get ambiguity. The only
// fix, unfortunately, is lots of overloads to cover the ambiguous cases.
inline constexpr TextView::TextView(const char *ptr, size_t n) noexcept
  : super_type(ptr, n == npos ? (ptr ? ::strlen(ptr) : 0) : n) {}
inline constexpr TextView::TextView(const char *ptr, unsigned n) noexcept : super_type(ptr, size_t(n)) {}
inline constexpr TextView::TextView(const char *ptr, ssize_t n) noexcept
  : super_type(ptr, n < 0 ? (ptr ? ::strlen(ptr) : 0) : size_t(n)) {}
inline constexpr TextView::TextView(const char *ptr, int n) noexcept
  : super_type(ptr, n < 0 ? (ptr ? ::strlen(ptr) : 0) : size_t(n)) {}
inline constexpr TextView::TextView(std::nullptr_t) noexcept : super_type(nullptr, 0) {}
inline TextView::TextView(std::string const &str) noexcept : super_type(str) {}
inline constexpr TextView::TextView(super_type const &that) noexcept : super_type(that) {}
template <size_t N> constexpr TextView::TextView(const char (&s)[N]) noexcept : super_type(s, s[N - 1] ? N : N - 1) {}
template <typename C, typename> constexpr TextView::TextView(C const &c) : super_type(c.data(), c.size()) {}

inline void
TextView::init_delimiter_set(std::string_view const &delimiters, std::bitset<256> &set) {
  set.reset();
  for (char c : delimiters)
    set[static_cast<uint8_t>(c)] = true;
}

inline auto
TextView::clear() -> self_type & {
  new (this) self_type();
  return *this;
}

inline constexpr char
TextView::operator*() const {
  return this->empty() ? char(0) : *(this->data());
}

inline constexpr bool
TextView::operator!() const noexcept {
  return this->empty();
}

inline constexpr TextView::operator bool() const noexcept {
  return !this->empty();
}

inline auto
TextView::operator++() -> self_type & {
  this->remove_prefix(1);
  return *this;
}

inline auto
TextView::operator++(int) -> self_type {
  self_type zret{*this};
  this->remove_prefix(1);
  return zret;
}

inline auto
TextView::operator+=(size_t n) -> self_type & {
  this->remove_prefix(n);
  return *this;
}

template <size_t N>
inline auto
TextView::operator=(const char (&s)[N]) -> self_type & {
  return *this = self_type{s, s[N - 1] ? N : N - 1};
}

inline auto
TextView::operator=(super_type const &that) -> self_type & {
  this->super_type::operator=(that);
  return *this;
}

inline auto
TextView::operator=(char *&s) -> self_type & {
  this->super_type::operator=(s);
  return *this;
}

inline auto
TextView::operator=(char const *&s) -> self_type & {
  this->super_type::operator=(s);
  return *this;
}

inline auto
TextView::operator=(const std::string &s) -> self_type & {
  this->super_type::operator=(s);
  return *this;
}

inline auto
TextView::assign(char *&c_str) -> self_type & {
  return this->assign(c_str, strlen(c_str));
}

inline auto
TextView::assign(char const *&c_str) -> self_type & {
  return this->assign(c_str, strlen(c_str));
}

inline auto
TextView::assign(const std::string &s) -> self_type & {
  *this = super_type(s);
  return *this;
}

inline TextView &
TextView::assign(char const *ptr, size_t n) {
  *this = super_type(ptr, n == npos ? (ptr ? ::strlen(ptr) : 0) : n);
  return *this;
}

inline TextView &
TextView::assign(char const *b, char const *e) {
  *this = super_type(b, e - b);
  return *this;
}

template <size_t N>
inline auto
TextView::assign(char const (&s)[N]) noexcept -> self_type & {
  return *this = self_type{s, s[N - 1] ? N : N - 1};
}

inline constexpr auto
TextView::prefix(size_t n) const noexcept -> self_type {
  return {this->data(), std::min(n, this->size())};
}

inline constexpr TextView
TextView::prefix(int n) const noexcept {
  return {this->data(), std::min<size_t>(n, this->size())};
}

inline auto
TextView::prefix_at(char c) const -> self_type {
  self_type zret; // default to empty return.
  if (auto n = this->find(c); n != npos) {
    zret.assign(this->data(), n);
  }
  return zret;
}

inline TextView
TextView::prefix_at(std::string_view const &delimiters) const {
  self_type zret; // default to empty return.
  if (auto n = this->find_first_of(delimiters); n != npos) {
    zret.assign(this->data(), n);
  }
  return zret;
}

template <typename F>
auto
TextView::prefix_if(F const &pred) const -> self_type {
  self_type zret; // default to empty return.
  if (auto n = this->find_if(pred); n != npos) {
    zret.assign(this->data(), n);
  }
  return zret;
}

inline auto
TextView::remove_prefix(size_t n) -> self_type & {
  this->super_type::remove_prefix(std::min(n, this->size()));
  return *this;
}

inline TextView &
TextView::remove_prefix_at(char c) {
  if (auto n = this->find(c); n != npos) {
    this->super_type::remove_prefix(n + 1);
  }
  return *this;
}

inline auto
TextView::remove_prefix_at(std::string_view const &delimiters) -> self_type & {
  if (auto n = this->find_first_of(delimiters); n != npos) {
    this->super_type::remove_prefix(n + 1);
  }
  return *this;
}

template <typename F>
auto
TextView::remove_prefix_if(F const &pred) -> self_type & {
  if (auto n = this->find_if(pred); n != npos) {
    this->super_type::remove_prefix(n + 1);
  }
  return *this;
}

inline TextView
TextView::split_prefix(size_t n) {
  self_type zret; // default to empty return.
  if (n < this->size()) {
    zret = this->prefix(n);
    this->remove_prefix(std::min(n + 1, this->size()));
  }
  return zret;
}

inline TextView
TextView::split_prefix(int n) {
  return this->split_prefix(size_t(n));
}

inline TextView
TextView::split_prefix_at(char c) {
  return this->split_prefix(this->find(c));
}

inline TextView
TextView::split_prefix_at(std::string_view const &delimiters) {
  return this->split_prefix(this->find_first_of(delimiters));
}

template <typename F>
TextView::self_type
TextView::split_prefix_if(F const &pred) {
  return this->split_prefix(this->find_if(pred));
}

inline TextView
TextView::take_prefix(size_t n) {
  n              = std::min(n, this->size());
  self_type zret = this->prefix(n);
  this->remove_prefix(std::min(n + 1, this->size()));
  return zret;
}

inline TextView
TextView::take_prefix_at(char c) {
  return this->take_prefix(this->find(c));
}

inline TextView
TextView::take_prefix_at(std::string_view const &delimiters) {
  return this->take_prefix(this->find_first_of(delimiters));
}

template <typename F>
auto
TextView::take_prefix_if(F const &pred) -> self_type {
  return this->take_prefix(this->find_if(pred));
}

inline constexpr TextView
TextView::suffix(size_t n) const noexcept {
  n = std::min(n, this->size());
  return {this->data_end() - n, n};
}

inline constexpr TextView
TextView::suffix(int n) const noexcept {
  return this->suffix(size_t(n));
}

inline TextView
TextView::suffix_at(char c) const {
  self_type zret;
  if (auto n = this->rfind(c); n != npos) {
    ++n;
    zret.assign(this->data() + n, this->size() - n);
  }
  return zret;
}

inline TextView
TextView::suffix_at(std::string_view const &delimiters) const {
  self_type zret;
  if (auto n = this->find_last_of(delimiters); n != npos) {
    ++n;
    zret.assign(this->data() + n, this->size() - n);
  }
  return zret;
}

template <typename F>
auto
TextView::suffix_if(F const &pred) const -> self_type {
  self_type zret;
  if (auto n = this->rfind_if(pred); n != npos) {
    ++n;
    zret.assign(this->data() + n, this->size() - n);
  }
  return zret;
}

inline auto
TextView::remove_suffix(size_t n) -> self_type & {
  this->super_type::remove_suffix(std::min(n, this->size()));
  return *this;
}

inline TextView &
TextView::remove_suffix_at(char c) {
  if (auto n = this->rfind(c); n != npos) {
    return this->remove_suffix(this->size() - n);
  }
  return this->clear();
}

inline TextView &
TextView::remove_suffix_at(std::string_view const &delimiters) {
  if (auto n = this->find_last_of(delimiters); n != npos) {
    return this->remove_suffix(this->size() - n);
  }
  return this->clear();
}

template <typename F>
TextView::self_type &
TextView::remove_suffix_if(F const &pred) {
  if (auto n = this->rfind_if(pred); n != npos) {
    return this->remove_suffix(this->size() - n);
  }
  return this->clear();
}

inline TextView
TextView::split_suffix(size_t n) {
  self_type zret;
  n    = std::min(n, this->size());
  zret = this->suffix(n);
  this->remove_suffix(n + 1);
  return zret;
}

inline auto
TextView::split_suffix(int n) -> self_type {
  return this->split_suffix(size_t(n));
}

inline TextView
TextView::split_suffix_at(char c) {
  auto idx = this->rfind(c);
  return npos == idx ? self_type{} : this->split_suffix(this->size() - (idx + 1));
}

inline auto
TextView::split_suffix_at(std::string_view const &delimiters) -> self_type {
  auto idx = this->find_last_of(delimiters);
  return npos == idx ? self_type{} : this->split_suffix(this->size() - (idx + 1));
}

template <typename F>
TextView::self_type
TextView::split_suffix_if(F const &pred) {
  return this->split_suffix(this->rfind_if(pred));
}

inline TextView
TextView::take_suffix(size_t n) {
  self_type zret{*this};
  *this = zret.split_prefix(n);
  return zret;
}

inline TextView
TextView::take_suffix(int n) {
  return this->take_suffix(size_t(n));
}

inline TextView
TextView::take_suffix_at(char c) {
  return this->take_suffix(this->rfind(c));
}

inline TextView
TextView::take_suffix_at(std::string_view const &delimiters) {
  return this->take_suffix(this->find_last_of(delimiters));
}

template <typename F>
TextView::self_type
TextView::take_suffix_if(F const &pred) {
  return this->take_suffix_at(this->rfind_if(pred));
}

template <typename F>
inline size_t
TextView::find_if(F const &pred) const {
  for (const char *spot = this->data(), *limit = this->data_end(); spot < limit; ++spot)
    if (pred(*spot))
      return spot - this->data();
  return npos;
}

template <typename F>
inline size_t
TextView::rfind_if(F const &pred) const {
  for (const char *spot = this->data_end(), *limit = this->data(); spot > limit;)
    if (pred(*--spot))
      return spot - this->data();
  return npos;
}

inline TextView &
TextView::ltrim(char c) {
  this->remove_prefix(this->find_first_not_of(c));
  return *this;
}

inline TextView &
TextView::rtrim(char c) {
  auto n = this->find_last_not_of(c);
  this->remove_suffix(this->size() - (n == npos ? 0 : n + 1));
  return *this;
}

inline TextView &
TextView::trim(char c) {
  return this->ltrim(c).rtrim(c);
}

inline TextView &
TextView::ltrim(CharSet const &delimiters) {
  const char *spot  = this->data();
  const char *limit = this->data_end();

  while (spot < limit && delimiters(*spot)) {
    ++spot;
  }
  this->remove_prefix(spot - this->data());

  return *this;
}

inline TextView &
TextView::ltrim(std::string_view const &delimiters) {
  return this->ltrim(CharSet(delimiters));
}

inline TextView &
TextView::ltrim(const char *delimiters) {
  return this->ltrim(CharSet(delimiters));
}

inline TextView &
TextView::rtrim(CharSet const &delimiters) {
  const char *spot  = this->data_end();
  const char *limit = this->data();
  while (limit < spot-- && delimiters(*spot)) {
  }

  this->remove_suffix(this->data_end() - (spot + 1));
  return *this;
}

inline TextView &
TextView::rtrim(std::string_view const &delimiters) {
  return this->rtrim(CharSet(delimiters));
}

inline TextView &
TextView::trim(CharSet const &delimiters) {
  const char *spot;
  const char *limit;

  // Do this explicitly, so we don't have to initialize the character set twice.
  for (spot = this->data(), limit = this->data_end(); spot < limit && delimiters(*spot); ++spot)
    ;
  this->remove_prefix(spot - this->data());

  spot  = this->data_end();
  limit = this->data();
  while (limit < spot-- && delimiters(*spot)) {
  }
  this->remove_suffix(this->data_end() - (spot + 1));

  return *this;
}

inline TextView &
TextView::trim(std::string_view const &delimiters) {
  return this->trim(CharSet(delimiters));
}

inline TextView &
TextView::trim(const char *delimiters) {
  return this->trim(CharSet(delimiters));
}

template <typename F>
TextView::self_type &
TextView::ltrim_if(F const &pred) {
  const char *spot;
  const char *limit;
  for (spot = this->data(), limit = this->data_end(); spot < limit && pred(*spot); ++spot)
    ;
  this->remove_prefix(spot - this->data());
  return *this;
}

template <typename F>
TextView::self_type &
TextView::rtrim_if(F const &pred) {
  const char *spot  = this->data_end();
  const char *limit = this->data();
  while (limit < spot-- && pred(*spot))
    ;
  this->remove_suffix(this->data_end() - (spot + 1));
  return *this;
}

template <typename F>
TextView::self_type &
TextView::trim_if(F const &pred) {
  return this->ltrim_if(pred).rtrim_if(pred);
}

constexpr inline auto
TextView::data() const noexcept -> value_type const * {
  return super_type::data();
}

constexpr inline auto
TextView::data_end() const noexcept -> value_type const * {
  return this->data() + this->size();
}

inline constexpr TextView
TextView::substr(size_type pos, size_type count) const noexcept {
  if (pos >= this->size()) {
    return {};
  }
  count = std::min(this->size() - pos, count);
  return {this->data() + pos, count};
}

inline bool
TextView::starts_with(std::string_view const &prefix) const noexcept {
  return this->size() >= prefix.size() && 0 == ::memcmp(this->data(), prefix.data(), prefix.size());
}

inline bool
TextView::starts_with(char const *prefix) const {
  return this->starts_with(super_type(prefix));
}
inline bool
TextView::starts_with_nocase(char const *prefix) const {
  return this->starts_with_nocase(super_type{prefix});
}

inline bool
TextView::starts_with(char c) const noexcept {
  return !this->empty() && c == this->front();
}
inline bool
TextView::starts_with_nocase(char c) const noexcept {
  return !this->empty() && tolower(c) == tolower(this->front());
}

inline bool
TextView::starts_with_nocase(std::string_view const &prefix) const noexcept {
  return this->size() >= prefix.size() && 0 == ::strncasecmp(this->data(), prefix.data(), prefix.size());
}

inline bool
TextView::ends_with(std::string_view const &suffix) const noexcept {
  return this->size() >= suffix.size() && 0 == ::memcmp(this->data_end() - suffix.size(), suffix.data(), suffix.size());
}

inline bool
TextView::ends_with_nocase(std::string_view const &suffix) const noexcept {
  return this->size() >= suffix.size() && 0 == ::strncasecmp(this->data_end() - suffix.size(), suffix.data(), suffix.size());
}

inline bool
TextView::ends_with(char const *suffix) const {
  return this->ends_with(super_type(suffix));
}

inline bool
TextView::ends_with_nocase(char const *suffix) const {
  return this->ends_with_nocase(super_type(suffix));
}

inline bool
TextView::ends_with(char c) const noexcept {
  return !this->empty() && c == this->back();
}
inline bool
TextView::ends_with_nocase(char c) const noexcept {
  return !this->empty() && tolower(c) == tolower(this->back());
}

template <typename Stream>
Stream &
TextView::stream_write(Stream &os, const TextView &b) const {
  // Local function, avoids extra template work.
  static const auto stream_fill = [](Stream &ostream, size_t n) -> Stream & {
    static constexpr size_t pad_size = 8;
    typename Stream::char_type padding[pad_size];

    std::fill_n(padding, pad_size, ostream.fill());
    for (; n >= pad_size && ostream.good(); n -= pad_size)
      ostream.write(padding, pad_size);
    if (n > 0 && ostream.good())
      ostream.write(padding, n);
    return ostream;
  };

  const std::size_t w = os.width();
  if (w <= b.size()) {
    os.write(b.data(), b.size());
  } else {
    const std::size_t pad_size = w - b.size();
    const bool align_left      = (os.flags() & Stream::adjustfield) == Stream::left;
    if (!align_left && os.good())
      stream_fill(os, pad_size);
    if (os.good())
      os.write(b.data(), b.size());
    if (align_left && os.good())
      stream_fill(os, pad_size);
  }
  return os;
}

template <typename F>
TextView::self_type
TextView::clip_prefix_of(F const &pred) {
  size_t idx = 0;
  for (auto spot = this->data(), limit = spot + this->size(); spot < limit && pred(*spot); ++spot, ++idx)
    ; // empty
  TextView token = this->prefix(idx);
  this->remove_prefix(idx);
  return token;
}

template <typename F>
TextView::self_type
TextView::clip_suffix_of(F const &pred) {
  size_t idx = this->size() - 1;
  for (auto spot = this->data() + idx, limit = this->data(); spot >= limit && pred(*spot); --spot, --idx)
    ; // empty
  TextView token = this->suffix(idx);
  this->remove_suffix(idx);
  return token;
}
/// @endcond TextView_INTERNAL

// Provide an instantiation for @c std::ostream as it's likely this is the only one ever used.
extern template std::ostream &TextView::stream_write(std::ostream &, const TextView &) const;

/** A transform view.
 *
 * @tparam X Transform functor type.
 * @tparam V Source view type.
 *
 * A transform view acts like a view on the original source view @a V with each element transformed by
 * @a X.
 *
 * This is used most commonly with @c std::string_view. For example, if the goal is to handle a
 * piece of text as if it were lower case without changing the actual text, the following would
 * make that possible.
 * @code
 * std:::string_view source; // original text.
 * TransformView<int (*)(int) noexcept, std::string_view> xv(&tolower, source);
 * @endcode
 *
 * To avoid having to figure out the exact signature of the transform, the convenience function
 * @c transform_view_of is provide.
 * @code
 * std::string_view source; // original text.
 * auto xv = transform_view_of(&tolower, source);
 * @endcode
 *
 * This class supports iterators but those should be avoided as use of them makes extra copies of the instance which
 * may be expensive if the functor is expensive. In cases where the functor is a function pointer or a lambda this isn't
 * an issue.
 */
template <typename X, typename V> class TransformView {
  using self_type       = TransformView; ///< Self reference type.
  using source_iterator = decltype(V{}.begin());

public:
  using transform_type    = X; ///< Export transform functor type.
  using source_view_type  = V; ///< Export source view type.
  using source_value_type = std::remove_reference_t<decltype(*source_iterator{})>;
  /// Result type of calling the transform on an element of the source view.
  using value_type = std::invoke_result_t<transform_type, source_value_type>;
  /// This class serves as its own iterator.
  using iterator = self_type;

  /** Construct a transform view using transform @a xf on source view @a v.
   *
   * @param xf Transform instance.
   * @param v Source view.
   */
  TransformView(transform_type &&xf, source_view_type const &v);

  /** Construct a transform view using transform @a xf on source view @a v.
   *
   * @param xf Transform instance.
   * @param v Source view.
   */
  TransformView(transform_type const &xf, source_view_type const &v);

  /// Copy constructor.
  TransformView(self_type const &that) = default;
  /// Move constructor.
  TransformView(self_type &&that) = default;

  /// Copy assignment.
  self_type &operator=(self_type const &that) = default;
  /// Move assignment.
  self_type &operator=(self_type &&that) = default;

  /// Equality.
  bool operator==(self_type const &that) const;
  /// Inequality.
  bool operator!=(self_type const &that) const;

  /// Get the current element.
  value_type operator*() const;
  /// Move to next element.
  self_type &operator++();
  /// Move to next element.
  self_type operator++(int);

  /// Check if view is empty.
  bool empty() const;
  /// Check if bool is not empty.
  explicit operator bool() const;

  /// Iterator to first transformed character.
  iterator
  begin() const {
    return *this;
  }
  /// Iterator past last transformed character.
  iterator
  end() const {
    return self_type{_xf, _limit};
  }

protected:
  transform_type _xf;     ///< Per character transform functor.
  source_iterator _spot;  ///< Current location in the source view.
  source_iterator _limit; ///< End of source view.

  /// Special constructor for making an empty instance to serve as the @c end iterator.
  TransformView(transform_type &&xf, source_iterator &&limit) : _xf(xf), _spot(limit), _limit(limit) {}
};

template <typename X, typename V>
TransformView<X, V>::TransformView(transform_type &&xf, source_view_type const &v) : _xf(xf), _spot(v.begin()), _limit(v.end()) {}

template <typename X, typename V>
TransformView<X, V>::TransformView(transform_type const &xf, source_view_type const &v)
  : _xf(xf), _spot(v.begin()), _limit(v.end()) {}

template <typename X, typename V>
auto
TransformView<X, V>::operator*() const -> value_type {
  return _xf(*_spot);
}

template <typename X, typename V>
auto
TransformView<X, V>::operator++() -> self_type & {
  ++_spot;
  return *this;
}

template <typename X, typename V>
auto
TransformView<X, V>::operator++(int) -> self_type {
  self_type zret{*this};
  ++_spot;
  return zret;
}

template <typename X, typename V>
bool
TransformView<X, V>::empty() const {
  return _spot == _limit;
}

template <typename X, typename V> TransformView<X, V>::operator bool() const {
  return _spot != _limit;
}

template <typename X, typename V>
bool
TransformView<X, V>::operator==(self_type const &that) const {
  return _spot == that._spot && _limit == that._limit;
}

template <typename X, typename V>
bool
TransformView<X, V>::operator!=(self_type const &that) const {
  return _spot != that._spot || _limit != that._limit;
}

/** Create a transformed view of a source.
 *
 * @tparam X The transform functor type.
 * @tparam V The source type.
 * @param xf The transform.
 * @param src The view source.
 * @return A @c TransformView that applies @a xf to @a src.
 */
template <typename X, typename V>
TransformView<X, V>
transform_view_of(X const &xf, V const &src) {
  return TransformView<X, V>(xf, src);
}

/** Identity transform view.
 *
 * @tparam V The source type.
 *
 * This is a transform that returns the input unmodified. This is convenient when a transform is
 * required in general but not in all cases.
 */
template <typename V> class TransformView<void, V> {
  using self_type = TransformView; ///< Self reference type.
  /// Iterator over source, for internal use.
  using source_iterator = decltype(V{}.begin());

public:
  using source_view_type  = V; ///< Export source view type.
  using source_value_type = std::remove_reference_t<decltype(*source_iterator{})>;
  /// Result type of calling the transform on an element of the source view.
  using value_type = source_value_type;
  /// This class serves as its own iterator.
  using iterator = self_type;

  /** Construct identity transform view from @a v.
   *
   * @param v Source view.
   */
  TransformView(source_view_type const &v) : _spot(v.begin()), _limit(v.end()) {}

  /// Copy constructor.
  TransformView(self_type const &that) = default;
  /// Move constructor.
  TransformView(self_type &&that) = default;

  /// Copy assignment.
  self_type &operator=(self_type const &that) = default;
  /// Move assignment.
  self_type &operator=(self_type &&that) = default;

  /// Equality.
  bool operator==(self_type const &that) const;
  /// Inequality.
  bool operator!=(self_type const &that) const;

  /// Get the current element.
  value_type operator*() const;

  /// Move to next element.
  self_type &operator++();

  /// Move to next element.
  self_type operator++(int);

  /// Check if view is empty.
  bool empty() const;

  /// Check if bool is not empty.
  explicit operator bool() const;

  /// Iterator to first transformed character.
  iterator begin() const;
  /// Iterator past last transformed character.
  iterator end() const;

protected:
  source_iterator _spot;  ///< Current location.
  source_iterator _limit; ///< End marker.

  /// Special constructor for making an empty instance to serve as the @c end iterator.
  explicit TransformView(source_iterator &&limit) : _spot(limit), _limit(limit) {}
};

template <typename V>
auto
TransformView<void, V>::operator*() const -> value_type {
  return *_spot;
}

template <typename V>
auto
TransformView<void, V>::operator++() -> self_type & {
  ++_spot;
  return *this;
}

template <typename V>
auto
TransformView<void, V>::operator++(int) -> self_type {
  auto zret{*this};
  ++*this;
  return zret;
}

template <typename V>
bool
TransformView<void, V>::operator==(const self_type &that) const {
  return _spot == that._spot && _limit == that._limit;
}

template <typename V>
bool
TransformView<void, V>::operator!=(const self_type &that) const {
  return _spot != that._spot || _limit != that._limit;
}

template <typename V>
bool
TransformView<void, V>::empty() const {
  return _spot == _limit;
}

template <typename V> TransformView<void, V>::operator bool() const {
  return _spot != _limit;
}

template <typename V>
auto
TransformView<void, V>::begin() const -> self_type {
  return *this;
}

template <typename V>
auto
TransformView<void, V>::end() const -> self_type {
  return self_type{_limit};
}

/// @cond INTERNAL_DETAIL
// Capture @c void transforms and make them identity transforms.
template <typename V>
TransformView<void, V>
transform_view_of(V const &v) {
  return TransformView<void, V>(v);
}

/// @endcond

/** User literals for TextView.
 *
 * - _tv : TextView
 * - _sv : std::string_view
 */
namespace literals {
/** Literal constructor for @c std::string_view.
 *
 * @param s The source string.
 * @param n Size of the source string.
 * @return A @c string_view
 *
 * @internal This is provided because the STL one does not support @c constexpr which seems
 * rather bizarre to me, but there it is. Update: this depends on the version of the compiler,
 * so hopefully someday this can be removed.
 */
constexpr std::string_view
operator"" _sv(const char *s, size_t n) {
  return {s, n};
}

/** Literal constructor for @c swoc::TextView.
 *
 * @param s The source string.
 * @param n Size of the source string.
 * @return A @c string_view
 *
 * @internal This is provided because the STL one does not support @c constexpr which seems
 * rather bizarre to me, but there it is. Update: this depends on the version of the compiler,
 * so hopefully someday this can be removed.
 */
constexpr swoc::TextView
operator"" _tv(const char *s, size_t n) {
  return {s, n};
}
} // namespace literals

}} // namespace swoc::SWOC_VERSION_NS

namespace std {
/// Write the contents of @a view to the stream @a os.
ostream &operator<<(ostream &os, const swoc::TextView &view);

/// @cond INTERNAL_DETAIL
/* For interaction with specific STL interfaces, primarily std::filesystem. Along with the
 * dereference operator, this enables a @c TextView to act as a character iterator to a C string
 * even if the internal view is not nul terminated.
 * @note Putting these directly in the class doesn't seem to work.
 */
template <> struct iterator_traits<swoc::TextView> {
  using value_type        = char;
  using pointer_type      = const char *;
  using reference_type    = const char &;
  using difference_type   = ssize_t;
  using iterator_category = forward_iterator_tag;
};

template <typename X, typename V> struct iterator_traits<swoc::TransformView<X, V>> {
  using value_type        = typename swoc::TransformView<X, V>::value_type;
  using pointer_type      = const value_type *;
  using reference_type    = const value_type &;
  using difference_type   = ssize_t;
  using iterator_category = forward_iterator_tag;
};

template <> struct hash<swoc::TextView> {
  static constexpr hash<string_view> super_hash{};
  size_t
  operator()(swoc::TextView const &s) const {
    return super_hash(s);
  }
};
/// @endcond

} // namespace std

#if __GNUC__ == 11
#pragma GCC diagnostic pop
#endif
