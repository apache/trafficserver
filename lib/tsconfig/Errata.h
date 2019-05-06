# if !defined TS_ERRATA_HEADER
# define TS_ERRATA_HEADER

/** @file
    Stacking error message handling.

    The problem addressed by this library is the ability to pass back
    detailed error messages from failures. It is hard to get good
    diagnostics because the specific failures and general context are
    located in very different stack frames. This library allows local
    functions to pass back local messages which can be easily
    augmented as the error travels up the stack frame.

    This could be done with exceptions but
    - That is more effort to implemention
    - Generally more expensive.

    Each message on a stack contains text and a numeric identifier.
    The identifier value zero is reserved for messages that are not
    errors so that information can be passed back even in the success
    case.

    The implementation takes the position that success is fast and
    failure is expensive. Therefore it is optimized for the success
    path, imposing very little overhead. On the other hand, if an
    error occurs and is handled, that is generally so expensive that
    optimizations are pointless (although, of course, one should not
    be gratuitiously expensive).

    The library also provides the @c Rv ("return value") template to
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

# include <memory>
# include <string>
# include <iosfwd>
# include <sstream>
# include <deque>
# include "NumericType.h"
# include "IntrusivePtr.h"

namespace ts {

/** Class to hold a stack of error messages (the "errata").
    This is a smart handle class, which wraps the actual data
    and can therefore be treated a value type with cheap copy
    semantics. Default construction is very cheap.
 */
class Errata {
protected:
    /// Implementation class.
    struct Data;
    /// Handle for implementation class instance.
    typedef IntrusivePtr<Data> ImpPtr;
public:
    typedef Errata self; /// Self reference type.

    /// Message ID.
    typedef NumericType<unsigned int, struct MsgIdTag> Id;

    /* Tag / level / code severity.
       This is intended for clients to use to provide additional
       classification of a message. A severity code, as for syslog,
       is a common use.

    */
    typedef NumericType<unsigned int, struct CodeTag> Code;
    struct Message;

    typedef std::deque<Message> Container; ///< Storage type for messages.
    // We iterate backwards to look like a stack.
//    typedef Container::reverse_iterator iterator; ///< Message iteration.
    /// Message const iteration.
//    typedef Container::const_reverse_iterator const_iterator;
    /// Reverse message iteration.
//    typedef Container::iterator reverse_iterator;
    /// Reverse constant message iteration.
//    typedef Container::const_iterator const_reverse_iterator;

    /// Default constructor - empty errata, very fast.
    Errata();
    /// Copy constructor, very fast.
    Errata (
        self const& that ///< Object to copy
    );
    /// Construct from string.
    /// Message Id and Code are default.
    explicit Errata(
        std::string const& text ///< Finalized message text.
    );
    /// Construct with @a id and @a text.
    /// Code is default.
    Errata(
      Id id, ///< Message id.
      std::string const& text ///< Message text.
    );
    /// Construct with @a id, @a code, and @a text.
    Errata(
      Id id, ///< Message text.
      Code code, ///< Message code.
      std::string const& text ///< Message text.
    );
    /** Construct from a message instance.
        This is equivalent to default constructing an @c errata and then
        invoking @c push with an argument of @a msg.
    */
    Errata(
      Message const& msg ///< Message to push
    );

    /// Move constructor.
    Errata(self && that);
    /// Move constructor from @c Message.
    Errata(Message && msg);

    /// destructor
    ~Errata();

    /// Self assignment.
    /// @return A reference to this object.
    self& operator = (
      const self& that ///< Source instance.
    );

    /// Move assignment.
    self& operator = (self && that);

    /** Assign message.
        All other messages are discarded.
        @return A reference to this object.
    */
    self& operator = (
      Message const& msg ///< Source message.
    );

    /** Push @a text as a message.
        The message is constructed from just the @a text.
        It becomes the top message.
        @return A reference to this object.
    */
    self& push(std::string const& text);
    /** Push @a text as a message with message @a id.
        The message is constructed from @a text and @a id.
        It becomes the top message.
        @return A reference to this object.
    */
    self& push(Id id, std::string const& text);
    /** Push @a text as a message with message @a id and @a code.
        The message is constructed from @a text and @a id.
        It becomes the top message.
        @return A reference to this object.
    */
    self& push(Id id, Code code, std::string const& text);
    /** Push a message.
        @a msg becomes the top message.
        @return A reference to this object.
    */
    self& push(Message const& msg);
    self& push(Message && msg);

    /** Push a constructed @c Message.
	The @c Message is set to have the @a id and @a code. The other arguments are converted
	to strings and concatenated to form the messsage text.
	@return A reference to this object.
    */
    template < typename ... Args >
      self& push(Id id, Code code, Args const& ... args);

    /** Push a nested status.
        @a err becomes the top item.
        @return A reference to this object.
    */
    self& push(self const& err);

    /** Access top message.
        @return If the errata is empty, a default constructed message
        otherwise the most recent message.
     */
    Message const& top() const;

    /** Move messages from @a that to @c this errata.
        Messages from @a that are put on the top of the
        stack in @c this and removed from @a that.
    */
    self& pull(self& that);

    /// Remove last message.
    void pop();

    /// Remove all messages.
    void clear();

    /** Inhibit logging.
        @note This only affects @c this as a top level @c errata.
        It has no effect on this @c this being logged as a nested
        @c errata.
    */
    self& doNotLog();

    friend std::ostream& operator<< (std::ostream&, self const&);

    /// Default glue value (a newline) for text rendering.
    static std::string const DEFAULT_GLUE;

    /** Test status.

        Equivalent to @c success but more convenient for use in
        control statements.

        @return @c true if no messages or last message has a zero
        message ID, @c false otherwise.
     */
    operator bool() const;

    /** Test errata for no failure condition.

        Equivalent to @c operator @c bool but easier to invoke.

        @return @c true if no messages or last message has a zero
        message ID, @c false otherwise.
     */
    bool isOK() const;

    /// Number of messages in the errata.
    size_t size() const;

    /*  Forward declares.
        We have to make our own iterators as the least bad option. The problem
        is that we have recursive structures so declaration order is difficult.
        We can't use the container iterators here because the element type is
        not yet defined. If we define the element type here, it can't contain
        an Errata and we have to do funky things to get around that. So we
        have our own iterators, which are just shadowing sublclasses of the
        container iterators.
     */
    class iterator;
    class const_iterator;

    /// Reference to top item on the stack.
    iterator begin();
    /// Reference to top item on the stack.
    const_iterator begin() const;
    //! Reference one past bottom item on the stack.
    iterator end();
    //! Reference one past bottom item on the stack.
    const_iterator end() const;

    // Logging support.

    /** Base class for erratum sink.
        When an errata is abandoned, this will be called on it to perform
        any client specific logging. It is passed around by handle so that
        it doesn't have to support copy semantics (and is not destructed
        until application shutdown). Clients can subclass this class in order
        to preserve arbitrary data for the sink or retain a handle to the
        sink for runtime modifications.
     */
    class Sink : public IntrusivePtrCounter {
    public:
        typedef Sink self; ///< Self reference type.
        typedef IntrusivePtr<self> Handle;  ///< Handle type.

        /// Handle an abandoned errata.
        virtual void operator() (Errata const&) const = 0;
        /// Force virtual destructor.
        virtual ~Sink() {}
    };

    //! Register a sink for discarded erratum.
    static void registerSink(Sink::Handle const& s);

    /// Register a function as a sink.
    typedef void (*SinkHandlerFunction)(Errata const&);

    // Wrapper class to support registering functions as sinks.
    struct SinkFunctionWrapper : public Sink {
        /// Constructor.
        SinkFunctionWrapper(SinkHandlerFunction f) : m_f(f) { }
        /// Operator to invoke the function.
        void operator() (Errata const& e) const override { m_f(e); }
        SinkHandlerFunction m_f; ///< Client supplied handler.
    };

    /// Register a sink function for abandonded erratum.
    static void registerSink(SinkHandlerFunction f) {
        registerSink(Sink::Handle(new SinkFunctionWrapper(f)));
    }

    /** Simple formatted output.

        Each message is written to a line. All lines are indented with
        whitespace @a offset characters. Lines are indented an
        additional @a indent. This value is increased by @a shift for
        each level of nesting of an @c Errata. if @a lead is not @c
        NULL the indentation is overwritten by @a lead if @a indent is
        non-zero. It acts as a "continuation" marker for nested
        @c Errata.

     */
    std::ostream& write(
      std::ostream& out, ///< Output stream.
      int offset, ///< Lead white space for every line.
      int indent, ///< Additional indention per line for messages.
      int shift, ///< Additional @a indent for nested @c Errata.
      char const* lead ///< Leading text for nested @c Errata.
    ) const;
    /// Simple formatted output to fixed sized buffer.
    /// @return Number of characters written to @a buffer.
    size_t write(
      char* buffer, ///< Output buffer.
      size_t n, ///< Buffer size.
      int offset, ///< Lead white space for every line.
      int indent, ///< Additional indention per line for messages.
      int shift, ///< Additional @a indent for nested @c Errata.
      char const* lead ///< Leading text for nested @c Errata.
    ) const;

protected:
    /// Construct from implementation pointer.
    /// Used internally by nested classes.
    Errata(ImpPtr const& ptr);
    /// Implementation instance.
    ImpPtr m_data;

    /// Return the implementation instance, allocating and unsharing as needed.
    Data* pre_write();
    /// Force and return an implementation instance.
    /// Does not follow copy on write.
    Data const* instance();

    /// Used for returns when no data is present.
    static Message const NIL_MESSAGE;

    friend struct Data;
    friend class Item;

};

extern std::ostream& operator<< (std::ostream& os, Errata const& stat);

/// Storage for a single message.
struct Errata::Message {
  typedef Message self; ///< Self reference type.

  /// Default constructor.
  /// The message has Id = 0, default code,  and empty text.
  Message() = default;

  /// Construct from text.
  /// Id is zero and Code is default.
  Message(
    std::string const& text ///< Finalized message text.
  );

  /// Construct with @a id and @a text.
  /// Code is default.
  Message(
    Id id, ///< ID of message in table.
    std::string const& text ///< Final text for message.
  );

  /// Construct with @a id, @a code, and @a text.
  Message(
    Id id, ///< Message Id.
    Code code, ///< Message Code.
    std::string const& text ///< Final text for message.
  );

  /// Construct with an @a id, @a code, and a @a message.
  /// The message contents are created by converting the variable arguments
  /// to strings using the stream operator and concatenated in order.
  template < typename ... Args>
    Message(
	    Id id, ///< Message Id.
	    Code code, ///< Message Code.
	    Args const& ... text
	    );

  /// Reset to the message to default state.
  self& clear();

  /// Set the message Id.
  self& set(
    Id id ///< New message Id.
  );

  /// Set the code.
  self& set(
    Code code ///< New code for message.
  );

  /// Set the text.
  self& set(
    std::string const& text ///< New message text.
  );

  /// Set the text.
  self& set(
    char const* text ///< New message text.
  );

  /// Set the errata.
  self& set(
    Errata const& err ///< Errata to store.
  );

  /// Get the text of the message.
  std::string const& text() const;

  /// Get the code.
  Code getCode() const;
  /// Get the nested status.
  /// @return A status object, which is not @c NULL if there is a
  /// nested status stored in this item.
  Errata getErrata() const;

  /** The default message code.

      This value is used as the Code value for constructing and
      clearing messages. It can be changed to control the value
      used for empty messages.
  */
  static Code Default_Code;

  /// Type for overriding success message test.
  typedef bool (*SuccessTest)(Message const& m);

  /** Success message test.

      When a message is tested for being "successful", this
      function is called. It may be overridden by a client.
      The initial value is @c DEFAULT_SUCCESS_TEST.

      @note This is only called when there are Messages in the
      Errata. An empty Errata (@c NULL or empty stack) is always
      a success. Only the @c top Message is checked.

      @return @c true if the message indicates success,
      @c false otherwise.
  */
  static SuccessTest Success_Test;

  /// Indicate success if the message code is zero.
  /// @note Used as the default success test.
  static bool isCodeZero(Message const& m);

  static SuccessTest const DEFAULT_SUCCESS_TEST;

  template < typename ... Args> static std::string stringify(Args const& ... items);

  Id m_id = 0; ///< Message ID.
  Code m_code = Default_Code; ///< Message code.
  std::string m_text; ///< Final text.
  Errata m_errata; ///< Nested errata.
};

/** This is the implementation class for Errata.

    It holds the actual messages and is treated as a passive data
    object with nice constructors.

    We implement reference counting semantics by hand for two
    reasons. One is that we need to do some specialized things, but
    mainly because the client can't see this class so we can't
*/
struct Errata::Data : public IntrusivePtrCounter {
  typedef Data self; ///< Self reference type.

  //! Default constructor.
  Data();

  /// Destructor, to do logging.
  ~Data();

  //! Number of messages.
  size_t size() const;

  /// Get the top message on the stack.
  Message const& top() const;

  /// Put a message on top of the stack.
  void push(Message const& msg);
  void push(Message && msg);

  /// Log this when it is deleted.
  mutable bool m_log_on_delete = true;

  //! The message stack.
  Container m_items;
};

/// Forward iterator for @c Messages in an @c Errata.
class Errata::iterator : public Errata::Container::reverse_iterator {
public:
    typedef iterator self; ///< Self reference type.
    typedef Errata::Container::reverse_iterator super; ///< Parent type.
    iterator(); ///< Default constructor.
    /// Copy constructor.
    iterator(
        self const& that ///< Source instance.
    );
    /// Construct from super class.
    iterator(
        super const& that ///< Source instance.
    );
    /// Assignment.
    self& operator = (self const& that);
    /// Assignment from super class.
    self& operator = (super const& that);
    /// Prefix increment.
    self& operator ++ ();
    /// Prefix decrement.
    self& operator -- ();
};

/// Forward constant iterator for @c Messages in an @c Errata.
class Errata::const_iterator : public Errata::Container::const_reverse_iterator {
public:
    typedef const_iterator self; ///< Self reference type.
    typedef Errata::Container::const_reverse_iterator super; ///< Parent type.
    const_iterator(); ///< Default constructor.
    /// Copy constructor.
    const_iterator(
        self const& that ///< Source instance.
    );
    const_iterator(
        super const& that ///< Source instance.
    );
    /// Assignment.
    self& operator = (self const& that);
    /// Assignment from super class.
    self& operator = (super const& that);
    /// Prefix increment.
    self& operator ++ ();
    /// Prefix decrement.
    self& operator -- ();
};

/** Helper class for @c Rv.
    This class enables us to move the implementation of non-templated methods
    and members out of the header file for a cleaner API.
 */
struct RvBase {
  Errata _errata;    ///< The status from the function.

  /** Default constructor. */
  RvBase();

  /** Construct with specific status.
   */
  RvBase (
    Errata const& s ///< Status to copy
  );

  //! Test the return value for success.
  bool isOK() const;

  /** Clear any stacked errors.
      This is useful during shutdown, to silence irrelevant errors caused
      by the shutdown process.
  */
  void clear();

  /// Inhibit logging of the errata.
  void doNotLog();
};

/** Return type for returning a value and status (errata).  In
    general, a method wants to return both a result and a status so
    that errors are logged properly. This structure is used to do that
    in way that is more usable than just @c std::pair.  - Simpler and
    shorter typography - Force use of @c errata rather than having to
    remember it (and the order) each time - Enable assignment directly
    to @a R for ease of use and compatibility so clients can upgrade
    asynchronously.
 */
template < typename R >
struct Rv : public RvBase {
  typedef Rv self;       ///< Standard self reference type.
  typedef RvBase super; ///< Standard super class reference type.
  typedef R Result; ///< Type of result value.

  Result _result;             ///< The actual result of the function.

  /** Default constructor.
      The default constructor for @a R is used.
      The status is initialized to SUCCESS.
  */
  Rv();

  /** Standard (success) constructor.

      This copies the result and sets the status to SUCCESS.

      @note Not @c explicit so that clients can return just a result
       and have it be marked as SUCCESS.
   */
  Rv(
    Result const& r  ///< The function result
  );

  /** Construct from a result and a pre-existing status object.

      @internal No constructor from just an Errata to avoid
      potential ambiguity with constructing from result type.
   */
  Rv(
    Result const& r,         ///< The function result
    Errata const& s    ///< A pre-existing status object
  );

  /** User conversion to the result type.

      This makes it easy to use the function normally or to pass the
      result only to other functions without having to extract it by
      hand.
  */
  operator Result const& () const;

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
  Result& operator = (
    Result const& r  ///< Result to assign
  ) {
    _result = r;
    return _result;
  }

  /** Add the status from another instance to this one.
      @return A reference to @c this object.
  */
  template < typename U >
  self& push(
    Rv<U> const& that ///< Source of status messages
  );

  /** Set the result.

      This differs from assignment of the function result in that the
      return value is a reference to the @c Rv, not the internal
      result. This makes it useful for assigning a result local
      variable and then returning.

      @code
      Rv<int> zret;
      int value;
      // ... complex computation, result in value
      return zret.set(value);
      @endcode
  */
  self& set(
    Result const& r  ///< Result to store
  );

  /** Return the result.
      @return A reference to the result value in this object.
  */
  Result& result();

  /** Return the result.
      @return A reference to the result value in this object.
  */
  Result const& result() const;

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata& errata();

  /** Return the status.
      @return A reference to the @c errata in this object.
  */
  Errata const& errata() const;

  /// Directly set the errata
  self& operator = (
    Errata const& status ///< Errata to assign.
  );

  /// Push a message on to the status.
  self& push(
    Errata::Message const& msg
  );
};

/** Combine a function result and status in to an @c Rv.
    This is useful for clients that want to declare the status object
    and result independently.
 */
template < typename R > Rv<R>
MakeRv(
  R const& r,          ///< The function result
  Errata const& s      ///< The pre-existing status object
) {
    return Rv<R>(r, s);
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
// Inline methods.
inline Errata::Message::Message(std::string const& text)
  : m_text(text) {
}
inline Errata::Message::Message(Id id, std::string const& text)
  : m_text(text) {
}
inline Errata::Message::Message(Id id, Code code, std::string const& text)
  : m_text(text) {
}
template < typename ... Args>
Errata::Message::Message(Id id, Code code, Args const& ... text)
  : m_id(id), m_code(code), m_text(stringify(text ...))
{
}

inline Errata::Message& Errata::Message::clear() {
  m_id = 0;
  m_code = Default_Code;
  m_text.erase();
  m_errata.clear();
  return *this;
}

inline std::string const& Errata::Message::text() const { return m_text; }
inline Errata::Code Errata::Message::getCode() const { return m_code; }
inline Errata Errata::Message::getErrata() const { return m_errata; }

inline Errata::Message& Errata::Message::set(Id id) {
  m_id = id;
  return *this;
}
inline Errata::Message& Errata::Message::set(Code code) {
  m_code = code;
  return *this;
}
inline Errata::Message& Errata::Message::set(std::string const& text) {
  m_text = text;
  return *this;
}
inline Errata::Message& Errata::Message::set(char const* text) {
  m_text = text;
  return *this;
}
inline Errata::Message& Errata::Message::set(Errata const& err) {
  m_errata = err;
  m_errata.doNotLog();
  return *this;
}

template < typename ... Args>
std::string Errata::Message::stringify(Args const& ... items)
{
  std::ostringstream s;
  (void)(int[]){0, ( (s << items) , 0 ) ... };
  return s.str();
}

inline Errata::Errata() {}
inline Errata::Errata(Id id, Code code, std::string const& text) {
  this->push(Message(id, code, text));
}
inline Errata::Errata(Message const& msg) {
  this->push(msg);
}
inline Errata::Errata(Message && msg) {
  this->push(std::move(msg));
}

inline Errata::operator bool() const { return this->isOK(); }

inline size_t Errata::size() const {
  return m_data ? m_data->m_items.size() : 0;
}

inline bool Errata::isOK() const {
  return nullptr == m_data
    || 0 == m_data->size()
    || Message::Success_Test(this->top())
    ;
}

inline Errata&
Errata::push(std::string const& text) {
  this->push(Message(text));
  return *this;
}

inline Errata&
Errata::push(Id id, std::string const& text) {
  this->push(Message(id, text));
  return *this;
}

inline Errata&
Errata::push(Id id, Code code, std::string const& text) {
  this->push(Message(id, code, text));
  return *this;
}

template < typename ... Args >
auto Errata::push(Id id, Code code, Args const& ... args) -> self&
{
  this->push(Message(id, code, args ...));
  return *this;
}

inline Errata::Message const&
Errata::top() const {
  return m_data ? m_data->top() : NIL_MESSAGE;
}
inline Errata& Errata::doNotLog() {
  this->instance()->m_log_on_delete = false;
  return *this;
}

inline Errata::Data::Data()  {}
inline size_t Errata::Data::size() const { return m_items.size(); }

inline Errata::iterator::iterator() { }
inline Errata::iterator::iterator(self const& that) : super(that) { }
inline Errata::iterator::iterator(super const& that) : super(that) { }
inline Errata::iterator& Errata::iterator::operator = (self const& that) { this->super::operator = (that); return *this; }
inline Errata::iterator& Errata::iterator::operator = (super const& that) { this->super::operator = (that); return *this; }
inline Errata::iterator& Errata::iterator::operator ++ () { this->super::operator ++ (); return *this; }
inline Errata::iterator& Errata::iterator::operator -- () { this->super::operator -- (); return *this; }

inline Errata::const_iterator::const_iterator() { }
inline Errata::const_iterator::const_iterator(self const& that) : super(that) { }
inline Errata::const_iterator::const_iterator(super const& that) : super(that) { }
inline Errata::const_iterator& Errata::const_iterator::operator = (self const& that) { super::operator = (that); return *this; }
inline Errata::const_iterator& Errata::const_iterator::operator = (super const& that) { super::operator = (that); return *this; }
inline Errata::const_iterator& Errata::const_iterator::operator ++ () { this->super::operator ++ (); return *this; }
inline Errata::const_iterator& Errata::const_iterator::operator -- () { this->super::operator -- (); return *this; }

inline RvBase::RvBase() { }
inline RvBase::RvBase(Errata const& errata) : _errata(errata) { }
inline bool RvBase::isOK() const { return _errata; }
inline void RvBase::clear() { _errata.clear(); }
inline void RvBase::doNotLog() { _errata.doNotLog(); }

template < typename T > Rv<T>::Rv() : _result()  { }
template < typename T > Rv<T>::Rv(Result const& r) : _result(r) { }
template < typename T > Rv<T>::Rv(Result const& r, Errata const& errata)
  : super(errata)
  , _result(r) {
}
template < typename T > Rv<T>::operator Result const&() const {
  return _result;
}
template < typename T > T const& Rv<T>::result() const { return _result; }
template < typename T > T& Rv<T>::result() { return _result; }
template < typename T > Errata const& Rv<T>::errata() const { return _errata; }
template < typename T > Errata& Rv<T>::errata() { return _errata; }
template < typename T > Rv<T>&
Rv<T>::set(Result const& r) {
  _result = r;
  return *this;
}
template < typename T > Rv<T>&
Rv<T>::operator = (Errata const& errata) {
  _errata = errata;
  return *this;
}
template < typename T > Rv<T>&
Rv<T>::push(Errata::Message const& msg) {
  _errata.push(msg);
  return *this;
}
template < typename T > template < typename U > Rv<T>&
Rv<T>::push(Rv<U> const& that) {
  _errata.push(that.errata());
  return *this;
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
} // namespace ts

# endif // TS_ERRATA_HEADER
