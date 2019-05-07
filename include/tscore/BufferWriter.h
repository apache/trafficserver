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
#include <iosfwd>
#include <string_view>

#include "tscpp/util/TextView.h"
#include "tscpp/util/MemSpan.h"
#include "tscore/BufferWriterForward.h"

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
  write(const std::string_view &sv)
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
      safest mechanism is to create a @c FixedBufferWriter on the auxiliary buffer and write to that.

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
      only in conjunction with @a auxBuffer to indicate that @a n bytes of the auxiliary buffer has
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
  /// @return Number of additional characters that can be written without causing an error condition.
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
      string is based on Python style formatting, each argument substitution marked by braces, {}. Each
      specification has three parts, a @a name, a @a specifier, and an @a extension. These are
      separated by colons. The name should be either omitted or a number, the index of the argument to
      use. If omitted the place in the format string is used as the argument index. E.g. "{} {} {}",
      "{} {1} {}", and "{0} {1} {2}" are equivalent. Using an explicit index does not reset the
      position of subsequent substitutions, therefore "{} {0} {}" is equivalent to "{0} {0} {2}".
  */
  template <typename... Rest> BufferWriter &print(TextView fmt, Rest &&... rest);
  /** Print overload to take arguments as a tuple instead of explicitly.
      This is useful for forwarding variable arguments from other functions / methods.
  */
  template <typename... Args> BufferWriter &printv(TextView fmt, std::tuple<Args...> const &args);

  /// Print using a preparsed @a fmt.
  template <typename... Args> BufferWriter &print(BWFormat const &fmt, Args &&... args);
  /** Print overload to take arguments as a tuple instead of explicitly.
      This is useful for forwarding variable arguments from other functions / methods.
  */
  template <typename... Args> BufferWriter &printv(BWFormat const &fmt, std::tuple<Args...> const &args);

  /// Output the buffer contents to the @a stream.
  /// @return The destination stream.
  virtual std::ostream &operator>>(std::ostream &stream) const = 0;
  /// Output the buffer contents to the file for file descriptor @a fd.
  /// @return The number of bytes written.
  virtual ssize_t operator>>(int fd) const = 0;
};

/** A @c BufferWrite concrete subclass to write to a fixed size buffer.
 *
 * Copies and moves are forbidden because that leaves the original in a potentially bad state. An
 * instance is cheap to construct and should be done explicitly when needed.
 */
class FixedBufferWriter : public BufferWriter
{
  using super_type = BufferWriter;
  using self_type  = FixedBufferWriter;

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
  FixedBufferWriter(FixedBufferWriter &&)                 = delete;
  FixedBufferWriter &operator=(FixedBufferWriter &&) = delete;

  FixedBufferWriter(MemSpan &span) : _buf(span.begin()), _capacity(static_cast<size_t>(span.size())) {}

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
    const size_t newSize = _attempted + length;

    if (_buf) {
      if (newSize <= _capacity) {
        std::memcpy(_buf + _attempted, data, length);
      } else if (_attempted < _capacity) {
        std::memcpy(_buf + _attempted, data, _capacity - _attempted);
      }
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
  /// This can be used to clear the output by calling @c reduce(0). In contrast
  /// to @c clip this reduces the data in the buffer, rather than the capacity.
  self_type &
  reduce(size_t n)
  {
    ink_assert(n <= _attempted);

    _attempted = n;
    return *this;
  }

  /// Clear the buffer, reset to empty (no data).
  /// This is a convenience for reusing a buffer. For instance
  /// @code
  ///   bw.reset().print("....."); // clear old data and print new data.
  /// @endcode
  /// This is equivalent to @c reduce(0) but clearer for that case.
  self_type &
  reset()
  {
    _attempted = 0;
    return *this;
  }

  /// Provide a string_view of all successfully written characters.
  std::string_view
  view() const
  {
    return std::string_view(_buf, size());
  }

  /// Provide a @c string_view of all successfully written characters as a user conversion.
  operator std::string_view() const { return view(); }

  /** Get a @c FixedBufferWriter for the unused output buffer.

      If @a reserve is non-zero then the buffer size for the auxiliary writer will be @a reserve bytes
      smaller than the remaining buffer. This "reserves" space for additional output after writing
      to the auxiliary buffer, in a manner similar to @c clip / @c extend.
   */
  FixedBufferWriter
  auxWriter(size_t reserve = 0)
  {
    return {this->auxBuffer(), reserve < this->remaining() ? this->remaining() - reserve : 0};
  }

  /// Output the buffer contents to the @a stream.
  std::ostream &operator>>(std::ostream &stream) const override;
  /// Output the buffer contents to the file for file descriptor @a fd.
  ssize_t operator>>(int fd) const override;

  // Overrides for co-variance
  template <typename... Rest> self_type &print(TextView fmt, Rest &&... rest);
  template <typename... Args> self_type &printv(TextView fmt, std::tuple<Args...> const &args);
  template <typename... Args> self_type &print(BWFormat const &fmt, Args &&... args);
  template <typename... Args> self_type &printv(BWFormat const &fmt, std::tuple<Args...> const &args);

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
  using self_type  = LocalBufferWriter;
  using super_type = FixedBufferWriter;

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

    The argument can be passed by value if that would be more efficient.
  */

namespace bw_fmt
{
  /// Internal signature for template generated formatting.
  /// @a args is a forwarded tuple of arguments to be processed.
  template <typename TUPLE> using ArgFormatterSignature = BufferWriter &(*)(BufferWriter &w, BWFSpec const &, TUPLE const &args);

  /// Internal error / reporting message generators
  void Err_Bad_Arg_Index(BufferWriter &w, int i, size_t n);

  // MSVC will expand the parameter pack inside a lambda but not gcc, so this indirection is required.

  /// This selects the @a I th argument in the @a TUPLE arg pack and calls the formatter on it. This
  /// (or the equivalent lambda) is needed because the array of formatters must have a homogenous
  /// signature, not vary per argument. Effectively this indirection erases the type of the specific
  /// argument being formatted. Instances of this have the signature @c ArgFormatterSignature.
  template <typename TUPLE, size_t I>
  BufferWriter &
  Arg_Formatter(BufferWriter &w, BWFSpec const &spec, TUPLE const &args)
  {
    return bwformat(w, spec, std::get<I>(args));
  }

  /// This exists only to expand the index sequence into an array of formatters for the tuple type
  /// @a TUPLE.  Due to language limitations it cannot be done directly. The formatters can be
  /// accessed via standard array access in contrast to templated tuple access. The actual array is
  /// static and therefore at run time the only operation is loading the address of the array.
  template <typename TUPLE, size_t... N>
  ArgFormatterSignature<TUPLE> *
  Get_Arg_Formatter_Array(std::index_sequence<N...>)
  {
    static ArgFormatterSignature<TUPLE> fa[sizeof...(N)] = {&bw_fmt::Arg_Formatter<TUPLE, N>...};
    return fa;
  }

  /// Perform alignment adjustments / fill on @a w of the content in @a lw.
  /// This is the normal mechanism, but a number of the builtin types handle this internally
  /// for performance reasons.
  void Do_Alignment(BWFSpec const &spec, BufferWriter &w, BufferWriter &lw);

  /// Global named argument table.
  using GlobalSignature = void (*)(BufferWriter &, BWFSpec const &);
  using GlobalTable     = std::map<std::string_view, GlobalSignature>;
  extern GlobalTable BWF_GLOBAL_TABLE;
  extern GlobalSignature Global_Table_Find(std::string_view name);

  /// Generic integral conversion.
  BufferWriter &Format_Integer(BufferWriter &w, BWFSpec const &spec, uintmax_t n, bool negative_p);

  /// Generic floating point conversion.
  BufferWriter &Format_Floating(BufferWriter &w, BWFSpec const &spec, double n, bool negative_p);

} // namespace bw_fmt

using BWGlobalNameSignature = bw_fmt::GlobalSignature;
/// Add a global @a name to BufferWriter formatting, output generated by @a formatter.
/// @return @c true if the name was register, @c false if not (name already in use).
bool bwf_register_global(std::string_view name, BWGlobalNameSignature formatter);

/** Compiled BufferWriter format.

    @note This is not as useful as hoped, the performance is not much better using this than parsing
    on the fly (about 30% better, which is fine for tight loops but not for general use).
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
  static bool parse(TextView &fmt, std::string_view &literal, std::string_view &spec);

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

template <typename... Args>
BufferWriter &
BufferWriter::print(TextView fmt, Args &&... args)
{
  return this->printv(fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::printv(TextView fmt, std::tuple<Args...> const &args)
{
  using namespace std::literals;
  static constexpr int N = sizeof...(Args); // used as loop limit
  static const auto fa   = bw_fmt::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Args...>{});
  int arg_idx            = 0; // the next argument index to be processed.

  while (fmt.size()) {
    // Next string piece of interest is an (optional) literal and then an (optional) format specifier.
    // There will always be a specifier except for the possible trailing literal.
    std::string_view lit_v;
    std::string_view spec_v;
    bool spec_p = BWFormat::parse(fmt, lit_v, spec_v);

    if (lit_v.size()) {
      this->write(lit_v);
    }

    if (spec_p) {
      BWFSpec spec{spec_v}; // parse the specifier.
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
        ++arg_idx;
      } else if (spec._name.size()) {
        auto gf = bw_fmt::Global_Table_Find(spec._name);
        if (gf) {
          gf(lw, spec);
        } else {
          lw.write("{~"sv).write(spec._name).write("~}"sv);
        }
      }
      if (lw.extent()) {
        bw_fmt::Do_Alignment(spec, *this, lw);
      }
    }
  }
  return *this;
}

template <typename... Args>
BufferWriter &
BufferWriter::print(BWFormat const &fmt, Args &&... args)
{
  return this->printv(fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
BufferWriter &
BufferWriter::printv(BWFormat const &fmt, std::tuple<Args...> const &args)
{
  using namespace std::literals;
  static constexpr int N = sizeof...(Args);
  static const auto fa   = bw_fmt::Get_Arg_Formatter_Array<decltype(args)>(std::index_sequence_for<Args...>{});

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
      } else if (item._spec._name.size()) {
        lw.write("{~"sv).write(item._spec._name).write("~}"sv);
      }
    }
    bw_fmt::Do_Alignment(item._spec, *this, lw);
  }
  return *this;
}

// Must be first so that other inline formatters can use it.
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, std::string_view sv);

// Pointers that are not specialized.
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const void *ptr)
{
  BWFSpec ptr_spec{spec};
  ptr_spec._radix_lead_p = true;

  if (ptr == nullptr) {
    if (spec._type == 's' || spec._type == 'S') {
      ptr_spec._type = BWFSpec::DEFAULT_TYPE;
      ptr_spec._ext  = ""_sv; // clear any extension.
      return bwformat(w, spec, spec._type == 's' ? "null"_sv : "NULL"_sv);
    } else if (spec._type == BWFSpec::DEFAULT_TYPE) {
      return w; // print nothing if there is no format character override.
    }
  }

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

// Capture this explicitly so it doesn't go to any other pointer type.
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, std::nullptr_t)
{
  return bwformat(w, spec, static_cast<void *>(nullptr));
}

template <size_t N>
BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const char (&a)[N])
{
  return bwformat(w, spec, std::string_view(a, N - 1));
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, const char *v)
{
  if (spec._type == 'x' || spec._type == 'X' || spec._type == 'p') {
    bwformat(w, spec, static_cast<const void *>(v));
  } else if (v != nullptr) {
    bwformat(w, spec, std::string_view(v));
  } else {
    bwformat(w, spec, nullptr);
  }
  return w;
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, TextView tv)
{
  return bwformat(w, spec, static_cast<std::string_view>(tv));
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, std::string const &s)
{
  return bwformat(w, spec, std::string_view{s});
}

template <typename F>
auto
bwformat(BufferWriter &w, BWFSpec const &spec, F &&f) ->
  typename std::enable_if<std::is_floating_point<typename std::remove_reference<F>::type>::value, BufferWriter &>::type
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
bwformat(BufferWriter &w, BWFSpec const &spec, I &&i) ->
  typename std::enable_if<std::is_unsigned<typename std::remove_reference<I>::type>::value &&
                            std::is_integral<typename std::remove_reference<I>::type>::value,
                          BufferWriter &>::type
{
  return bw_fmt::Format_Integer(w, spec, i, false);
}

template <typename I>
auto
bwformat(BufferWriter &w, BWFSpec const &spec, I &&i) ->
  typename std::enable_if<std::is_signed<typename std::remove_reference<I>::type>::value &&
                            std::is_integral<typename std::remove_reference<I>::type>::value,
                          BufferWriter &>::type
{
  return i < 0 ? bw_fmt::Format_Integer(w, spec, -i, true) : bw_fmt::Format_Integer(w, spec, i, false);
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &, char c)
{
  return w.write(c);
}

inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, bool f)
{
  using namespace std::literals;
  if ('s' == spec._type) {
    w.write(f ? "true"sv : "false"sv);
  } else if ('S' == spec._type) {
    w.write(f ? "TRUE"sv : "FALSE"sv);
  } else {
    bw_fmt::Format_Integer(w, spec, static_cast<uintmax_t>(f), false);
  }
  return w;
}

// Generically a stream operator is a formatter with the default specification.
template <typename V>
BufferWriter &
operator<<(BufferWriter &w, V &&v)
{
  return bwformat(w, BWFSpec::DEFAULT, std::forward<V>(v));
}

// std::string support
/** Print to a @c std::string

    Print to the string @a s. If there is overflow then resize the string sufficiently to hold the output
    and print again. The effect is the string is resized only as needed to hold the output.
 */
template <typename... Args>
std::string &
bwprintv(std::string &s, ts::TextView fmt, std::tuple<Args...> const &args)
{
  auto len = s.size(); // remember initial size
  size_t n = ts::FixedBufferWriter(const_cast<char *>(s.data()), s.size()).printv(fmt, std::move(args)).extent();
  s.resize(n);   // always need to resize - if shorter, must clip pre-existing text.
  if (n > len) { // dropped data, try again.
    ts::FixedBufferWriter(const_cast<char *>(s.data()), s.size()).printv(fmt, std::move(args));
  }
  return s;
}

template <typename... Args>
std::string &
bwprint(std::string &s, ts::TextView fmt, Args &&... args)
{
  return bwprintv(s, fmt, std::forward_as_tuple(args...));
}

// -- FixedBufferWriter --
inline FixedBufferWriter::FixedBufferWriter(std::nullptr_t) : _buf(nullptr), _capacity(0) {}

inline FixedBufferWriter::FixedBufferWriter(char *buffer, size_t capacity) : _buf(buffer), _capacity(capacity)
{
  ink_assert(_capacity == 0 || buffer != nullptr);
}

template <typename... Args>
inline auto
FixedBufferWriter::print(TextView fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
inline auto
FixedBufferWriter::printv(TextView fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, args));
}

template <typename... Args>
inline auto
FixedBufferWriter::print(BWFormat const &fmt, Args &&... args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, std::forward_as_tuple(args...)));
}

template <typename... Args>
inline auto
FixedBufferWriter::printv(BWFormat const &fmt, std::tuple<Args...> const &args) -> self_type &
{
  return static_cast<self_type &>(this->super_type::printv(fmt, args));
}

// Basic format wrappers - these are here because they're used internally.
namespace bwf
{
  namespace detail
  {
    /** Write out raw memory in hexadecimal wrapper.
     *
     * This wrapper indicates the contained view should be dumped as raw memory in hexadecimal format.
     * This is intended primarily for internal use by other formatting logic.
     *
     * @see Hex_Dump
     */
    struct MemDump {
      std::string_view _view;

      /** Dump @a n bytes starting at @a mem as hex.
       *
       * @param mem First byte of memory to dump.
       * @param n Number of bytes.
       */
      MemDump(void const *mem, size_t n) : _view(static_cast<char const *>(mem), n) {}
    };
  } // namespace detail

  /** Treat @a t as raw memory and dump the memory as hexadecimal.
   *
   * @tparam T Type of argument.
   * @param t Object to dump.
   * @return @a A wrapper to do a hex dump.
   *
   * This is the standard way to do a hexadecimal memory dump of an object.
   *
   * @internal This function exists so that other types can overload it for special processing,
   * which would not be possible with just @c HexDump.
   */
  template <typename T>
  detail::MemDump
  Hex_Dump(T const &t)
  {
    return {&t, sizeof(T)};
  }
} // namespace bwf

BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, bwf::detail::MemDump const &hex);

} // end namespace ts

namespace std
{
inline ostream &
operator<<(ostream &s, ts::BufferWriter const &w)
{
  return w >> s;
}
} // end namespace std
