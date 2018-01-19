/** @file

   Class for handling "views" of text. Views presume the memory for the buffer is managed
   elsewhere and allow efficient access to segments of the buffer without copies. Views are read
   only as the view doesn't own the memory. Along with generic buffer methods are specialized
   methods to support better string parsing, particularly token based parsing.

   This class is based on @c ts::string_view and is easily and cheaply converted to and from that class.


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
#include <bitset>
#include <functional>
#include <iosfwd>
#include <memory.h>
#include <algorithm>
#include <string>
#include <ts/string_view.h>

/// Apache Traffic Server commons.
namespace ts
{
class TextView;
/// Compare the memory in two views.
/// Return based on the first different byte. If one argument is a prefix of the other, the prefix
/// is considered the "smaller" value.
/// @return
/// - -1 if @a lhs byte is less than @a rhs byte.
/// -  1 if @a lhs byte is greater than @a rhs byte.
/// -  0 if the views contain identical memory.
int memcmp(TextView const &lhs, TextView const &rhs);
using ::memcmp; // Make this an overload, not an override.
/// Compare the strings in two views.
/// Return based on the first different character. If one argument is a prefix of the other, the prefix
/// is considered the "smaller" value.
/// @return
/// - -1 if @a lhs char is less than @a rhs char.
/// -  1 if @a lhs char is greater than @a rhs char.
/// -  0 if the views contain identical strings.
int strcmp(TextView const &lhs, TextView const &rhs);
using ::strcmp; // Make this an overload, not an override.
/// Compare the strings in two views.
/// Return based on the first different character. If one argument is a prefix of the other, the prefix
/// is considered the "smaller" value. The values are compared ignoring case.
/// @return
/// - -1 if @a lhs char is less than @a rhs char.
/// -  1 if @a lhs char is greater than @a rhs char.
/// -  0 if the views contain identical strings.
///
/// @internal Why not <const&>? Because the implementation would make copies anyway, might as well save
/// the cost of passing the pointers.
int strcasecmp(TextView lhs, TextView rhs);
using ::strcasecmp; // Make this an overload, not an override.

/** Convert the text in @c TextView @a src to a numeric value.

    If @a parsed is non-null then the part of the string actually parsed is placed there.
    @a base sets the conversion base. This defaults to 10 with two special cases:

    - If the number starts with a literal '0' then it is treated as base 8.
    - If the number starts with the literal characters '0x' or '0X' then it is treated as base 16.
*/
intmax_t svtoi(TextView src, TextView *parsed = nullptr, int base = 10);

/** A read only view of contiguous piece of memory.

    A @c TextView does not own the memory to which it refers, it is simply a view of part of some
    (presumably) larger memory object. The purpose is to allow working in a read only way a specific
    part of the memory. A classic example for ATS is working with HTTP header fields and values
    which need to be accessed independently but preferably without copying. A @c TextView supports this style.

    @c TextView is based on an earlier classes @c ConstBuffer, @c StringView and influenced by @c
    Boost.string_ref and @c std::string_view. None of these were adequate for how use of @c
    ConstBuffer evolved with regard to text based manipulations. @c TextView is a super set of @c
    std::string_view (and therefore our local implementation, @ts::string_view). It is designed to
    be a drop in replacement.

    @note To simplify the interface there is no constructor just a character pointer. Constructors require
    either a literal string or an explicit length. This avoid ambiguities which are much more annoying that
    explicitly calling @c strlen on a character pointer.
 */
class TextView : public string_view
{
  using self_type  = TextView;    ///< Self reference type.
  using super_type = string_view; ///< Parent type.

public:
  /// Default constructor (empty buffer).
  constexpr TextView();

  /** Construct explicitly with a pointer and size.
   */
  constexpr TextView(const char *ptr, ///< Pointer to buffer.
                     size_t n         ///< Size of buffer.
                     );

  /** Construct explicitly with a pointer and size.
      If @a n is negative it is treated as 0.
      @internal Overload for convience, otherwise get "narrow conversion" errors.
   */
  constexpr TextView(const char *ptr, ///< Pointer to buffer.
                     int n            ///< Size of buffer.
                     );

  /** Construct from a half open range of two pointers.
      @note The byte at @start is in the view but the byte at @a end is not.
  */
  constexpr TextView(const char *start, ///< First byte in the view.
                     const char *end    ///< First byte not in the view.
                     );

  /** Constructor from constant string.

      Construct directly from an array of characters. All elements of the array are
      included in the view unless the last element is nul, in which case it is elided.
      If this is inapropriate then a constructor with an explicit size should be used.

      @code
        TextView a("A literal string");
      @endcode
   */
  template <size_t N> constexpr TextView(const char (&s)[N]);

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr TextView(std::nullptr_t);

  /// Construct from a @c ts::string_view.
  constexpr TextView(super_type const &that);

  /// Construct from @c std::string, referencing the entire string contents.
  /// @internal Not all compilers make @c std::string methods called @c constexpr
  constexpr TextView(std::string const &str);

  /// Pointer to byte past the last byte in the view.
  const char *data_end() const;

  /// Assignment.
  self_type &operator                    =(super_type const &that);
  template <size_t N> self_type &operator=(const char (&s)[N]);

  /// Explicitly set the view.
  self_type &set_view(char const *ptr, size_t n);

  /// Explicitly set the view to the range [ @a b , @a e )
  self_type &set_view(char const *b, char const *e);

  /// @return The first byte in the view.
  char operator*() const;

  /** Shift the view to discard the first byte.
      @return @a this.
  */
  self_type &operator++();

  /** Shift the view to discard the leading @a n bytes.
      Equivalent to @c ts::string_view::remove_prefix
      @return @a this
  */
  self_type &operator+=(size_t n);

  /// Check for empty view.
  /// @return @c true if the view has a zero pointer @b or size.
  bool operator!() const;

  /// Check for non-empty view.
  /// @return @c true if the view refers to a non-empty range of bytes.
  explicit operator bool() const;

  /// Clear the view (become an empty view).
  self_type &clear();

  /// Get the offset of the first character for which @a pred is @c true.
  template <typename F> size_t find_if(F const &pred) const;
  /// Get the offset of the last character for which @a pred is @c true.
  template <typename F> size_t rfind_if(F const &pred) const;

  /** Remove bytes that match @a c from the start of the view.
  */
  self_type &ltrim(char c);

  /** Remove bytes from the start of the view that are in @a delimiters.
  */
  self_type &ltrim(super_type const &delimiters);

  /** Remove bytes from the start of the view that are in @a delimiters.
      @internal This is needed to avoid collisions with the templated predicate style.
      @return @c *this
  */
  self_type &ltrim(const char *delimiters);

  /** Remove bytes from the start of the view for which @a pred is @c true.
      @a pred must be a functor taking a @c char argument and returning @c bool.
      @return @c *this
  */
  template <typename F> self_type &ltrim_if(F const &pred);

  /** Remove bytes that match @a c from the end of the view.
  */
  self_type &rtrim(char c);

  /** Remove bytes from the end of the view that are in @a delimiters.
  */
  self_type &rtrim(super_type const &delimiters);

  /** Remove bytes from the end of the view that are in @a delimiters.
      @internal This is needed to avoid collisions with the templated predicate style.
      @return @c *this
   */
  self_type &rtrim(const char *delimiters);

  /** Remove bytes from the start and end of the view for which @a pred is @c true.
      @a pred must be a functor taking a @c char argument and returning @c bool.
      @return @c *this
  */
  template <typename F> self_type &rtrim_if(F const &pred);

  /** Remove bytes that match @a c from the end of the view.
  */
  self_type &trim(char c);

  /** Remove bytes from the start and end of the view that are in @a delimiters.
  */
  self_type &trim(super_type const &delimiters);

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

  /** Get the prefix of size @a n.

      If @a n is greater than the size the entire view is returned.

      @return A view of the prefix.
  */
  self_type prefix(size_t n) const;
  /// Convenience overload to avoid ambiguity for literal numbers.
  self_type prefix(int n) const;
  /** Get the prefix delimited by the first occurence of the character @a c.

      If @a c is not found the entire view is returned.
      The delimiter character is not included in the returned view.

      @return A view of the prefix.
  */
  self_type prefix(char c) const;
  /** Get the prefix delimited by the first occurence of a character in @a delimiters.

      If no such character is found the entire view is returned.
      The delimiter character is not included in the returned view.

      @return A view of the prefix.
  */
  self_type prefix(super_type const &delimiters) const;
  /** Get the prefix delimited by the first character for which @a pred is @c true.

      If no such character is found the entire view is returned
      The delimiter character is not included in the returned view.

      @return A view of the prefix.
  */
  template <typename F> self_type prefix_if(F const &pred) const;

  /** Split a prefix from the view on the character at offset @a n.

      The view is split in to two parts and the byte at offset @a n is discarded. @a this retains
      all data @b after offset @a n (equivalent to <tt>TextView::substr(n+1)</tt>). A new view
      containing the initial bytes up to but not including the byte at offset @a n is returned,
      (equivalent to <tt>TextView(0, n)</tt>).

      This is convenient when tokenizing.

      If @a n is larger than the size of the view no change is made and an empty buffer is
      returned. Therefore this method is most useful when checking for the presence of the delimiter
      is desirable, as the result of @c find methods can be passed directly to this method.

      @note This method and its overloads always remove the delimiter character.

      @code
        void f(TextView& text) {
          TextView token = text.get_prefix_at(text.find(delimiter));
          if (token) { // ... process token }
      @endcode

      @return The prefix bounded at offset @a n or an empty view if @a n is more than the view
      size.

      @see take_prefix_at
  */
  self_type split_prefix_at(size_t n);

  /// Convenience overload for literal numbers.
  self_type split_prefix_at(int n);
  /// Convenience overload, split on character.
  self_type split_prefix_at(char c);
  /// Convenience overload, split on delimiter set.
  self_type split_prefix_at(super_type const &delimiters);
  /// Convenience overload, split on predicate.
  template <typename F> self_type split_prefix_if(F const &pred);

  /** Split a prefix from the view on the character at offset @a n.

      The view is split in to two parts and the byte at offset @a n is discarded. @a this retains
      all data @b after offset @a n (equivalent to <tt>TextView::substr(n+1)</tt>). A new view
      containing the initial bytes up to but not including the byte at offset @a n is returned,
      (equivalent to <tt>TextView(0, n)</tt>).

      This is convenient when tokenizing.

      If @a n is larger than the view size then the entire view is removed and returned, leaving an
      empty view. Therefore if @this is not empty, a non-empty view is always returned. This is desirable
      if a non-empty return view is always wanted, regardless of whether a delimiter is present.

      @note This method and its overloads always remove the delimiter character.

      @code
      TextView text;
      while (text) {
        TextView token = text.take_prefix_at(text.find(delimiter));
        // token will always be non-empty because text was not empty.
      }
      @endcode

      @return The prefix bounded at offset @a n or the entire view if @a n is more than the view
      size.

      @see split_prefix_at
  */
  self_type take_prefix_at(size_t n);

  /// Convenience overload, extract on delimiter set.
  self_type take_prefix_at(char c);
  /// Convenience overload, extract on delimiter set.
  self_type take_prefix_at(super_type const &delimiters);
  /// Convenience overload, extract on predicate.
  template <typename F> self_type take_prefix_if(F const &pred);

  /** Get the last @a n characters of the view.

      @return A buffer that contains @a n characters at the end of the view.
  */
  self_type suffix(size_t n) const;

  /// Convenience overload to avoid ambiguity for literal numbers.
  self_type suffix(int n) const;
  /// Convenience overload for character.
  self_type suffix(char c) const;
  /// Convenience overload for delimiter set.
  self_type suffix(super_type const &delimiters) const;
  /// Convenience overload for delimiter set.
  self_type suffix(const char *delimiters) const;
  /// Get the prefix delimited by the first character for which @a pred is @c true.
  template <typename F> self_type suffix_if(F const &pred) const;

  /** Split the view to get a suffix of size @a n.

      The view is split in to two parts, a suffix of size @a n and a remainder which is the original
      view less @a n + 1 characters at the end. That is, the character between the suffix and the
      remainder is discarded. This is equivalent to <tt>TextView::suffix(this->size()-n)</tt> and
      <tt>TextView::remove_suffix(this->size() - (n+1))</tt>.

      If @a n is equal to or larger than the size of the view the entire view is removed as the
      suffix.

      @return The suffix of size @a n.

      @see split_suffix_at
  */
  self_type split_suffix(size_t n);
  /// Convenience overload for literal integers.
  self_type split_suffix(int n);

  /** Split the view on the character at offset @a n.

      The view is split in to two parts and the byte at offset @a n is discarded. @a this retains
      all data @b before offset @a n (equivalent to <tt>TextView::prefix(this->size()-n-1)</tt>). A
      new view containing the trailing bytes after offset @a n is returned, (equivalent to
      <tt>TextView::suffix(n))</tt>).

      If @a n is larger than the size of the view no change is made and an empty buffer is
      returned. Therefore this method is most useful when checking for the presence of the delimiter
      is desirable, as the result of @c find methods can be passed directly to this method.

      @note This method and its overloads always remove the delimiter character.

      @return The suffix bounded at offset @a n or an empty view if @a n is more than the view
      size.
  */
  self_type split_suffix_at(size_t n);

  /// Convenience overload for literal integers.
  self_type split_suffix_at(int n);
  /// Convenience overload for character.
  self_type split_suffix_at(char c);
  /// Convenience overload for delimiter set.
  self_type split_suffix_at(super_type const &delimiters);
  /// Split the view on the last character for which @a pred is @c true.
  template <typename F> self_type split_suffix_if(F const &pred);

  /** Split the view on the character at offset @a n.

      The view is split in to two parts and the byte at offset @a n is discarded. @a this retains
      all data @b before offset @a n (equivalent to <tt>TextView::prefix(this->size()-n-1)</tt>). A
      new view containing the trailing bytes after offset @a n is returned, (equivalent to
      <tt>TextView::suffix(n))</tt>).

      If @a n is larger than the view size then the entire view is removed and returned, leaving an
      empty view. Therefore if @this is not empty, a non-empty view is always returned. This is desirable
      if a non-empty return view is always wanted, regardless of whether a delimiter is present.

      @note This method and its overloads always remove the delimiter character.

      @return The suffix bounded at offset @a n or the entire view if @a n is more than the view
      size.
  */
  self_type take_suffix_at(size_t n);

  /// Convenience overload for literal integers.
  self_type take_suffix_at(int n);
  /// Convenience overload for character.
  self_type take_suffix_at(char c);
  /// Convenience overload for delimiter set.
  self_type take_suffix_at(super_type const &delimiters);
  /// Split the view on the last character for which @a pred is @c true.
  template <typename F> self_type take_suffix_if(F const &pred);

  /** Prefix check.
      @return @c true if @a this is a prefix of @a that.
  */
  bool isPrefixOf(super_type const &that) const;

  /** Case ignoring prefix check.
      @return @c true if @a this is a prefix of @a that, ignoring case.
  */
  bool isNoCasePrefixOf(super_type const &that) const;

  // Functors for using this class in STL containers.
  /// Ordering functor, lexicographic comparison.
  struct LessThan {
    bool
    operator()(self_type const &lhs, self_type const &rhs)
    {
      return -1 == strcmp(lhs, rhs);
    }
  };
  /// Ordering functor, case ignoring lexicographic comparison.
  struct LessThanNoCase {
    bool
    operator()(self_type const &lhs, self_type const &rhs)
    {
      return -1 == strcasecmp(lhs, rhs);
    }
  };

  /// Specialized stream operator implementation.
  /// @note Use the standard stream operator unless there is a specific need for this, which is unlikely.
  /// @return The stream @a os.
  /// @internal Needed because @c std::ostream::write must be used and
  /// so alignment / fill have to be explicitly handled.
  template <typename Stream> Stream &stream_write(Stream &os, const TextView &b) const;

protected:
  /// Faster find on a delimiter set, taking advantage of supporting only ASCII.
  size_t search(super_type const &delimiters) const;
  /// Faster reverse find on a delimiter set, taking advantage of supporting only ASCII.
  size_t rsearch(super_type const &delimiters) const;

  /// Initialize a bit mask to mark which characters are in this view.
  static void init_delimiter_set(super_type const &delimiters, std::bitset<256> &set);
};
// ----------------------------------------------------------
// Inline implementations.

// === TextView Implementation ===
inline constexpr TextView::TextView()
{
}
inline constexpr TextView::TextView(const char *ptr, size_t n) : super_type(ptr, n)
{
}
inline constexpr TextView::TextView(const char *ptr, int n) : super_type(ptr, n < 0 ? 0 : n)
{
}
inline constexpr TextView::TextView(const char *start, const char *end) : super_type(start, end - start)
{
}
inline constexpr TextView::TextView(std::nullptr_t) : super_type(nullptr, 0)
{
}
inline constexpr TextView::TextView(std::string const &str) : super_type(str)
{
}
inline constexpr TextView::TextView(super_type const &that) : super_type(that)
{
}
template <size_t N> constexpr TextView::TextView(const char (&s)[N]) : super_type(s, s[N - 1] ? N : N - 1)
{
}

inline void
TextView::init_delimiter_set(super_type const &delimiters, std::bitset<256> &set)
{
  set.reset();
  for (char c : delimiters)
    set[static_cast<uint8_t>(c)] = true;
}

inline const char *
TextView::data_end() const
{
  return this->data() + this->size();
}

inline TextView &
TextView::clear()
{
  new (this) self_type();
  return *this;
}

inline char TextView::operator*() const
{
  return *(this->data());
}

inline bool TextView::operator!() const
{
  return !this->empty();
}

inline TextView::operator bool() const
{
  return !this->empty();
}

inline TextView &TextView::operator++()
{
  this->remove_prefix(1);
  return *this;
}

inline TextView &
TextView::operator+=(size_t n)
{
  this->remove_prefix(n);
  return *this;
}

template <size_t N>
inline TextView &
TextView::operator=(const char (&s)[N])
{
  return *this = self_type{s, s[N - 1] ? N : N - 1};
}

inline TextView &
TextView::operator=(super_type const &that)
{
  this->super_type::operator=(that);
  return *this;
}

inline TextView &
TextView::set_view(char const *ptr, size_t n)
{
  *this = super_type(ptr, n);
  return *this;
}

inline TextView &
TextView::set_view(char const *b, char const *e)
{
  *this = super_type(b, e - b);
  return *this;
}

inline size_t
TextView::search(super_type const &delimiters) const
{
  std::bitset<256> valid;
  this->init_delimiter_set(delimiters, valid);

  for (const char *spot = this->data(), *limit = this->data_end(); spot < limit; ++spot)
    if (valid[static_cast<uint8_t>(*spot)])
      return spot - this->data();
  return npos;
}

inline size_t
TextView::rsearch(super_type const &delimiters) const
{
  std::bitset<256> valid;
  this->init_delimiter_set(delimiters, valid);

  for (const char *spot = this->data_end(), *limit = this->data(); spot > limit;)
    if (valid[static_cast<uint8_t>(*--spot)])
      return spot - this->data();
  return npos;
}

inline TextView
TextView::prefix(size_t n) const
{
  return self_type(this->data(), std::min(n, this->size()));
}

inline TextView
TextView::prefix(int n) const
{
  return this->prefix(static_cast<size_t>(n));
}

inline TextView
TextView::prefix(char c) const
{
  return this->prefix(this->find(c));
}

inline TextView
TextView::prefix(super_type const &delimiters) const
{
  return this->prefix(this->search(delimiters));
}

template <typename F>
inline TextView
TextView::prefix_if(F const &pred) const
{
  return this->prefix(this->find(pred));
}

inline TextView
TextView::split_prefix_at(size_t n)
{
  self_type zret; // default to empty return.
  if (n < this->size()) {
    zret = this->prefix(n);
    this->remove_prefix(n + 1);
  }
  return zret;
}

inline TextView
TextView::split_prefix_at(int n)
{
  return this->split_prefix_at(static_cast<size_t>(n));
}

inline TextView
TextView::split_prefix_at(char c)
{
  return this->split_prefix_at(this->find(c));
}

inline TextView
TextView::split_prefix_at(super_type const &delimiters)
{
  return this->split_prefix_at(this->search(delimiters));
}

template <typename F>
inline TextView
TextView::split_prefix_if(F const &pred)
{
  return this->split_prefix_at(this->find_if(pred));
}

inline TextView
TextView::take_prefix_at(size_t n)
{
  n              = std::min(n, this->size());
  self_type zret = this->prefix(n);
  this->remove_prefix(n + 1);
  return zret;
}

inline TextView
TextView::take_prefix_at(char c)
{
  return this->take_prefix_at(this->find(c));
}

inline TextView
TextView::take_prefix_at(super_type const &delimiters)
{
  return this->take_prefix_at(this->search(delimiters));
}

template <typename F>
inline TextView
TextView::take_prefix_if(F const &pred)
{
  return this->take_prefix_at(this->find_if(pred));
}

inline TextView
TextView::suffix(size_t n) const
{
  n = std::min(n, this->size());
  return self_type(this->data_end() - n, n);
}

inline TextView
TextView::suffix(int n) const
{
  return this->suffix(static_cast<size_t>(n));
}

inline TextView
TextView::suffix(char c) const
{
  return this->suffix((this->size() - std::min(this->size(), this->rfind(c))) - 1);
}

inline TextView
TextView::suffix(super_type const &delimiters) const
{
  return this->suffix((this->size() - std::min(this->size(), this->rsearch(delimiters))) - 1);
}

template <typename F>
inline TextView
TextView::suffix_if(F const &pred) const
{
  return this->suffix((this->size() - std::min(this->size(), this->rfind_if(pred))) - 1);
}

inline TextView
TextView::split_suffix(size_t n)
{
  self_type zret;
  n    = std::min(n, this->size());
  zret = this->suffix(n);
  this->remove_suffix(n + 1); // haha, saved by integer overflow!
  return zret;
}

inline TextView
TextView::split_suffix(int n)
{
  return this->split_suffix(static_cast<size_t>(n));
}

inline TextView
TextView::split_suffix_at(size_t n)
{
  self_type zret;
  if (n < this->size()) {
    n    = this->size() - n;
    zret = this->suffix(n - 1);
    this->remove_suffix(n);
  }
  return zret;
}

inline TextView
TextView::split_suffix_at(int n)
{
  return this->split_suffix_at(static_cast<size_t>(n));
}

inline TextView
TextView::split_suffix_at(char c)
{
  return this->split_suffix_at(this->rfind(c));
}

inline TextView
TextView::split_suffix_at(super_type const &delimiters)
{
  return this->split_suffix_at(this->rsearch(delimiters));
}

template <typename F>
inline TextView
TextView::split_suffix_if(F const &pred)
{
  return this->split_suffix_at(this->rfind_if(pred));
}

inline TextView
TextView::take_suffix_at(size_t n)
{
  self_type zret{*this};
  *this = zret.split_prefix_at(n);
  return zret;
}

inline TextView
TextView::take_suffix_at(int n)
{
  return this->take_suffix_at(static_cast<size_t>(n));
}

inline TextView
TextView::take_suffix_at(char c)
{
  return this->take_suffix_at(this->rfind(c));
}

inline TextView
TextView::take_suffix_at(super_type const &delimiters)
{
  return this->take_suffix_at(this->rsearch(delimiters));
}

template <typename F>
inline TextView
TextView::take_suffix_if(F const &pred)
{
  return this->take_suffix_at(this->rfind_if(pred));
}

template <typename F>
inline size_t
TextView::find_if(F const &pred) const
{
  for (const char *spot = this->data(), *limit = this->data_end(); spot < limit; ++spot)
    if (pred(*spot))
      return spot - this->data();
  return npos;
}

template <typename F>
inline size_t
TextView::rfind_if(F const &pred) const
{
  for (const char *spot = this->data_end(), *limit = this->data(); spot > limit;)
    if (pred(*--spot))
      return spot - this->data();
  return npos;
}

inline TextView &
TextView::ltrim(char c)
{
  this->remove_prefix(this->find_first_not_of(c));
  return *this;
}

inline TextView &
TextView::rtrim(char c)
{
  auto n = this->find_last_not_of(c);
  this->remove_suffix(this->size() - (n == npos ? 0 : n + 1));
  return *this;
}

inline TextView &
TextView::trim(char c)
{
  return this->ltrim(c).rtrim(c);
}

inline TextView &
TextView::ltrim(super_type const &delimiters)
{
  std::bitset<256> valid;
  this->init_delimiter_set(delimiters, valid);
  const char *spot;
  const char *limit;

  for (spot = this->data(), limit = this->data_end(); spot < limit && valid[static_cast<uint8_t>(*spot)]; ++spot)
    ;
  this->remove_prefix(spot - this->data());

  return *this;
}

inline TextView &
TextView::ltrim(const char *delimiters)
{
  return this->ltrim(ts::string_view(delimiters));
}

inline TextView &
TextView::rtrim(super_type const &delimiters)
{
  std::bitset<256> valid;
  this->init_delimiter_set(delimiters, valid);
  const char *spot  = this->data_end();
  const char *limit = this->data();

  while (limit < spot && valid[static_cast<uint8_t>(*--spot)])
    ;

  this->remove_suffix(this->data_end() - (spot + 1));
  return *this;
}

inline TextView &
TextView::trim(super_type const &delimiters)
{
  std::bitset<256> valid;
  this->init_delimiter_set(delimiters, valid);
  const char *spot;
  const char *limit;

  // Do this explicitly, so we don't have to initialize the character set twice.
  for (spot = this->data(), limit = this->data_end(); spot < limit && valid[static_cast<uint8_t>(*spot)]; ++spot)
    ;
  this->remove_prefix(spot - this->data());

  for (spot = this->data_end(), limit = this->data(); limit < spot && valid[static_cast<uint8_t>(*--spot)];)
    ;
  this->remove_suffix(this->data_end() - (spot + 1));

  return *this;
}

inline TextView &
TextView::trim(const char *delimiters)
{
  return this->trim(ts::string_view(delimiters));
}

template <typename F>
inline TextView &
TextView::ltrim_if(F const &pred)
{
  const char *spot;
  const char *limit;
  for (spot = this->data(), limit = this->data_end(); spot < limit && pred(*spot); ++spot)
    ;
  this->remove_prefix(spot - this->data());
  return *this;
}

template <typename F>
inline TextView &
TextView::rtrim_if(F const &pred)
{
  const char *spot;
  const char *limit;
  for (spot = this->data_end(), limit = this->data(); limit < spot && pred(*--spot);)
    ;
  this->remove_suffix(this->data_end() - (spot + 1));
  return *this;
}

template <typename F>
inline TextView &
TextView::trim_if(F const &pred)
{
  return this->ltrim_if(pred).rtrim_if(pred);
}

inline bool
TextView::isPrefixOf(super_type const &that) const
{
  return this->size() <= that.size() && 0 == memcmp(this->data(), that.data(), this->size());
}

inline bool
TextView::isNoCasePrefixOf(super_type const &that) const
{
  return this->size() <= that.size() && 0 == strncasecmp(this->data(), that.data(), this->size());
}

inline int
strcmp(TextView const &lhs, TextView const &rhs)
{
  return ts::memcmp(lhs, rhs);
}

template <typename Stream>
Stream &
TextView::stream_write(Stream &os, const TextView &b) const
{
  // Local function, avoids extra template work.
  static const auto stream_fill = [](Stream &os, size_t n) -> Stream & {
    static constexpr size_t pad_size = 8;
    typename Stream::char_type padding[pad_size];

    std::fill_n(padding, pad_size, os.fill());
    for (; n >= pad_size && os.good(); n -= pad_size)
      os.write(padding, pad_size);
    if (n > 0 && os.good())
      os.write(padding, n);
    return os;
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

// Provide an instantiation for @c std::ostream as it's likely this is the only one ever used.
extern template std::ostream &TextView::stream_write(std::ostream &, const TextView &) const;

} // end namespace ApacheTrafficServer

namespace std
{
ostream &operator<<(ostream &os, const ts::TextView &b);
ostream &operator<<(ostream &os, const ts::TextView &b);
}

#if 0
// Preserved for now, I may want this back later.
/** A read only view of contiguous piece of memory.

    A @c MemView does not own the memory to which it refers, it is simply a view of part of some
    (presumably) larger memory object. The purpose is to allow working in a read only way a specific
    part of the memory. This can avoid copying or allocation by allocating all needed memory at once
    and then working with it via instances of this class.

    MemView is based on an earlier class ConstBuffer and influenced by Boost.string_ref. Neither
    of these were adequate for how use of @c ConstBuffer evolved and so @c MemView is @c
    ConstBuffer with some additional stylistic changes based on Boost.string_ref.

    This class is closely integrated with @c StringView. These classes have the same underlying
    implementation and are differentiated only because of the return types and a few string oriented
    methods.
 */
class MemView
{
  typedef MemView self; ///< Self reference type.

protected:
  const void *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Size of memory chunk.

public:
  /// Default constructor (empty buffer).
  constexpr MemView();

  /** Construct explicitly with a pointer and size.
   */
  constexpr MemView(const void *ptr, ///< Pointer to buffer.
                    size_t n         ///< Size of buffer.
                    );

  /** Construct from a half open range of two pointers.
      @note The instance at @start is in the view but the instance at @a end is not.
  */
  template <typename T>
  constexpr MemView(T const *start, ///< First byte in the view.
                    T const *end    ///< First byte not in the view.
                    );

  /** Construct from a half open range of two pointers.
      @note The instance at @start is in the view but the instance at @a end is not.
  */
  MemView(void const *start, ///< First byte in the view.
          void const *end    ///< First byte not in the view.
          );

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr MemView(std::nullptr_t);

  /// Convert from StringView.
  constexpr MemView(StringView const &that);

  /** Equality.

      This is effectively a pointer comparison, buffer contents are not compared.

      @return @c true if @a that refers to the same view as @a this,
      @c false otherwise.
   */
  bool operator==(self const &that) const;

  /** Inequality.
      @return @c true if @a that does not refer to the same view as @a this,
      @c false otherwise.
   */
  bool operator!=(self const &that) const;

  /// Assignment - the view is copied, not the content.
  self &operator=(self const &that);

  /** Shift the view to discard the first byte.
      @return @a this.
  */
  self &operator++();

  /** Shift the view to discard the leading @a n bytes.
      @return @a this
  */
  self &operator+=(size_t n);

  /// Check for empty view.
  /// @return @c true if the view has a zero pointer @b or size.
  bool operator!() const;

  /// Check for non-empty view.
  /// @return @c true if the view refers to a non-empty range of bytes.
  explicit operator bool() const;

  /// Check for empty view (no content).
  /// @see operator bool
  bool isEmpty() const;

  /// @name Accessors.
  //@{
  /// Pointer to the first byte in the view.
  const void *begin() const;
  /// Pointer to first byte not in the view.
  const void *end() const;
  /// Number of bytes in the view.
  constexpr size_t size() const;
  /// Memory pointer.
  /// @note This is equivalent to @c begin currently but it's probably good to have separation.
  constexpr const void *ptr() const;
  /// @return the @a V value at index @a n.
  template <typename V> V at(ssize_t n) const;
  /// @return a pointer to the @a V value at index @a n.
  template <typename V> V const *at_ptr(ssize_t n) const;
  //@}

  /// Set the view.
  /// This is faster but equivalent to constructing a new view with the same
  /// arguments and assigning it.
  /// @return @c this.
  self &setView(const void *ptr, ///< Buffer address.
                size_t n = 0     ///< Buffer size.
                );

  /// Set the view.
  /// This is faster but equivalent to constructing a new view with the same
  /// arguments and assigning it.
  /// @return @c this.
  self &setView(const void *start, ///< First valid character.
                const void *end    ///< First invalid character.
                );

  /// Clear the view (become an empty view).
  self &clear();

  /// @return @c true if the byte at @a *p is in the view.
  bool contains(const void *p) const;

  /** Find a value.
      The memory is searched as if it were an array of the value type @a T.

      @return A pointer to the first occurrence of @a v in @a this
      or @c nullptr if @a v is not found.
  */
  template <typename V> const V *find(V v) const;

  /** Find a value.
      The memory is searched as if it were an array of the value type @a V.

      @return A pointer to the first value for which @a pred is @c true otherwise
      @c nullptr.
  */
  template <typename V> const V *find(std::function<bool(V)> const &pred);

  /** Get the initial segment of the view before @a p.

      The byte at @a p is not included. If @a p is not in the view an empty view
      is returned.

      @return A buffer that contains all data before @a p.
  */
  self prefix(const void *p) const;

  /** Split the view at @a p.

      The view is split in to two parts at @a p and the prefix is returned. The view is updated to
     contain the bytes not returned in the prefix. The prefix will not contain @a p.

      @note If @a *p refers to a byte that is not in @a this then @a this is not changed and an empty
      buffer is returned. Therefore this method can be safely called with the return value of
      calling @c find.

      @return A buffer containing data up to but not including @a p.

      @see extractPrefix
  */
  self splitPrefix(const void *p);

  /** Extract a prefix delimited by @a p.

      A prefix of @a this is removed from the view and returned. If @a p is not in the view then the
      entire view is extracted and returned.

      If @a p points at a byte in the view this is identical to @c splitPrefix.  If not then the
      entire view in @a this will be returned and @a this will become an empty view.

      @return The prefix bounded at @a p or the entire view if @a p is not a byte in the view.

      @see splitPrefix
  */
  self extractPrefix(const void *p);

  /** Get the trailing segment of the view after @a p.

      The byte at @a p is not included. If @a p is not in the view an empty view is returned.

      @return A buffer that contains all data after @a p.
  */
  self suffix(const void *p) const;

  /** Split the view at @a p.

      The view is split in to two parts and the suffix is returned. The view is updated to contain
      the bytes not returned in the suffix. The suffix will not contain @a p.

      @note If @a p does not refer to a byte in the view, an empty view is returned and @a this is
      unchanged.

      @return @a this.
  */
  self splitSuffix(const void *p);
};

inline constexpr MemView::MemView()
{
}
inline constexpr MemView::MemView(void const *ptr, size_t n) : _ptr(ptr), _size(n)
{
}
template <typename T> constexpr MemView::MemView(const T *start, const T *end) : _ptr(start), _size((end - start) * sizeof(T))
{
}
// <void*> is magic, handle that specially.
// No constexpr because the spec specifically forbids casting from <void*> to a typed pointer.
inline MemView::MemView(void const *start, void const *end)
  : _ptr(start), _size(static_cast<const char *>(end) - static_cast<char const *>(start))
{
}
inline constexpr MemView::MemView(std::nullptr_t) : _ptr(nullptr), _size(0)
{
}
inline constexpr MemView::MemView(StringView const &that) : _ptr(that.ptr()), _size(that.size())
{
}

inline MemView &
MemView::setView(const void *ptr, size_t n)
{
  _ptr  = ptr;
  _size = n;
  return *this;
}

inline MemView &
MemView::setView(const void *ptr, const void *limit)
{
  _ptr  = ptr;
  _size = static_cast<const char *>(limit) - static_cast<const char *>(ptr);
  return *this;
}

inline MemView &
MemView::clear()
{
  _ptr  = 0;
  _size = 0;
  return *this;
}

inline bool
MemView::operator==(self const &that) const
{
  return _size == that._size && _ptr == that._ptr;
}

inline bool
MemView::operator!=(self const &that) const
{
  return !(*this == that);
}

inline bool MemView::operator!() const
{
  return !(_ptr && _size);
}

inline MemView::operator bool() const
{
  return _ptr && _size;
}

inline bool
MemView::isEmpty() const
{
  return !(_ptr && _size);
}

inline MemView &MemView::operator++()
{
  _ptr = static_cast<const char *>(_ptr) + 1;
  --_size;
  return *this;
}

inline MemView &
MemView::operator+=(size_t n)
{
  if (n > _size) {
    _ptr  = nullptr;
    _size = 0;
  } else {
    _ptr = static_cast<const char *>(_ptr) + n;
    _size -= n;
  }
  return *this;
}

inline const void *
MemView::begin() const
{
  return _ptr;
}
inline constexpr const void *
MemView::ptr() const
{
  return _ptr;
}

inline const void *
MemView::end() const
{
  return static_cast<const char *>(_ptr) + _size;
}

inline constexpr size_t
MemView::size() const
{
  return _size;
}

inline MemView &
MemView::operator=(MemView const &that)
{
  _ptr  = that._ptr;
  _size = that._size;
  return *this;
}

inline bool
MemView::contains(const void *p) const
{
  return _ptr <= this->begin() && p < this->end();
}

inline MemView
MemView::prefix(const void *p) const
{
  self zret;
  if (this->contains(p))
    zret.setView(_ptr, p);
  return zret;
}

inline MemView
MemView::splitPrefix(const void *p)
{
  self zret; // default to empty return.
  if (this->contains(p)) {
    zret.setView(_ptr, p);
    this->setView(p, this->end());
  }
  return zret;
}

inline MemView
MemView::extractPrefix(const void *p)
{
  self zret{this->splitPrefix(p)};

  // For extraction if zret is empty, use up all of @a this
  if (!zret) {
    zret = *this;
    this->clear();
  }

  return zret;
}

inline MemView
MemView::suffix(const void *p) const
{
  self zret;
  if (this->contains(p))
    zret.setView(p, this->end());
  return zret;
}

inline MemView
MemView::splitSuffix(const void *p)
{
  self zret;
  if (this->contains(p)) {
    zret.setView(p, this->end());
    this->setView(_ptr, p);
  }
  return zret;
}

template <typename V>
inline V
MemView::at(ssize_t n) const
{
  return static_cast<V const *>(_ptr)[n];
}

template <typename V>
inline V const *
MemView::at_ptr(ssize_t n) const
{
  return static_cast<V const *>(_ptr) + n;
}

template <typename V>
inline const V *
MemView::find(V v) const
{
  for (const V *spot = static_cast<const V *>(_ptr), limit = spot + (_size / sizeof(V)); spot < limit; ++spot)
    if (v == *spot)
      return spot;
  return nullptr;
}

// Specialize char for performance.
template <>
inline const char *
MemView::find(char v) const
{
  return static_cast<const char *>(memchr(_ptr, v, _size));
}

template <typename V>
inline const V *
MemView::find(std::function<bool(V)> const &pred)
{
  for (const V *p = static_cast<const V *>(_ptr), *limit = p + (_size / sizeof(V)); p < limit; ++p)
    if (pred(*p))
      return p;
  return nullptr;
}

inline int
memcmp(MemView const &lhs, MemView const &rhs)
{
  int zret;
  size_t n;

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1, n = lhs.size();
  } else {
    n    = rhs.size();
    zret = rhs.size() < lhs.size() ? -1 : 0;
  }

  int r = ::memcmp(lhs.ptr(), rhs.ptr(), n);
  if (0 != r) { // If we got a not-equal, override the size based result.
    zret = r;
  }

  return zret;
}

namespace std
{
ostream &
operator<<(ostream &os, const ts::MemView &b)
{
  if (os.good()) {
    ostringstream out;
    out << b.size() << '@' << hex << b.ptr();
    os << out.str();
  }
  return os;
}
}
#endif
