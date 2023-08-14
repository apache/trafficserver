// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    Utilities for generating character sequences in buffers.
 */

#pragma once

#include <cstdlib>
#include <utility>
#include <cstring>
#include <vector>
#include <string>
#include <iosfwd>
#include <string_view>

#include "swoc/swoc_version.h"
#include "swoc/TextView.h"
#include "swoc/MemSpan.h"
#include "swoc/bwf_fwd.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

/** Wrapper for operations on a buffer.
 *
 * This maintains information about the size and amount in use of the buffer, preventing data
 * overruns. In all cases, methods that write to the buffer clip the input to the size of the
 * remaining buffer space. The @c error method can be used to detect such clipping. The theoretical
 * size of the buffer is also tracked such that if there is not enough buffer space, the amount
 * needed can be determined by the method @c extent.
 *
 * @note This is a protocol class, concrete subclasses implement the functionality.
 */
class BufferWriter {
public:
  /** Write @a c to the buffer.
   *
   * @param c Character to write.
   * @return @a this.
   */
  virtual BufferWriter &write(char c) = 0;

  /** Write @a length bytes starting at @a data to the buffer.
   *
   * @param data Source data.
   * @param length Number of bytes in the source data.
   * @return @a this.
   *
   * @internal This uses the single character write to output the data. It is presumed concrete
   * subclasses will override this method to use more efficient mechanisms, dependent on the type of
   * output buffer.
   */
  virtual BufferWriter &write(void const *data, size_t length);

  /** Write data to the buffer.
   *
   * @param span Data source.
   * @return @a this
   *
   * Data from @a span is written directly to the buffer, and clipped to the size of the buffer.
   *
   * @internal The char poointer overloads protect this method in order to protect against including
   * the terminal nul in the destination buffer.
   */
  BufferWriter & write(MemSpan<void const> span);

  /// @return Pointer to first byte in buffer.
  virtual const char *data() const = 0;

  /// Get the error state.
  /// @return @c true if in an error state, @c false if not.
  virtual bool error() const = 0;

  /** Address of the first unused byte in the output buffer.

      The address is fragile and calls to non-const methods can invalidate it.

      @return Address of the next output byte, or @c nullptr if there is no remaining capacity.
   */
  virtual char *aux_data();

  /// @return The number of bytes that can be successfully written to this buffer.
  virtual size_t capacity() const = 0;

  /// @return Number of characters written to the buffer, including those discarded.
  virtual size_t extent() const = 0;

  /// @return Number of bytes of valid (used) data in the buffer.
  size_t size() const;

  /// @return The number of bytes which have not yet been written.
  size_t remaining() const;

  /** A memory span of the unused bytes.
   *
   * @return A span of the unused bytes.
   *
   * This is a convenience method that is identical to
   * @code
   *   BufferWriter w;
   *   // ...
   *   MemSpan<char>{ w.aux_data(), w.remaining() };
   * @endcode
   */
  MemSpan<char> aux_span();

  /** Increase the extent by @a n bytes.
   *
   * @param n Number of bytes.
   * @return @c true if the commit is final, @c false if it should be retried.
   *
   * This is used to add data written in the @c aux_data to the written data in the buffer.
   *
   * The return value should be @c true unless the write operation proceeding the call to @c commit
   * along with the @c commit call should be retried. That is only reasonable if some state in the
   * concrete implementation has changed to make success possible on the next try. Generally this
   * will be because the implementation increased capacity.
   *
   * @internal Concrete subclasses @b must override this in a way consistent with the specific buffer type.
   */
  virtual bool commit(size_t n) = 0;

  /** Decrease the extent by @a n.
   *
   * @param n Number of bytes to remove from the extent.
   * @return @a this.
   *
   * The buffer content is unchanged, only the extent value is adjusted. This effectively discards
   * @a n bytes of already written data.
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
  /// @note This does not make the internal buffer size larger. It can only restore capacity earlier
  /// removed by @c restrict.
  /// @see restrict
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
   *
   * @internal This is used to perform justification for formatting.
   */
  virtual BufferWriter &copy(size_t dst, size_t src, size_t n) = 0;

  // Force virtual destructor.
  virtual ~BufferWriter();

  /** Formatted output to the buffer.
   *
   * @tparam Args Types of the format arguments.
   * @param fmt Format string to control formatted output.
   * @param args Parameters for the format string.
   * @return @a this.
   *
   * The format string is Python style.
   * See http://docs.solidwallofcode.com/libswoc/code/BW_Format.en.html for further information.
   *
   * @note This must be declared here, but the implementation is in @c bwf_base.h. That file does
   * not need to be included if formatted output is not used.
   */
  template <typename... Args> BufferWriter &print(const TextView &fmt, Args &&... args);

  /** Formatted output to the buffer.
   *
   * @tparam Args Types of the arguments for formatting.
   * @param fmt Format string.
   * @param args The format arguments in a tuple.
   * @return @a this
   *
   * This is the equivalent of the "va..." form for printing. Alternate front ends to formatted
   * output should gather their formatting arguments into a tuple, usually using
   * @c std::forward_as_tuple().
   */
  template <typename... Args> BufferWriter &print_v(const TextView &fmt, const std::tuple<Args...> &args);

  /** Formatted output to the buffer.
   *
   * @tparam Args Types of the format input parameters.
   * @param fmt Pre-condensed format.
   * @param args Arguments for the format string.
   * @return @a this.
   */
  template <typename... Args> BufferWriter &print(const bwf::Format &fmt, Args &&... args);

  /** Formatted output to the buffer.
   *
   * @tparam Args Types of the parameter for formatting.
   * @param fmt Pre-condensed format string.
   * @param args The format parameters in a tuple.
   * @return @a this
   *
   * This is the equivalent of the "va..." form for printing. Alternate front ends to formatted
   * output should gather their formatting arguments into a tuple, usually using
   * @c std::forward_as_tuple().
   */
  template <typename... Args> BufferWriter &print_v(const bwf::Format &fmt, const std::tuple<Args...> &args);

  /** Write formatted output of @a args to @a this buffer.
   *
   * @tparam Binding Type for the name binding instance.
   * @tparam Extractor Format extractor type.
   * @param names Name set for specifier names.
   * @param ex Format processor instance, which parse the format piecewise.
   * @param args The format parameters.
   *
   * @a Extractor must have at least two methods
   * - A conversion to @c bool that indicates if there is data left.
   * - A function of the signature <tt>bool ex(std::string_view& lit, bwf::Spec & spec)</tt>
   *
   * The latter must return whether a specifier was parsed, while filling in @a lit and @a spec
   * as appropriate for the next chunk of format string. No literal is represented by a empty
   * @a lit.
   *
   * The name binding must have a function operator that takes two arguments, a @c BufferWriter&
   * and a format specifier @c bwf::Spec. It is expected to generate output to the @c BufferWriter
   * instance based on data in the format specifier (which contains, among other things, the
   * name which caused the binding to be invoked).
   *
   * @note This is the base implementation, all of the other variants are wrappers for this.
   *
   * @see NameBinding
   */
  template <typename Binding, typename Extractor>
  BufferWriter &print_nfv(Binding &&names, Extractor &&ex, bwf::ArgPack const &args);

  /** Write formatted output of @a args to @a this buffer.
   *
   * @tparam Binding Name binding functor.
   * @tparam Extractor Format processor type.
   * @param names Name set for specifier names.
   * @param ex Format processor instance, which parse the format piecewise.
   *
   * @note This is primarily an internal convenience for certain situations where a format parameter
   * tuple is not needed and difficult to create.
   */
  template <typename Binding, typename Extractor> BufferWriter &print_nfv(Binding const &names, Extractor &&ex);

  /** Write formatted output to @a this buffer.
   *
   * @param names Name set for specifier names.
   * @param fmt Format string.
   *
   * This is intended to be use with context name binding where @a names has the bindings and the
   * format string @a fmt contains only references to those names, not to any arguments.
   */
  template <typename Binding> BufferWriter &print_n(Binding const &names, TextView const &fmt);

  /** Write formattted data.
   *
   * @tparam T Data type.
   * @param spec Format specifier.
   * @param t Instance to print.
   * @return @a this
   *
   * Essentially this forwards @a t to @c bwformat.
   */
  template <typename T> BufferWriter & format(bwf::Spec const& spec, T && t);

  /** Write formattted data.
   *
   * @tparam T Data type.
   * @param spec Format specifier.
   * @param t Instance to print.
   * @return @a this
   *
   * Essentially this forwards @a t to @c bwformat.
   */
  template <typename T> BufferWriter & format(bwf::Spec const& spec, T const& t);

  /** IO stream operator.
   *
   * @param stream Output stream.
   * @return @a stream
   *
   * Write the buffer contents to @a stream.
   */
  virtual std::ostream &operator>>(std::ostream &stream) const = 0;
};

/** A concrete @c BufferWriter class for a fixed buffer.
 *
 */
class FixedBufferWriter : public BufferWriter {
  using super_type = BufferWriter;
  using self_type  = FixedBufferWriter;

public:
  /** Construct a buffer writer on a fixed @a buffer of size @a capacity.

      If writing goes past the end of the buffer, the excess is dropped.
   */
  FixedBufferWriter(char *buffer, size_t capacity);

  /// Construct using the memory @a span as the buffer.
  FixedBufferWriter(MemSpan<void> const &span);

  /// Construct using the memory @a span as the buffer.
  FixedBufferWriter(MemSpan<char> const &span);

  /** Constructor an empty buffer with no capacity.
   * This can be useful to measure the extent of the output before allocating memory.
   */
  FixedBufferWriter(std::nullptr_t);

  FixedBufferWriter(const FixedBufferWriter &) = delete;

  FixedBufferWriter &operator=(const FixedBufferWriter &) = delete;

  /// Move constructor.
  FixedBufferWriter(FixedBufferWriter &&that);

  /// Move assignment.
  FixedBufferWriter &operator=(FixedBufferWriter &&that);

  /// Reset buffer.
  self_type &assign(MemSpan<char> const &span);

  /// Write a single character @a c to the buffer.
  FixedBufferWriter &write(char c) override;

  /// Write @a length bytes, starting at @a data, to the buffer.
  FixedBufferWriter &write(const void *data, size_t length) override;

  // Bring in non-overridden methods.
  using super_type::write;

  /// @return The start of the buffer.
  const char *data() const override;

  /// @return @c true if output has been discarded, @a false otherwise.
  bool error() const override;

  /// @return Start of the unused buffer, or @c nullptr is there is no remaining unwritten space.
  char *aux_data() override;

  /// Get the total capacity of the output buffer.
  size_t capacity() const override;

  /// Get the total output sent to the writer.
  size_t extent() const override;

  /// Advance the used part of the output buffer.
  bool commit(size_t n) override;

  /// Drop @a n characters from the end of the buffer.
  self_type &discard(size_t n) override;

  /// Reduce the capacity by @a n.
  self_type &restrict(size_t n) override;

  /// Restore @a n bytes of the capacity.
  self_type &restore(size_t n) override;

  /// Copy data in the buffer.
  FixedBufferWriter &copy(size_t dst, size_t src, size_t n) override;

  /// Erase the buffer, reset to empty (no valid data).
  /// This is a convenience for reusing a buffer. For instance
  /// @code
  ///   bw.clear().print("....."); // clear old data and print new data.
  /// @endcode
  /// This is equivalent to @c w.discard(w.size()) but clearer for that case.
  self_type &clear();

  self_type &detach();

  /// @return The used part of the buffer as a @c std::string_view.
  swoc::TextView view() const;

  /// Provide a @c string_view of all successfully written characters as a user conversion.
  operator std::string_view() const;

  /// Provide a @c string_view of all successfully written characters as a user conversion.
  operator swoc::TextView() const;

  /// Output the buffer contents to the @a stream.
  std::ostream &operator>>(std::ostream &stream) const override;

  /// @cond COVARY
  template <typename... Rest> self_type &print(TextView fmt, Rest &&... rest);

  template <typename... Args> self_type &print_v(TextView fmt, std::tuple<Args...> const &args);

  template <typename... Args> self_type &print(bwf::Format const &fmt, Args &&... args);

  template <typename... Args> self_type &print_v(bwf::Format const &fmt, std::tuple<Args...> const &args);
  /// @endcond

protected:
  char *const _buffer;   ///< Output buffer.
  size_t _capacity;      ///< Size of output buffer.
  size_t _attempted = 0; ///< Number of characters written, including those discarded due error condition.
};

/** A @c BufferWriter that has an internal buffer.
 *
 * @tparam N Number of bytes in internal buffer.
 *
 * The buffer is part of the class instance and is therefore allocated from the same memory pool
 * as the object. E.g, if this is declared as a local variable the buffer is on the stack.
 *
 * This was written to make code such as
 * @code
 * char buff[1024];
 * FixedBufferWriter w(buff, sizeof(buff));
 * @endcode
 * simpler as
 * @code
 * LocalBufferWriter<1024> w;
 * @endcode
 *
 * This also makes it possible to use inside expressions and other stream operations without concern
 * about having to previously declare the storage. E.g.
 * @code
 * create_note(LocalBufferWriter<256>().print("Note {}", idx).view());
 * @endcode
 */
template <size_t N> class LocalBufferWriter : public FixedBufferWriter {
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
BufferWriter::write(const void *data, size_t length) {
  const char *d = static_cast<const char *>(data);

  while (length--) {
    this->write(*(d++));
  }
  return *this;
}

# if 0
inline BufferWriter &
BufferWriter::write(const std::string_view &sv) {
  return this->write(sv.data(), sv.size());
}
# endif

inline BufferWriter &
BufferWriter::write(MemSpan<void const> span) { return this->write(span.data(), span.size()); }

inline char *
BufferWriter::aux_data() {
  return nullptr;
}

inline size_t
BufferWriter::size() const {
  return std::min(this->extent(), this->capacity());
}

inline size_t
BufferWriter::remaining() const {
  return this->capacity() - this->size();
}

// --- FixedBufferWriter ---
inline FixedBufferWriter::FixedBufferWriter(char *buffer, size_t capacity) : _buffer(buffer), _capacity(capacity) {
  if (_capacity != 0 && buffer == nullptr) {
    throw(std::invalid_argument{"FixedBufferWriter created with null buffer and non-zero size."});
  };
}

inline FixedBufferWriter::FixedBufferWriter(MemSpan<void> const &span)
  : _buffer{static_cast<char *>(span.data())}, _capacity{span.size()} {}

inline FixedBufferWriter::FixedBufferWriter(MemSpan<char> const &span) : _buffer{span.begin()}, _capacity{span.size()} {}

inline FixedBufferWriter::FixedBufferWriter(std::nullptr_t) : _buffer(nullptr), _capacity(0) {}

inline FixedBufferWriter::self_type &
FixedBufferWriter::detach() {
  const_cast<char *&>(_buffer) = nullptr;
  _capacity                    = 0;
  _attempted                   = 0;
  return *this;
}

inline FixedBufferWriter::FixedBufferWriter(FixedBufferWriter &&that)
  : _buffer(that._buffer), _capacity(that._capacity), _attempted(that._attempted) {
  that.detach();
}

inline FixedBufferWriter::self_type &
FixedBufferWriter::assign(MemSpan<char> const &span) {
  const_cast<char *&>(_buffer) = span.data();
  _capacity                    = span.size();
  _attempted                   = 0;
  return *this;
}

inline FixedBufferWriter &
FixedBufferWriter::operator=(FixedBufferWriter &&that) {
  const_cast<char *&>(_buffer) = that._buffer;
  _capacity                    = that._capacity;
  _attempted                   = that._attempted;
  that.detach();
  return *this;
}

inline FixedBufferWriter &
FixedBufferWriter::write(char c) {
  if (_attempted < _capacity) {
    _buffer[_attempted] = c;
  }
  ++_attempted;

  return *this;
}

inline FixedBufferWriter &
FixedBufferWriter::write(const void *data, size_t length) {
  const size_t newSize = _attempted + length;

  if (_buffer) {
    if (newSize <= _capacity) {
      std::memcpy(_buffer + _attempted, data, length);
    } else if (_attempted < _capacity) {
      std::memcpy(_buffer + _attempted, data, _capacity - _attempted);
    }
  }
  _attempted = newSize;

  return *this;
}

/// Return the output buffer.
inline const char *
FixedBufferWriter::data() const {
  return _buffer;
}

inline bool
FixedBufferWriter::error() const {
  return _attempted > _capacity;
}

inline char *
FixedBufferWriter::aux_data() {
  return error() ? nullptr : _buffer + _attempted;
}

inline bool
FixedBufferWriter::commit(size_t n) {
  _attempted += n;

  return true;
}

inline size_t
FixedBufferWriter::capacity() const {
  return _capacity;
}

inline size_t
FixedBufferWriter::extent() const {
  return _attempted;
}

inline auto
FixedBufferWriter::restrict(size_t n) -> self_type & {
  if (n > _capacity) {
    throw(std::invalid_argument{"FixedBufferWriter restrict value more than capacity"});
  }
  _capacity -= n;
  return *this;
}

inline auto
FixedBufferWriter::restore(size_t n) -> self_type & {
  if (error()) {
    _attempted = _capacity;
  }
  _capacity += n;
  return *this;
}

inline auto
FixedBufferWriter::discard(size_t n) -> self_type & {
  _attempted -= std::min(_attempted, n);
  return *this;
}

inline auto
FixedBufferWriter::clear() -> self_type & {
  _attempted = 0;
  return *this;
}

inline auto
FixedBufferWriter::copy(size_t dst, size_t src, size_t n) -> self_type & {
  auto limit = std::min<size_t>(_capacity, _attempted); // max offset of region possible.
  MemSpan<char> src_span{_buffer + src, std::min(limit, src + n)};
  MemSpan<char> dst_span{_buffer + dst, std::min(limit, dst + n)};
  std::memmove(dst_span.data(), src_span.data(), std::min(dst_span.size(), src_span.size()));
  return *this;
}

inline swoc::TextView
FixedBufferWriter::view() const {
  return {_buffer, size()};
}

inline FixedBufferWriter::operator std::string_view() const {
  return this->view();
}

inline FixedBufferWriter::operator swoc::TextView() const {
  return this->view();
}

// --- LocalBufferWriter ---
template <size_t N> LocalBufferWriter<N>::LocalBufferWriter() : super_type(_arr, N) {}

}} // namespace swoc::SWOC_VERSION_NS

/// @cond NOT_DOCUMENTED
namespace std {
inline ostream &
operator<<(ostream &s, swoc::BufferWriter const &w) {
  return w >> s;
}
} // end namespace std
/// @endcond
