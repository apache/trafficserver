# if ! defined TS_BUFFER_HEADER
# define TS_BUFFER_HEADER

/** @file
    Definitions for a buffer type, to carry a reference to a chunk of memory.

    Copyright 2010 Network Geographics, Inc.

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
 */

# if defined _MSC_VER
# include <stddef.h>
# else
# include <unistd.h>
# endif

// For memcmp()
# include <memory.h>

/// Apache Traffic Server commons.
namespace ts {
  struct ConstBuffer;
  /** A chunk of writable memory.
      A convenience class because we pass this kind of pair frequently.
   */
  struct Buffer {
    typedef Buffer self; ///< Self reference type.

    char * _ptr; ///< Pointer to base of memory chunk.
    size_t _size; ///< Size of memory chunk.

    /// Default constructor.
    /// Elements are in uninitialized state.
    Buffer();

    /// Construct from pointer and size.
    Buffer(
      char* ptr, ///< Pointer to buffer.
      size_t n ///< Size of buffer.
    );

    /** Equality.
        @return @c true if the @c Buffer instances are identical,
        @c false otherwise.
     */
    bool operator == (self const& that) const;
    /** Inequality.
        @return @c true if the @c Buffer instances are different,
        @c false otherwise.
     */
    bool operator != (self const& that) const;
    /** Equality for a constant buffer.
        @return @c true if @a that contains identical contents.
        @c false otherwise.
     */
    bool operator == (ConstBuffer const& that) const;
    /** Inequality.
        @return @c true if the instances have different content,
        @c false if they are identical.
     */
    bool operator != (ConstBuffer const& that) const;

    /// Set the chunk.
    /// Any previous values are discarded.
    /// @return @c this object.
    self& set(
      char* ptr, ///< Buffer address.
      size_t n ///< Buffer size.
    );
    /// Reset to empty.
    self& reset();
  };

  /** A chunk of read only memory.
      A convenience class because we pass this kind of pair frequently.
   */
  struct ConstBuffer {
    typedef ConstBuffer self; ///< Self reference type.

    char const * _ptr; ///< Pointer to base of memory chunk.
    size_t _size; ///< Size of memory chunk.

    /// Default constructor.
    /// Elements are in uninitialized state.
    ConstBuffer();

    /// Construct from pointer and size.
    ConstBuffer(
      char const * ptr, ///< Pointer to buffer.
      size_t n ///< Size of buffer.
    );
    /// Construct from writable buffer.
    ConstBuffer(
        Buffer const& buffer ///< Buffer to copy.
    );

    /** Equality.
        @return @c true if the @c Buffer instances are identical,
        @c false otherwise.
     */
    bool operator == (self const& that) const;
    /** Equality.
        @return @c true if the @c Buffer instances are identical,
        @c false otherwise.
     */
    bool operator == (Buffer const& that) const;
    /** Inequality.
        @return @c true if the @c Buffer instances are different,
        @c false otherwise.
     */
    bool operator != (self const& that) const;
    /** Inequality.
        @return @c true if the @c Buffer instances are different,
        @c false otherwise.
     */
    bool operator != (Buffer const& that) const;
    /// Assign from non-const Buffer.
    self& operator = (
        Buffer const& that ///< Source buffer.
    );

    /// Set the chunk.
    /// Any previous values are discarded.
    /// @return @c this object.
    self& set(
      char const * ptr, ///< Buffer address.
      size_t n ///< Buffer size.
    );
    /// Reset to empty.
    self& reset();
  };

  // ----------------------------------------------------------
  // Inline implementations.

  inline Buffer::Buffer() { }
  inline Buffer::Buffer(char* ptr, size_t n) : _ptr(ptr), _size(n) { }
  inline Buffer& Buffer::set(char* ptr, size_t n) { _ptr = ptr; _size = n; return *this; }
  inline Buffer& Buffer::reset() { _ptr = 0; _size = 0 ; return *this; }
  inline bool Buffer::operator != (self const& that) const { return ! (*this == that); }
  inline bool Buffer::operator != (ConstBuffer const& that) const { return ! (*this == that); }
  inline bool Buffer::operator == (self const& that) const {
      return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
  }
  inline bool Buffer::operator == (ConstBuffer const& that) const {
      return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
  }

  inline ConstBuffer::ConstBuffer() { }
  inline ConstBuffer::ConstBuffer(char const* ptr, size_t n) : _ptr(ptr), _size(n) { }
  inline ConstBuffer::ConstBuffer(Buffer const& that) : _ptr(that._ptr), _size(that._size) { }
  inline ConstBuffer& ConstBuffer::set(char const* ptr, size_t n) { _ptr = ptr; _size = n; return *this; }
  inline ConstBuffer& ConstBuffer::reset() { _ptr = 0; _size = 0 ; return *this; }
  inline bool ConstBuffer::operator != (self const& that) const { return ! (*this == that); }
  inline bool ConstBuffer::operator != (Buffer const& that) const { return ! (*this == that); }
  inline bool ConstBuffer::operator == (self const& that) const {
      return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
  }
  inline ConstBuffer& ConstBuffer::operator = (Buffer const& that) { _ptr = that._ptr ; _size = that._size; return *this; }
  inline bool ConstBuffer::operator == (Buffer const& that) const {
      return _size == that._size && 0 == memcmp(_ptr, that._ptr, _size);
  }
}

# endif // TS_BUFFER_HEADER
