/** @file

    Utilities for generating character sequences in buffers.

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

#if !defined TS_BUFFERWRITER_H_
#define TS_BUFFERWRITER_H_

#include <stdlib.h>
#include <utility>
#include <cstring>

#include <ts/string_view.h>
#include <ts/ink_assert.h>

namespace ts
{
/** Base (abstract) class for concrete buffer writers.
 */
class BufferWriter
{
public:
  /** Add the character @a c to the buffer.

      @a c is added only if there is room in the buffer. If not, the instance is put in to an error
      state. In either case the value for @c extent is incremented.

      @internal If any variant of @c write discards any characters, the instance must be put in an
      error state (indicated by the override of @c error).  Derived classes must not assume the
      write() functions will not be called when the instance is in an error state.

      @return @c *this
  */
  virtual BufferWriter &write(char c) = 0;

  /** Add @a data to the buffer, up to @a length bytes.

      Data is added only up to the remaining room in the buffer. If the remaining capacity is
      exceeded (i.e. data is not written to the output), the instance is put in to an error
      state. In either case the value for @c extent is incremented by @a length.

      @internal This uses the single character write to output the data. It is presumed concrete
      subclasses will override this method to use more efficient mechanisms, dependent on the type
      of output buffer.

      @return @c *this
  */
  virtual BufferWriter &
  write(const void *data, size_t length)
  {
    const char *d = static_cast<const char *>(data);

    while (length--) {
      write(*(d++));
    }
    return *this;
  }

  /** Add the contents of @a sv to the buffer, up to the size of the view.

      Data is added only up to the remaining room in the buffer. If the remaining capacity is
      exceeded (i.e. data is not written to the output), the instance is put in to an error
      state. In either case the value for @c extent is incremented by the size of @a sv.

      @return @c *this
  */
  BufferWriter &
  write(const string_view &sv)
  {
    return write(sv.data(), sv.size());
  }

  /// Get the address of the first byte in the output buffer.
  virtual const char *data() const = 0;

  /// Get the error state.
  /// @return @c true if in an error state, @c false if not.
  virtual bool error() const = 0;

  /** Get the address of the next output byte in the buffer.

      Succeeding calls to non-const member functions, other than this method, must be presumed to
      invalidate the current auxiliary buffer (contents and address).

      Care must be taken to not write to data beyond this plus @c remaining bytes. Usually the
      safest mechanism is to create a @c FixedBufferWriter on the auxillary buffer and write to that.

      @code
      ts::FixedBufferWriter subw(w.auxBuffer(), w.remaining());
      write_some_stuff(subw); // generate output into the buffer.
      w.fill(subw.extent()); // update main buffer writer.
      @endcode

      @return Address of the next output byte, or @c nullptr if there is no remaining capacity.
   */
  virtual char *
  auxBuffer()
  {
    return nullptr;
  }

  /** Advance the buffer position @a n bytes.

      This treats the next @a n bytes as being written without changing the content. This is useful
      only in conjuction with @a auxBuffer to indicate that @a n bytes of the auxillary buffer has
      been written by some other mechanism.

      @internal Concrete subclasses @b must override this to advance in a way consistent with the
      specific buffer type.

      @return @c *this
  */
  virtual BufferWriter &
  fill(size_t n)
  {
    return *this;
  }

  /// Get the total capacity.
  /// @return The total number of bytes that can be written without causing an error condition.
  virtual size_t capacity() const = 0;

  /// Get the extent.
  /// @return Total number of characters that have been written, including those discarded due to an error condition.
  virtual size_t extent() const = 0;

  /// Get the output size.
  /// @return Total number of characters that are in the buffer (successfully written and not discarded)
  size_t
  size() const
  {
    return std::min(this->extent(), this->capacity());
  }

  /// Get the remaining buffer space.
  /// @return Number of additional characters that can be written without causing an error condidtion.
  size_t
  remaining() const
  {
    return capacity() - size();
  }

  /// Reduce the capacity by @a n bytes
  /// If the capacity is reduced below the current @c size the instance goes in to an error state.
  /// @return @c *this
  virtual BufferWriter &clip(size_t n) = 0;

  /// Increase the capacity by @a n bytes.
  /// If there is an error condition, this function clears it and sets the extent to the size.  It
  /// then increases the capacity by n characters.
  virtual BufferWriter &extend(size_t n) = 0;

  // Force virtual destructor.
  virtual ~BufferWriter() {}
};

/** A @c BufferWrite concrete subclass to write to a fixed size buffer.
 */
class FixedBufferWriter : public BufferWriter
{
  using super_type = BufferWriter;

public:
  /** Construct a buffer writer on a fixed @a buffer of size @a capacity.

      If writing goes past the end of the buffer, the excess is dropped.

      @note If you create a instance of this class with capacity == 0 (and a nullptr buffer), you
      can use it to measure the number of characters a series of writes would result it (from the
      extent() value) without actually writing.
   */
  FixedBufferWriter(char *buffer, size_t capacity) : _buf(buffer), _capacity(capacity) {}

  FixedBufferWriter(const FixedBufferWriter &) = delete;
  FixedBufferWriter &operator=(const FixedBufferWriter &) = delete;
  /// Move constructor.
  FixedBufferWriter(FixedBufferWriter &&) = default;
  /// Move assignment.
  FixedBufferWriter &operator=(FixedBufferWriter &&) = default;

  /// Write a single character @a c to the buffer.
  FixedBufferWriter &
  write(char c) override
  {
    if (_attempted < _capacity) {
      _buf[_attempted] = c;
    }
    ++_attempted;

    return *this;
  }

  /// Write @a data to the buffer, up to @a length bytes.
  FixedBufferWriter &
  write(const void *data, size_t length) override
  {
    size_t newSize = _attempted + length;

    if (newSize <= _capacity) {
      std::memcpy(_buf + _attempted, data, length);

    } else if (_attempted < _capacity) {
      std::memcpy(_buf + _attempted, data, _capacity - _attempted);
    }
    _attempted = newSize;

    return *this;
  }

  // Bring in non-overridden methods.
  using super_type::write;

  /// Return the output buffer.
  const char *
  data() const override
  {
    return _buf;
  }

  /// Return whether there has been an error.
  bool
  error() const override
  {
    return _attempted > _capacity;
  }

  /// Get the start of the unused output buffer.
  char *
  auxBuffer() override
  {
    return error() ? nullptr : _buf + _attempted;
  }

  /// Advance the used part of the output buffer.
  FixedBufferWriter &
  fill(size_t n) override
  {
    _attempted += n;

    return *this;
  }

  /// Get the total capacity of the output buffer.
  size_t
  capacity() const override
  {
    return _capacity;
  }

  /// Get the total output sent to the writer.
  size_t
  extent() const override
  {
    return _attempted;
  }

  /// Reduce the capacity by @a n.
  FixedBufferWriter &
  clip(size_t n) override
  {
    ink_assert(n <= _capacity);

    _capacity -= n;

    return *this;
  }

  /// Extend the capacity by @a n.
  FixedBufferWriter &
  extend(size_t n) override
  {
    if (error()) {
      _attempted = _capacity;
    }

    _capacity += n;

    return *this;
  }

  /// Reduce extent to @a n.
  /// If @a n is less than the capacity the error condition, if any, is cleared.
  /// This can be used to clear the output by calling @c reduce(0)
  void
  reduce(size_t n)
  {
    ink_assert(n <= _attempted);

    _attempted = n;
  }

  /// Provide a string_view of all successfully written characters.
  string_view
  view() const
  {
    return string_view(_buf, size());
  }

  /// Provide a @c string_view of all successfully written characters as a user conversion.
  operator string_view() const { return view(); }

  /** Get a @c FixedBufferWriter for the unused output buffer.

      If @a reserve is non-zero then the buffer size for the auxillary writer will be @a reserve bytes
      smaller than the remaining buffer. This "reserves" space for additional output after writing
      to the auxillary buffer, in a manner similar to @c clip / @c extend.
   */
  FixedBufferWriter
  auxWriter(size_t reserve = 0)
  {
    return {this->auxBuffer(), reserve < this->remaining() ? this->remaining() - reserve : 0};
  }

protected:
  char *const _buf;      ///< Output buffer.
  size_t _capacity;      ///< Size of output buffer.
  size_t _attempted = 0; ///< Number of characters written, including those discarded due error condition.
private:
  // INTERNAL - Overload removed, make sure it's not used.
  BufferWriter &write(size_t n);
};

/** A buffer writer that writes to an array of char (of fixed size N) that is internal to the writer instance.

    It's called 'local' because instances are typically declared as stack-allocated, local function
    variables.
*/
template <size_t N> class LocalBufferWriter : public FixedBufferWriter
{
public:
  /// Construct an empty writer.
  LocalBufferWriter() : FixedBufferWriter(_arr, N) {}

  /// Copy another writer.
  /// Any data in @a that is copied over.
  LocalBufferWriter(const LocalBufferWriter &that) : FixedBufferWriter(_arr, N)
  {
    std::memcpy(_arr, that._arr, that.size());
    _attempted = that._attempted;
  }

  /// Copy another writer.
  /// Any data in @a that is copied over.
  template <size_t K> LocalBufferWriter(const LocalBufferWriter<K> &that) : FixedBufferWriter(_arr, N)
  {
    size_t n = std::min(N, that.size());
    std::memcpy(_arr, that.data(), n);
    // if a bigger space here, don't leave a gap between size and attempted.
    _attempted = N > K ? n : that.extent();
  }

  /// Copy another writer.
  /// Any data in @a that is copied over.
  LocalBufferWriter &
  operator=(const LocalBufferWriter &that)
  {
    if (this != &that) {
      _attempted = that.extent();
      std::memcpy(_buf, that._buf, that.size());
    }

    return *this;
  }

  /// Copy another writer.
  /// Any data in @a that is copied over.
  template <size_t K>
  LocalBufferWriter &
  operator=(const LocalBufferWriter<K> &that)
  {
    size_t n = std::min(N, that.size());
    // if a bigger space here, don't leave a gap between size and attempted.
    _attempted = N > K ? n : that.extent();
    std::memcpy(_arr, that.data(), n);
    return *this;
  }

  /// Increase capacity by @a n.
  LocalBufferWriter &
  extend(size_t n) override
  {
    if (error()) {
      _attempted = _capacity;
    }

    _capacity += n;

    ink_assert(_capacity <= N);

    return *this;
  }

protected:
  char _arr[N]; ///< output buffer.
};

// Define stream operators for built in @c write overloads.

inline BufferWriter &
operator<<(BufferWriter &b, char c)
{
  return b.write(c);
}

inline BufferWriter &
operator<<(BufferWriter &b, const string_view &sv)
{
  return b.write(sv);
}

inline BufferWriter &
operator<<(BufferWriter &w, intmax_t i)
{
  if (i) {
    char txt[std::numeric_limits<intmax_t>::digits10 + 1];
    int n = sizeof(txt);
    while (i) {
      txt[--n] = '0' + i % 10;
      i /= 10;
    }
    return w.write(txt + n, sizeof(txt) - n);
  } else {
    return w.write('0');
  }
}

// Annoying but otherwise ambiguous.
inline BufferWriter &
operator<<(BufferWriter &w, int i)
{
  return w << static_cast<intmax_t>(i);
}

inline BufferWriter &
operator<<(BufferWriter &w, uintmax_t i)
{
  if (i) {
    char txt[std::numeric_limits<uintmax_t>::digits10 + 1];
    int n = sizeof(txt);
    while (i) {
      txt[--n] = '0' + i % 10;
      i /= 10;
    }
    return w.write(txt + n, sizeof(txt) - n);
  } else {
    return w.write('0');
  }
}

// Annoying but otherwise ambiguous.
inline BufferWriter &
operator<<(BufferWriter &w, unsigned int i)
{
  return w << static_cast<uintmax_t>(i);
}

} // end namespace ts

#endif // include once
