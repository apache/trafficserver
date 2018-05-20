/** @file
 *
    Stacking error message handling.

    The problem addressed by this library is the ability to pass back
    detailed error messages from failures. It is hard to get good
    diagnostics because the specific failures and general context are
    located in very different stack frames. This library allows local
    functions to pass back local messages which can be easily
    augmented as the error travels up the stack frame.

    This aims to improve over exceptions by being lower cost and not requiring callers to handle the messages.
    On the other hand, the messages could be used just as easily with exceptions.

    Each message on a stack contains text and a numeric identifier.
    The identifier value zero is reserved for messages that are not
    errors so that information can be passed back even in the success
    case.

    The implementation takes the position that success must be fast and
    failure is expensive. Therefore Errata is optimized for the success
    path, imposing very little overhead in that case. On the other hand, if an
    error occurs and is handled, that is generally so expensive that
    optimizations are pointless (although, of course, code should not
    be gratuitiously expensive).

    The library provides the @c Rv ("return value") template to
    make returning values and status easier. This template allows a
    function to return a value and status pair with minimal changes.
    The pair acts like the value type in most situations, while
    providing access to the status.

    Each instance of an erratum is a wrapper class that emulates value
    semantics (copy on write). This means passing even large message
    stacks is inexpensive, involving only a pointer copy and reference
    counter increment and decrement. A success value is represented by
    an internal @c NULL so it is even cheaper to copy.

    To further ease use, the library has the ability to define @a
    sinks.  A sink is a function that acts on an erratum when it
    becomes unreferenced. The indended use is to send the messages to
    an output log. This makes reporting errors to a log from even
    deeply nested functions easy while preserving the ability of the
    top level logic to control such logging.

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

#include <vector>
#include <string_view>
#include <functional>
#include <ts/MemArena.h>
#include <ts/BufferWriter.h>
#include <tsconfig/NumericType.h>
#include <ts/IntrusiveDList.h>

namespace ts
{
/// Severity levels for Errata.
enum class Severity {
  DIAG, ///< Diagnostic only. DL_Diag
  DBG, ///< Debugging. DL_Debug ('DEBUG' is a macro)
  INFO, ///< Informative. DL_Status
  NOTE, ///< Notice. DL_Note ('Note' is a macro)
  WARN, ///< Warning. DL_Warning
  ERROR, ///< Error. DL_Error
  FATAL, ///< Fatal. DL_Fatal
  ALERT, ///< Alert. DL_Alert
  EMERGENCY, ///< Emergency. DL_Emergency.
};

/** Class to hold a stack of error messages (the "errata").
    This is a smart handle class, which wraps the actual data
    and can therefore be treated a value type with cheap copy
    semantics. Default construction is very cheap.
 */
class Errata
{
public:
  using Severity = ts::Severity; ///< Import for associated classes.

  /// Severity used if not specified.
  static constexpr Severity DEFAULT_SEVERITY{Severity::DIAG};
  /// Severity level at which the instance is a failure of some sort.
  static constexpr Severity FAILURE_SEVERITY{Severity::WARN};

  struct Annotation { // Forward declaration.
    using self_type   = Annotation;          ///< Self reference type.

    /// Default constructor.
    /// The message has default severity and empty text.
    Annotation();

    /** Construct with severity @a level and @a text.
     *
     * @param level Severity level.
     * @param text Annotation content (literal).
     */
    Annotation(Severity level, std::string_view text);

    /// Reset to the message to default state.
    self_type &clear();

    /// Get the severity.
    Severity severity() const;

    /// Get the text of the message.
    std::string_view text() const;

    /// Set the text of the message.
    self_type &assign(std::string_view text);

    /// Set the severity @a level
    self_type &assign(Severity level);

  protected:
    Severity _level{Errata::DEFAULT_SEVERITY}; ///< Annotation code.
    std::string_view _text;                         ///< Annotation text.

    /// Policy and links for intrusive list.
    struct Linkage {
      self_type * _next; ///< Next link.
      self_type * _prev; ///< Previous link.
      static self_type *& next_ptr(self_type*);
      static self_type *& prev_ptr(self_type*);
    } _link;

    friend class Errata;
  };

protected:
  using self_type   = Errata;          ///< Self reference type.
  /// Storage type for list of messages.
  /// Internally the vector is accessed backwards, in order to make it LIFO.
  using Container = IntrusiveDList<Annotation::Linkage>;

  /// Implementation class.
  struct Data {
    using self_type = Data; ///< Self reference type.

    /// Construct into @c MemArena.
    Data(ts::MemArena && arena);

    /// Check if there are any notes.
    bool empty() const;

    /** Duplicate @a src in the arena for this instance.
     *
     * @param src Source data.
     * @return View of copy in this arena.
     */
    std::string_view localize(std::string_view src);

    /// Get the remnant of the curret block in the arena.
    ts::MemSpan remnant();

    /// Allocate from the arena.
    ts::MemSpan alloc(size_t n);

    /// The message stack.
    Container _notes;
    /// Annotation text storage.
    ts::MemArena _arena;
    /// The effective severity of the message stack.
    Severity _level{Errata::DEFAULT_SEVERITY};
  };

public:
  /// Default constructor - empty errata, very fast.
  Errata();
  Errata(self_type const &that) = default;
  Errata(self_type &&that) = default;                              ///< Move constructor.
  self_type &operator=(self_type const &that) = delete;            // no copy assignemnt.
  self_type &operator=(self_type &&that) = default; ///< Move assignment.
  ~Errata();                                                       ///< Destructor.

  /** Add a new message to the top of stack with default severity and @a text.
   * @param level Severity of the message.
   * @param text Text of the message.
   * @return *this
   */
  self_type &note(std::string_view text);

  /** Add a new message to the top of stack with severity @a level and @a text.
   * @param level Severity of the message.
   * @param text Text of the message.
   * @return *this
   */
  self_type &note(Severity level, std::string_view text);

  /** Push a constructed @c Annotation.
      The @c Annotation is set to have the @a id and @a code. The other arguments are converted
      to strings and concatenated to form the messsage text.
      @return A reference to this object.
  */
  template <typename... Args> self_type &note(Severity level, std::string_view fmt, Args &&... args);

  /** Push a constructed @c Annotation.
      The @c Annotation is set to have the @a id and @a code. The other arguments are converted
      to strings and concatenated to form the messsage text.
      @return A reference to this object.
  */
  template <typename... Args> self_type &note_v(Severity level, std::string_view fmt, std::tuple<Args...> const& args);

  /** Copy messages from @a that to @a this.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   */
  self_type & note(self_type const& that);

  /** Copy messages from @a that to @a this, then clear @that.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   */
  self_type & note(self_type && that);

  /// Remove all messages.
  /// @note This is also used to prevent logging.
  self_type &clear();

  friend std::ostream &operator<<(std::ostream &, self_type const &);

  /// Default glue value (a newline) for text rendering.
  static const std::string_view DEFAULT_GLUE;

  /** Test status.

      Equivalent to @c success but more convenient for use in
      control statements.

      @return @c true if no messages or last message has a zero
      message ID, @c false otherwise.
   */
  explicit operator bool() const;

  /** Test errata for no failure condition.

      Equivalent to @c operator @c bool but easier to invoke.

      @return @c true if no messages or last message has a zero
      message ID, @c false otherwise.
   */
  bool is_ok() const;

  /** Get the maximum severity of the messages in the erratum.
   *
   * @return Max severity for all messages.
   */
  Severity severity() const;

  /// Number of messages in the errata.
  size_t count() const;

  using iterator = Container::iterator;
  using const_iterator = Container::const_iterator;

  /// Reference to top item on the stack.
  iterator begin();
  /// Reference to top item on the stack.
  const_iterator begin() const;
  //! Reference one past bottom item on the stack.
  iterator end();
  //! Reference one past bottom item on the stack.
  const_iterator end() const;

  const Annotation & front() const;

  // Logging support.

  /** Base class for erratum sink.

      When an errata is abandoned, this will be called on it to perform any client specific logging.
      It is passed around by handle so that it doesn't have to support copy semantics (and is not
      destructed until application shutdown). Clients can subclass this class in order to preserve
      arbitrary data for the sink or retain a handle to the sink for runtime modifications.
   */
  class Sink
  {
    using self_type = Sink;

  public:
    using Handle = std::shared_ptr<self_type>; ///< Handle type.

    /// Handle an abandoned errata.
    virtual void operator()(Errata const &) const = 0;
    /// Force virtual destructor.
    virtual ~Sink() {}
  };

  //! Register a sink for discarded erratum.
  static void register_sink(Sink::Handle const &s);

  /// Register a function as a sink.
  using SinkHandler = std::function<void (Errata const &)>;

  /// Convenience wrapper class to enable using functions directly for sinks.
  struct SinkWrapper : public Sink {
    /// Constructor.
    SinkWrapper(SinkHandler f) : _f(f) {}
    /// Operator to invoke the function.
    void operator()(Errata const &e) const override;
    SinkHandler _f; ///< Client supplied handler.
  };

  /// Register a sink function for abandonded erratum.
  static void
  register_sink(SinkHandler const& f)
  {
    register_sink(Sink::Handle(new SinkWrapper(f)));
  }

  /** Simple formatted output.
   */
  std::ostream &write(std::ostream &out) const;

protected:
  /// Implementation instance.
  // Although it may seem like move semantics make this unnecessary, that's not the case. The problem
  // is that code wants to work with an instance. It is rarely the case that an instance is constructed
  // just as it is returned (e.g. std::string). Code would therefore have to call std::move for
  // every return, which is not feasible.
  std::shared_ptr<Data> _data;

  /// Force data existence.
  /// @return A pointer to the data.
  const Data *data();

  /// Get a writeable data pointer.
  /// @c Data is cloned if there are other references.
  Data * writeable_data();

  /** Allocate a span of memory.
   *
   * @param n Number of bytes to allocate.
   * @return A span of the allocated memory.
   */
  MemSpan alloc(size_t n);

  /// Add a note which is already localized.
  self_type & note_localized(Severity, MemSpan span);

  /// Used for returns when no data is present.
  static Annotation const NIL_NOTE;

  friend struct Data;
  friend class Item;
};

extern std::ostream &operator<<(std::ostream &os, Errata const &stat);

/** Return type for returning a value and status (errata).  In general, a method wants to return
    both a result and a status so that errors are logged properly. This structure is used to do that
    in way that is more usable than just @c std::pair.  - Simpler and shorter typography - Force use
    of @c errata rather than having to remember it (and the order) each time - Enable assignment
    directly to @a R for ease of use and compatibility so clients can upgrade asynchronously.
 */
template <typename R> struct Rv : public std::tuple<R, Errata> {
  using self_type  = Rv;     ///< Standard self reference type.
  using super_type = std::tuple<R, Errata>;
  using result_type = R;      ///< Type of result value.

  static constexpr int RESULT = 0; ///< Tuple index for result.
  static constexpr int ERRATA = 1; ///< Tuple index for Errata.

  result_type _result{}; ///< The actual result of the function.

  /** Default constructor.
      The default constructor for @a R is used.
      The status is initialized to SUCCESS.
  */
  Rv();

  /** Construct with copy of @a result and empty Errata.
   *
   * Construct with a specified @a result and a default (successful) @c Errata.
   * @param result Return value / result.
   */
  Rv(result_type const &result);

  /** Construct with copy of @a result and move @a errata.
   *
   * Construct with a specified @a result and a default (successful) @c Errata.
   * @param result Return value / result.
   */
  Rv(result_type const &result, Errata && errata);
  Rv(result_type const &result, const Errata & errata);

  /** Construct with move of @a result and empty Errata.
   *
   * @param result The return / result value.
    */
  Rv(result_type &&result);

  /** Construct with result and move of @a errata.
   *
   * @param result The return / result value to assign.
   * @param errata Status to move.
    */
  Rv(result_type &&result, Errata &&errata);
  Rv(result_type &&result, const Errata &errata);

  /** Push a message in to the result.
   *
   * @param level Severity of the message.
   * @param text Text of the message.
   * @return @a *this
   */
  self_type &note(Severity level, std::string_view text);

  /** Push a message in to the result.
   *
   * @param level Severity of the message.
   * @param text Text of the message.
   * @return @a *this
   */
  template <typename... Args> self_type &note(Severity level, std::string_view fmt, Args &&... args);

  /** User conversion to the result type.

      This makes it easy to use the function normally or to pass the
      result only to other functions without having to extract it by
      hand.
  */
  operator result_type const &() const;

  /** Assignment from result type.

      This allows the result to be assigned to a pre-declared return
      value structure.  The return value is a reference to the
      internal result so that this operator can be chained in other
      assignments to instances of result type. This is most commonly
      used when the result is computed in to a local variable to be
      both returned and stored in a member.

      @code
      Rv<int> zret;
      int value;
      // ... complex computations, result in value
      this->m_value = zret = value;
      // ...
      return zret;
      @endcode

      @return A reference to the copy of @a r stored in this object.
  */
  result_type &
  operator=(result_type const &r ///< result_type to assign
            )
  {
    _result = r;
    return _result;
  }

  /** Set the result.

      This differs from assignment of the function result in that the
      return value is a reference to the @c Rv, not the internal
      result. This makes it useful for assigning a result local
      variable and then returning.

   * @param result Value to move.
   * @return @a this

      @code
      Rv<int> func(...) {
        Rv<int> zret;
        int value;
        // ... complex computation, result in value
        return zret.assign(value);
      }
      @endcode
  */
  self_type &assign(result_type const &result);

  /** Move the @a result to @a this.
   *
   * @param result Value to move.
   * @return @a this,
   */
  self_type &assign(result_type &&result);

  /** Return the result.
      @return A reference to the result value in this object.
  */
  result_type &result();

  /** Return the result.
      @return A reference to the result value in this object.
  */
  result_type const &result() const;

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata &errata();

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata const &errata() const;

  /** Get the internal @c Errata.
   *
   * @return Reference to internal @c Errata.
   */
  operator Errata& ();

  /** Replace current status with @a status.
   *
   * @param status Errata to move in to this instance.
   * @return *this
   */
  self_type &operator=(Errata &&status);

  /** Check the status of the return value.
   *
   * @return @a true if the value is valid / OK, @c false otherwise.
   */
  inline bool is_ok() const;

  /// Clear the errata.
  self_type& clear();
};

/** Combine a function result and status in to an @c Rv.
    This is useful for clients that want to declare the status object
    and result independently.
 */
template <typename R>
Rv<typename std::remove_reference<R>::type>
MakeRv(R && r,      ///< The function result
       Errata &&erratum ///< The pre-existing status object
       )
{
  return Rv<typename std::remove_reference<R>::type>(std::forward<R>(r), std::move(erratum));
}
/* ----------------------------------------------------------------------- */
// Inline methods for Annotation

inline Errata::Annotation::Annotation()
{
}

inline Errata::Annotation::Annotation(Severity level, std::string_view text) : _level(level), _text(text)
{
}

inline Errata::Annotation &
Errata::Annotation::clear()
{
  _level = Errata::DEFAULT_SEVERITY;
  _text  = std::string_view{};
  return *this;
}

inline std::string_view
Errata::Annotation::text() const
{
  return _text;
}

inline Errata::Severity
Errata::Annotation::severity() const
{
  return _level;
}

inline Errata::Annotation &
Errata::Annotation::assign(std::string_view text)
{
  _text = text;
  return *this;
}

inline Errata::Annotation &
Errata::Annotation::assign(Severity level)
{
  _level = level;
  return *this;
}

inline auto Errata::Annotation::Linkage::next_ptr(self_type * note) -> self_type *& {
  return note->_link._next;
}

inline auto Errata::Annotation::Linkage::prev_ptr(self_type * note) -> self_type *& {
  return note->_link._prev;
}

/* ----------------------------------------------------------------------- */
// Inline methods for Errata::Data

inline Errata::Data::Data(MemArena && arena) {
  _arena = std::move(arena);
}

inline ts::MemSpan
Errata::Data::remnant() {
  return _arena.remnant();
}

inline ts::MemSpan
Errata::Data::alloc(size_t n) {
  return _arena.alloc(n);
}

inline bool
Errata::Data::empty() const {
  return _notes.empty();
}

/* ----------------------------------------------------------------------- */
// Inline methods for Errata

inline Errata::Errata()
{
}

inline Errata::operator bool() const
{
  return this->is_ok();
}

inline const Errata::Annotation &
Errata::front() const {
  return *(_data->_notes.head());
}

inline Errata &
Errata::note(std::string_view text)
{
  this->note(DEFAULT_SEVERITY, text);
  return *this;
}

template <typename... Args>
Errata &
Errata::note(Severity level, std::string_view fmt, Args &&... args) {
  return this->note_v(level, fmt, std::forward_as_tuple(args...));
}

inline Errata &
Errata::note(self_type && that) {
  this->note(that);
  that.clear();
  return *this;
}

template <typename... Args>
Errata &
Errata::note_v(Severity level, std::string_view fmt, std::tuple<Args...> const&args)
{
  Data * data = this->writeable_data();
  MemSpan span{data->remnant()};
  FixedBufferWriter bw{span};
  if (bw.printv(fmt, args).error()) {
    // Not enough space, get a big enough chunk and do it again.
    span = this->alloc(bw.extent());
    FixedBufferWriter{span}.printv(fmt, args);
  } else {
    data->alloc(bw.extent()); // reserve the part of the remnant actually used.
  }
  this->note_localized(level, span);
  return *this;
}

inline void Errata::SinkWrapper::operator()(Errata const &e) const
{
  _f(e);
}
/* ----------------------------------------------------------------------- */
// Inline methods for Rv

template < typename R > inline bool
Rv<R>::is_ok() const
{
  return std::get<ERRATA>(*this).is_ok();
}

template < typename R > inline auto
Rv<R>::clear() -> self_type &
{
  std::get<ERRATA>(*this).clear();
}

template <typename T> Rv<T>::Rv()
{
}

template <typename T> Rv<T>::Rv(result_type const &r) : super_type(r, Errata())
{
}

template <typename T> Rv<T>::Rv(result_type const &r, Errata && errata) : super_type(r, std::move(errata))
{
}

template <typename T> Rv<T>::Rv(result_type const &r, const Errata & errata) : super_type(r, errata)
{
}

template <typename R> Rv<R>::Rv(R &&r) : super_type(std::move(r), Errata())
{
}

template <typename R> Rv<R>::Rv(R &&r, Errata &&errata) : super_type(std::move(r), std::move(errata))
{
}

template <typename R> Rv<R>::Rv(R &&r, const Errata &errata) : super_type(std::move(r), errata)
{
}

template <typename T> Rv<T>::operator result_type const &() const
{
  return std::get<RESULT>(*this);
}

template <typename T>
T const &
Rv<T>::result() const
{
  return std::get<RESULT>(*this);
}

template <typename T>
T &
Rv<T>::result()
{
  return std::get<RESULT>(*this);
}

template <typename T>
Errata const &
Rv<T>::errata() const
{
  return std::get<ERRATA>(*this);
}

template <typename T>
Errata &
Rv<T>::errata()
{
  return std::get<ERRATA>(*this);
}

template < typename T > Rv<T>::operator Errata&() { return std::get<ERRATA>(*this); }

template <typename T>
Rv<T> &
Rv<T>::assign(result_type const &r)
{
  std::get<RESULT>(*this) = r;
  return *this;
}

template <typename R>
Rv<R> &
Rv<R>::assign(R &&r)
{
  std::get<RESULT>(*this) = std::forward<R>(r);
  return *this;
}

template <typename R>
auto
Rv<R>::operator=(Errata &&errata) -> self_type &
{
  std::get<ERRATA>(*this) = std::move(errata);
  return *this;
}

template <typename R>
auto
Rv<R>::note(Severity level, std::string_view text) -> self_type &
{
  std::get<ERRATA>(*this).note(level, text);
  return *this;
}

template <typename R>
template <typename... Args>
Rv<R> &
Rv<R>::note(Severity level, std::string_view fmt, Args &&... args)
{
  std::get<ERRATA>(*this).note_v(level, fmt, std::forward_as_tuple(args...));
  return *this;
}

BufferWriter&
bwformat(BufferWriter& w, BWFSpec const& spec, Severity);

BufferWriter&
bwformat(BufferWriter& w, BWFSpec const& spec, Errata::Annotation const&);

BufferWriter&
bwformat(BufferWriter& w, BWFSpec const& spec, Errata const&);

} // namespace ts
