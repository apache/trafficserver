#if !defined TS_MEM_VIEW
#define TS_MEM_VIEW

/** @file

   Class for handling "views" of a buffer. Views presume the memory for the buffer is managed
   elsewhere and allow efficient access to segments of the buffer without copies. Views are read
   only as the view doesn't own the memory. Along with generic buffer methods are specialized
   methods to support better string parsing, particularly token based parsing.

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

#include <bitset>
#include <functional>
#include <iosfwd>
#include <memory.h>
#include <algorithm>
#include <string>

/// Apache Traffic Server commons.
namespace ts
{
class MemView;
class StringView;

/// Compare the memory in two views.
/// Return based on the first different byte. If one argument is a prefix of the other, the prefix
/// is considered the "smaller" value.
/// @return
/// - -1 if @a lhs byte is less than @a rhs byte.
/// -  1 if @a lhs byte is greater than @a rhs byte.
/// -  0 if the views contain identical memory.
int memcmp(MemView const &lhs, MemView const &rhs);
/// Compare the strings in two views.
/// Return based on the first different character. If one argument is a prefix of the other, the prefix
/// is considered the "smaller" value.
/// @return
/// - -1 if @a lhs char is less than @a rhs char.
/// -  1 if @a lhs char is greater than @a rhs char.
/// -  0 if the views contain identical strings.
int strcmp(StringView const &lhs, StringView const &rhs);
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
int strcasecmp(StringView lhs, StringView rhs);

/** Convert the text in @c StringView @a src to a numeric value.

    If @a parsed is non-null then the part of the string actually parsed is placed there.
    @a base sets the conversion base. This defaults to 10 with two special cases:

    - If the number starts with a literal '0' then it is treated as base 8.
    - If the number starts with the literal characters '0x' or '0X' then it is treated as base 16.
*/
intmax_t svtoi(StringView src, StringView *parsed = nullptr, int base = 10);

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
  bool is_empty() const;

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

/** A read only view of contiguous piece of memory.

    A @c StringView does not own the memory to which it refers, it is simply a view of part of some
    (presumably) larger memory object. The purpose is to allow working in a read only way a specific
    part of the memory. A classic example for ATS is working with HTTP header fields and values
    which need to be accessed independently but preferably without copying. A @c StringView supports this style.

    MemView is based on an earlier class ConstBuffer and influenced by Boost.string_ref. Neither
    of these were adequate for how use of @c ConstBuffer evolved and so @c MemView is @c
    ConstBuffer with some additional stylistic changes based on Boost.string_ref.

    In particular @c MemView is designed both to support passing via API (to replace the need to
    pass two parameters for one real argument) and to aid in parsing input without copying.

 */
class StringView
{
  typedef StringView self; ///< Self reference type.

protected:
  const char *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Size of memory chunk.

public:
  /// Default constructor (empty buffer).
  constexpr StringView();

  /** Construct explicitly with a pointer and size.
   */
  constexpr StringView(const char *ptr, ///< Pointer to buffer.
                       size_t n         ///< Size of buffer.
                       );

  /** Construct from a half open range of two pointers.
      @note The byte at @start is in the view but the byte at @a end is not.
  */
  constexpr StringView(const char *start, ///< First byte in the view.
                       const char *end    ///< First byte not in the view.
                       );

  /** Construct from nullptr.
      This implicitly makes the length 0.
  */
  constexpr StringView(std::nullptr_t);

  /** Construct from null terminated string.
      @note The terminating null is not included. @c strlen is used to determine the length.
  */
  explicit StringView(const char *s);

  /// Construct from @c MemView to reference the same view.
  /// @internal Can't be @c constexpr because @c static_cast of @c <void*> is not permitted.
  StringView(MemView const &that);

  /// Construct from @c std::string, referencing the entire string contents.
  StringView(std::string const &str);

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

  /// @return The first byte in the view.
  char operator*() const;

  /// @return the byte at offset @a n.
  char operator[](size_t n) const;

  /// @return the byte at offset @a n.
  char operator[](int n) const;

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
  bool is_empty() const;

  /// @name Accessors.
  //@{
  /// Pointer to the first byte in the view.
  const char *begin() const;
  /// Pointer to first byte not in the view.
  const char *end() const;
  /// Number of bytes in the view.
  constexpr size_t size() const;
  /// Memory pointer.
  /// @note This is equivalent to @c begin currently but it's probably good to have separation.
  constexpr const char *ptr() const;
  //@}

  /// Set the view.
  /// This is faster but equivalent to constructing a new view with the same
  /// arguments and assigning it.
  /// @return @c this.
  self &setView(const char *ptr, ///< Buffer address.
                size_t n = 0     ///< Buffer size.
                );

  /// Set the view.
  /// This is faster but equivalent to constructing a new view with the same
  /// arguments and assigning it.
  /// @return @c this.
  self &setView(const char *start, ///< First valid character.
                const char *end    ///< First invalid character.
                );

  /// Clear the view (become an empty view).
  self &clear();

  /// @return @c true if the byte at @a *p is in the view.
  bool contains(const char *p) const;

  /** Find a byte.
      @return A pointer to the first occurrence of @a c in @a this
      or @c nullptr if @a c is not found.
  */
  const char *find(char c) const;

  /** Find a byte.
      @return A pointer to the first occurence of any of @a delimiters in @a
      this or @c nullptr if not found.
  */
  const char *find(self delimiters) const;

  /** Find a byte.
      @return A pointer to the first byte for which @a pred is @c true otherwise
      @c nullptr.
  */
  const char *find(std::function<bool(char)> const &pred) const;

  /** Remove bytes that match @a c from the start of the view.
  */
  self &ltrim(char c);
  /** Remove bytes from the start of the view that are in @a delimiters.
  */
  self &ltrim(self delimiters);
  /** Remove bytes from the start of the view for which @a pred is @c true.
  */
  self &ltrim(std::function<bool(char)> const &pred);

  /** Remove bytes that match @a c from the end of the view.
  */
  self &rtrim(char c);
  /** Remove bytes from the end of the view that are in @a delimiters.
  */
  self &rtrim(self delimiters);
  /** Remove bytes from the start and end of the view for which @a pred is @c true.
  */
  self &rtrim(std::function<bool(char)> const &pred);

  /** Remove bytes that match @a c from the end of the view.
  */
  self &trim(char c);
  /** Remove bytes from the start and end of the view that are in @a delimiters.
  */
  self &trim(self delimiters);
  /** Remove bytes from the start and end of the view for which @a pred is @c true.
  */
  self &trim(std::function<bool(char)> const &pred);

  /** Get the initial segment of the view before @a p.

      The byte at @a p is not included. If @a p is not in the view an empty view
      is returned.

      @return A buffer that contains all data before @a p.
  */
  self prefix(const char *p) const;

  /// Convenience overload for character.
  self prefix(char c);
  /// Convenience overload, split on delimiter set.
  self prefix(self delimiters) const;
  /// Convenience overload, split on predicate.
  self prefix(std::function<bool(char)> const &pred) const;

  /** Split the view on the character at @a p.

      The view is split in to two parts and the byte at @a p is discarded. @a this retains all data
      @b after @a p (equivalent to <tt>MemView(p+1, this->end()</tt>). A new view containing the
      initial bytes up to but not including @a p is returned, (equivalent to
      <tt>MemView(this->begin(), p)</tt>).

      This is convenient when tokenizing and @a p points at a delimiter.

      @note If @a *p refers toa byte that is not in @a this then @a this is not changed and an empty
      buffer is returned. Therefore this method can be safely called with the return value of
      calling @c find.

      @code
        void f(MemView& text) {
          MemView token = text.splitPrefix(text.find(delimiter));
          if (token) { // ... process token }
      @endcode

      @return A buffer containing data up to but not including @a p.

      @see extractPrefix
  */
  self splitPrefix(const char *p);

  /// Convenience overload, split on character.
  self splitPrefix(char c);
  /// Convenience overload, split on delimiter set.
  self splitPrefix(self delimiters);
  /// Convenience overload, split on predicate.
  self splitPrefix(std::function<bool(char)> const &pred);

  /** Extract a prefix delimited by @a p.

      A prefix of @a this is removed from the view and returned. If @a p is not in the view then the
      entire view is extracted and returned.

      If @a p points at a byte in the view this is identical to @c splitPrefix.  If not then the
      entire view in @a this will be returned and @a this will become an empty view. This is easier
      to use when repeated extracting tokens. The source view will become empty after extracting the
      last token.

      @code
      MemView text;
      while (text) {
        MemView token = text.extractPrefix(text.find(delimiter));
        // .. process token which will always be non-empty because text was not empty.
      }
      @endcode

      @return The prefix bounded at @a p or the entire view if @a p is not a byte in the view.

      @see splitPrefix
  */
  self extractPrefix(const char *p);

  /// Convenience overload, extract on delimiter set.
  self extractPrefix(char c);
  /// Convenience overload, extract on delimiter set.
  self extractPrefix(self delimiters);
  /// Convenience overload, extract on predicate.
  self extractPrefix(std::function<bool(char)> const &pred);

  /** Get the trailing segment of the view after @a p.

      The byte at @a p is not included. If @a p is not in the view an empty view is returned.

      @return A buffer that contains all data after @a p.
  */
  self suffix(const char *p) const;

  /// Convenience overload for character.
  self suffix(char c);
  /// Convenience overload for delimiter set.
  self suffix(self delimiters);
  /// Convenience overload for predicate.
  self suffix(std::function<bool(char)> const &pred);

  /** Split the view on the character at @a p.

      The view is split in to two parts and the byte at @a p is discarded. @a this retains all data
      @b before @a p (equivalent to <tt>MemView(this->begin(), p)</tt>). A new view containing
      the trailing bytes after @a p is returned, (equivalent to <tt>MemView(p+1,
      this->end())</tt>).

      @note If @a p does not refer to a byte in the view, an empty view is returned and @a this is
      unchanged.

      @return @a this.
  */
  self splitSuffix(const char *p);

  /// Convenience overload for character.
  self splitSuffix(char c);
  /// Convenience overload for delimiter set.
  self splitSuffix(self delimiters);
  /// Convenience overload for predicate.
  self splitSuffix(std::function<bool(char)> const &pred);

  // Functors for using this class in STL containers.
  /// Ordering functor, lexicographic comparison.
  struct LessThan {
    bool
    operator()(MemView const &lhs, MemView const &rhs)
    {
      return -1 == strcmp(lhs, rhs);
    }
  };
  /// Ordering functor, case ignoring lexicographic comparison.
  struct LessThanNoCase {
    bool
    operator()(MemView const &lhs, MemView const &rhs)
    {
      return -1 == strcasecmp(lhs, rhs);
    }
  };

  /// Specialized stream operator implementation.
  /// @note Use the standard stream operator unless there is a specific need for this, which is unlikely.
  /// @return The stream @a os.
  /// @internal Needed because @c std::ostream::write must be used and
  /// so alignment / fill have to be explicitly handled.
  template <typename Stream> Stream &stream_write(Stream &os, const StringView &b) const;

protected:
  /// Initialize a bit mask to mark which characters are in this view.
  void initDelimiterSet(std::bitset<256> &set);
};
// ----------------------------------------------------------
// Inline implementations.

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
MemView::is_empty() const
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

// === StringView Implementation ===
inline constexpr StringView::StringView()
{
}
inline constexpr StringView::StringView(const char *ptr, size_t n) : _ptr(ptr), _size(n)
{
}
inline constexpr StringView::StringView(const char *start, const char *end) : _ptr(start), _size(end - start)
{
}
inline constexpr StringView::StringView(std::nullptr_t) : _ptr(nullptr), _size(0)
{
}
inline StringView::StringView(MemView const &that) : _ptr(static_cast<const char *>(that.ptr())), _size(that.size())
{
}
inline StringView::StringView(std::string const &str) : _ptr(str.data()), _size(str.size())
{
}

inline void StringView::initDelimiterSet(std::bitset<256> &set)
{
  set.reset();
  for (char c : *this)
    set[static_cast<uint8_t>(c)] = true;
}

inline StringView &
StringView::setView(const char *ptr, size_t n)
{
  _ptr  = ptr;
  _size = n;
  return *this;
}

inline StringView &
StringView::setView(const char *ptr, const char *limit)
{
  _ptr  = ptr;
  _size = limit - ptr;
  return *this;
}

inline StringView &
StringView::clear()
{
  _ptr  = 0;
  _size = 0;
  return *this;
}

inline bool
StringView::operator==(self const &that) const
{
  return _size == that._size && _ptr == that._ptr;
}

inline bool
StringView::operator!=(self const &that) const
{
  return !(*this == that);
}

inline bool StringView::operator!() const
{
  return !(_ptr && _size);
}

inline StringView::operator bool() const
{
  return _ptr && _size;
}

inline bool
StringView::is_empty() const
{
  return !(_ptr && _size);
}

inline char StringView::operator*() const
{
  return *_ptr;
}

inline StringView &StringView::operator++()
{
  ++_ptr;
  --_size;
  return *this;
}

inline StringView &
StringView::operator+=(size_t n)
{
  if (n > _size) {
    _ptr  = nullptr;
    _size = 0;
  } else {
    _ptr += n;
    _size -= n;
  }
  return *this;
}

inline const char *
StringView::begin() const
{
  return _ptr;
}
inline constexpr const char *
StringView::ptr() const
{
  return _ptr;
}

inline const char *
StringView::end() const
{
  return _ptr + _size;
}

inline constexpr size_t
StringView::size() const
{
  return _size;
}

inline StringView &
StringView::operator=(StringView const &that)
{
  _ptr  = that._ptr;
  _size = that._size;
  return *this;
}

inline char StringView::operator[](size_t n) const
{
  return _ptr[n];
}

inline char StringView::operator[](int n) const
{
  return _ptr[n];
}

inline bool
StringView::contains(const char *p) const
{
  return _ptr <= p && p < _ptr + _size;
}

inline StringView
StringView::prefix(const char *p) const
{
  self zret;
  if (this->contains(p))
    zret.setView(_ptr, p);
  return zret;
}

inline StringView
StringView::prefix(char c)
{
  return this->prefix(this->find(c));
}

inline StringView
StringView::prefix(self delimiters) const
{
  return this->prefix(this->find(delimiters));
}

inline StringView
StringView::prefix(std::function<bool(char)> const &pred) const
{
  return this->prefix(this->find(pred));
}

inline StringView
StringView::splitPrefix(const char *p)
{
  self zret; // default to empty return.
  if (this->contains(p)) {
    zret.setView(_ptr, p);
    this->setView(p + 1, this->end());
  }
  return zret;
}

inline StringView
StringView::splitPrefix(char c)
{
  return this->splitPrefix(this->find(c));
}

inline StringView
StringView::splitPrefix(self delimiters)
{
  return this->splitPrefix(this->find(delimiters));
}

inline StringView
StringView::splitPrefix(std::function<bool(char)> const &pred)
{
  return this->splitPrefix(this->find(pred));
}

inline StringView
StringView::extractPrefix(const char *p)
{
  self zret{this->splitPrefix(p)};

  // For extraction if zret is empty, use up all of @a this
  if (!zret) {
    zret = *this;
    this->clear();
  }

  return zret;
}

inline StringView
StringView::extractPrefix(char c)
{
  return this->extractPrefix(this->find(c));
}

inline StringView
StringView::extractPrefix(self delimiters)
{
  return this->extractPrefix(this->find(delimiters));
}

inline StringView
StringView::extractPrefix(std::function<bool(char)> const &pred)
{
  return this->extractPrefix(this->find(pred));
}

inline StringView
StringView::suffix(const char *p) const
{
  self zret;
  if (this->contains(p))
    zret.setView(p + 1, _ptr + _size);
  return zret;
}

inline StringView
StringView::suffix(char c)
{
  return this->suffix(this->find(c));
}

inline StringView
StringView::suffix(self delimiters)
{
  return this->suffix(this->find(delimiters));
}

inline StringView
StringView::suffix(std::function<bool(char)> const &pred)
{
  return this->suffix(this->find(pred));
}

inline StringView
StringView::splitSuffix(const char *p)
{
  self zret;
  if (this->contains(p)) {
    zret.setView(p + 1, this->end());
    this->setView(_ptr, p);
  }
  return zret;
}

inline StringView
StringView::splitSuffix(char c)
{
  return this->splitSuffix(this->find(c));
}

inline StringView
StringView::splitSuffix(self delimiters)
{
  return this->splitSuffix(this->find(delimiters));
}

inline StringView
StringView::splitSuffix(std::function<bool(char)> const &pred)
{
  return this->splitSuffix(this->find(pred));
}

inline const char *
StringView::find(char c) const
{
  return static_cast<const char *>(memchr(_ptr, c, _size));
}

inline const char *
StringView::find(self delimiters) const
{
  std::bitset<256> valid;
  delimiters.initDelimiterSet(valid);

  for (const char *p = this->begin(), *limit = this->end(); p < limit; ++p)
    if (valid[static_cast<uint8_t>(*p)])
      return p;

  return nullptr;
}

inline const char *
StringView::find(std::function<bool(char)> const &pred) const
{
  const char *p = std::find_if(this->begin(), this->end(), pred);
  return p == this->end() ? nullptr : p;
}

inline StringView &
StringView::ltrim(char c)
{
  while (_size && *_ptr == c)
    ++*this;
  return *this;
}

inline StringView &
StringView::rtrim(char c)
{
  while (_size && _ptr[_size - 1] == c)
    --_size;
  return *this;
}
inline StringView &
StringView::trim(char c)
{
  this->ltrim(c);
  return this->rtrim(c);
}

inline StringView &
StringView::ltrim(self delimiters)
{
  std::bitset<256> valid;
  delimiters.initDelimiterSet(valid);

  while (_size && valid[static_cast<uint8_t>(*_ptr)])
    ++*this;

  return *this;
}

inline StringView &
StringView::rtrim(self delimiters)
{
  std::bitset<256> valid;
  delimiters.initDelimiterSet(valid);

  while (_size && valid[static_cast<uint8_t>(_ptr[_size - 1])])
    --_size;

  return *this;
}

inline StringView &
StringView::trim(self delimiters)
{
  std::bitset<256> valid;
  delimiters.initDelimiterSet(valid);
  // Do this explicitly, so we don't have to initialize the character set twice.
  while (_size && valid[static_cast<uint8_t>(_ptr[_size - 1])])
    --_size;
  while (_size && valid[static_cast<uint8_t>(_ptr[0])])
    ++*this;
  return *this;
}

inline StringView &
StringView::ltrim(std::function<bool(char)> const &pred)
{
  while (_size && pred(_ptr[0]))
    ++*this;
  return *this;
}

inline StringView &
StringView::rtrim(std::function<bool(char)> const &pred)
{
  while (_size && pred(_ptr[_size - 1]))
    --_size;
  return *this;
}

inline StringView &
StringView::trim(std::function<bool(char)> const &pred)
{
  this->ltrim(pred);
  return this->rtrim(pred);
}

inline int
strcmp(StringView const &lhs, StringView const &rhs)
{
  return ts::memcmp(lhs, rhs);
}

namespace detail
{
  /// Write padding to the stream, using the current stream fill character.
  template <typename Stream>
  void
  stream_fill(Stream &os, std::size_t n)
  {
    static constexpr size_t pad_size = 8;
    typename Stream::char_type padding[pad_size];

    std::fill_n(padding, pad_size, os.fill());
    for (; n >= pad_size && os.good(); n -= pad_size)
      os.write(padding, pad_size);
    if (n > 0 && os.good())
      os.write(padding, n);
  }

  extern template void stream_fill(std::ostream &, std::size_t);
} // detail

template <typename Stream>
Stream &
StringView::stream_write(Stream &os, const StringView &b) const
{
  const std::size_t w = os.width();
  if (w <= b.size()) {
    os.write(b.ptr(), b.size());
  } else {
    const std::size_t pad_size = w - b.size();
    const bool align_left      = (os.flags() & Stream::adjustfield) == Stream::left;
    if (!align_left && os.good())
      detail::stream_fill(os, pad_size);
    if (os.good())
      os.write(b.ptr(), b.size());
    if (align_left && os.good())
      detail::stream_fill(os, pad_size);
  }
  return os;
}

// Provide an instantiation for @c std::ostream as it's likely this is the only one ever used.
extern template std::ostream &StringView::stream_write(std::ostream &, const StringView &) const;

} // end namespace ApacheTrafficServer

namespace std
{
ostream &operator<<(ostream &os, const ts::MemView &b);
ostream &operator<<(ostream &os, const ts::StringView &b);
}

#endif // TS_BUFFER_HEADER
