/** @file

    Defines ThrowSkipFixedBufferWriter class.

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

#include <cstring>
#include <cstdio>

#include <ts/BufferWriter.h>
#include <ts/ink_assert.h>

namespace ts
{
/*
The purpose of this utility is to help with transtioning to the use of MIOBufferWriters, rather than writing to MIOBuffers
directly. Eventually, there should be little or no use of it.  Functions of the form:

int/bool f(... char *buf, int bufSize, int *bufIdxInOut, *skipInOut) { ... }

should be changed to:

bool f(... ts::BufferWriter &bw) { ... }

int/bool f(... char *buf, int bufSize, int *bufIdxInOut, *skipInOut)
{
  ts::ThrowSkipFixedBufferWriter bw(buf, bufSize, bufIdxInOut, skipInOut);

  bool done = true;

  try {
    f(... bw);

  } catch (ts::ThrowSkipFixedBufferWriter::OverflowException) {
    done = false;
  }

  bw.legacyAdjust(bufIndxInOut, skipInOut);

  return done;
}

If f() was originally using mime_mem_print(), then TestThrowSkipFixedBufferWriter should be used instead of
ThrowSkipFixedBufferWriter, if the test mode when data is written to stdout must be preserved.

If the use of exceptions causes significant performance problems, it should be possible to swtich to using setjmp()/longjmp().
*/

// A buffer writer that writes to an array of char that is external to the writer instance.  A given number of input characters
// are skipped before before any are written to the array.  If the capacity of the array is exceeded an exception is thrown.
//
class ThrowSkipFixedBufferWriter : public BufferWriter
{
public:
  // 'buf' is a pointer to the external array of char to write to.  'bufferSize' is the number of bytes in the array.
  // 'skip' is the number of witten characters to skip before actually storing successive writes to the buffer.
  //
  // if 'buf' is null, all written characters are simply written to standard output (for test purposes).
  //
  ThrowSkipFixedBufferWriter(char *buf, size_t bufferSize, size_t skip = 0)
    : _buf(buf), _capacity(bufferSize + skip), _skip(skip), _attempted(0)
  {
  }

  // Convenience constructor for functions designed for functions used to write to MIOBuffer instances without
  // MIOBufferWriter.
  //
  ThrowSkipFixedBufferWriter(char *buf, int bufferSize, const int *bufIdxInOut, const int *bytesToSkipInOut)
    : _buf(buf + *bufIdxInOut), _capacity(*bytesToSkipInOut + bufferSize - *bufIdxInOut), _skip(*bytesToSkipInOut), _attempted(0)
  {
  }

  // Excpection thrown if capacity execeed.
  //
  struct OverflowException {
  };

  ThrowSkipFixedBufferWriter &
  write(char c) override
  {
    if (!_buf) {
      // Bizarre mime_mem_printf() test mode emulation.

      std::putchar(c);

      return *this;
    }

    if (_attempted >= _capacity) {
      throw OverflowException();
    }
    if (_attempted >= _skip) {
      *_bufFree() = c;
    }
    ++_attempted;

    return *this;
  }

  ThrowSkipFixedBufferWriter &
  write(const void *data_, size_t length) override
  {
    const char *data = static_cast<const char *>(data_);

    if (!_buf) {
      // Bizarre mime_mem_printf() test mode emulation.

      while (length--) {
        std::putchar(*(data++));
      }

      return *this;
    }

    size_t newAttempted = _attempted + length;
    bool overflow       = false;

    if (newAttempted > _capacity) {
      overflow     = true;
      length       = _capacity - _attempted;
      newAttempted = _capacity;
    }

    if (_attempted < _skip) {
      if (newAttempted <= _skip) {
        _attempted = newAttempted;

        return *this;
      }

      data += _skip - _attempted;
      length -= _skip - _attempted;
      _attempted = _skip;
    }

    std::memcpy(_bufFree(), data, length);

    _attempted = newAttempted;

    if (overflow) {
      throw OverflowException();
    }

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

  size_t
  actuallyWritten() const
  {
    return _attempted <= _skip ? 0 : (_attempted - _skip);
  }

  bool
  full() const
  {
    return _attempted == _capacity;
  }

  // Adjust input/output indexes for functions designed for writting to MIOBuffer instances directly using MIOBufferWriter.
  // After calling this member function on an instance, there should be no further calls to non-const member functions on the
  // instance.
  //
  void
  legacyAdjust(int *bufIdxInOut, int *bytesToSkipInOut) const
  {
    if (_attempted <= _skip) {
      *bytesToSkipInOut = _skip - _attempted;

    } else {
      *bytesToSkipInOut = 0;
      *bufIdxInOut += _attempted - _skip;
    }
  }

  // This always returns false, since overflow is signaled with an exception.
  bool
  error() const override
  {
    return false;
  }

  // Should not be used.
  const char *
  data() const override
  {
    ink_assert(false);

    return nullptr;
  }

  // Should not be used.
  char *
  auxBuffer() override
  {
    ink_assert(false);

    return nullptr;
  }

  // Should not be used.
  ThrowSkipFixedBufferWriter &
  write(size_t n) override
  {
    ink_assert(false);

    return *this;
  }

  // Should not be used.
  ThrowSkipFixedBufferWriter &
  clip(size_t n) override
  {
    ink_assert(false);

    return *this;
  }

  // Should not be used.
  ThrowSkipFixedBufferWriter &
  extend(size_t n) override
  {
    ink_assert(false);

    return *this;
  }

  // No copying
  //
  ThrowSkipFixedBufferWriter(const ThrowSkipFixedBufferWriter &) = delete;
  ThrowSkipFixedBufferWriter &operator=(const ThrowSkipFixedBufferWriter &) = delete;

  // Moving is OK.
  //
  ThrowSkipFixedBufferWriter(ThrowSkipFixedBufferWriter &&) = default;
  ThrowSkipFixedBufferWriter &operator=(ThrowSkipFixedBufferWriter &&) = default;

protected:
  char *const _buf;

  const size_t _capacity; // Includes skipped.

  const size_t _skip;

  size_t _attempted; // Number of characters written, including those discarded due error condition.

  // Only call this if _attempted >= _skip .
  char *
  _bufFree() const
  {
    return _buf + _attempted - _skip;
  }
};

class TestThrowSkipFixedBufferWriter : public ThrowSkipFixedBufferWriter
{
public:
  using Base = ThrowSkipFixedBufferWriter;

  // Constructor for functions designed for functions used to write to MIOBuffer instances without MIOBufferWriter.
  // Handle null buf pointer in a way that's compatible with mime_mem_print().
  //
  TestThrowSkipFixedBufferWriter(char *buf, int bufferSize, const int *bufIdxInOut, const int *bytesToSkipInOut)
    : Base(buf ? (buf + *bufIdxInOut) : nullptr, buf ? (bufferSize - *bufIdxInOut) : 0, buf ? *bytesToSkipInOut : 0)
  {
    if (!buf) {
      ink_assert(bufIdxInOut == nullptr);
      ink_assert(bytesToSkipInOut == nullptr);
    }
  }

  // Overload, handle null buf pointer like mime_mem_print().
  //
  void
  legacyAdjust(int *bufIdxInOut, int *bytesToSkipInOut)
  {
    if (_buf) {
      Base::legacyAdjust(bufIdxInOut, bytesToSkipInOut);
    }
  }
};

} // end namespace ts
