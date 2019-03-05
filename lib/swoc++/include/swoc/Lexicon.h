/** @file

    Assistant class for translating strings to and from enumeration values.

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

#include <string_view>
#include <initializer_list>
#include <tuple>
#include <functional>
#include <array>
#include "swoc/IntrusiveHashMap.h"
#include "swoc/MemArena.h"
#include "swoc/bwf_base.h"
#include "swoc/ext/HashFNV.h"

namespace swoc
{
namespace detail
{
  /** Create an r-value reference to a temporary formatted string.
   *
   * @tparam Args Format string argument types.
   * @param fmt Format string.
   * @param args Arguments to format string.
   * @return r-value reference to a @c std::string containing the formatted string.
   */
  template <typename... Args>
  std::string &&
  what(std::string_view const &fmt, Args &&... args)
  {
    std::string zret;
    swoc::bwprint_v(zret, fmt, std::forward_as_tuple(args...));
    return std::move(zret);
  }
} // namespace detail

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

    @note All names and value must be unique across the Lexicon. All name comparisons are case
    insensitive.
 */
template <typename E> class Lexicon
{
  using self_type = Lexicon; ///< Self reference type.

protected:
  struct Item;

public:
  /// Used for initializer lists that have just a primary value.
  using Pair = std::tuple<E, std::string_view>;
  /// A function to be called if a value is not found.
  using UnknownValueHandler = std::function<std::string_view(E)>;
  /// A function to be called if a name is not found.
  using UnknownNameHandler = std::function<E(std::string_view)>;

  /// Element of an initializer list that contains secondary names.
  struct Definition {
    const E &value;                                       ///< Value for definition.
    const std::initializer_list<std::string_view> &names; ///< Primary then secondary names.
  };

  /// Template argument carrying struct.
  /// @note Needed to pass a compile time constant to a constructor as compile time constant.
  template <E e> struct Require {
  };

  /// Construct empty instance.
  Lexicon();
  /// Construct with secondary names.
  Lexicon(const std::initializer_list<Definition> &items);
  /// Construct with primary names only.
  Lexicon(const std::initializer_list<Pair> &items);
  /// Construct and verify the number of definitions.
  template <E e> Lexicon(const Require<e> &, const std::array<Definition, static_cast<size_t>(e)> &defines);
  /// Construct and verify the number of pairs.
  template <E e> Lexicon(const Require<e> &, const std::array<Pair, static_cast<size_t>(e)> &defines);

  /** Get the name for a @a value.
   *
   * @param value Value to look up.
   * @return The name for @a value.
   */
  std::string_view operator[](E value);

  /** Get the value for a @a name.
   *
   * @param name Name to look up.
   * @return The value for the @a name.
   */
  E operator[](std::string_view const &name);

  /// Define the @a names for a @a value.
  /// The first name is the primary name. All @a names must be convertible to @c std::string_view.
  /// <tt>lexicon.define(Value, primary, [secondary, ... ]);</tt>
  template <typename... Args> self_type &define(E value, Args &&... names);
  // These are really for consistency with constructors, they're not expected to be commonly used.
  /// Define a value and names.
  /// <tt>lexicon.define(Value, { primary, [secondary, ...] });</tt>
  self_type &define(E value, const std::initializer_list<std::string_view> &names);
  self_type &define(const Pair &pair);
  self_type &define(const Definition &init);

  /** Set a default @a value.
   *
   * @param value Value to return if a name is not found.
   * @return @c *this
   */
  self_type &set_default(E value);

  /** Set a default @a name.
   *
   * @param name Name to return if a value is not found.
   * @return @c *this
   *
   * @note The @a name is copied to local storage.
   */
  self_type &set_default(std::string_view name);

  /** Set a default @a handler for names that are not found.
   *
   * @param handler Function to call with a name that was not found.
   * @return @c this
   *
   * @a handler is passed the name that was not found as a @c std::string_view and must return a
   * value which is then returned to the caller.
   */
  self_type &set_default(const UnknownNameHandler &handler);

  /** Set a default @a handler for values that are not found.
   *
   * @param handler Function to call with a value that was not found.
   * @return @c *this
   *
   * @a handler is passed the value that was not found and must return a name as a @c std::string_view.
   * Caution must be used because the returned name must not leak and must be thread safe. The most
   * common use would be for logging bad values.
   */
  self_type &set_default(const UnknownValueHandler &handler);

  /// Get the number of values with definitions.
  size_t count() const;

  /// Iterator over pairs of values and primary name pairs.
  class const_iterator
  {
    using self_type = const_iterator;

  public:
    using value_type        = const Pair;
    using pointer           = value_type *;
    using reference         = value_type &;
    using difference_type   = ptrdiff_t;
    using iterator_category = std::bidirectional_iterator_tag;

    const_iterator()                      = default;
    const_iterator(self_type const &that) = default;
    const_iterator(self_type &&that)      = default;

    reference operator*() const;
    pointer operator->() const;

    self_type &operator=(self_type const &that) = default;
    bool operator==(self_type const &that) const;
    bool operator!=(self_type const &that) const;

    self_type &operator++();
    self_type operator++(int);

    self_type &operator--();
    self_type operator--(int);

  protected:
    const_iterator(const Item *item);
    void update();
    const Item *_item{nullptr};
    typename std::remove_const<value_type>::type _v;

    friend Lexicon;
  };

  // There is no modifying elements in the Lexicon so only constant iteration.
  using iterator = const_iterator;

  const_iterator begin() const;
  const_iterator end() const;

protected:
  // Because std::variant is broken up through clang 6, we have to do something uglier.
  //  using NameDefault  = std::variant<std::monostate, std::string_view, UnknownValueHandler>;
  //  using ValueDefault = std::variant<std::monostate, E, UnknownNameHandler>;

  /// Type marker for internal variant.
  enum class Content {
    NIL,    ///< Nothing, not set.
    SCALAR, ///< A specific value/name.
    HANDLER ///< A function
  };

  /// Default (no value) struct for variant initialization.
  struct NilValue {
  };

  /// Handler for values that are not in the Lexicon.
  struct NameDefault {
    using self_type = NameDefault; ///< Self reference type.

    NameDefault() = default; ///< Default constructor.
    ~NameDefault();          ///< Destructor.

    /** Set the handler to return a fixed value.
     *
     * @param name Name to return.
     * @return @a this
     */
    self_type &operator=(std::string_view name);

    /** Set the handler to call a function to compute the default name.
     *
     * @param handler Handler called to compute the name.
     * @return @a this
     */
    self_type &operator=(const UnknownValueHandler &handler);

    /** Compute the default name for @a value.
     *
     * @param value Value without a name.
     * @return A name for that value.
     */
    std::string_view operator()(E value);

    /// Internal clean up, needed for assignment and destructor.
    self_type &destroy();

    /// Initialize internal variant to contain nothing.
    Content _content{Content::NIL};
    /// Compute the required raw storage.
    static constexpr size_t N = std::max<size_t>(sizeof(std::string_view), sizeof(UnknownValueHandler));
    /// Provide raw storage for the variant.
    char _store[N];
  };

  struct ValueDefault {
    using self_type = ValueDefault;

    ValueDefault() = default;
    ~ValueDefault();

    self_type &operator=(E value);
    self_type &operator=(const UnknownNameHandler &handler);

    E operator()(std::string_view name);

    self_type &destroy();

    Content _content{Content::NIL};
    static constexpr size_t N = std::max<size_t>(sizeof(E), sizeof(UnknownNameHandler));
    char _store[N];
  };

  /// Each unique pair of value and name is stored as an instance of this class.
  /// The primary is stored first and is therefore found by normal lookup.
  struct Item {
    Item(E, std::string_view);

    E _value;               ///< Definition value.
    std::string_view _name; ///< Definition name

    /// Intrusive linkage for name lookup.
    struct NameLinkage {
      Item *_next{nullptr};
      Item *_prev{nullptr};

      static Item *&next_ptr(Item *);

      static Item *&prev_ptr(Item *);

      static std::string_view key_of(Item *);

      static uint32_t hash_of(std::string_view s);

      static bool equal(std::string_view const &lhs, std::string_view const &rhs);
    } _name_link;

    /// Intrusive linkage for value lookup.
    struct ValueLinkage {
      Item *_next{nullptr};
      Item *_prev{nullptr};

      static Item *&next_ptr(Item *);

      static Item *&prev_ptr(Item *);

      static E key_of(Item *);

      static uintmax_t hash_of(E);

      static bool equal(E lhs, E rhs);
    } _value_link;
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

template <typename E>
auto
Lexicon<E>::Item::NameLinkage::next_ptr(Item *item) -> Item *&
{
  return item->_name_link._next;
}

template <typename E>
auto
Lexicon<E>::Item::NameLinkage::prev_ptr(Item *item) -> Item *&
{
  return item->_name_link._prev;
}

template <typename E>
auto
Lexicon<E>::Item::ValueLinkage::next_ptr(Item *item) -> Item *&
{
  return item->_value_link._next;
}

template <typename E>
auto
Lexicon<E>::Item::ValueLinkage::prev_ptr(Item *item) -> Item *&
{
  return item->_value_link._prev;
}

template <typename E>
std::string_view
Lexicon<E>::Item::NameLinkage::key_of(Item *item)
{
  return item->_name;
}

template <typename E>
E
Lexicon<E>::Item::ValueLinkage::key_of(Item *item)
{
  return item->_value;
}

template <typename E>
uint32_t
Lexicon<E>::Item::NameLinkage::hash_of(std::string_view s)
{
  return Hash32FNV1a().hash_immediate(transform_view_of(&toupper, s));
}

template <typename E>
uintmax_t
Lexicon<E>::Item::ValueLinkage::hash_of(E value)
{
  // In almost all cases, the values will be (roughly) sequential, so an identity hash works well.
  return static_cast<uintmax_t>(value);
}

template <typename E>
bool
Lexicon<E>::Item::NameLinkage::equal(std::string_view const &lhs, std::string_view const &rhs)
{
  return 0 == strcasecmp(lhs, rhs);
}

template <typename E>
bool
Lexicon<E>::Item::ValueLinkage::equal(E lhs, E rhs)
{
  return lhs == rhs;
}

// -------

template <typename E>
auto
Lexicon<E>::NameDefault::destroy() -> self_type &
{
  if (_content == Content::HANDLER) {
    reinterpret_cast<UnknownValueHandler *>(_store)->~UnknownValueHandler();
  }
  _content = Content::NIL;
  return *this;
}

template <typename E> Lexicon<E>::NameDefault::~NameDefault()
{
  this->destroy();
}

template <typename E>
auto
Lexicon<E>::NameDefault::operator=(std::string_view name) -> self_type &
{
  this->destroy();
  new (_store) std::string_view(name);
  _content = Content::SCALAR;
  return *this;
}

template <typename E>
auto
Lexicon<E>::NameDefault::operator=(const UnknownValueHandler &handler) -> self_type &
{
  this->destroy();
  new (_store) UnknownValueHandler(handler);
  _content = Content::HANDLER;
  return *this;
}

template <typename E>
std::string_view
Lexicon<E>::NameDefault::operator()(E value)
{
  switch (_content) {
  case Content::SCALAR:
    return *reinterpret_cast<std::string_view *>(_store);
    break;
  case Content::HANDLER:
    return (*(reinterpret_cast<UnknownValueHandler *>(_store)))(value);
    break;
  default:
    throw std::domain_error(detail::what("Lexicon: unknown enumeration '{}'", uintmax_t(value)));
    break;
  }
}

// -------

template <typename E>
auto
Lexicon<E>::ValueDefault::destroy() -> self_type &
{
  if (_content == Content::HANDLER) {
    reinterpret_cast<UnknownNameHandler *>(_store)->~UnknownNameHandler();
  }
  _content = Content::NIL;
  return *this;
}

template <typename E> Lexicon<E>::ValueDefault::~ValueDefault()
{
  this->destroy();
}

template <typename E>
auto
Lexicon<E>::ValueDefault::operator=(E value) -> self_type &
{
  this->destroy();
  *(reinterpret_cast<E *>(_store)) = value;
  _content                         = Content::SCALAR;
  return *this;
}

template <typename E>
auto
Lexicon<E>::ValueDefault::operator=(const UnknownNameHandler &handler) -> self_type &
{
  this->destroy();
  new (_store) UnknownNameHandler(handler);
  _content = Content::HANDLER;
  return *this;
}

template <typename E>
E
Lexicon<E>::ValueDefault::operator()(std::string_view name)
{
  switch (_content) {
  case Content::SCALAR:
    return *(reinterpret_cast<E *>(_store));
    break;
  case Content::HANDLER:
    return (*(reinterpret_cast<UnknownNameHandler *>(_store)))(name);
    break;
  default:
    throw std::domain_error(swoc::LocalBufferWriter<128>().print("Lexicon: unknown name '{}'\0", name).data());
    break;
  }
}
// -------
// Lexicon

template <typename E> Lexicon<E>::Lexicon() {}

template <typename E> Lexicon<E>::Lexicon(const std::initializer_list<Definition> &items)
{
  for (auto item : items) {
    this->define(item.value, item.names);
  }
}

template <typename E> Lexicon<E>::Lexicon(const std::initializer_list<Pair> &items)
{
  for (auto item : items) {
    this->define(item);
  }
}

template <typename E>
template <E e>
Lexicon<E>::Lexicon(const Require<e> &, const std::array<Definition, static_cast<size_t>(e)> &defines)
{
  for (auto &def : defines) {
    this->define(def);
  }
}

template <typename E>
template <E e>
Lexicon<E>::Lexicon(const Require<e> &, const std::array<Pair, static_cast<size_t>(e)> &defines)
{
  for (auto &def : defines) {
    this->define(def);
  }
}

template <typename E>
std::string_view
Lexicon<E>::localize(std::string_view const &name)
{
  auto span = _arena.alloc(name.size());
  memcpy(span.data(), name.data(), name.size());
  return span.view();
}

template <typename E> std::string_view Lexicon<E>::operator[](E value)
{
  auto spot = _by_value.find(value);
  if (spot != _by_value.end()) {
    return spot->_name;
  }
  return _name_default(value);
}

template <typename E> E Lexicon<E>::operator[](std::string_view const &name)
{
  auto spot = _by_name.find(name);
  if (spot != _by_name.end()) {
    return spot->_value;
  }
  return _value_default(name);
}

template <typename E>
auto
Lexicon<E>::define(E value, const std::initializer_list<std::string_view> &names) -> self_type &
{
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
Lexicon<E>::define(E value, Args &&... names) -> self_type &
{
  static_assert(sizeof...(Args) > 0, "A defined value must have at least a priamry name");
  return this->define(value, {std::forward<Args>(names)...});
}

template <typename E>
auto
Lexicon<E>::define(const Pair &pair) -> self_type &
{
  return this->define(std::get<0>(pair), {std::get<1>(pair)});
}

template <typename E>
auto
Lexicon<E>::define(const Definition &init) -> self_type &
{
  return this->define(init.value, init.names);
}

template <typename E>
auto
Lexicon<E>::set_default(std::string_view name) -> self_type &
{
  _name_default = this->localize(name);
  return *this;
}

template <typename E>
auto
Lexicon<E>::set_default(E value) -> self_type &
{
  _value_default = value;
  return *this;
}

template <typename E>
auto
Lexicon<E>::set_default(const UnknownValueHandler &handler) -> self_type &
{
  _name_default = handler;
  return *this;
}

template <typename E>
auto
Lexicon<E>::set_default(const UnknownNameHandler &handler) -> self_type &
{
  _value_default = handler;
  return *this;
}

template <typename E>
size_t
Lexicon<E>::count() const
{
  return _by_value.count();
}

template <typename E>
auto
Lexicon<E>::begin() const -> const_iterator
{
  return const_iterator{static_cast<const Item *>(_by_value.begin())};
}

template <typename E>
auto
Lexicon<E>::end() const -> const_iterator
{
  return {};
}

// Iterators

template <typename E>
void
Lexicon<E>::const_iterator::update()
{
  std::get<0>(_v) = _item->_value;
  std::get<1>(_v) = _item->_name;
}

template <typename E> Lexicon<E>::const_iterator::const_iterator(const Item *item) : _item(item)
{
  if (_item) {
    this->update();
  };
}

template <typename E> auto Lexicon<E>::const_iterator::operator*() const -> reference
{
  return _v;
}

template <typename E> auto Lexicon<E>::const_iterator::operator-> () const -> pointer
{
  return &_v;
}

template <typename E>
bool
Lexicon<E>::const_iterator::operator==(self_type const &that) const
{
  return _item == that._item;
}

template <typename E>
bool
Lexicon<E>::const_iterator::operator!=(self_type const &that) const
{
  return _item != that._item;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator++() -> self_type &
{
  if (nullptr != (_item = _item->_value_link._next)) {
    this->update();
  }
  return *this;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator++(int) -> self_type
{
  self_type tmp{*this};
  ++*this;
  return tmp;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator--() -> self_type &
{
  if (nullptr != (_item = _item->_value_link->_prev)) {
    this->update();
  }
  return *this;
}

template <typename E>
auto
Lexicon<E>::const_iterator::operator--(int) -> self_type
{
  self_type tmp;
  ++*this;
  return tmp;
}

} // namespace swoc
