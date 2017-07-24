#if !defined TS_PRINTER_H_
#define TS_PRINTER_H_

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

#include <ts/MemView.h>
#include <ts/ink_assert.h>

namespace ts
{
// Abstract class.
//
class BasePrinterIface
{
protected:
  // The _pushBack() functions "add" characters at the end.  If these functions discard any characters, this must put the instance
  // in an error state (indicated by the override of error() ).  Derived classes must not assume the _pushBack() functions will
  // not be called when the instance is in an error state.

  virtual void _pushBack(char c) = 0;

  virtual void
  _pushBack(ts::StringView sV)
  {
    for (size_t i = 0; i < sV.size(); ++i) {
      _pushBack(sV[i]);
    }
  }

public:
  virtual bool error() const = 0;

  template <typename... Args>
  BasePrinterIface &
  operator()(const char &c, Args &&... args)
  {
    _pushBack(c);

    (*this)(std::forward<Args>(args)...);

    return *this;
  }

  template <typename... Args>
  BasePrinterIface &
  operator()(const ts::StringView &sv, Args &&... args)
  {
    _pushBack(sv);

    (*this)(std::forward<Args>(args)...);

    return *this;
  }

  // Use this member function as a less verbose way to convert a string literal to a StringView and add the contents to the
  // printed sequence.
  //
  template <size_t N, typename... Args>
  BasePrinterIface &
  l(const char (&s)[N], Args &&... args)
  {
    _pushBack(StringView(s, N - 1)); // Don't print terminal nul at the end of the string literal.

    (*this)(std::forward<Args>(args)...);

    return *this;
  }

  // Make destructor virtual.
  //
  ~BasePrinterIface() {}

private:
  void
  operator()()
  {
  }
};

// Abstract class.
//
class PrinterIface : public BasePrinterIface
{
public:
  // Returns pointer to an auxiliary buffer.  Succeeding calls to non-const member functions, other than auxBuf(), must be presumed
  // to invalidate the current auxiliary buffer (contents and address).
  //
  virtual char *auxBuf() = 0;

  // Size of auxiliary buffer (number of chars).
  //
  virtual size_t auxBufCapacity() const = 0;

  // Print the first n characters that have been placed in the auxiliary buffer.  (This call invalidates the auxiliary buffer.)
  //
  virtual void auxPrint(size_t n) = 0;
};

// A concrete printer that prints into a char array.
//
class Printer : public PrinterIface
{
public:
  // Construct an instance, and provide it with a buffer with the given capacity (number of chars) to store the results of printing.
  //
  Printer(char *buf, size_t capacity) : _buf(buf), _capacity(capacity), _size(0) {}

  // No copying. (No move constructor/assignment because none for base class.)
  Printer(const Printer &) = delete;
  Printer &operator=(const Printer &) = delete;

  StringView
  sV() const
  {
    return StringView(_buf, _size);
  }

  operator StringView() const { return sV(); }

  size_t
  capacity() const
  {
    return _capacity;
  }

  size_t
  size() const
  {
    return _size;
  }

  // Discard characters currently at the end of the buffer.
  //
  void
  resize(size_t smaller_size)
  {
    ink_assert(smaller_size <= _size);
    _size = smaller_size;
  }

  size_t
  remain() const
  {
    return error() ? 0 : _capacity - _size;
  }

  char *
  auxBuf() override
  {
    return _buf + _size;
  }

  size_t
  auxBufCapacity() const override
  {
    return remain();
  }

  void
  auxPrint(size_t n) override
  {
    ink_assert(n <= auxBufCapacity());
    _size += n;
  }

  bool
  error() const override
  {
    return _size > _capacity;
  }

protected:
  void
  _pushBack(char c) override
  {
    if (_size >= _capacity) {
      // Overflow error.
      //
      _size = _capacity + 1;

    } else {
      _buf[_size++] = c;
    }
  }

  void
  _pushBack(ts::StringView sV) override
  {
    if ((_size + sV.size()) > _capacity) {
      // Overflow error.
      //
      _size = _capacity + 1;

    } else {
      std::memcpy(_buf + _size, sV.begin(), sV.size());
      _size += sV.size();
    }
  }

  char *const _buf; // Pointer to buffer that is printed into.
  const size_t _capacity;
  size_t _size;
};

// An encapsulated array of N chars, with a Printer interface.
//
template <size_t N> class BuffPrinter : public Printer
{
public:
  BuffPrinter() : Printer(_arr, N) {}

  BuffPrinter(const BuffPrinter &that) : Printer(_arr, N) { *this = that; }

  BuffPrinter &
  operator=(const BuffPrinter &that)
  {
    if (this != &that) {
      _size = that._size;

      std::memcpy(_arr, that._arr, _size);
    }

    return (*this);
  }

  // Move construction/assignment intentionally defaulted to copying.

protected:
  char _arr[N];
};

} // end namespace ts

#endif // include once
