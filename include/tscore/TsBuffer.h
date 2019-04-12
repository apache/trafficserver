/** @file
    Definitions for a buffer type, to carry a reference to a chunk of memory.

    @internal This is a copy of TsBuffer.h in lib/tsconfig. That should
    eventually be replaced with this promoted file.

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

#if defined _MSC_VER
#include <stddef.h>
#else
#include <unistd.h>
#endif

// For memcmp()
#include <memory.h>

/// Apache Traffic Server commons.
namespace ts
{
struct ConstBuffer;
/** A chunk of writable memory.
    A convenience class because we pass this kind of pair frequently.

    @note The default construct leaves the object
    uninitialized. This is for performance reasons. To construct an
    empty @c Buffer use @c Buffer(0).
 */
struct Buffer {
  typedef Buffer self; ///< Self reference type.
  typedef bool (self::*pseudo_bool)() const;

  char *_ptr   = nullptr; ///< Pointer to base of memory chunk.
  size_t _size = 0;       ///< Size of memory chunk.

  /// Default constructor (empty buffer).
  Buffer();

  /** Construct from pointer and size.
      @note Due to ambiguity issues do not call this with
      two arguments if the first argument is 0.
   */
  Buffer(char *ptr, ///< Pointer to buffer.
         size_t n   ///< Size of buffer.
  );
  /** Construct from two pointers.
      @note This presumes a half open range, (start, end]
  */
  Buffer(char *start, ///< First valid character.
         char *end    ///< First invalid character.
  );

  /** Equality.
      @return @c true if @a that refers to the same memory as @a this,
      @c false otherwise.
   */
  bool operator==(self const &that) const;
  /** Inequality.
      @return @c true if @a that does not refer to the same memory as @a this,
      @c false otherwise.
   */
  bool operator!=(self const &that) const;
  /** Equality for a constant buffer.
      @return @c true if @a that refers to the same memory as @a this.
      @c false otherwise.
   */
  bool operator==(ConstBuffer const &that) const;
  /** Inequality.
      @return @c true if @a that does not refer to the same memory as @a this,
      @c false otherwise.
   */
  bool operator!=(ConstBuffer const &that) const;

  /// @return The first character in the buffer.
  char operator*() const;
  /** Discard the first character in the buffer.
      @return @a this object.
  */
  self &operator++();

  /// Check for empty buffer.
  /// @return @c true if the buffer has a zero pointer @b or size.
  bool operator!() const;
  /// Check for non-empty buffer.
  /// @return @c true if the buffer has a non-zero pointer @b and size.
  operator pseudo_bool() const;

  /// @name Accessors.
  //@{
  /// Get the data in the buffer.
  char *data() const;
  /// Get the size of the buffer.
  size_t size() const;
  //@}

  /// Set the chunk.
  /// Any previous values are discarded.
  /// @return @c this object.
  self &set(char *ptr,   ///< Buffer address.
            size_t n = 0 ///< Buffer size.
  );
  /// Reset to empty.
  self &reset();
};

/** A chunk of read only memory.
    A convenience class because we pass this kind of pair frequently.
 */
struct ConstBuffer {
  typedef ConstBuffer self; ///< Self reference type.
  typedef bool (self::*pseudo_bool)() const;

  char const *_ptr = nullptr; ///< Pointer to base of memory chunk.
  size_t _size     = 0;       ///< Size of memory chunk.

  /// Default constructor (empty buffer).
  ConstBuffer();

  /** Construct from pointer and size.
   */
  ConstBuffer(char const *ptr, ///< Pointer to buffer.
              size_t n         ///< Size of buffer.
  );
  /** Construct from two pointers.
      @note This presumes a half open range (start, end]
      @note Due to ambiguity issues do not invoke this with
      @a start == 0.
  */
  ConstBuffer(char const *start, ///< First valid character.
              char const *end    ///< First invalid character.
  );
  /// Construct from writable buffer.
  ConstBuffer(Buffer const &buffer ///< Buffer to copy.
  );

  /** Equality.
      @return @c true if @a that refers to the same memory as @a this,
      @c false otherwise.
   */
  bool operator==(self const &that) const;
  /** Equality.
      @return @c true if @a that refers to the same memory as @a this,
      @c false otherwise.
   */
  bool operator==(Buffer const &that) const;
  /** Inequality.
      @return @c true if @a that does not refer to the same memory as @a this,
      @c false otherwise.
   */
  bool operator!=(self const &that) const;
  /** Inequality.
      @return @c true if @a that does not refer to the same memory as @a this,
      @c false otherwise.
   */
  bool operator!=(Buffer const &that) const;
  /// Assign from non-const Buffer.
  self &operator=(Buffer const &that ///< Source buffer.
  );

  /// @return The first character in the buffer.
  char operator*() const;
  /** Discard the first character in the buffer.
      @return @a this object.
  */
  self &operator++();
  /** Discard the first @a n characters.
      @return @a this object.
  */
  self &operator+=(size_t n);

  /// Check for empty buffer.
  /// @return @c true if the buffer has a zero pointer @b or size.
  bool operator!() const;
  /// Check for non-empty buffer.
  /// @return @c true if the buffer has a non-zero pointer @b and size.
  operator pseudo_bool() const;

  /// @name Accessors.
  //@{
  /// Get the data in the buffer.
  char const *data() const;
  /// Get the size of the buffer.
  size_t size() const;
  /// Access a character (no bounds check).
  char operator[](int n) const;
  //@}
  /// @return @c true if @a p points at a character in @a this.
  bool contains(char const *p) const;

  /// Set the chunk.
  /// Any previous values are discarded.
  /// @return @c this object.
  self &set(char const *ptr, ///< Buffer address.
            size_t n = 0     ///< Buffer size.
  );
  /** Set from 2 pointers.
      @note This presumes a half open range (start, end]
  */
  self &set(char const *start, ///< First valid character.
            char const *end    ///< First invalid character.
  );
  /// Reset to empty.
  self &reset();

  /** Find a character.
      @return A pointer to the first occurrence of @a c in @a this
      or @c nullptr if @a c is not found.
  */
  char const *find(char c) const;

  /** Split the buffer on the character at @a p.

      The buffer is split in to two parts and the character at @a p
      is discarded. @a this retains all data @b after @a p. The
      initial part of the buffer is returned. Neither buffer will
      contain the character at @a p.

      This is convenient when tokenizing and @a p points at the token
      separator.

      @note If @a *p is not in the buffer then @a this is not changed
      and an empty buffer is returned. This means the caller can
      simply pass the result of @c find and check for an empty
      buffer returned to detect no more separators.

      @return A buffer containing data up to but not including @a p.
  */
  self splitOn(char const *p);

  /** Split the buffer on the character @a c.

      The buffer is split in to two parts and the occurrence of @a c
      is discarded. @a this retains all data @b after @a c. The
      initial part of the buffer is returned. Neither buffer will
      contain the first occurrence of @a c.

      This is convenient when tokenizing and @a c is the token
      separator.

      @note If @a c is not found then @a this is not changed and an
      empty buffer is returned.

      @return A buffer containing data up to but not including @a p.
  */
  self splitOn(char c);
  /** Get a trailing segment of the buffer.

      @return A buffer that contains all data after @a p.
  */
  self after(char const *p) const;
  /** Get a trailing segment of the buffer.

      @return A buffer that contains all data after the first
      occurrence of @a c.
  */
  self after(char c) const;
  /** Remove trailing segment.

      Data at @a p and beyond is removed from the buffer.
      If @a p is not in the buffer, no change is made.

      @return @a this.
  */
  self &clip(char const *p);
};

// ----------------------------------------------------------
// Inline implementations.

inline Buffer::Buffer() {}
inline Buffer::Buffer(char *ptr, size_t n) : _ptr(ptr), _size(n) {}
inline Buffer &
Buffer::set(char *ptr, size_t n)
{
  _ptr  = ptr;
  _size = n;
  return *this;
}
inline Buffer::Buffer(char *start, char *end) : _ptr(start), _size(end - start) {}
inline Buffer &
Buffer::reset()
{
  _ptr  = nullptr;
  _size = 0;
  return *this;
}
inline bool
Buffer::operator!=(self const &that) const
{
  return !(*this == that);
}
inline bool
Buffer::operator!=(ConstBuffer const &that) const
{
  return !(*this == that);
}
inline bool
Buffer::operator==(self const &that) const
{
  return _size == that._size && _ptr == that._ptr;
}
inline bool
Buffer::operator==(ConstBuffer const &that) const
{
  return _size == that._size && _ptr == that._ptr;
}
inline bool Buffer::operator!() const
{
  return !(_ptr && _size);
}
inline Buffer::operator pseudo_bool() const
{
  return _ptr && _size ? &self::operator! : nullptr;
}
inline char Buffer::operator*() const
{
  return *_ptr;
}
inline Buffer &
Buffer::operator++()
{
  ++_ptr;
  --_size;
  return *this;
}
inline char *
Buffer::data() const
{
  return _ptr;
}
inline size_t
Buffer::size() const
{
  return _size;
}

inline ConstBuffer::ConstBuffer() {}
inline ConstBuffer::ConstBuffer(char const *ptr, size_t n) : _ptr(ptr), _size(n) {}
inline ConstBuffer::ConstBuffer(char const *start, char const *end) : _ptr(start), _size(end - start) {}
inline ConstBuffer::ConstBuffer(Buffer const &that) : _ptr(that._ptr), _size(that._size) {}
inline ConstBuffer &
ConstBuffer::set(char const *ptr, size_t n)
{
  _ptr  = ptr;
  _size = n;
  return *this;
}

inline ConstBuffer &
ConstBuffer::set(char const *start, char const *end)
{
  _ptr  = start;
  _size = end - start;
  return *this;
}

inline ConstBuffer &
ConstBuffer::reset()
{
  _ptr  = nullptr;
  _size = 0;
  return *this;
}
inline bool
ConstBuffer::operator!=(self const &that) const
{
  return !(*this == that);
}
inline bool
ConstBuffer::operator!=(Buffer const &that) const
{
  return !(*this == that);
}
inline bool
ConstBuffer::operator==(self const &that) const
{
  return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
}
inline ConstBuffer &
ConstBuffer::operator=(Buffer const &that)
{
  _ptr  = that._ptr;
  _size = that._size;
  return *this;
}
inline bool
ConstBuffer::operator==(Buffer const &that) const
{
  return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
}
inline bool ConstBuffer::operator!() const
{
  return !(_ptr && _size);
}
inline ConstBuffer::operator pseudo_bool() const
{
  return _ptr && _size ? &self::operator! : nullptr;
}
inline char ConstBuffer::operator*() const
{
  return *_ptr;
}
inline ConstBuffer &
ConstBuffer::operator++()
{
  ++_ptr;
  --_size;
  return *this;
}
inline ConstBuffer &
ConstBuffer::operator+=(size_t n)
{
  _ptr += n;
  _size -= n;
  return *this;
}
inline char const *
ConstBuffer::data() const
{
  return _ptr;
}
inline char ConstBuffer::operator[](int n) const
{
  return _ptr[n];
}
inline size_t
ConstBuffer::size() const
{
  return _size;
}
inline bool
ConstBuffer::contains(char const *p) const
{
  return _ptr <= p && p < _ptr + _size;
}

inline ConstBuffer
ConstBuffer::splitOn(char const *p)
{
  self zret; // default to empty return.
  if (this->contains(p)) {
    size_t n = p - _ptr;
    zret.set(_ptr, n);
    _ptr = p + 1;
    _size -= n + 1;
  }
  return zret;
}

inline char const *
ConstBuffer::find(char c) const
{
  return static_cast<char const *>(memchr(_ptr, c, _size));
}

inline ConstBuffer
ConstBuffer::splitOn(char c)
{
  return this->splitOn(this->find(c));
}

inline ConstBuffer
ConstBuffer::after(char const *p) const
{
  return this->contains(p) ? self(p + 1, (_size - (p - _ptr)) - 1) : self();
}
inline ConstBuffer
ConstBuffer::after(char c) const
{
  return this->after(this->find(c));
}
inline ConstBuffer &
ConstBuffer::clip(char const *p)
{
  if (this->contains(p)) {
    _size = p - _ptr;
  }
  return *this;
}

} // namespace ts

typedef ts::Buffer TsBuffer;
typedef ts::ConstBuffer TsConstBuffer;
