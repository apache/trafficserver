# if ! defined(TS_CONFIG_VALUE_HEADER)
# define TS_CONFIG_VALUE_HEADER

/** @file

    TS Configuration API definition.

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

#include <cstring>
#include "tscore/TsBuffer.h"
#include <tsconfig/NumericType.h>
#include <tsconfig/IntrusivePtr.h>
#include <tsconfig/Errata.h>
#include <vector>

namespace ts { namespace config {

// Forward declares.
class Value;
class Path;

namespace detail {
  /** Class to provide a "pseudo bool" value.
      This is used as the return type for the positive logical operator
      (the converse of @c operator! ). This makes a class directly
      usable in logical expressions. It is like a pointer but @b not
      convertible to anything else, and so avoiding any undesirable
      automatic conversions and the resulting ambiguities.
  */
  struct PseudoBool {
    typedef bool (PseudoBool::*Type)() const; ///< The type itself.
    bool operator ! () const; ///< A method to use for the @c true value.
    static Type const TRUE; ///< The @c true equivalent.
    static Type const FALSE; ///< The @c false equivalent.
  };
}

/// Type of value.
enum ValueType {
  VoidValue, ///< No value, invalid.
  ListValue, ///< List of values.
  GroupValue, ///< Group of values.
  StringValue, ///< Text string.
  IntegerValue, ///< Integer.
  PathValue, ///< Path.
  // Update N_VALUE_TYPES if you change the last enum value !!
};
/// Number of value types.
static size_t const N_VALUE_TYPES = PathValue + 1;

/** A path to a value in a configuration.
 */
class Path {
  friend class Value;
protected:
  class ImplType : public IntrusivePtrCounter {
    friend class Path;
  public:
    ImplType(); ///< Constructor.
  protected:
    /** Container for path elements.
        We are subtle with our elements, which can be either a string
        or a numeric index. By convention, if the pointer in the buffer is
        @c NULL, then the size is a numeric index. Otherwise it's a name.
    */
    typedef std::vector<ConstBuffer> Elements;
    Elements _elements; ///< Path elements.
  };
public:
  typedef Path self; ///< Self reference type.

  Path(); ///< Default constructor.

  /// Append a string tag to the path.
  self& append(
    ConstBuffer const& tag ///< Text of tag.
  );
  /// Append a numeric index to the path.
  self& append(
    size_t idx ///< Index.
  );
  /// Reset to default constructed state.
  self& reset();

  /// Get the number of elements in this path.
  size_t count() const;

  /// Access an element by @a index.
  ConstBuffer const& operator [] (
    size_t index ///< Element index.
  ) const;

  /** Parser for path text.
      This is restartable so a path can be parsed in pieces.
      @internal Sadly, FLEX is just too much overhead to be useful here.
  */
  class Parser {
  public:
    typedef Parser self; ///< Self reference type.

    Parser(); ///< Default constructor.
    /** Construct with input.

        This default constructs the Parser then calls @c setInput with
        @a text. It is provided as a convenience as that will be the
        common use case.

        @see setInput.
    */
    Parser(
      ConstBuffer const& text ///< Input text.
    );

    /** Set the input @a text.
        Parsing state is reset and the next parsing call will
        start at the beginning of @a text.
    */
    self& setInput(
      ConstBuffer const& text ///< Input buffer.
    );

    /// Parsing result.
    enum Result {
      ERROR, ///< Bad input.
      TAG, ///< Path tag.
      INDEX, ///< Path index.
      EOP, ///< End Of Path.
    };

    /** Parse the next element in the path.

        @a cbuff may be @c NULL in which case no data about elements
        is available.  In general this should be called until @c EOP
        or @c ERROR is returned, each call returning the next element.

        @return A parse @c Result.
        - TAG: A tag was found. The start and length are stored in @a cbuff.
        - INDEX: An index was found. The value is in @a cbuff._size.
        - EOP: No more path elements were found. Do not continue parsing.
        - ERROR: A syntax error was encountered. See the errata for detail. Do not continue parsing.
    */
    Rv<Result> parse(
      ConstBuffer* cbuff = nullptr ///< [out] Parsed path element.
    );

    /// Check if input is available.
    bool hasInput() const;

  protected:
    ConstBuffer _input; ///< Current input buffer.
    char const* _c; ///< Next input character.
  };
protected:
  typedef IntrusivePtr<ImplType> ImplPtr; ///< Smart pointer to implementation.
  ImplPtr _ptr; ///< Our instance.
  /// Force an implementation instance and return a pointer to it.
  ImplType* instance();
};

namespace detail {
  /// Null buffer, handy in several places.
  extern Buffer const NULL_BUFFER;
  /// Null buffer, handy in several places.
  extern ConstBuffer const NULL_CONST_BUFFER;
  /// Index type for value items in the global table.
  typedef NumericType<size_t, struct ValueIndexTag> ValueIndex;
  /// Index value that presents NULL (invalid value).
  static ValueIndex const NULL_VALUE_INDEX = static_cast<ValueIndex::raw_type>(-1);
  /// Numeric type for configuration generation.
  typedef NumericType<size_t, struct GenerationTag> Generation;

  /** Value type properties.
      These are used as bit masks on elements of an array.
  */
  static unsigned int const IS_VALID = 1;
  static unsigned int const IS_LITERAL = 1<<1;
  static unsigned int const IS_CONTAINER = 1<<2;

  /// Value type property table.
  extern unsigned int const Type_Property[N_VALUE_TYPES];

  /** A value in the configuration.
      This is used in a global table so it handles all types of Values.
      Members that are not used for scalars are designed to be @c NULL
      pointers in that case.
  */
  class ValueItem {
    // Apparently the C++ standard, 7.3.1.2, states that unqualified
    // friend classes only considers the current namespace, not any
    // outer ones. So we have to fully qualify this. Blech.
    friend class ts::config::Value;
    friend class ValueTable;
  public:
    /// Default constructor.
    ValueItem();
    /// Construct empty item of a specific type.
    ValueItem(ValueType type);
    /// Get item type.
    ValueType getType() const;
  protected:
    ValueType _type;      ///< Type of value.
    ValueIndex _parent = 0;   ///< Table index of parent value.
    ConstBuffer _text;    ///< Text of value (if scalar).
    ConstBuffer _name;    ///< Local name of value, if available.
    size_t _local_index;  ///< Index among siblings.
    int _srcLine;         ///< Source line.
    int _srcColumn;       ///< Source column.

    /// Container for children of this item.
    typedef std::vector<ValueIndex> ChildGroup;
    /// Child items of this item.
    ChildGroup _children;
    /// Path if present.
    Path _path;

    // This is for optimizing named access at some point in the future.
    /// Hold a child item name in a table for fast lookup.
    struct Name {
      ConstBuffer _text; ///< Text of name.
      ValueIndex _index; ///< Index of child.
    };
    /// Container for child names.
    typedef std::vector<Name> NameGroup;
    /** Child names, if appropriate.
        This is faulted in when needed, if this value is an aggregate with
        named children. The list must be sorted on name so that it can be binary
        searched for performance.
    */
    NameGroup _names;
  };

  class ValueTable;

  /** Table of configuration values.
      This holds all the values for a specific configuration.
  */
  class ValueTableImpl : public IntrusivePtrCounter {
    friend class ValueTable;
  public:
    typedef ValueTableImpl self; ///< Self reference type.

    ValueTableImpl(); ///< Constructor.
    ~ValueTableImpl(); ///< Destructor.
  protected:
    /// Container for value items.
    typedef std::vector<ValueItem> ItemTable;
    ItemTable _values; ///< All configuration values.
    Generation _generation; ///< Generation number of configuration.
    /// A group of buffers.
    typedef std::vector<Buffer> BufferGroup;
    /** Locally allocated buffers.
        These are freed when this object is destroyed.
    */
    BufferGroup _buffers;

    static ValueItem NULL_ITEM; ///< Null item for invalid access return.
  };

  /** Wrapper class for a table of configuration values.
      @internal Really, this should be merged in to Configuration. The original
      differences have evolved out of the implementation.
  */
  class ValueTable {
  public:
    typedef ValueTable self; ///< Self reference type.
    typedef ValueTableImpl ImplType; ///< Implementation type.

    /// Table size.
    /// @return The number of value items in the table.
    size_t size() const;
    /// Generation.
    /// @return The generation number.
    Generation generation() const;

    /// Const access by index.
    /// @return The value item at index @a idx.
    ValueItem const& operator [] (
      ValueIndex idx ///< Index of item.
    ) const;
    /// Access by index.
    /// @return The value item at index @a idx.
    ValueItem& operator [] (
      ValueIndex idx ///< Index of item.
    );

    /// Force the existence of the root item in the table.
    /// @return @c this object.
    self& forceRootItem();
    /** Create a new item (value) with optional @a name
        The table must contain @a parent. If @a name is omitted, the item
        has an empty name.
        @return Index of the new value item.
    */
    Rv<ValueIndex> make(
      ValueIndex parent, ///< Index of parent for item.
      ValueType type, ///< Type of item.
      ConstBuffer const& name = NULL_BUFFER ///< Name (may be empty).
    );

    /// Test for not table existence.
    /// @return @c false if the implementation instance exists, @c true if not.
    bool operator ! () const;
    /// Test for table existence.
    /// @return @c true if the implementation instance exists, @c false if not.
    operator PseudoBool::Type() const;
    /// Reset to default constructed state.
    /// @return @c this object.
    self& reset();

    /** Allocate a local buffer.
        This buffer will persist until the implementation instance
        is destoyed.
        @return The allocated buffer.
    */
    Buffer alloc(size_t n);
  protected:
    typedef IntrusivePtr<ImplType> ImplPtr; ///< Smart pointer to implementation instance.
    ImplPtr _ptr; ///< Implementation instance.

    /// Force an implementation instance and return a pointer to it.
    ImplType* instance();
  };
} // namespace detail

/** Container for a configuration.
    This is a wrapper class that holds a shared reference to a configuration.
*/
class Configuration {
  friend class Value;
public:
  typedef Configuration self; ///< Self reference type.

  /** Check if configuration is (not) valid.
      @return @c true if this configuration is invalid, @c false otherwise.
  */
  bool operator ! () const;
  /** Check if the configuration is valid.
      @return The equivalent of @c true if this does @b not contain a value,
      the equivalent of @c false if it does.
  */
  operator detail::PseudoBool::Type () const;
  /** Get the root @c Value of the configuration.
      The root is always a group and has no name.
      @return The root value.
  */
  Value getRoot() const;

  /// Get the number of child values on the root value.
  size_t childCount() const;
  /** Root value child access by @a index
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    size_t idx ///< Index of child value.
  ) const;
  /** Root value child access by @a name.
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    ConstBuffer const& name
  ) const;
  /** Root value child access by @a name.
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    char const* name ///< Null terminated string.
  ) const;

  /** Find a value.
      @return The value if found, an void valid if not.
  */
  Value find(
    char const* path ///< configuration path to value.
  );
  /** Load a configuration from a file.

      @note Check the returned errata for problems during configuration
      load. It is probably not a good idea to use the configuration in
      any error are reported.
      @return A new @c Configuration and errata.
  */
  static Rv<self> loadFromPath(
    char const* path ///< file system path.
  );
  /** Allocate a local buffer of size @a n.
      This buffer will persist until the implementation instance
      is destroyed.
      @return The allocated buffer.
  */
  Buffer alloc(
    size_t n ///< requested size of buffer.
  );
protected:
  detail::ValueTable _table; ///< Table of values from the configuration.
};

/** This holds a value from the configuration.

    @internal It is critical that none of the type specific subclasses define any data members
    so that instances can be freely converted to and from this base class.
*/
class Value {
  friend class Configuration;
public:
  typedef Value self; ///< Self reference type.
  /// Default constructors.
  /// Creates an @c NULL instance.
  Value();
  /// Destructor.
  ~Value();

  /// Get the type of value.
  ValueType getType() const;
  /// Test if this is a valid value.
  /// @return @c true if this contains a value, @c false otherwise.
  bool hasValue() const;
  /** Operator form of @c hasValue.
      @see hasValue
      @return @c true if this does @b not contain a value, @c false if it does.
  */
  bool operator ! () const;
  /** Logical form of @c hasValue for use in logical expressions.
      @see hasValue
      @return The equivalent of @c true if this does @b not contain a value,
      the equivalent of @c false if it does.
  */
  operator detail::PseudoBool::Type () const;

  /** Get the value text.
      @return The text in the configuration file for this item if the item
      is a scalar, an empty buffer otherwise.
  */
  ConstBuffer const& getText() const;
  /// Set the @a text for this value.
  self& setText(
    ConstBuffer const& text
  );

  /** Get local name.
      This gets the local name of the value. That is the name by which it
      is known to its parent container.

      @internal Only works for groups now. It should be made to work
      for lists. This would require allocating strings for each index,
      which should be shared across values. For instance, all values
      at index 1 should return the same string "1", not separately
      allocated for each value.
   */
  ConstBuffer const& getName() const;
  /** Get local index.
      This gets the local index for the value. This is the index which,
      if used on the parent, would yield this value.
      @return The local index.
   */
  size_t getIndex() const;

  /// Test for a literal value.
  /// @return @c true if the value is a literal,
  /// @c false if it is a container or invalid.
  bool isLiteral() const;
  /// Test for value container.
  /// @return @c true if the value is a container (can have child values),
  /// @c false otherwise.
  bool isContainer() const;
  /// Get the parent value.
  Value getParent() const;
  /// Test if this is the root value for the configuration.
  bool isRoot() const;

  /// Get the number of child values.
  size_t childCount() const;
  /** Child access by @a index
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    size_t idx ///< Index of child value.
  ) const;
  /** Child access by @a name.
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    ConstBuffer const& name
  ) const;
  /** Child access by @a name.
      @return The child or a @c Void value if there is no child with @a name.
  */
  Value operator [] (
    char const* name ///< Null terminated string.
  ) const;

  /** @name Creating child values.

      These methods all take an optional @a name argument. This is
      required if @c this is a @c Group and ignored if @c this is a @c
      List.

      These methods will fail if
      - @c this is not a container.
      - @c this is a @c Group and no @a name is provided.

      @note Currently for groups, duplicate names are not
      detected. The duplicates will be inaccessible by name but can
      still be found by index. This is a problem but I am still
      pondering the appropriate solution.

      @see isContainer
      @return The new value, or an invalid value plus errata on failure.

      @internal I original had this as a single method, but changed to
      separate per type.  Overall less ugly because we can get the
      arguments more useful.
  */
  //@{
  /// Create a @c String value.
  Rv<Value> makeString(
    ConstBuffer const& text, ///< String content.
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  /// Create an @c Integer value.
  Rv<Value> makeInteger(
    ConstBuffer const& text, ///< Text of number.
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  /// Create a @c Group value.
  Rv<Value> makeGroup(
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  /// Create a @c List value.
  Rv<Value> makeList(
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  /// Create a @c Path value.
  Rv<Value> makePath(
    Path const& path, ///< Path.
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  /// Create a child by type.
  /// Client must fill in any other required elements.
  Rv<Value> makeChild(
    ValueType type, ///< Type of child.
    ConstBuffer const& name = detail::NULL_BUFFER///< Optional name of value.
  );
  //@}

  /** Find a value.
      @return The value if found, an void valid if not.
  */
  Value find(
    ConstBuffer const& path ///< Path relative to this value.
  );
  /** Find a value.
      @return The value if found, an void valid if not.
  */
  Value find(
    char const* path ///< Path relative to this value.
  );
  /** Find a value using a precondensed path.
      @return The value if found, an void valid if not.
  */
  Value find(
    Path const& path ///< Path relative to this value.
  );

  /** Reset to default constructed state.
      @note This wrapper is reset, the value in the configuration is unchanged.
      @return @c this object.
  */
  self& reset();

  /// Set source line.
  /// @return @c this object.
  self& setSourceLine(
    int line ///< Line in source stream.
  );
  /// Set source column.
  /// @return @c this object.
  self& setSourceColumn(
    int col ///< Column in source stream.
  );
  /// Set the source location.
  self& setSource(
    int line, ///< Line in source stream.
    int col ///< Column in source stream.
  );
  /// Get source line.
  /// @return The line in the source stream for this value.
  int getSourceLine() const;
  /// Get source column.
  /// @return The column in the source stream for this value.
  int getSourceColumn() const;

protected:
  // Note: We store an index and not a pointer because a pointer will go stale
  // if any items are added or removed from the underlying table.
  // Also, by storing the configuration, we hold it in memory as long as a Value
  // is in client hands.
  Configuration _config; ///< The configuration for this value.
  detail::ValueIndex _vidx; ///< Index of item.

  static Buffer const NULL_BUFFER; ///< Empty buffer to return on method failures.

  /// Construct from raw data.
  Value(
    Configuration cfg, ///< Source configuration.
    detail::ValueIndex vidx  ///< Index of value.
  );

  /** Get raw item pointer.
      @note This pointer is unstable and must be recomputed on each method invocation.
      @return The item pointer or @c NULL if this value is invalid.
  */
  detail::ValueItem* item();
  /** Get constant raw item pointer.
      @note This pointer is unstable and must be recomputed on each method invocation.
      @return The item pointer or @c NULL if this value is invalid.
  */
  detail::ValueItem const* item() const;
};

// Inline methods.
namespace detail {
  inline bool ValueTable::operator ! () const { return ! _ptr; }
  inline ValueTable::operator PseudoBool::Type () const { return _ptr ? PseudoBool::TRUE : PseudoBool::FALSE; }
  inline size_t ValueTable::size() const { return _ptr ? _ptr->_values.size() : 0; }
  inline Generation ValueTable::generation() const { return _ptr ? _ptr->_generation : Generation(0); }
  inline ValueItem const& ValueTable::operator [] (ValueIndex idx) const { return const_cast<self*>(this)->operator [] (idx); }
  inline ValueTable& ValueTable::reset() { _ptr.reset(); return *this; }

  inline ValueItem::ValueItem() : _type(VoidValue), _local_index(0), _srcLine(0), _srcColumn(0) {}
  inline ValueItem::ValueItem(ValueType type) : _type(type), _local_index(0), _srcLine(0), _srcColumn(0) {}
  inline ValueType ValueItem::getType() const { return _type; }
}

inline Value::~Value() { }
inline Value::Value() : _vidx(detail::NULL_VALUE_INDEX) {}
inline Value::Value(Configuration cfg, detail::ValueIndex vidx) : _config(cfg), _vidx(vidx) { }
inline bool Value::hasValue() const { return _config && _vidx != detail::NULL_VALUE_INDEX; }
inline Value::operator detail::PseudoBool::Type () const { return this->hasValue() ? detail::PseudoBool::TRUE : detail::PseudoBool::FALSE; }
inline bool Value::operator ! () const { return ! this->hasValue(); }
inline ValueType Value::getType() const { return this->hasValue() ? _config._table[_vidx]._type : VoidValue; }
inline ConstBuffer const& Value::getText() const {
  return this->hasValue() ? _config._table[_vidx]._text : detail::NULL_CONST_BUFFER;
}
inline Value& Value::setText(ConstBuffer const& text) {
  detail::ValueItem* item = this->item();
  if (item) item->_text = text;
  return *this;
}
inline ConstBuffer const& Value::getName() const {
  detail::ValueItem const* item = this->item();
  return item ? item->_name : detail::NULL_CONST_BUFFER;
}
inline size_t Value::getIndex() const {
  detail::ValueItem const* item = this->item();
  return item ? item->_local_index : 0;
}

inline bool Value::isLiteral() const { return 0 != (detail::IS_LITERAL & detail::Type_Property[this->getType()]); }
inline bool Value::isContainer() const { return 0 != (detail::IS_CONTAINER & detail::Type_Property[this->getType()]); }
inline Value Value::getParent() const { return this->hasValue() ? Value(_config, _config._table[_vidx]._parent) : Value(); }
inline bool Value::isRoot() const { return this->hasValue() && _vidx == 0; }
inline Value& Value::reset() { _config = Configuration(); _vidx = detail::NULL_VALUE_INDEX; return *this; }
inline detail::ValueItem* Value::item() { return this->hasValue() ? &(_config._table[_vidx]) : nullptr; }
inline detail::ValueItem const* Value::item() const { return const_cast<self*>(this)->item(); }
inline Value Value::operator [] (char const* name) const { return (*this)[ConstBuffer(name, strlen(name))]; }
inline size_t Value::childCount() const {
  detail::ValueItem const* item = this->item();
  return item ? item->_children.size() : 0;
}
inline Value Value::find(char const* path) { return this->find(ConstBuffer(path, strlen(path))); }
inline int Value::getSourceLine() const {
  detail::ValueItem const* item = this->item();
  return item ? item->_srcLine : 0;
}
inline int Value::getSourceColumn() const {
  detail::ValueItem const* item = this->item();
  return item ? item->_srcColumn : 0;
}
inline Value& Value::setSourceLine(int line) {
  detail::ValueItem* item = this->item();
  if (item) item->_srcLine = line;
  return *this;
}
inline Value& Value::setSourceColumn(int col) {
  detail::ValueItem* item = this->item();
  if (item) item->_srcColumn = col;
  return *this;
}
inline Value& Value::setSource(int line, int col) {
  detail::ValueItem* item = this->item();
  if (item) {
    item->_srcLine = line;
    item->_srcColumn = col;
  }
  return *this;
}

inline Path::ImplType::ImplType() { }

inline Path::Path() { }
inline Path::ImplType* Path::instance() { if (!_ptr) _ptr.reset(new ImplType); return _ptr.get(); }
inline Path& Path::append(ConstBuffer const& tag) { this->instance()->_elements.push_back(tag); return *this; }
inline Path& Path::append(size_t index) { this->instance()->_elements.push_back(ConstBuffer(nullptr, index)); return *this; }
inline size_t Path::count() const { return _ptr ? _ptr->_elements.size() : 0; }
inline ConstBuffer const& Path::operator [] (size_t idx) const { return _ptr ? _ptr->_elements[idx] : detail::NULL_CONST_BUFFER; }

inline Path::Parser::Parser() { }
inline Path::Parser::Parser( ConstBuffer const& text ) : _input(text), _c(text._ptr) { }
inline bool Path::Parser::hasInput() const { return _input._ptr && _input._ptr + _input._size > _c; }

inline bool Configuration::operator ! () const { return ! _table; }
inline Configuration::operator detail::PseudoBool::Type() const { return _table.operator detail::PseudoBool::Type(); }
inline Value Configuration::find( char const* path ) { return this->getRoot().find(path); }
inline Buffer Configuration::alloc(size_t n) { return _table.alloc(n);  }
inline size_t Configuration::childCount() const { return this->getRoot().childCount(); }
inline Value Configuration::operator [] (size_t idx) const { return (this->getRoot())[idx]; }
inline Value Configuration::operator [] ( ConstBuffer const& name ) const { return (this->getRoot())[name]; }
inline Value Configuration::operator [] ( char const* name ) const { return (this->getRoot())[name]; }

}} // namespace ts::config

# endif
