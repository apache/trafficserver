/** @file

    Utilities for generating character sequences in buffers.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
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

#include "swoc/TextView.h"
#include "swoc/MemSpan.h"

namespace swoc
{
namespace bwf
{
  struct Spec;
  class Format;
  class BoundNames;
} // namespace bwf

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
  virtual BufferWriter &write(const void *data, size_t length);

  /** Add the contents of @a sv to the buffer, up to the size of the view.

      Data is added only up to the remaining room in the buffer. If the remaining capacity is
      exceeded (i.e. data is not written to the output), the instance is put in to an error
      state. In either case the value for @c extent is incremented by the size of @a sv.

      @return @c *this
  */
  BufferWriter &write(const std::string_view &sv);

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
      swoc::FixedBufferWriter subw(w.aux_data(), w.remaining());
      write_some_stuff(subw); // generate output into the buffer.
      w.fill(subw.extent()); // update main buffer writer.
      @endcode

      @return Address of the next output byte, or @c nullptr if there is no remaining capacity.
   */
  virtual char *aux_data();

  /// Get the total capacity.
  /// @return The total number of bytes that can be written without causing an error condition.
  virtual size_t capacity() const = 0;

  /// Get the extent.
  /// @return Total number of characters that have been written, including those discarded due to an error condition.
  virtual size_t extent() const = 0;

  /// Get the output size.
  /// @return Total number of characters that are in the buffer (successfully written and not discarded)
  size_t size() const;

  /// Get the remaining buffer space.
  /// @return Number of additional characters that can be written without causing an error condidtion.
  size_t remaining() const;

  /** Increase the extent by @a n bytes.

      @param n The number of bytes to add to the extent.

      The buffer content is unchanged, only the extent value is adjusted.

      This is useful
      when doing local buffer filling using @c aux_data. After writing data there, it can be
      added to the extent using this method.

      @internal Concrete subclasses @b must override this to advance in a way consistent with the
      specific buffer type.

      @return @c *this
  */
  virtual BufferWriter &commit(size_t n) = 0;

  /** Decrease the extent by @a n.
   *
   * @param n Number of bytes to remove from the extent.
   *
   * The buffer content is unchanged, only the extent value is adjusted.
   */
  virtual BufferWriter &discard(size_t n) = 0;

  /// Reduce the capacity by @a n bytes
  /// If the capacity is reduced below the current @c size the instance goes in to an error state.
  /// @see restore
  /// @return @c *this
  virtual BufferWriter &restrict(size_t n) = 0;

  /// Restore @a n bytes of capacity.
  /// If there is an error condition, this function clears it and sets the extent to the size.  It
  /// then increases the capacity by n characters.
  /// @note It is required that any restored capacity have been previously removed by @c shrink.
  /// @see shrink
  virtual BufferWriter &restore(size_t n) = 0;

  /** Copy data from one part of the buffer to another.
   *
   * The copy is guaranteed to be correct even if the @a src and @a dst overlap. The regions are
   * clipped by the current extent. That is, bytes cannot be copied to nor from unwritten buffer.
   * If the extent is currently more than the capacity, the copy is performed as if the buffer
   * existed and then clipped to the actual buffer space.
   *
   * @param dst Offset of the first by to copy onto.
   * @param src Offset of the first byte to copy from.
   * @param n Number of bytes to copy.
   * @return @c *this
   */
  virtual BufferWriter &copy(size_t dst, size_t src, size_t n) = 0;

  // Force virtual destructor.
  virtual ~BufferWriter();

  /** BufferWriter print.

      This prints its arguments to the @c BufferWriter @a w according to the format @a fmt. The format
      string is based on Python style formating, each argument substitution marked by braces, {}. Each
      specification has three parts, a @a name, a @a specifier, and an @a extention. These are
      separated by colons. The name should be either omitted or a number, the index of the argument to
      use. If omitted the place in the format string is used as the argument index. E.g. "{} {} {}",
      "{} {1} {}", and "{0} {1} {2}" are equivalent. Using an explicit index does not reset the
      position of subsequent substiations, therefore "{} {0} {}" is equivalent to "{0} {0} {2}".

      @note This must be declared here, but the implementation is in @c bwf_base.h
  */
  template <typename... Rest> BufferWriter &print(const TextView &fmt, Rest &&... rest);

  /** Print overload to take arguments as a tuple instead of explicitly.
      This is useful for forwarding variable arguments from other functions / methods.
  */
  template <typename... Args> BufferWriter &printv(const TextView &fmt, const std::tuple<Args...> &args);

  /// Print using a preparsed @a fmt.
  template <typename... Args> BufferWriter &print(const bwf::Format &fmt, Args &&... args);
  template <typename... Args> BufferWriter &printv(const bwf::Format &fmt, const std::tuple<Args...> &args);

  /** Print the arguments on to the buffer.
   *
   * This is the base implementation, all of the other variants are wrappers for this.
   *
   * @tparam F Format processor - returns chunks of the format.
   * @tparam Args Arguments for the format.
   * @param names Name set for specifier names.
   */
  template <typename F, typename... Args>
  BufferWriter &print_nv(bwf::BoundNames const &names, F &&f, std::tuple<Args...> const &args);
  /// Convenience for no format argument style invocation.
  template <typename F> BufferWriter &print_nv(const bwf::BoundNames &names, F &&f);

  /// Output the buffer contents to the @a stream.
  /// @return The destination stream.
  virtual std::ostream &operator>>(std::ostream &stream) const = 0;
};

/** A @c BufferWrite concrete subclass to write to a fixed size buffer.
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

  /** Construct from span
   *
   */
  FixedBufferWriter(MemSpan<char> const &span);

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
  FixedBufferWriter &write(char c) override;

  /// Write @a data to the buffer, up to @a length bytes.
  FixedBufferWriter &write(const void *data, size_t length) override;

  // Bring in non-overridden methods.
  using super_type::write;

  /// Return the output buffer.
  const char *data() const override;

  /// Return whether there has been an error.
  bool error() const override;

  /// Get the start of the unused output buffer.
  char *aux_data() override;

  /// Get the total capacity of the output buffer.
  size_t capacity() const override;

  /// Get the total output sent to the writer.
  size_t extent() const override;

  /// Advance the used part of the output buffer.
  self_type &commit(size_t n) override;

  /// Drop @a n characters from the end of the buffer.
  /// The extent is reduced but the data is not overwritten and can be recovered with
  /// @c fill.
  self_type &discard(size_t n) override;

  /// Reduce the capacity by @a n.
  self_type &restrict(size_t n) override;

  /// Extend the capacity by @a n.
  self_type &restore(size_t n) override;

  /// Copy data in the buffer.
  FixedBufferWriter &copy(size_t dst, size_t src, size_t n) override;

  /// Clear the buffer, reset to empty (no data).
  /// This is a convenience for reusing a buffer. For instance
  /// @code
  ///   bw.reset().print("....."); // clear old data and print new data.
  /// @endcode
  /// This is equivalent to @c reduce(0) but clearer for that case.
  self_type &clear();

  /// Provide a string_view of all successfully written characters.
  std::string_view view() const;

  /// Provide a @c string_view of all successfully written characters as a user conversion.
  operator std::string_view() const;

  /// Output the buffer contents to the @a stream.
  std::ostream &operator>>(std::ostream &stream) const override;

  // Overrides for co-variance
  template <typename... Rest> self_type &print(TextView fmt, Rest &&... rest);

  template <typename... Args> self_type &printv(TextView fmt, std::tuple<Args...> const &args);

  template <typename... Args> self_type &print(bwf::Format const &fmt, Args &&... args);

  template <typename... Args> self_type &printv(bwf::Format const &fmt, std::tuple<Args...> const &args);

protected:
  char *const _buf;        ///< Output buffer.
  size_t _capacity;        ///< Size of output buffer.
  size_t _attempted   = 0; ///< Number of characters written, including those discarded due error condition.
  size_t _restriction = 0; ///< Restricted capacity.
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
  LocalBufferWriter();
  LocalBufferWriter(const LocalBufferWriter &that) = delete;
  LocalBufferWriter &operator=(const LocalBufferWriter &that) = delete;

protected:
  char _arr[N]; ///< output buffer.
};

// --------------- Implementation --------------------

inline BufferWriter::~BufferWriter() {}

inline BufferWriter &
BufferWriter::write(const void *data, size_t length)
{
  const char *d = static_cast<const char *>(data);

  while (length--) {
    this->write(*(d++));
  }
  return *this;
}

inline BufferWriter &
BufferWriter::write(const std::string_view &sv)
{
  return this->write(sv.data(), sv.size());
}

inline char *
BufferWriter::aux_data()
{
  return nullptr;
}

inline size_t
BufferWriter::size() const
{
  return std::min(this->extent(), this->capacity());
}

inline size_t
BufferWriter::remaining() const
{
  return this->capacity() - this->size();
}

// --- FixedBufferWriter ---
inline FixedBufferWriter::FixedBufferWriter(char *buffer, size_t capacity) : _buf(buffer), _capacity(capacity)
{
  if (_capacity != 0 && buffer == nullptr) {
    throw(std::invalid_argument{"FixedBufferWriter created with null buffer and non-zero size."});
  };
}

inline FixedBufferWriter::FixedBufferWriter(MemSpan<char> const &span) : _buf{span.begin()}, _capacity{span.size()} {}

inline FixedBufferWriter::FixedBufferWriter(std::nullptr_t) : _buf(nullptr), _capacity(0) {}

inline FixedBufferWriter &
FixedBufferWriter::write(char c)
{
  if (_attempted < _capacity) {
    _buf[_attempted] = c;
  }
  ++_attempted;

  return *this;
}

inline FixedBufferWriter &
FixedBufferWriter::write(const void *data, size_t length)
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

/// Return the output buffer.
inline const char *
FixedBufferWriter::data() const
{
  return _buf;
}

inline bool
FixedBufferWriter::error() const
{
  return _attempted > _capacity;
}

inline char *
FixedBufferWriter::aux_data()
{
  return error() ? nullptr : _buf + _attempted;
}

inline auto
FixedBufferWriter::commit(size_t n) -> self_type &
{
  _attempted += n;

  return *this;
}

inline size_t
FixedBufferWriter::capacity() const
{
  return _capacity;
}

inline size_t
FixedBufferWriter::extent() const
{
  return _attempted;
}

inline auto
FixedBufferWriter::restrict(size_t n) -> self_type &
{
  if (n > _capacity) {
    throw(std::invalid_argument{"FixedBufferWriter restrict value more than capacity"});
  }

  _capacity -= n;
  _restriction += n;

  return *this;
}

inline auto
FixedBufferWriter::restore(size_t n) -> self_type &
{
  if (error()) {
    _attempted = _capacity;
  }
  n = std::min(n, _restriction);

  _capacity += n;
  _restriction -= n;

  return *this;
}

inline auto
FixedBufferWriter::discard(size_t n) -> self_type &
{
  _attempted -= std::min(_attempted, n);
  return *this;
}

inline auto
FixedBufferWriter::clear() -> self_type &
{
  _attempted = 0;
  return *this;
}

inline auto
FixedBufferWriter::copy(size_t dst, size_t src, size_t n) -> self_type &
{
  auto limit = std::min<size_t>(_capacity, _attempted); // max offset of region possible.
  MemSpan<char> src_span{_buf + src, std::min(limit, src + n)};
  MemSpan<char> dst_span{_buf + dst, std::min(limit, dst + n)};
  std::memmove(dst_span.data(), src_span.data(), std::min(dst_span.size(), src_span.size()));
  return *this;
}

inline std::string_view
FixedBufferWriter::view() const
{
  return std::string_view(_buf, size());
}

/// Provide a @c string_view of all successfully written characters as a user conversion.
inline FixedBufferWriter::operator std::string_view() const
{
  return this->view();
}

// --- LocalBufferWriter ---
template <size_t N> LocalBufferWriter<N>::LocalBufferWriter() : super_type(_arr, N) {}

} // namespace swoc

namespace std
{
inline ostream &
operator<<(ostream &s, swoc::BufferWriter const &w)
{
  return w >> s;
}
} // end namespace std
