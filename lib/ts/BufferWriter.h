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

#pragma once

#include <cstdlib>
#include <utility>
#include <cstring>
#include <vector>
#include <string>
#include <ts/ink_std_compat.h>

#include <ts/TextView.h>
#include <ts/MemSpan.h>
#include <ts/BufferWriterForward.h>

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

  /** BufferWriter print.

      This prints its arguments to the @c BufferWriter @a w according to the format @a fmt. The format
      string is based on Python style formating, each argument substitution marked by braces, {}. Each
      specification has three parts, a @a name, a @a specifier, and an @a extention. These are
      separated by colons. The name should be either omitted or a number, the index of the argument to
      use. If omitted the place in the format string is used as the argument index. E.g. "{} {} {}",
      "{} {1} {}", and "{0} {1} {2}" are equivalent. Using an explicit index does not reset the
      position of subsequent substiations, therefore "{} {0} {}" is equivalent to "{0} {0} {2}".
  */
  template <typename... Rest> BufferWriter &print(TextView fmt, Rest... rest);

  template <typename... Rest> BufferWriter &print(BWFormat const &fmt, Rest... rest);

  //    bwprint(*this, fmt, std::forward<Rest>(rest)...);
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
  FixedBufferWriter(char *buffer, size_t capacity);

  /** Construct empty buffer.
   * This is useful for doing sizing before allocating a buffer.
   */
  FixedBufferWriter(std::nullptr_t);

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
#if defined(__clang_analyzer__)
    assert(_capacity == 0 || _buf != nullptr); // make clang-analyzer happy.
#endif

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

// --------------- Implementation --------------------
/** Overridable formatting for type @a V.

    This is the output generator for data to a @c BufferWriter. Default stream operators call this with
    the default format specification (although those can be overloaded specifically for performance).
    User types should overload this function to format output for that type.

    @code
      BufferWriter &
      bwformat(BufferWriter &w, BWFSpec  &, V const &v)
      {
        // generate output on @a w
      }
    @endcode
  */

namespace bw_fmt
{
  template <typename TUPLE> using ArgFormatterSignature = BufferWriter &(*)(BufferWriter &w, BWFSpec const &, TUPLE const &args);

  /// Internal error / reporting message generators
  void Err_Bad_Arg_Index(BufferWriter &w, int i, size_t n);

  // MSVC will expand the parameter pack inside a lambda but not gcc, so this indirection is required.

  /// This selects the @a I th argument in the @a TUPLE arg pack and calls the formatter on it. This
  /// (or the equivalent lambda) is needed because the array of formatters must have a homogenous
  /// signature, not vary per argument. Effectively this indirection erases the type of the specific
  /// argument being formatter.
  template <typename TUPLE, size_t I>
  BufferWriter &
  Arg_Formatter(BufferWriter &w, BWFSpec const &spec, TUPLE const &args)
  {
    return bwformat(w, spec, std::get<I>(args));
  }

  /// This exists only to expand the index sequence into an array of formatters for the tuple type
  /// @a TUPLE.  Due to langauge limitations it cannot be done directly. The formatters can be
  /// access via standard array access in constrast to templated tuple access. The actual array is
  /// static and therefore at run time the only operation is loading the address of the array.
  template <typename TUPLE, size_t... N>
  ArgFormatterSignature<TUPLE> *
  Get_Arg_Formatter_Array(std::index_sequence<N...>)
  {
    static ArgFormatterSignature<TUPLE> fa[sizeof...(N)] = {&bw_fmt::Arg_Formatter<TUPLE, N>...};
    return fa;
  }

  /// Perform alignment adjustments / fill on @a w of the content in @a lw.
  void Do_Alignment(BWFSpec const &spec, BufferWriter &w, BufferWriter &lw);

  /// Global named argument table.
  using GlobalSignature = void (*)(BufferWriter &, BWFSpec const &);
  using GlobalTable     = std::map<string_view, GlobalSignature>;
  extern GlobalTable BWF_GLOBAL_TABLE;
  extern GlobalSignature Global_Table_Find(string_view name);

  /// Generic integral conversion.
  BufferWriter &Format_Integer(BufferWriter &w, BWFSpec const &spec, uintmax_t n, bool negative_p);

  /// Generic floating point conversion.
  BufferWriter &Format_Floating(BufferWriter &w, BWFSpec const &spec, double n, bool negative_p);

} // namespace bw_fmt

/** Compiled BufferWriter format
 */
class BWFormat
{
public:
  /// Construct from a format string @a fmt.
  BWFormat(TextView fmt);
  ~BWFormat();

  /** Parse elements of a format string.

      @param fmt The format string [in|out]
      @param literal A literal if found
      @param spec A specifier if found (less enclosing braces)
      @return @c true if a specifier was found, @c false if not.

      Pull off the next literal and/or specifier from @a fmt. The return value distinguishes
      the case of no specifier found (@c false) or an empty specifier (@c true).

   */
  static bool parse(TextView &fmt, string_view &literal, string_view &spec);

  /** Parsed items from the format string.

      Literals are handled by putting the literal text in the extension field and setting the
      global formatter @a _gf to @c LiteralFormatter, which writes out the extension as a literal.
   */
  struct Item {
    BWFSpec _spec; ///< Specification.
    /// If the spec has a global formatter name, cache it here.
    mutable bw_fmt::GlobalSignature _gf = nullptr;

    Item() {}
    Item(BWFSpec const &spec, bw_fmt::GlobalSignature gf) : _spec(spec), _gf(gf) {}
  };

  using Items = std::vector<Item>;
  Items _items; ///< Items from format string.

protected:
  /// Handles literals by writing the contents of the extension directly to @a w.
  static void Format_Literal(BufferWriter &w, BWFSpec const &spec);
};

template <typename... Rest>
BufferWriter &
BufferWriter::print(TextView fmt, Rest... rest)
{
  static constexpr int N = sizeof...(Rest);
  auto args(std::forward_as_tuple(rest...));
  auto fa     = bw_fmt::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Rest...>{});
  int arg_idx = 0;

  while (fmt.size()) {
    string_view lit_v;
    string_view spec_v;
    bool spec_p = BWFormat::parse(fmt, lit_v, spec_v);

    if (lit_v.size()) {
      this->write(lit_v);
    }
    if (spec_p) {
      BWFSpec spec{spec_v};
      size_t width = this->remaining();
      if (spec._max < width) {
        width = spec._max;
      }
      FixedBufferWriter lw{this->auxBuffer(), width};

      if (spec._name.size() == 0) {
        spec._idx = arg_idx;
      }
      if (0 <= spec._idx) {
        if (spec._idx < N) {
          fa[spec._idx](lw, spec, args);
        } else {
          bw_fmt::Err_Bad_Arg_Index(lw, spec._idx, N);
        }
      } else if (spec._name.size()) {
        auto gf = bw_fmt::Global_Table_Find(spec._name);
        if (gf) {
          gf(lw, spec);
        } else {
          static constexpr TextView msg{"{invalid name:"};
          lw.write(msg).write(spec._name).write('}');
        }
      }
      if (lw.extent()) {
        bw_fmt::Do_Alignment(spec, *this, lw);
      }
      ++arg_idx;
    }
  }
  return *this;
}

template <typename... Rest>
BufferWriter &
BufferWriter::print(BWFormat const &fmt, Rest... rest)
{
  static constexpr int N = sizeof...(Rest);
  auto const args(std::forward_as_tuple(rest...));
  static const auto fa = bw_fmt::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Rest...>{});

  for (BWFormat::Item const &item : fmt._items) {
    size_t width = this->remaining();
    if (item._spec._max < width) {
      width = item._spec._max;
    }
    FixedBufferWriter lw{this->auxBuffer(), width};
    if (item._gf) {
      item._gf(lw, item._spec);
    } else {
      auto idx = item._spec._idx;
      if (0 <= idx && idx < N) {
        fa[idx](lw, item._spec, args);
      } else if (item._spec._name.size() && (nullptr != (item._gf = bw_fmt::Global_Table_Find(item._spec._name)))) {
        item._gf(lw, item._spec);
      }
    }
    bw_fmt::Do_Alignment(item._spec, *this, lw);
  }
  return *this;
}

// Generically a stream operator is a formatter with the default specification.
template <typename V>
BufferWriter &
operator<<(BufferWriter &w, V &&v)
{
  return bwformat(w, BWFSpec::DEFAULT, std::forward<V>(v));
}

// Pointers
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const void *ptr)
{
  BWFSpec ptr_spec{spec};
  ptr_spec._radix_lead_p = true;
  if (ptr_spec._type == BWFSpec::DEFAULT_TYPE || ptr_spec._type == 'p') {
    ptr_spec._type = 'x'; // if default or 'p;, switch to lower hex.
  } else if (ptr_spec._type == 'P') {
    ptr_spec._type = 'X'; // P means upper hex, overriding other specializations.
  }
  return bw_fmt::Format_Integer(w, ptr_spec, reinterpret_cast<intptr_t>(ptr), false);
}

// MemSpan
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, MemSpan const &span);

// -- Common formatters --

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, string_view sv);

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &, char c)
{
  return w.write(c);
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, bool f)
{
  if ('s' == spec._type) {
    w.write(f ? "true"_sv : "false"_sv);
  } else if ('S' == spec._type) {
    w.write(f ? "TRUE"_sv : "FALSE"_sv);
  } else {
    bw_fmt::Format_Integer(w, spec, static_cast<uintmax_t>(f), false);
  }
  return w;
}

template <size_t N>
BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const char (&a)[N])
{
  return bwformat(w, spec, string_view(a, N - 1));
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const char *v)
{
  if (spec._type == 'x' || spec._type == 'X') {
    bwformat(w, spec, static_cast<const void *>(v));
  } else {
    bwformat(w, spec, string_view(v));
  }
  return w;
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, TextView const &tv)
{
  return bwformat(w, spec, static_cast<string_view>(tv));
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, double const &d)
{
  return d < 0 ? bw_fmt::Format_Floating(w, spec, -d, true) : bw_fmt::Format_Floating(w, spec, d, false);
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, float const &f)
{
  return f < 0 ? bw_fmt::Format_Floating(w, spec, -f, true) : bw_fmt::Format_Floating(w, spec, f, false);
}

/* Integer types.

   Due to some oddities for MacOS building, need a bit more template magic here. The underlying
   integer rendering is in @c Format_Integer which takes @c intmax_t or @c uintmax_t. For @c
   bwformat templates are defined, one for signed and one for unsigned. These forward their argument
   to the internal renderer. To avoid additional ambiguity the template argument is checked with @c
   std::enable_if to invalidate the overload if the argument type isn't a signed / unsigned
   integer. One exception to this is @c char which is handled by a previous overload in order to
   treat the value as a character and not an integer. The overall benefit is this works for any set
   of integer types, rather tuning and hoping to get just the right set of overloads.
 */

template <typename I>
auto
bwformat(BufferWriter &w, BWFSpec const &spec, I const &i) ->
  typename std::enable_if<std::is_unsigned<I>::value, BufferWriter &>::type
{
  return bw_fmt::Format_Integer(w, spec, i, false);
}

template <typename I>
auto
bwformat(BufferWriter &w, BWFSpec const &spec, I const &i) ->
  typename std::enable_if<std::is_signed<I>::value, BufferWriter &>::type
{
  return i < 0 ? bw_fmt::Format_Integer(w, spec, -i, true) : bw_fmt::Format_Integer(w, spec, i, false);
}

// std::string support
/** Print to a @c std::string

    Print to the string @a s. If there is overflow then resize the string sufficiently to hold the output
    and print again. The effect is the string is resized only as needed to hold the output.
 */
template <typename... Rest>
void
bwprint(std::string &s, ts::TextView fmt, Rest &&... rest)
{
  auto len = s.size();
  size_t n = ts::FixedBufferWriter(const_cast<char *>(s.data()), s.size()).print(fmt, std::forward<Rest>(rest)...).extent();
  s.resize(n);   // always need to resize - if shorter, must clip pre-existing text.
  if (n > len) { // dropped data, try again.
    ts::FixedBufferWriter(const_cast<char *>(s.data()), s.size()).print(fmt, std::forward<Rest>(rest)...);
  }
}

// -- FixedBufferWriter --
inline FixedBufferWriter::FixedBufferWriter(char *buffer, size_t capacity) : _buf(buffer), _capacity(capacity)
{
  ink_assert(_capacity == 0 || buffer != nullptr);
}

inline FixedBufferWriter::FixedBufferWriter(std::nullptr_t) : _buf(nullptr), _capacity(0) {}

} // end namespace ts
