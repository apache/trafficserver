#if !defined TS_BUFFERWRITER_H_
#define TS_BUFFERWRITER_H_

/** @file

    Utilities for generating character sequences.

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

#include <utility>
#include <cstring>

#include <ts/string_view.h>
#include <ts/ink_assert.h>

namespace ts
{
// Abstract class.
//
class BufferWriter
{
public:
  // The write() functions "add" characters at the end.  If these functions discard any characters, this must put the instance
  // in an error state (indicated by the override of error() ).  Derived classes must not assume the write() functions will
  // not be called when the instance is in an error state.

  virtual BufferWriter &write(char c) = 0;

  virtual BufferWriter &
  write(const void *data, size_t length)
  {
    const char *d = static_cast<const char *>(data);

    while (length--) {
      write(*(d++));
    }
    return *this;
  }

  BufferWriter &
  write(const string_view &sV)
  {
    return write(sV.data(), sV.size());
  }

  /// Return the written buffer.
  virtual const char *data() const = 0;

  // Returns true if the instance is in an error state.
  //
  virtual bool error() const = 0;

  // Returns pointer to an auxiliary buffer (or nullptr if none is available).  Succeeding calls to non-const member functions,
  // other than auxBuffer(), must be presumed to invalidate the current auxiliary buffer (contents and address).  Results
  // are UNDEFINED if character locations at or beyond auxBuffer()[remaining()] are written.
  //
  virtual char *
  auxBuffer()
  {
    return nullptr;
  }

  // Write the first n characters that have been placed in the auxiliary buffer.  This call invalidates the auxiliary buffer.
  // This function should not be called if no auxiliary buffer is available.
  //
  virtual BufferWriter &
  write(size_t n)
  {
    return *this;
  }

  // Returns number of total characters that can be written without causing an error condidtion.
  //
  virtual size_t capacity() const = 0;

  // Total number of characters that have been written, including those discarded due to an error condition.
  //
  virtual size_t extent() const = 0;

  // Total number of characters that are in the buffer (successfully written and not discarded).
  //
  size_t
  size() const
  {
    size_t e = extent(), c = capacity();

    return e < c ? e : c;
  }

  // Returns number of additional characters that can be written without causing an error condidtion.
  //
  size_t
  remaining() const
  {
    return capacity() - size();
  }

  // Reduce the capacity by n characters, potentially creating an error condition.
  //
  virtual BufferWriter &clip(size_t n) = 0;

  // If there is an error condition, this function clears it and sets the extent to the size.  It then increases the
  // capacity by n characters.
  //
  virtual BufferWriter &extend(size_t n) = 0;

  // Make destructor virtual.
  //
  virtual ~BufferWriter() {}
};

// A buffer writer that writes to an array of char that is external to the writer instance.
//
class FixedBufferWriter : public BufferWriter
{
protected:
  FixedBufferWriter(char *buf, size_t capacity, size_t attempted) : _buf(buf), _capacity(capacity), _attempted(attempted) {}
public:
  // 'buf' is a pointer to the external array of char to write to.  'capacity' is the number of bytes in the array.
  //
  // If you create a instance of this class with capacity == 0 (and a nullptr buffer), you can use it to measure the number of
  // characters a series of writes would result it (from the extent() value) without actually writing.
  //
  FixedBufferWriter(char *buf, size_t capacity) : FixedBufferWriter(buf, capacity, 0) {}
  FixedBufferWriter &
  write(char c) override
  {
    if (_attempted < _capacity) {
      _buf[_attempted] = c;
    }
    ++_attempted;

    return *this;
  }

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

  // It's not clear to my why g++ needs this using declaration in order to consider the inherited versions of 'write' when
  // resolving calls to a 'write' member ( wkaras@oath.com ).
  //
  using BufferWriter::write;

  /// Return the written buffer.
  const char *
  data() const override
  {
    return _buf;
  }

  bool
  error() const override
  {
    return _attempted > _capacity;
  }

  char *
  auxBuffer() override
  {
    return error() ? nullptr : _buf + _attempted;
  }

  FixedBufferWriter &
  write(size_t n) override
  {
    _attempted += n;

    return *this;
  }

  size_t
  capacity() const override
  {
    return _capacity;
  }

  size_t
  extent() const override
  {
    return _attempted;
  }

  FixedBufferWriter &
  clip(size_t n) override
  {
    ink_assert(n <= _capacity);

    _capacity -= n;

    return *this;
  }

  FixedBufferWriter &
  extend(size_t n) override
  {
    if (error()) {
      _attempted = _capacity;
    }

    _capacity += n;

    return *this;
  }

  // Reduce extent.  If extent is less than capacity, error condition is cleared.
  //
  void
  reduce(size_t smallerExtent)
  {
    ink_assert(smallerExtent <= _attempted);

    _attempted = smallerExtent;
  }

  // Provide a string_view of all successfully written characters.
  //
  string_view
  view() const
  {
    return string_view(_buf, size());
  }

  operator string_view() const { return view(); }
  // No copying
  //
  FixedBufferWriter(const FixedBufferWriter &) = delete;
  FixedBufferWriter &operator=(const FixedBufferWriter &) = delete;

  // Moving is OK.
  //
  FixedBufferWriter(FixedBufferWriter &&) = default;
  FixedBufferWriter &operator=(FixedBufferWriter &&) = default;

protected:
  char *const _buf;

  size_t _capacity;

  size_t _attempted; // Number of characters written, including those discarded due error condition.
};

// A buffer writer that writes to an array of char (of fixed dimension N) that is internal to the writer instance.
// It's called 'local' because instances are typically declared as stack-allocated, local function variables.
//
template <size_t N> class LocalBufferWriter : public FixedBufferWriter
{
public:
  LocalBufferWriter() : FixedBufferWriter(_arr, N) {}
  LocalBufferWriter(const LocalBufferWriter &that) : FixedBufferWriter(_arr, that._capacity, that._attempted)
  {
    std::memcpy(_arr, that._arr, size());
  }

  LocalBufferWriter &
  operator=(const LocalBufferWriter &that)
  {
    if (this != &that) {
      _capacity = that._capacity;

      _attempted = that._attempted;

      std::memcpy(_buf, that._buf, size());
    }

    return *this;
  }

  LocalBufferWriter &
  extend(size_t n) override
  {
    if (error()) {
      _attempted = _capacity;
    }

    _capacity += n;

    ink_assert(_capacity < N);

    return *this;
  }

  // Move construction/assignment intentionally defaulted to copying.

protected:
  char _arr[N];
};

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

} // end namespace ts

#endif // include once
