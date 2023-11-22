// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file

    Assistant class for translating strings to and from enumeration values.
 */

#pragma once

#include <string_view>
#include <initializer_list>
#include <tuple>
#include <functional>
#include <array>
#include <variant>

#include "swoc/swoc_version.h"
#include "swoc/IntrusiveHashMap.h"
#include "swoc/MemArena.h"
#include "swoc/bwf_base.h"
#include "swoc/ext/HashFNV.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
namespace detail {
/** Create an r-value reference to a temporary formatted string.
 *
 * @tparam Args Format string argument types.
 * @param fmt Format string.
 * @param args Arguments to format string.
 * @return r-value reference to a @c std::string containing the formatted string.
 *
 * This is used when throwing exceptions.
 */
template <typename... Args>
std::string
what(std::string_view const &fmt, Args &&...args) {
  std::string zret;
  return swoc::bwprint_v(zret, fmt, std::forward_as_tuple(args...));
}

// Exported because inner classes in template classes cannot be used in partial specialization
// which is required for tuple support. This should be removed next time there is a API changing
// release because tuple access is being deprecated.
template <typename E> struct lexicon_pair_type {
  E _value;
  TextView _name;

  /// Constructor.
  /// @internal Required to make the @c Lexicon constructors work as intended by forbidding
  /// construction of this type with only a value.
  lexicon_pair_type(E value, TextView name) : _value(value), _name(name) {}
};

} // namespace detail

/// Policy template use to specify the hash function for the integral type of @c Lexicon.
/// The default is @c std::hash but that can be overridden by specializing this method.
template <typename E>
size_t
Lexicon_Hash(E e) {
  static constexpr std::hash<E> hasher;
  return hasher(e);
}

/** A bidirectional mapping between names and enumeration values.

    This is intended to be a support class to make interacting with enumerations easier for
    configuration and logging. Names and enumerations can then be easily and reliably interchanged.
    The names are case insensitive but preserving.

    Each enumeration has a @a primary name and an arbitrary number of @a secondary names. When
    converting from an enumeration, the primary name is used. However, any of the names will be
    converted to the enumeration. For instance, a @c Lexicon for a boolean might have the primary
    name of @c TRUE be "true" with the secondary names "1", "yes", "enable". In that case converting
    @c TRUE would always be "true", while converting any of "true", "1", "yes", or "enable" would
    yield @c TRUE. This is convenient for parsing configurations to be more tolerant of input.

    The constructors are a bit baroque, but this is necessary in order to be able to declare
    constant instances of the @c Lexicon. If this isn't necessary, everything that can be done via
    the constructor can be done with other methods. The implementation of the constructors consists
    entirely of calls to @c define and @c set_default, the only difference is these methods can
    be called on a @c const instance from there.

    @note All names and value must be unique across the Lexicon. All name comparisons are case
    insensitive.
 */
template <typename E> class Lexicon {
  using self_type = Lexicon; ///< Self reference type.

protected:
  struct Item;

public:
  /// An association of an enumeration value and a name.
  /// @ note Used for initializer lists that have just a primary value.
  using Pair = detail::lexicon_pair_type<E>;

  // Deprecated - use the member names now.
  /// Index in @c Pair for the enumeration value.
  static constexpr auto VALUE_IDX = 0;
  /// Index in @c Pair for name.
  static constexpr auto NAME_IDX = 1;

  /** A function to be called if a value is not found to provide a default name.
   * @param value The value.
   * @return A name for the value.
   *
   * The name is return by view and therefore managing the lifetime of the name is problematic.
   * Generally it should be process lifetime, unless some other shorter lifetime can be managed
   * without a destructor being called. Unfortunately this can't be done any better without
   * imposing memory management costs on normal use.
   */
  using UnknownValueHandler = std::function<TextView(E)>;

  /** A function to be called if a name is not found, to provide a default value.
   * @param name The name
   * @return An enumeration value.
   *
   * The @a name is provided and a value in the enumeration type is expected.
   */
  using UnknownNameHandler = std::function<E(TextView)>;

  /** A default handler.
   *
   * This handles providing a default value or name for a missing name or value.
   */
  using Default = std::variant<std::monostate, E, TextView, UnknownNameHandler, UnknownValueHandler>;

  /// Element of an initializer list that contains secondary names.
  /// @note This is used only in the constructor and contains transient data.
  struct Definition {
    const E &value;                               ///< Value for definition.
    std::initializer_list<TextView> const &names; ///< Primary then secondary names.
  };

  /// Construct empty instance.
  Lexicon();

  using with       = std::initializer_list<Pair> const &;
  using with_multi = std::initializer_list<Definition> const &;

  /** Construct with names, possible secondary values, and optional default handlers.
   *
   * @param items A list of initializers, each of which is a name and a list of values.
   * @param handler_1 A default handler.
   * @param handler_2 A default hander.
   *
   * Each item in the intializers must be a @c Definition, that is a name and a list of values.
   * The first value is the primary value and is required. Subsequent values are optional
   * and become secondary values.
   *
   * The default handlers are optional can be omitted. If so, exceptions are thrown when values
   * or names not in the @c Lexicon are used. See @c set_default for more details.
   *
   * @see set_default.
   */
  explicit Lexicon(with_multi items, Default handler_1 = Default{}, Default handler_2 = Default{});

  /** Construct with names / value pairs, and optional default handlers.
   *
   * @param items A list of initializers, each of which is a name and a list of values.
   * @param handler_1 A default handler.
   * @param handler_2 A default handler.
   *
   * Each item in the intializers must be a @c Pair, that is a name and a value.
   *
   * The default handlers are optional can be omitted. If so, exceptions are thrown when values
   * or names not in the @c Lexicon are used. See @c set_default for more details.
   *
   * @see set_default.
   */
  explicit Lexicon(with items, Default handler_1 = Default{}, Default handler_2 = Default{});

  /** Construct with only default values / handlers.
   *
   * @param handler_1 A default handler.
   * @param handler_2 A default handler.
   *
   * @a handler_2 is optional can be omitted. The argument values are the same as for
   * @c set_default.
   *
   * @see set_default.
   */
  explicit Lexicon(Default handler_1, Default handler_2 = Default{});

  Lexicon(self_type &&that) = default;

  /** Get the name for a @a value.
   *
   * @param value Value to look up.
   * @return The name for @a value.
   */
  TextView operator[](E const &value) const;

  /** Get the value for a @a name.
   *
   * @param name Name to look up.
   * @return The value for the @a name.
   */
  E operator[](TextView const &name) const;

  /// Define the @a names for a @a value.
  /// The first name is the primary name. All @a names must be convertible to @c std::string_view.
  /// <tt>lexicon.define(Value, primary, [secondary, ... ]);</tt>
  template <typename... Args> self_type &define(E value, Args &&...names);

  // These are really for consistency with constructors, they're not expected to be commonly used.
  /// Define a value and names.
  /// <tt>lexicon.define(Value, { primary, [secondary, ...] });</tt>
  self_type &define(E value, const std::initializer_list<TextView> &names);

  /** Define a name, value pair.
   *
   * @param pair A @c Pair of the name and value to define.
   * @return @a this.
   */
  self_type &define(const Pair &pair);

  /** Define a name with a primary and secondary values.
   *
   * @param init The @c Definition with the name and values.
   * @return @a this.
   *
   * This defines the name, with the first value in the value list becoming the primary value
   * and subsequent values (if any) being the secondary values. A primary value is required but
   * secondary values are not. This is to make it possible to define all values in this style
   * even if some do not have secondary values.
   */
  self_type &define(const Definition &init);

  /** Set default handler.
   *
   * @param handler The handler.
   * @return @a this.
   *
   * The @a handler can be of various types.
   *
   * - An enumeration value. This sets the default value handler to return that value for any
   *   name not found.
   *
   * - A @c string_view. The sets the default name handler to return the @c string_view as the
   *   name for any value not found.
   *
   * - A @c DefaultNameHandler. This is a functor that takes an enumeration value parameter and
   *   returns a @c string_view as the name for any value that is not found.
   *
   * - A @c DefaultValueHandler. This is a functor that takes a name as a @c string_view and returns
   *   an enumeration value as the value for any name that is not found.
   */
  self_type &set_default(Default const &handler);

  /// Get the number of values with definitions.
  size_t count() const;

protected:
  /// Common features of container iterators.
  class base_iterator {
    using self_type = base_iterator;

  public:
    using value_type        = const Pair;                      ///< Iteration value.
    using pointer           = value_type *;                    ///< Pointer to iteration value.
    using reference         = value_type &;                    ///< Reference to iteration value.
    using difference_type   = ptrdiff_t;                       ///< Type of difference between iterators.
    using iterator_category = std::bidirectional_iterator_tag; ///< Concepts for iterator.
    /// Default constructor (invalid iterator)
    base_iterator() = default;
    /// Dereference.
    reference operator*() const;
    /// Dereference.
    pointer operator->() const;
    /// Equality.
    bool operator==(self_type const &that) const;
    /// Inequality.
    bool operator!=(self_type const &that) const;

  protected:
    explicit base_iterator(Item const *item) : _item(item) {}

    const Item *_item{nullptr}; ///< Current location in the container.
  };

public:
  /** Iterator over pairs of values and primary name pairs.
   *  The value type is a @c Pair with the value and name.
   */
  class value_iterator : public base_iterator {
    using super_type = base_iterator;
    using self_type  = value_iterator;

  public:
    using value_type = typename super_type::value_type;
    using pointer    = typename super_type::pointer;
    using reference  = typename super_type::reference;

    /// Default constructor.
    value_iterator() = default;

    /// Copy constructor.
    value_iterator(self_type const &that) = default;

    /// Move constructor.
    value_iterator(self_type &&that) = default;

    /// Assignment.
    self_type &operator=(self_type const &that) = default;

    /// Increment.
    self_type &operator++();

    /// Increment.
    self_type operator++(int);

    /// Decrement.
    self_type &operator--();

    /// Decrement.
    self_type operator--(int);

  protected:
    value_iterator(const Item *item) : super_type(item){}; ///< Internal constructor.

    friend Lexicon;
  };

  class name_iterator : public base_iterator {
  private:
    using self_type  = name_iterator;
    using super_type = base_iterator;

  public:
    /// Default constructor.
    name_iterator() = default;

    /// Copy constructor.
    name_iterator(self_type const &that) = default;

    /// Move constructor.
    name_iterator(self_type &&that) = default;

    /// Assignment.
    self_type &operator=(self_type const &that) = default;

    /// Increment.
    self_type &operator++();

    /// Increment.
    self_type operator++(int);

    /// Decrement.
    self_type &operator--();

    /// Decrement.
    self_type operator--(int);

  protected:
    name_iterator(const Item *item) : super_type(item){}; ///< Internal constructor.

    friend Lexicon;
  };

  /// Iterator over values (each with a primary name).
  using const_iterator = value_iterator;
  /// Iterator over values.
  /// @note All iteration is over constant pairs, no modification is possible.
  using iterator = const_iterator;

  /// Iteration begin.
  const_iterator begin() const;

  /// Iteration end.
  const_iterator end() const;

  /// Iteration over names - every value/name pair.
  name_iterator
  begin_names() const {
    return {_by_name.begin()};
  }
  /// Iteration over names - every value/name pair.
  name_iterator
  end_names() const {
    return {_by_name.end()};
  }

  /// @cond INTERNAL
  // Helper struct to return to enable container iteration for names.
  struct ByNameHelper {
    self_type const &_lexicon;
    ByNameHelper(self_type const &self) : _lexicon(self) {}
    name_iterator
    begin() const {
      return _lexicon.begin_names();
    }
    name_iterator
    end() const {
      return _lexicon.end_names();
    }
  };
  /// @endcond

  /** Enable container iteration by name.
   * The return value is a tempoary of indeterminate type that provides @c begin and @c end methods which
   * return name based iterators for @a this. This is useful for container based iteration. E.g. to iterate
   * over all of the value/name pairs,
   * @code
   * for ( auto const & pair : lexicon.by_names()) {
   *   // code
   * }
   * @endcode
   * @return Temporary.
   */
  ByNameHelper
  by_names() const {
    return {*this};
  }

protected:
  /// Handle providing a default name.
  using NameDefault = std::variant<std::monostate, std::string_view, UnknownValueHandler>;
  /// Handle providing a default value.
  using ValueDefault = std::variant<std::monostate, E, UnknownNameHandler>;

  /// Visitor functor for handling @c NameDefault.
  struct NameDefaultVisitor {
    E _value; ///< Value to use for default.

    /// Visitor - invalid value type.
    std::string_view
    operator()(std::monostate const &) const {
      throw std::domain_error("Lexicon: invalid enumeration value");
    }

    /// Visitor - literal string.
    std::string_view
    operator()(TextView const &name) const {
      return name;
    }

    /// Visitor - string generator.
    std::string_view
    operator()(UnknownValueHandler const &handler) const {
      return handler(_value);
    }
  };

  /// Visitor functor for handling @c ValueDefault.
  struct ValueDefaultVisitor {
    std::string_view _name; ///< Name of visited pair.

    /// Vistor - invalid value.
    E
    operator()(std::monostate const &) const {
      throw std::domain_error(detail::what("Lexicon: Unknown name \"{}\"", _name).data());
    }

    /// Visitor - value.
    E
    operator()(E const &value) const {
      return value;
    }

    /// Visitor - value generator.
    E
    operator()(UnknownNameHandler const &handler) const {
      return handler(_name);
    }
  };

  /// Each unique pair of value and name is stored as an instance of this class.
  /// The primary is stored first and is therefore found by normal lookup.
  struct Item {
    /** Construct with a @a name and a primary @a value.
     *
     * @param value The primary value.
     * @param name The name.
     *
     */
    Item(E value, TextView name);

    Pair _payload; ///< Enumeration and name.

    /// @cond INTERNAL_DETAIL
    // Intrusive list linkage support.
    struct NameLinkage {
      Item *_next{nullptr};
      Item *_prev{nullptr};

      static Item *&next_ptr(Item *);
      static Item *&prev_ptr(Item *);
      static std::string_view key_of(Item *);
      static uint32_t hash_of(std::string_view s);
      static bool equal(std::string_view const &lhs, std::string_view const &rhs);
    } _name_link;

    // Intrusive linkage for value lookup.
    struct ValueLinkage {
      Item *_next{nullptr};
      Item *_prev{nullptr};

      static Item *&next_ptr(Item *);
      static Item *&prev_ptr(Item *);
      static E key_of(Item *);
      static size_t hash_of(E);
      static bool equal(E lhs, E rhs);
    } _value_link;
    /// @endcond
  };

  /// Copy @a name in to local storage.
  TextView localize(TextView const &name);

  /// Storage for names.
  MemArena _arena{1024};
  /// Access by name.
  IntrusiveHashMap<typename Item::NameLinkage> _by_name;
  /// Access by value.
  IntrusiveHashMap<typename Item::ValueLinkage> _by_value;
  NameDefault _name_default;   ///< Name to return if no value not found.
  ValueDefault _value_default; ///< Value to return if name not found.
};

// ==============
// Implementation

// ----
// Item

template <typename E> Lexicon<E>::Item::Item(E value, TextView name) : _payload{value, name} {}

/// @cond INTERNAL_DETAIL
template <typename E>
auto
Lexicon<E>::Item::NameLinkage::next_ptr(Item *item) -> Item *& {
  return item->_name_link._next;
}

template <typename E>
auto
Lexicon<E>::Item::NameLinkage::prev_ptr(Item *item) -> Item *& {
  return item->_name_link._prev;
}

template <typename E>
auto
Lexicon<E>::Item::ValueLinkage::next_ptr(Item *item) -> Item *& {
  return item->_value_link._next;
}

template <typename E>
auto
Lexicon<E>::Item::ValueLinkage::prev_ptr(Item *item) -> Item *& {
  return item->_value_link._prev;
}

template <typename E>
std::string_view
Lexicon<E>::Item::NameLinkage::key_of(Item *item) {
  return item->_payload._name;
}

template <typename E>
E
Lexicon<E>::Item::ValueLinkage::key_of(Item *item) {
  return item->_payload._value;
}

template <typename E>
uint32_t
Lexicon<E>::Item::NameLinkage::hash_of(std::string_view s) {
  return Hash32FNV1a().hash_immediate(transform_view_of(&toupper, s));
}

template <typename E>
size_t
Lexicon<E>::Item::ValueLinkage::hash_of(E value) {
  return Lexicon_Hash<E>(value);
}

template <typename E>
bool
Lexicon<E>::Item::NameLinkage::equal(std::string_view const &lhs, std::string_view const &rhs) {
  return 0 == strcasecmp(lhs, rhs);
}

template <typename E>
bool
Lexicon<E>::Item::ValueLinkage::equal(E lhs, E rhs) {
  return lhs == rhs;
}
/// @endcond

// -------
// Lexicon

template <typename E> Lexicon<E>::Lexicon() {}

template <typename E> Lexicon<E>::Lexicon(with_multi items, Default handler_1, Default handler_2) {
  for (auto const &item : items) {
    this->define(item.value, item.names);
  }

  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E> Lexicon<E>::Lexicon(with items, Default handler_1, Default handler_2) {
  for (auto const &item : items) {
    this->define(item);
  }

  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E> Lexicon<E>::Lexicon(Default handler_1, Default handler_2) {
  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E>
TextView
Lexicon<E>::localize(TextView const &name) {
  auto span = _arena.alloc_span<char>(name.size());
  memcpy(span, name);
  return {span.data(), span.size()};
}

template <typename E>
TextView
Lexicon<E>::operator[](E const &value) const {
  if (auto spot = _by_value.find(value); spot != _by_value.end()) {
    return spot->_payload._name;
  }
  return std::visit(NameDefaultVisitor{value}, _name_default);
}

template <typename E>
E
Lexicon<E>::operator[](TextView const &name) const {
  if (auto spot = _by_name.find(name); spot != _by_name.end()) {
    return spot->_payload._value;
  }
  return std::visit(ValueDefaultVisitor{name}, _value_default);
}

template <typename E>
auto
Lexicon<E>::define(E value, const std::initializer_list<TextView> &names) -> self_type & {
  if (names.size() < 1) {
    throw std::invalid_argument("A defined value must have at least a primary name");
  }
  for (auto const &name : names) {
    if (_by_name.find(name) != _by_name.end()) {
      throw std::invalid_argument(detail::what("Duplicate name '{}' in Lexicon", name));
    }
    auto i = _arena.make<Item>(value, this->localize(name));
    _by_name.insert(i);
    // Only put primary names in the value table.
    if (_by_value.find(value) == _by_value.end()) {
      _by_value.insert(i);
    }
  }
  return *this;
}

template <typename E>
template <typename... Args>
auto
Lexicon<E>::define(E value, Args &&...names) -> self_type & {
  static_assert(sizeof...(Args) > 0, "A defined value must have at least a primary name");
  return this->define(value, {std::forward<Args>(names)...});
}

template <typename E>
auto
Lexicon<E>::define(const Pair &pair) -> self_type & {
  return this->define(pair._value, pair._name);
}

template <typename E>
auto
Lexicon<E>::define(const Definition &init) -> self_type & {
  return this->define(init.value, init.names);
}

template <typename E>
auto
Lexicon<E>::set_default(Default const &handler) -> self_type & {
  switch (handler.index()) {
  case 0:
    break;
  case 1:
    _value_default = std::get<1>(handler);
    break;
  case 3:
    _value_default = std::get<3>(handler);
    break;
  case 2:
    _name_default = std::get<2>(handler);
    break;
  case 4:
    _name_default = std::get<4>(handler);
    break;
  }
  return *this;
}

template <typename E>
size_t
Lexicon<E>::count() const {
  return _by_value.count();
}

template <typename E>
auto
Lexicon<E>::begin() const -> const_iterator {
  return const_iterator{static_cast<const Item *>(_by_value.begin())};
}

template <typename E>
auto
Lexicon<E>::end() const -> const_iterator {
  return {};
}

// Iterators

template <typename E>
auto
Lexicon<E>::base_iterator::operator*() const -> reference {
  return _item->_payload;
}

template <typename E>
auto
Lexicon<E>::base_iterator::operator->() const -> pointer {
  return &(_item->_payload);
}

template <typename E>
bool
Lexicon<E>::base_iterator::operator==(self_type const &that) const {
  return _item == that._item;
}

template <typename E>
bool
Lexicon<E>::base_iterator::operator!=(self_type const &that) const {
  return _item != that._item;
}

template <typename E>
auto
Lexicon<E>::value_iterator::operator++() -> self_type & {
  super_type::_item = super_type::_item->_value_link._next;
  return *this;
}

template <typename E>
auto
Lexicon<E>::value_iterator::operator++(int) -> self_type {
  self_type tmp{*this};
  ++*this;
  return tmp;
}

template <typename E>
auto
Lexicon<E>::value_iterator::operator--() -> self_type & {
  super_type::_item = super_type::_item->_value_link->_prev;
  return *this;
}

template <typename E>
auto
Lexicon<E>::value_iterator::operator--(int) -> self_type {
  self_type tmp;
  ++*this;
  return tmp;
}

template <typename E>
auto
Lexicon<E>::name_iterator::operator++() -> self_type & {
  super_type::_item = super_type::_item->_name_link._next;
  return *this;
}

template <typename E>
auto
Lexicon<E>::name_iterator::operator++(int) -> self_type {
  self_type tmp{*this};
  ++*this;
  return tmp;
}

template <typename E>
auto
Lexicon<E>::name_iterator::operator--() -> self_type & {
  super_type::_item = super_type::_item->_name_link->_prev;
  return *this;
}

template <typename E>
auto
Lexicon<E>::name_iterator::operator--(int) -> self_type {
  self_type tmp;
  ++*this;
  return tmp;
}

template <typename E>
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, Lexicon<E> const &lex) {
  bool sep_p = false;
  if (spec._type == 's' || spec._type == 'S') {
    for (auto &&[value, name] : lex) {
      if (sep_p) {
        w.write(',');
      }
      bwformat(w, spec, name);
      sep_p = true;
    }
  } else if (spec.has_numeric_type()) {
    for (auto &&[value, name] : lex) {
      if (sep_p) {
        w.write(',');
      }
      bwformat(w, spec, unsigned(value));
      sep_p = true;
    }
  } else {
    for (auto &&[value, name] : lex) {
      if (sep_p) {
        w.write(',');
      }
      w.print("[{},{}]", name, unsigned(value));
      sep_p = true;
    }
  }
  return w;
}

}} // namespace swoc::SWOC_VERSION_NS

namespace std {

template <size_t IDX, typename E> class tuple_element<IDX, swoc::detail::lexicon_pair_type<E>> {
  static_assert("swoc::Lexicon::Pair tuple index out of range");
};

template <typename E> class tuple_element<0, swoc::detail::lexicon_pair_type<E>> {
public:
  using type = E;
};

template <typename E> class tuple_element<1, swoc::detail::lexicon_pair_type<E>> {
public:
  using type = swoc::TextView;
};

template <size_t IDX, typename E>
auto
get(swoc::detail::lexicon_pair_type<E> const &p) -> typename std::tuple_element<IDX, swoc::detail::lexicon_pair_type<E>>::type {
  if constexpr (IDX == 0) {
    return p._value;
  } else if constexpr (IDX == 1) {
    return p._name;
  }
}

} // namespace std
