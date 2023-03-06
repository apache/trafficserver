// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
    Stacking error message handling.

    The problem addressed by this library is the ability to pass back detailed error messages from
    failures. It is hard to get good diagnostics because the specific failures and general context
    are located in very different stack frames. This library allows local functions to pass back
    local messages which can be easily augmented as the error travels up the stack frame.

    This aims to improve over exceptions by being lower cost and not requiring callers to handle the
    messages. On the other hand, the messages could be used just as easily with exceptions.

    Each message on a stack contains text and a numeric identifier. The identifier value zero is
    reserved for messages that are not errors so that information can be passed back even in the
    success case.

    The implementation takes the position that success must be fast and failure is expensive.
    Therefore Errata is optimized for the success path, imposing very little overhead in that case.
    On the other hand, if an error occurs and is handled, that is generally so expensive that
    optimizations are pointless (although, of course, code should not be gratuitiously expensive).

    The library provides the @c Rv ("return value") template to make returning values and status
    easier. This template allows a function to return a value and status pair with minimal changes.
    The pair acts like the value type in most situations, while providing access to the status.

    Each instance of an erratum is a wrapper class that emulates value semantics (copy on write).
    This means passing even large message stacks is inexpensive, involving only a pointer copy and
    reference counter increment and decrement. A success value is represented by an internal @c NULL
    so it is even cheaper to copy.

    To further ease use, the library has the ability to define @a sinks.  A sink is a function that
    acts on an erratum when it becomes unreferenced. The intended use is to send the messages to an
    output log. This makes reporting errors to a log from even deeply nested functions easy while
    preserving the ability of the top level logic to control such logging.
 */

#pragma once

#include <vector>
#include <string_view>
#include <functional>
#include <atomic>
#include <optional>

#include "swoc/swoc_version.h"
#include "swoc/MemSpan.h"
#include "swoc/MemArena.h"
#include "swoc/bwf_base.h"
#include "swoc/IntrusiveDList.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

/** Class to hold a stack of error messages (the "errata"). This is a smart handle class, which
 * wraps the actual data and can therefore be treated a value type with cheap copy semantics.
 * Default construction is very cheap.
 */
class Errata {
public:
  using code_type     = std::error_code; ///< Type for message code.
  using severity_type = uint8_t;         ///< Underlying type for @c Severity.

  /// Severity value for an instance.
  /// This provides conversion to a numeric value, but not from. The result is constructors must be
  /// passed an explicit serverity, avoiding ambiguity with other possible numeric arguments.
  struct Severity {
    severity_type _raw; ///< Severity numeric value

    explicit constexpr Severity(severity_type n) : _raw(n) {} ///< No implicit conversion from numeric.

    Severity(Severity const &that) = default;
    Severity &operator=(Severity const &that) = default;

    operator severity_type() const { return _raw; } ///< Implicit conversion to numeric.
  };

  /// Code used if not specified.
  static inline const code_type DEFAULT_CODE;
  /// Severity used if not specified.
  static Severity DEFAULT_SEVERITY;
  /// Severity level at which the instance is a failure of some sort.
  static Severity FAILURE_SEVERITY;
  /// Minimum severity level for an @c Annotation.
  /// If an @c Annotation is added with an explicit @c Severity that is smaller the @c Annotation is discarded instead of added.
  /// This defaults to zero and no filtering is done unless it is overwritten.
  static Severity FILTER_SEVERITY;

  /// Mapping of severity to string.
  /// Values larger than the span size will be rendered as numbers.
  /// Defaults to an empty span, meaning all severities will be printed as integers.
  static MemSpan<TextView const> SEVERITY_NAMES;

  /** An annotation to the Errata consisting of a severity and informative text.
   *
   * The text cannot be changed because of memory ownership risks.
   */
  class Annotation {
    using self_type = Annotation; ///< Self reference type.

  public:
    /// Reset to the message to default state.
    self_type &clear();

    /// Get the text of the message.
    swoc::TextView text() const;

    /// Get the nesting level.
    unsigned short level() const;

    /// Check if @a this has a @c Severity.
    bool has_severity() const;

    /// Retrieve the local severity.
    /// @return The local severity.
    /// @note The behavior is undefined if there is no local severity.
    Severity severity() const;

    /// Retrieve the local severity.
    /// @return The local severity or @a default_severity if none is set.
    Severity severity(Severity default_severity) const;

    /// Set the @a severity of @a this.
    self_type &assign(Severity severity);

  protected:
    std::string_view _text;            ///< Annotation text.
    unsigned short _level{0};          ///< Nesting level for display purposes.
    std::optional<Severity> _severity; ///< Severity.

    /// @{{
    /// Policy and links for intrusive list.
    self_type *_next{nullptr};
    self_type *_prev{nullptr};
    /// @}}
    /// Intrusive list link descriptor.
    /// @note Must explicitly use defaults because ICC and clang consider them inaccessible
    /// otherwise. I consider it a bug in the compiler that a default identical to an explicit
    /// value has different behavior.
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;

    /// Default constructor.
    /// The message has default severity and empty text.
    Annotation();

    /** Construct with @a text.
     *
     * @param text Annotation content (literal).
     * @param severity Local severity.
     * @param level Nesting level.
     *
     * @a text is presumed to be stable for the @c Annotation lifetime - this constructor simply copies
     * the view.
     */
    explicit Annotation(std::string_view text, std::optional<Severity> severity = std::optional<Severity>{},
                        unsigned short level = 0);

    friend class Errata;
    friend class swoc::MemArena; // needed for @c MemArena::make
  };

protected:
  using self_type = Errata; ///< Self reference type.
  /// Storage type for list of messages.
  /// Internally the vector is accessed backwards, in order to make it LIFO.
  using Container = IntrusiveDList<Annotation::Linkage>;

  /// Implementation class.
  struct Data {
    using self_type = Data; ///< Self reference type.

    /// Construct and take ownership of @a arena.
    /// @internal This assumes the instance is being constructed in @a arena and therefore transfers the
    /// @a arena into internal storage so that everything is in the @a arena.
    Data(swoc::MemArena &&arena);

    /// Check if there are any notes.
    bool empty() const;

    /** Duplicate @a src in the arena for this instance.
     *
     * @param src Source data.
     * @return View of copy in this arena.
     */
    std::string_view localize(std::string_view src);

    /// Get the remnant of the current block in the arena.
    swoc::MemSpan<char> remnant();

    /// Allocate from the arena.
    swoc::MemSpan<char> alloc(size_t n);

    Severity _severity{Errata::DEFAULT_SEVERITY}; ///< Severity.
    code_type _code{Errata::DEFAULT_CODE};        ///< Message code / ID
    Container _notes;                             ///< The message stack.
    swoc::MemArena _arena;                        ///< Annotation text storage.
  };

public:
  /// Default constructor - empty errata, very fast.
  Errata()                      = default;
  Errata(self_type const &that) = delete;                          ///< No constant copy construction.
  Errata(self_type &&that) noexcept;                               ///< Move constructor.
  self_type &operator=(self_type const &that) = delete;            // no copy assignemnt.
  self_type &operator                         =(self_type &&that); ///< Move assignment.
  ~Errata();                                                       ///< Destructor.

  // Note based constructors.
  explicit Errata(Severity severity);
  Errata(code_type const &type, Severity severity, std::string_view const &text);
  Errata(Severity severity, std::string_view const &text);
  Errata(code_type const &type, std::string_view const &text);
  explicit Errata(std::string_view const &text);
  template <typename... Args> Errata(code_type const &type, Severity severity, std::string_view fmt, Args &&... args);
  template <typename... Args> Errata(code_type const &type, std::string_view fmt, Args &&... args);
  template <typename... Args> Errata(Severity severity, std::string_view fmt, Args &&... args);
  template <typename... Args> explicit Errata(std::string_view fmt, Args &&... args);

  /** Add an @c Annotation to the top with @a text.
   * @param text Text of the message.
   * @return *this
   *
   * The error code is set to the default.
   * @a text is localized to @a this and does not need to be persistent.
   */
  self_type &note(std::string_view text);

  /** Add an @c Annotation to the top with @a text and local @a severity.
   * @param severity The local severity.
   * @param text Text of the message.
   * @return *this
   *
   * The error code is set to the default.
   * @a text is localized to @a this and does not need to be persistent.
   * The severity is updated to @a severity if the latter is more severe.
   */
  self_type &note(Severity severity, std::string_view text);

  /** Add an @c Annotation to the top based on error code @a code.
   * @param code Error code.
   * @return *this
   *
   * The annotation text is constructed as the short, long, and numeric value of @a code.
   */
  self_type &note(code_type const &code);

  /** Append an @c Annotation to the top based on error code @a code with @a severity.
   * @param severity Local severity.
   * @param code Error code.
   * @return *this
   *
   * The annotation text is constructed as the short, long, and numeric value of @a code.
   */
  self_type &note(code_type const &code, Severity severity);

  /** Append an @c Annotation to the top based with optional @a severity.
   * @param severity Local severity.
   * @param text Annotation text.
   * @return *this
   *
   * This is a unified interface for other fixed text @c note methods which all forward to this
   * method. If @a severity does not have a value then the annotation is never filtered.
   *
   * The severity is updated to @a severity if the latter is set and more severe.
   *
   * @see FILTER_SEVERITY
   */
  self_type &note_s(std::optional<Severity> severity, std::string_view text);

  /** Append an @c Annotation.
   * @param fmt Format string (@c BufferWriter style).
   * @param args Arguments for values in @a fmt.
   *
   *  @return A reference to this object.
   */
  template <typename... Args> self_type &note(std::string_view fmt, Args &&... args);

  /** Append an @c Annotation.
   * @param fmt Format string (@c BufferWriter style).
   * @param args Arguments for values in @a fmt.
   * @return A reference to this object.
   *
   * This is intended for use by external "helper" methods that pass their own arguments to this
   * using @c forward_as_tuple.
   */
  template <typename... Args> self_type &note_v(std::string_view fmt, std::tuple<Args...> const &args);

  /** Append an @c Annotation.
   * @param severity Local severity.
   * @param fmt Format string (@c BufferWriter style).
   * @param args Arguments for values in @a fmt.
   * @return A reference to this object.
   *
   * The severity is updated to @a severity if the latter is more severe.
   */
  template <typename... Args> self_type &note(Severity severity, std::string_view fmt, Args &&... args);

  /** Append an @c Annotation.
   * @param severity Local severity.
   * @param fmt Format string (@c BufferWriter style).
   * @param args Arguments for values in @a fmt.
   * @return A reference to this object.
   *
   * This is intended for use by external "helper" methods that pass their own arguments to this
   * using @c forward_as_tuple.
   *
   * The severity is updated to @a severity if the latter is more severe.
   */
  template <typename... Args> self_type &note_v(Severity severity, std::string_view fmt, std::tuple<Args...> const &args);

  /** Append an @c Annotation.
   * @param severity Local severity.
   * @param fmt Format string (@c BufferWriter style).
   * @param args Arguments for values in @a fmt.
   * @return A reference to this object.
   *
   * The severity is updated to @a severity if the latter is set and more severe.
   *
   * This the effective implementation method for all variadic styles of the @a note method.
   */
  template <typename... Args>
  self_type &note_sv(std::optional<Severity> severity, std::string_view fmt, std::tuple<Args...> const &args);

  /** Copy messages from @a that to @a this.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   *
   * The code and severity of @a that are discarded.
   */
  self_type &note(self_type const &that);

  /** Copy messages from @a that to @a this, then clear @a that.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   *
   * The code and severity of @a that are discarded.
   */
  self_type &note(self_type &&that);

  /** Reset to default state.
   *
   * @return @a this
   *
   * All messages are discarded and the state is returned to success.
   */
  self_type &clear();

  /** Log and clear @a this.
   *
   * @return @a this
   *
   * The content is sent to the defined @c Sink instances then reset to the default state.
   *
   * @see register_sink
   * @see clear
   */
  self_type & sink();

  friend std::ostream &operator<<(std::ostream &, self_type const &);

  /// Default glue value (a newline) for text rendering.
  static std::string_view DEFAULT_GLUE;

  /** Test status.

      Equivalent to @c is_ok but more convenient for use in
      control statements.

   *  @return @c false at least one message has a severity of @c FAILURE_SEVERITY or greater, @c
   *  true if not.
   */

  explicit operator bool() const;

  /** Test status.
   *
   * Equivalent to <tt>!is_ok()</tt> but potentially more convenient.
   *
   *  @return @c true at least one message has a severity of @c FAILURE_SEVERITY or greater, @c
   *  false if not.
   */
  bool operator!() const;

  /** Test errata for no failure condition.

      Equivalent to @c operator @c bool but easier to invoke.

      @return @c true if no message has a severity of @c FAILURE_SEVERITY
      or greater, @c false if at least one such message is in the stack.
   */
  bool is_ok() const;

  /** Get the maximum severity of the messages in the erratum.
   *
   * @return Max severity for all messages.
   */
  Severity severity() const;

  /** Set the @a severity.
   *
   * @param severity Severity value.
   * @return @a this
   *
   * @see update
   */
  self_type & assign(Severity severity);

  /** Set the severity.
     *
     * @param severity Minimum severity
     * @return @a this
     *
     * This sets the internal severity to the maximum of @a severity and the current severity.
     *
     * @see assign
   */
  self_type &update(Severity severity);

  /// The code for the top message.
  code_type const &code() const;

  /// Set the @a code for @a this.
  self_type & assign(code_type code);

  /// Number of messages in the errata.
  size_t length() const;

  /// Check for no messages
  /// @return @c true if there is one or messages.
  bool empty() const;

  using iterator       = Container::iterator;
  using const_iterator = Container::const_iterator;

  /// Reference to top item on the stack.
  iterator begin();
  /// Reference to top item on the stack.
  const_iterator begin() const;
  //! Reference one past bottom item on the stack.
  iterator end();
  //! Reference one past bottom item on the stack.
  const_iterator end() const;

  const Annotation &front() const;

  const Annotation &back() const;

  // Logging support.

  /** Base class for erratum sink.

      When an errata is abandoned, this will be called on it to perform any client specific logging.
      It is passed around by handle so that it doesn't have to support copy semantics (and is not
      destructed until application shutdown). Clients can subclass this class in order to preserve
      arbitrary data for the sink or retain a handle to the sink for runtime modifications.
   */
  class Sink {
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
  using SinkHandler = std::function<void(Errata const &)>;

  /// Convenience wrapper class to enable using functions directly for sinks.
  struct SinkWrapper : public Sink {
    /// Constructor.
    SinkWrapper(SinkHandler const& f) : _f(f) {}
    SinkWrapper(SinkHandler && f) : _f(std::move(f)) {}
    /// Operator to invoke the function.
    void operator()(Errata const &e) const override;
    SinkHandler _f; ///< Client supplied handler.
  };

  /// Register a sink function for abandonded erratum.
  static void
  register_sink(SinkHandler const &f) {
    register_sink(Sink::Handle(new SinkWrapper(f)));
  }

  /// Register a sink function for abandonded erratum.
  static void
  register_sink(SinkHandler && f) {
    register_sink(Sink::Handle(new SinkWrapper(std::move(f))));
  }

  /** Simple formatted output.
   */
  std::ostream &write(std::ostream &out) const;

protected:
  /// Construct with code and severity, but no annotations.
  Errata(code_type const &code, Severity severity);

  /// Implementation instance.
  /// @internal Because this is used with a self-containing @c MemArena standard smart pointers do not
  /// work correctly. Instead the @c clear method must be used to release the memory.
  /// @see clear
  Data *_data = nullptr;

  /// Force data existence.
  /// @return A pointer to the data.
  Data *data();

  /** Allocate a span of memory.
   *
   * @param n Number of bytes to allocate.
   * @return A span of the allocated memory.
   */
  MemSpan<char> alloc(size_t n);

  /// Add @c Annotation with already localized text.
  self_type &note_localized(std::string_view const &text, std::optional<Severity> severity = std::optional<Severity>{});

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
template <typename R> class Rv {
public:
  using result_type = R; ///< Type of result value.
  using code_type   = Errata::code_type;
  using Severity    = Errata::Severity;

protected:
  using self_type = Rv; ///< Standard self reference type.

  result_type _r; ///< The result.
  Errata _errata; ///< The errata.

public:
  /** Default constructor.
      The default constructor for @a R is used.
      The status is initialized to SUCCESS.
  */
  Rv();

  /** Construct with copy of @a result and empty (successful) Errata.
   *
   * @param result Result of the operation.
   */
  Rv(result_type const &result);

  /** Construct with copy of @a result and move @a errata.
   *
   * @param result Return value / result.
   * @param errata Source errata to move.
   */
  Rv(result_type const &result, Errata &&errata);

  /** Construct with move of @a result and empty (successful) Errata.
   *
   * @param result The return / result value.
   */
  Rv(result_type &&result);

  /** Construct with a move of result and @a errata.
   *
   * @param result The return / result value to move.
   * @param errata Status to move.
   */
  Rv(result_type &&result, Errata &&errata);

  /** Construct only from @a errata
   *
   * @param errata Errata instance.
   *
   * This is useful for error conditions. The result is default constructed and the @a errata
   * consumed by the return value. If @c result_type is a smart pointer or other cheaply default
   * constructed class this can make the code much cleaner;
   *
   * @code
   * // Assume Thing can be default constructed cheaply.
   * Rv<Thing> func(...) {
   *   if (something_bad) {
   *     return Errata("Bad thing happen!");
   *   }
   *   return Thing{arg1, arg2};
   * }
   * @endcode
   */
  Rv(Errata &&errata);

  /** Append a message in to the result.
   *
   * @param text Text for the error.
   * @return @a *this
   */
  self_type &note(std::string_view text);

  /** Append a message in to the result.
   *
   * @param severity Local severity.
   * @param text Text for the error.
   * @return @a *this
   */
  self_type &note(Severity severity, std::string_view text);

  /** Append a message in to the result.
   *
   * @param code the error code.
   * @return @a *this
   *
   * The annotation text is constructed as the short, long, and numeric value of @a code, which is then discarded.
   */
  self_type &note(code_type const &code);

  /** Append a message in to the result.
   *
   * @param code the error code.
   * @param severity Local severity.
   * @return @a *this
   *
   * The annotation text is constructed as the short, long, and numeric value of @a code, which is then discarded.
   */
  self_type &note(code_type const &code, Severity severity);

  /** Append a message in to the result.
   *
   * @tparam Args Format string argument types.
   * @param fmt Format string.
   * @param args Arguments for @a fmt.
   * @return @a *this
   */
  template <typename... Args> self_type &note(std::string_view fmt, Args &&... args);

  /** Append a message in to the result.
   *
   * @tparam Args Format string argument types.
   * @param severity Local severity.
   * @param fmt Format string.
   * @param args Arguments for @a fmt.
   * @return @a *this
   */
  template <typename... Args> self_type &note(Severity severity, std::string_view fmt, Args &&... args);

  /** Copy messages from @a that to @a this.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   *
   * The code and severity of @a that are discarded.
   */
  self_type &note(Errata const &that);

  /** Copy messages from @a that to @a this, then clear @a that.
   *
   * @param that Source object from which to copy.
   * @return @a *this
   *
   * The code and severity of @a that are discarded.
   */
  self_type &note(Errata &&that);

  /** User conversion to the result type.

      This makes it easy to use the function normally or to pass the result only to other functions
      without having to extract it by hand.
  */
  operator result_type const &() const;

  /** Assignment from result type.

      @param r Result.

      This allows the result to be assigned to a pre-declared return value structure.  The return
      value is a reference to the internal result so that this operator can be chained in other
      assignments to instances of result type. This is most commonly used when the result is
      computed in to a local variable to be both returned and stored in a member.

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
  result_type &operator=(result_type const &r);

  /** Move assign a result @a r to @a this.
   *
   * @param r Result.
   * @return @a r
   */
  result_type &operator=(result_type &&r);

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
  operator Errata &();

  /** Replace current status with @a status.
   *
   * @param status Errata to move in to this instance.
   * @return *this
   *
   * The current @c Errata for @a this is discarded and replaced with @a status.
   */
  self_type &operator=(Errata &&status);

  /** Check the status of the return value.
   *
   * @return @a true if the value is valid / OK, @c false otherwise.
   */
  inline bool is_ok() const;

  /// Clear the errata.
  self_type &clear();
};

/** Combine a function result and status in to an @c Rv.
    This is useful for clients that want to declare the status object
    and result independently.
 */
template <typename R>
Rv<typename std::remove_reference<R>::type>
MakeRv(R &&r,           ///< The function result
       Errata &&erratum ///< The pre-existing status object
) {
  return Rv<typename std::remove_reference<R>::type>(std::forward<R>(r), std::move(erratum));
}
/* ----------------------------------------------------------------------- */
// Inline methods for Annotation

inline Errata::Annotation::Annotation() = default;

inline Errata::Annotation::Annotation(std::string_view text, std::optional<Severity> severity, unsigned short level)
  : _text(text), _level(level), _severity(severity) {}

inline Errata::Annotation &
Errata::Annotation::clear() {
  _text = std::string_view{};
  return *this;
}

inline swoc::TextView
Errata::Annotation::text() const {
  return _text;
}

inline unsigned short
Errata::Annotation::level() const {
  return _level;
}

inline bool
Errata::Annotation::has_severity() const {
  return _severity.has_value();
}
inline auto
Errata::Annotation::severity() const -> Severity {
  return *_severity;
}
inline auto
Errata::Annotation::severity(Errata::Severity default_severity) const -> Severity {
  return _severity.value_or(default_severity);
}
inline auto
Errata::Annotation::assign(Errata::Severity severity) -> self_type & {
  _severity = severity;
  return *this;
}

/* ----------------------------------------------------------------------- */
// Inline methods for Errata::Data

inline Errata::Data::Data(MemArena &&arena) {
  _arena = std::move(arena);
}

inline swoc::MemSpan<char>
Errata::Data::remnant() {
  return _arena.remnant().rebind<char>();
}

inline swoc::MemSpan<char>
Errata::Data::alloc(size_t n) {
  return _arena.alloc(n).rebind<char>();
}

inline bool
Errata::Data::empty() const {
  return _notes.empty();
}

/* ----------------------------------------------------------------------- */
// Inline methods for Errata

inline Errata::Errata(self_type &&that) noexcept {
  std::swap(_data, that._data);
}

inline Errata::Errata(Severity severity) {
  this->data()->_severity = severity;
}

inline Errata::Errata(const code_type &code, Severity severity) {
  auto d       = this->data();
  d->_severity = severity;
  d->_code     = code;
}

inline Errata::Errata(const code_type &type, Severity severity, const std::string_view &text) : Errata(type, severity) {
  this->note(text);
}

inline Errata::Errata(const std::string_view &text) : Errata(DEFAULT_CODE, DEFAULT_SEVERITY, text) {}
inline Errata::Errata(code_type const &code, const std::string_view &text) : Errata(code, DEFAULT_SEVERITY, text) {}
inline Errata::Errata(Severity severity, const std::string_view &text) : Errata(DEFAULT_CODE, severity, text) {}

template <typename... Args>
Errata::Errata(code_type const &code, Severity severity, std::string_view fmt, Args &&... args) : Errata(code, severity) {
  this->note_v(fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata::Errata(code_type const &code, std::string_view fmt, Args &&... args)
  : Errata(code, DEFAULT_SEVERITY, fmt, std::forward<Args>(args)...) {}
template <typename... Args>
Errata::Errata(Severity severity, std::string_view fmt, Args &&... args)
  : Errata(DEFAULT_CODE, severity, fmt, std::forward<Args>(args)...) {}
template <typename... Args>
Errata::Errata(std::string_view fmt, Args &&... args) : Errata(DEFAULT_CODE, DEFAULT_SEVERITY, fmt, std::forward<Args>(args)...) {}

inline Errata&
Errata::clear() {
  if (_data) {
    _data->~Data(); // destructs the @c MemArena in @a _data which releases memory.
    _data = nullptr;
  }
  return *this;
}

inline auto
Errata::operator=(self_type &&that) -> self_type & {
  if (this != &that) {
    this->clear();
    std::swap(_data, that._data);
  }
  return *this;
}

inline Errata::operator bool() const {
  return this->is_ok();
}

inline bool
Errata::operator!() const {
  return !this->is_ok();
}

inline bool
Errata::empty() const {
  return _data == nullptr || _data->_notes.count() == 0;
}

inline auto
Errata::code() const -> code_type const & {
  return this->empty() ? DEFAULT_CODE : _data->_code;
}

inline auto Errata::assign(code_type code) -> self_type & {
  this->data()->_code = code;
  return *this;
}

inline auto
Errata::severity() const -> Severity {
  return _data ? _data->_severity : DEFAULT_SEVERITY;
}

inline auto Errata::assign(Severity severity) -> self_type & {
  this->data()->_severity = severity;
  return *this;
}

inline auto Errata::update(Severity severity) -> self_type & {
  if (_data) {
    _data->_severity = std::max(_data->_severity, severity);
  } else {
    this->assign(severity);
  }
  return *this;
}

inline size_t
Errata::length() const {
  return _data ? _data->_notes.count() : 0;
}

inline bool
Errata::is_ok() const {
  return this->empty() || _data->_severity < FAILURE_SEVERITY;
}

inline const Errata::Annotation &
Errata::front() const {
  return *(_data->_notes.head());
}

inline const Errata::Annotation &
Errata::back() const {
  return *(_data->_notes.tail());
}

inline Errata &
Errata::note(self_type &&that) {
  this->note(that); // no longer an rvalue reference, so no recursion.
  that.clear();
  return *this;
}

inline Errata &
Errata::note(std::string_view text) {
  return this->note_s({}, text);
}

inline Errata &
Errata::note(Severity severity, std::string_view text) {
  return this->note_s(severity, text);
}

template <typename... Args>
Errata &
Errata::note_sv(std::optional<Severity> severity, std::string_view fmt, std::tuple<Args...> const &args) {
  if (severity.has_value()) {
    this->update(*severity);
  }

  if (!severity.has_value() || *severity >= FILTER_SEVERITY) {
    Data *data = this->data();
    auto span  = data->remnant();
    FixedBufferWriter bw{span};
    if (!bw.print_v(fmt, args).error()) {
      span = span.prefix(bw.extent());
      data->alloc(bw.extent()); // require the part of the remnant actually used.
    } else {
      // Not enough space, get a big enough chunk and do it again.
      span = this->alloc(bw.extent());
      FixedBufferWriter{span}.print_v(fmt, args);
    }
    this->note_localized(TextView(span), severity);
  }
  return *this;
}

template <typename... Args>
Errata &
Errata::note_v(std::string_view fmt, std::tuple<Args...> const &args) {
  return this->note_sv({}, fmt, args);
}

template <typename... Args>
Errata &
Errata::note_v(Severity severity, std::string_view fmt, std::tuple<Args...> const &args) {
  return this->note_sv(severity, fmt, args);
}

template <typename... Args>
Errata &
Errata::note(std::string_view fmt, Args &&... args) {
  return this->note_sv({}, fmt, std::forward_as_tuple(args...));
}

template <typename... Args>
Errata &
Errata::note(Severity severity, std::string_view fmt, Args &&... args) {
  return this->note_sv(severity, fmt, std::forward_as_tuple(args...));
}

inline Errata::iterator
Errata::begin() {
  return _data ? _data->_notes.begin() : iterator();
}

inline Errata::const_iterator
Errata::begin() const {
  return _data ? _data->_notes.begin() : const_iterator();
}

inline Errata::iterator
Errata::end() {
  return _data ? _data->_notes.end() : iterator();
}

inline Errata::const_iterator
Errata::end() const {
  return _data ? _data->_notes.end() : const_iterator();
}

inline void
Errata::SinkWrapper::operator()(Errata const &e) const {
  _f(e);
}
/* ----------------------------------------------------------------------- */
// Inline methods for Rv

template <typename R>
inline auto
Rv<R>::note(std::string_view text) -> self_type & {
  _errata.note(text);
  return *this;
}

template <typename R>
inline auto
Rv<R>::note(Severity severity, std::string_view text) -> self_type & {
  _errata.note(severity, text);
  return *this;
}

template <typename R>
auto
Rv<R>::note(const code_type &code) -> self_type & {
  _errata.note(code);
  return *this;
}

template <typename R>
auto
Rv<R>::note(const code_type &code, Severity severity) -> self_type & {
  _errata.note(code, severity);
  return *this;
}

template <typename R>
template <typename... Args>
auto
Rv<R>::note(std::string_view fmt, Args &&... args) -> self_type & {
  _errata.note(fmt, std::forward<Args>(args)...);
  return *this;
}

template <typename R>
template <typename... Args>
auto
Rv<R>::note(Severity severity, std::string_view fmt, Args &&... args) -> self_type & {
  _errata.note(severity, fmt, std::forward<Args>(args)...);
  return *this;
}

template <typename R>
bool
Rv<R>::is_ok() const {
  return _errata.is_ok();
}

template <typename R>
auto
Rv<R>::clear() -> self_type & {
  errata().clear();
  return *this;
}

template <typename T> Rv<T>::Rv() {}

template <typename T> Rv<T>::Rv(result_type const &r) : _r(r) {}

template <typename T> Rv<T>::Rv(result_type const &r, Errata &&errata) : _r(r), _errata(std::move(errata)) {}

template <typename R> Rv<R>::Rv(R &&r) : _r(std::move(r)) {}

template <typename R> Rv<R>::Rv(R &&r, Errata &&errata) : _r(std::move(r)), _errata(std::move(errata)) {}

template <typename R> Rv<R>::Rv(Errata &&errata) : _errata{std::move(errata)} {}

template <typename T> Rv<T>::operator result_type const &() const {
  return _r;
}

template <typename R>
R const &
Rv<R>::result() const {
  return _r;
}

template <typename R>
R &
Rv<R>::result() {
  return _r;
}

template <typename R>
Errata const &
Rv<R>::errata() const {
  return _errata;
}

template <typename R>
Errata &
Rv<R>::errata() {
  return _errata;
}

template <typename R> Rv<R>::operator Errata &() {
  return _errata;
}

template <typename R>
Rv<R> &
Rv<R>::assign(result_type const &r) {
  _r = r;
  return *this;
}

template <typename R>
Rv<R> &
Rv<R>::assign(R &&r) {
  _r = std::move(r);
  return *this;
}

template <typename R>
auto
Rv<R>::operator=(Errata &&errata) -> self_type & {
  _errata = std::move(errata);
  return *this;
}

template <typename R>
inline Rv<R> &
Rv<R>::note(Errata const &that) {
  this->_errata.note(that);
  return *this;
}

template <typename R>
inline Rv<R> &
Rv<R>::note(Errata &&that) {
  this->_errata.note(std::move(that));
  that.clear();
  return *this;
}

template <typename R>
auto
Rv<R>::operator=(result_type const &r) -> result_type & {
  _r = r;
  return _r;
}

template <typename R>
auto
Rv<R>::operator=(result_type &&r) -> result_type & {
  _r = std::move(r);
  return _r;
}

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Errata::Severity);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Errata::Annotation const &);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, Errata const &);

}} // namespace swoc::SWOC_VERSION_NS

// Tuple / structured binding support.
namespace std {
/// @cond INTERNAL_DETAIL
template <size_t IDX, typename R> class tuple_element<IDX, swoc::Rv<R>> { static_assert("swoc:Rv tuple index out of range"); };

template <typename R> class tuple_element<0, swoc::Rv<R>> {
public:
  using type = typename swoc::Rv<R>::result_type;
};

template <typename R> class tuple_element<1, swoc::Rv<R>> {
public:
  using type = swoc::Errata;
};

template <typename R> class tuple_size<swoc::Rv<R>> : public std::integral_constant<size_t, 2> {};
/// @endcond
} // namespace std

namespace swoc { inline namespace SWOC_VERSION_NS {
// Not sure how much of this is needed, but experimentally all of these were needed in one
// use case or another of structured binding. I wasn't able to make this work if this was
// defined in namespace @c std. Also, because functions can't be partially specialized, it is
// necessary to use @c constexpr @c if to handle the cases. This should roll up nicely when
// compiled.

/// @cond INTERNAL_DETAIL
template <size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type &
get(swoc::Rv<R> &&rv) {
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
}

template <size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type &
get(swoc::Rv<R> &rv) {
  static_assert(0 <= IDX && IDX <= 1, "Errata tuple index out of range (0..1)");
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
  // Shouldn't need this due to the @c static_assert but the Intel compiler requires it.
  throw std::domain_error("Errata index value out of bounds");
}

template <size_t IDX, typename R>
typename std::tuple_element<IDX, swoc::Rv<R>>::type const &
get(swoc::Rv<R> const &rv) {
  static_assert(0 <= IDX && IDX <= 1, "Errata tuple index out of range (0..1)");
  if constexpr (IDX == 0) {
    return rv.result();
  } else if constexpr (IDX == 1) {
    return rv.errata();
  }
  // Shouldn't need this due to the @c static_assert but the Intel compiler requires it.
  throw std::domain_error("Errata index value out of bounds");
}
/// @endcond
}} // namespace swoc::SWOC_VERSION_NS
