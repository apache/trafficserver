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
what(std::string_view const &fmt, Args &&... args) {
  std::string zret;
  return swoc::bwprint_v(zret, fmt, std::forward_as_tuple(args...));
}
} // namespace detail

/// Policy template use to specify the hash function for the integral type of @c Lexicon.
/// The default is to cast to the required hash value type, which is usually sufficient.
/// In some cases the cast doesn't work and this must be specialized.
template <typename E>
uintmax_t
Lexicon_Hash(E e) {
  return static_cast<uintmax_t>(e);
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
  /** A function to be called if a value is not found to provide a default name.
   * @param value The value.
   * @return A name for the value.
   *
   * The name is return by view and therefore managing the lifetime of the name is problematic.
   * Generally it should be process lifetime, unless some other shorter lifetime can be managed
   * without a destructor being called. Unfortunately this can't be done any better without
   * imposing memory management costs on normal use.
   */
  using UnknownValueHandler = std::function<std::string_view(E)>;

  /** A function to be called if a name is not found, to provide a default value.
   * @param name The name
   * @return An enumeration value.
   *
   * The @a name is provided and a value in the enumeration type is expected.
   */
  using UnknownNameHandler = std::function<E(std::string_view)>;

  /** A default handler.
   *
   * This handles providing a default value or name for a missing name or value.
   */
  using DefaultHandler = std::variant<std::monostate, E, std::string_view, UnknownNameHandler, UnknownValueHandler>;

  /// Used for initializer lists that have just a primary value.
  using Pair = std::tuple<E, std::string_view>;

  /// Element of an initializer list that contains secondary names.
  struct Definition {
    const E &value;                                       ///< Value for definition.
    const std::initializer_list<std::string_view> &names; ///< Primary then secondary names.
  };

  /// Construct empty instance.
  Lexicon();

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
  explicit Lexicon(const std::initializer_list<Definition> &items, DefaultHandler handler_1 = DefaultHandler{},
                   DefaultHandler handler_2 = DefaultHandler{});

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
  explicit Lexicon(const std::initializer_list<Pair> &items, DefaultHandler handler_1 = DefaultHandler{},
                   DefaultHandler handler_2 = DefaultHandler{});

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
  explicit Lexicon(DefaultHandler handler_1, DefaultHandler handler_2 = DefaultHandler{});

  Lexicon(self_type &&that) = default;

  /** Get the name for a @a value.
   *
   * @param value Value to look up.
   * @return The name for @a value.
   */
  std::string_view operator[](E value) const;

  /** Get the value for a @a name.
   *
   * @param name Name to look up.
   * @return The value for the @a name.
   */
  E operator[](std::string_view const &name) const;

  /// Define the @a names for a @a value.
  /// The first name is the primary name. All @a names must be convertible to @c std::string_view.
  /// <tt>lexicon.define(Value, primary, [secondary, ... ]);</tt>
  template <typename... Args> self_type &define(E value, Args &&... names);

  // These are really for consistency with constructors, they're not expected to be commonly used.
  /// Define a value and names.
  /// <tt>lexicon.define(Value, { primary, [secondary, ...] });</tt>
  self_type &define(E value, const std::initializer_list<std::string_view> &names);

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
  self_type &set_default(DefaultHandler const &handler);

  /// Get the number of values with definitions.
  size_t count() const;

  /** Iterator over pairs of values and primary name pairs.
   * Value is a 2-tuple of the enumeration type and the primary name.
   */
  class const_iterator {
    using self_type = const_iterator;

  public:
    using value_type        = const Pair; ///< Iteration value.
    using pointer           = value_type *; ///< Pointer to iteration value.
    using reference         = value_type &; ///< Reference to iteration value.
    using difference_type   = ptrdiff_t; ///< Type of difference between iterators.
    using iterator_category = std::bidirectional_iterator_tag; ///< Concepts for iterator.

    /// Default constructor.
    const_iterator() = default;

    /// Copy constructor.
    const_iterator(self_type const &that) = default;

    /// Move construcgtor.
    const_iterator(self_type &&that) = default;

    /// Dereference.
    reference operator*() const;

    /// Dereference.
    pointer operator->() const;

    /// Assignment.
    self_type &operator=(self_type const &that) = default;

    /// Equality.
    bool operator==(self_type const &that) const;

    /// Inequality.
    bool operator!=(self_type const &that) const;

    /// Increment.
    self_type &operator++();

    /// Increment.
    self_type operator++(int);

    /// Decrement.
    self_type &operator--();

    /// Decrement.
    self_type operator--(int);

  protected:
    const_iterator(const Item *item); ///< Internal constructor.

    /// Update the internal values after changing the iterator location.
    void update();

    const Item *_item{nullptr};                      ///< Current location in the container.
    typename std::remove_const<value_type>::type _v; ///< Synthesized value for dereference.

    friend Lexicon;
  };

  /// Pair iterator.
  /// @note All iteration is over constant pairs, no modification is possible.
  using iterator = const_iterator;

  /// Iteration begin.
  const_iterator begin() const;

  /// Iteration end.
  const_iterator end() const;

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
      throw std::domain_error(detail::what("Lexicon: invalid enumeration value {}", static_cast<int>(_value)).data());
    }

    /// Visitor - literal string.
    std::string_view
    operator()(std::string_view const &name) const {
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
    Item(E value, std::string_view name);

    E _value;               ///< Definition value.
    std::string_view _name; ///< Definition name

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
      static uintmax_t hash_of(E);
      static bool equal(E lhs, E rhs);
    } _value_link;
    /// @endcond
  };

  /// Copy @a name in to local storage.
  std::string_view localize(std::string_view const &name);

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

template <typename E> Lexicon<E>::Item::Item(E value, std::string_view name) : _value(value), _name(name) {}

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
  return item->_name;
}

template <typename E>
E
Lexicon<E>::Item::ValueLinkage::key_of(Item *item) {
  return item->_value;
}

template <typename E>
uint32_t
Lexicon<E>::Item::NameLinkage::hash_of(std::string_view s) {
  return Hash32FNV1a().hash_immediate(transform_view_of(&toupper, s));
}

template <typename E>
uintmax_t
Lexicon<E>::Item::ValueLinkage::hash_of(E value) {
  // In almost all cases, the values will be (roughly) sequential, so an identity hash works well.
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

template <typename E>
Lexicon<E>::Lexicon(const std::initializer_list<Definition> &items, DefaultHandler handler_1, DefaultHandler handler_2) {
  for (auto const &item : items) {
    this->define(item.value, item.names);
  }

  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E>
Lexicon<E>::Lexicon(const std::initializer_list<Pair> &items, DefaultHandler handler_1, DefaultHandler handler_2) {
  for (auto const &item : items) {
    this->define(item);
  }

  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E> Lexicon<E>::Lexicon(DefaultHandler handler_1, DefaultHandler handler_2) {
  for (auto &&h : {handler_1, handler_2}) {
    this->set_default(h);
  }
}

template <typename E>
std::string_view
Lexicon<E>::localize(std::string_view const &name) {
  auto span = _arena.alloc(name.size());
  memcpy(span.data(), name.data(), name.size());
  return span.view();
}

template <typename E>
std::string_view
Lexicon<E>::operator[](E value) const {
  auto spot = _by_value.find(value);
  if (spot != _by_value.end()) {
    return spot->_name;
  }
  return std::visit(NameDefaultVisitor{value}, _name_default);
}

template <typename E>
E
Lexicon<E>::operator[](std::string_view const &name) const {
  auto spot = _by_name.find(name);
  if (spot != _by_name.end()) {
    return spot->_value;
  }
  return std::visit(ValueDefaultVisitor{name}, _value_default);
}

template <typename E>
auto
Lexicon<E>::define(E value, const std::initializer_list<std::string_view> &names) -> self_type & {
  if (names.size() < 1) {
    throw std::invalid_argument("A defined value must have at least a primary name");
  }
  for (auto name : names) {
    if (_by_name.find(name) != _by_name.end()) {
      throw std::invalid_argument(detail::what("Duplicate name '{}' in Lexicon", name));
    }
    auto i = new Item(value, this->localize(name));
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
Lexicon<E>::define(E value, Args &&... names) -> self_type & {
  static_assert(sizeof...(Args) > 0, "A defined value must have at least a primary name");
  return this->define(value, {std::forward<Args>(names)...});
}

template <typename E>
auto
Lexicon<E>::define(const Pair &pair) -> self_type & {
  return this->define(std::get<0>(pair), {std::get<1>(pair)});
}

template <typename E>
auto
Lexicon<E>::define(const Definition &init) -> self_type & {
  return this->define(init.value, init.names);
}

template <typename E>
auto
Lexicon<E>::set_default(DefaultHandler const &handler) -> self_type & {
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
void
Lexicon<E>::const_iterator::update() {
  std::get<0>(_v) = _item->_value;
  std::get<1>(_v) = _item->_name;
}

template <typename E> Lexicon<E>::const_iterator::const_iterator(const Item *item) : _item(item) {
  if (_item) {
    this->update();
  };
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator*() const -> reference {
  return _v;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator->() const -> pointer {
  return &_v;
}

template <typename E>
bool
Lexicon<E>::const_iterator::operator==(self_type const &that) const {
  return _item == that._item;
}

template <typename E>
bool
Lexicon<E>::const_iterator::operator!=(self_type const &that) const {
  return _item != that._item;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator++() -> self_type & {
  if (nullptr != (_item = _item->_value_link._next)) {
    this->update();
  }
  return *this;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator++(int) -> self_type {
  self_type tmp{*this};
  ++*this;
  return tmp;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator--() -> self_type & {
  if (nullptr != (_item = _item->_value_link->_prev)) {
    this->update();
  }
  return *this;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator--(int) -> self_type {
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
