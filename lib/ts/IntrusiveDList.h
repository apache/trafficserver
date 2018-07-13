/** @file

    Intrusive double linked list container.

    This provide support for a doubly linked list container for an
    arbitrary class that uses the class directly and not wrapped. It
    requires the class to provide the list pointers.

    @note This is a header only library.

    @note Due to bugs in either the C++ standard or gcc (or both), the
    link members @b must be declared in the class used for the
    list. If they are declared in a super class you will get "could
    not convert template argument" errors, even though it should
    work. This is because @c &T::m is of type @c S::* if @c S is a
    super class of @c T and @c m is declared in @c S. My view is that
    if I write "&T::m" I want a "T::*" and the compiler shouldn't go
    rummaging through the class hierarchy for some other type. For
    MSVC you can @c static_cast the template arguments as a
    workaround, but not in gcc.

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

/// FreeBSD doesn't like just declaring the tag struct we need so we have to include the file.
#include <iterator>
#include <type_traits>

/** Intrusive doubly linked list container.

    This holds items in a doubly linked list using links in the items. Items are placed in the list by changing the
    pointers. An item can be in only one list for a set of links, but an item can contain multiple sets of links. This
    requires different specializations of this template because link access is part of the type specification. Memory
    for items is not managed by this class - instances must be allocated and released elsewhere. In particular removing
    an item from the list does not destruct or free the item.

    Access to the links is described by a linkage class which is required to contain the following members:

    - The static method @c next_ptr which returns a reference to the pointer to the next item.

    - The static method @c prev_ptr which returns a reference to the pointer to the previous item.

    The pointer methods take a single argument of @c Item* and must return a reference to a pointer instance. This
    type is deduced from the methods and is not explicitly specified. It must be cheaply copyable and stateless.

    An example declaration woudl be

    @code
      // Item in the list.
      struct Thing {
        Thing* _next;
        Thing* _prev;
        Data _payload;

        // Linkage descriptor.
        struct Linkage {
          static Thing*& next_ptr(Thing* Thing) { return Thing->_next; }
          static Thing*& prev_ptr(Thing* Thing) { return Thing->_prev; }
        };
      };

      using ThingList = IntrusiveDList<Thing::Linkage>;
    @endcode

    Element access is done by using either STL style iteration, or direct access to the member pointers. A client can
    have its own mechanism for getting an element to start, or use the @c head and/or @c tail methods to get the
    first and last elements in the list respectively. Note if the list is empty then @c Linkage::NIL will be returned.

  */
template <typename L> class IntrusiveDList
{
  friend class iterator;

public:
  using self_type = IntrusiveDList; ///< Self reference type.
  /// The list item type.
  using value_type = typename std::remove_pointer<typename std::remove_reference<decltype(L::next_ptr(nullptr))>::type>::type;

  /** Const iterator for the list.
   */
  class const_iterator
  {
    using self_type = const_iterator; ///< Self reference type.
    friend class IntrusiveDList;

  public:
    using list_type  = IntrusiveDList;                       ///< Container type.
    using value_type = const typename list_type::value_type; /// Import for API compliance.
    // STL algorithm compliance.
    using iterator_category = std::bidirectional_iterator_tag;
    using pointer           = value_type *;
    using reference         = value_type &;
    using difference_type   = int;

    /// Default constructor.
    const_iterator();

    /// Pre-increment.
    /// Move to the next element in the list.
    /// @return The iterator.
    self_type &operator++();

    /// Pre-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator.
    self_type &operator--();

    /// Post-increment.
    /// Move to the next element in the list.
    /// @return The iterator value before the increment.
    self_type operator++(int);

    /// Post-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator value before the decrement.
    self_type operator--(int);

    /// Dereference.
    /// @return A reference to the referent.
    value_type &operator*() const;

    /// Dereference.
    /// @return A pointer to the referent.
    value_type *operator->() const;

    /// Equality
    bool operator==(self_type const &that) const;

    /// Inequality
    bool operator!=(self_type const &that) const;

  protected:
    // These are stored non-const to make implementing @c iterator easier. This class provides the required @c const
    // protection.
    list_type *_list{nullptr};                   ///< Needed to descrement from @c end() position.
    typename list_type::value_type *_v{nullptr}; ///< Referenced element.

    /// Internal constructor for containers.
    const_iterator(const list_type *list, value_type *v);
  };

  /** Iterator for the list.
   */
  class iterator : public const_iterator
  {
    using self_type  = iterator;       ///< Self reference type.
    using super_type = const_iterator; ///< Super class type.

    friend class IntrusiveDList;

  public:
    using list_type  = IntrusiveDList;                 /// Must hoist this for direct use.
    using value_type = typename list_type::value_type; /// Import for API compliance.
    // STL algorithm compliance.
    using iterator_category = std::bidirectional_iterator_tag;
    using pointer           = value_type *;
    using reference         = value_type &;

    /// Default constructor.
    iterator();

    /// Pre-increment.
    /// Move to the next element in the list.
    /// @return The iterator.
    self_type &operator++();

    /// Pre-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator.
    self_type &operator--();

    /// Post-increment.
    /// Move to the next element in the list.
    /// @return The iterator value before the increment.
    self_type operator++(int);

    /// Post-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator value before the decrement.
    self_type operator--(int);

    /// Dereference.
    /// @return A reference to the referent.
    value_type &operator*() const;

    /// Dereference.
    /// @return A pointer to the referent.
    value_type *operator->() const;

  protected:
    /// Internal constructor for containers.
    iterator(list_type *list, value_type *v);
  };

  /// Empty check.
  /// @return @c true if the list is empty.
  bool empty() const;

  /// Presence check (linear time).
  /// @return @c true if @a v is in the list, @c false if not.
  bool contains(value_type *v) const;

  /// Add @a elt as the first element in the list.
  /// @return This container.
  self_type &prepend(value_type *v);

  /// Add @elt as the last element in the list.
  /// @return This container.
  self_type &append(value_type *v);

  /// Remove the first element of the list.
  /// @return A poiner to the removed item, or @c nullptr if the list was empty.
  value_type *take_head();

  /// Remove the last element of the list.
  /// @return A poiner to the removed item, or @c nullptr if the list was empty.
  value_type *take_tail();

  /// Insert a new element @a elt after @a target.
  /// The caller is responsible for ensuring @a target is in this list and @a elt is not in a list.
  /// @return This list.
  self_type &insert_after(value_type *target, value_type *v);

  /// Insert a new element @a v before @a target.
  /// The caller is responsible for ensuring @a target is in this list and @a elt is not in a list.
  /// @return This list.
  self_type &insert_before(value_type *target, value_type *v);

  /// Insert a new element @a elt after @a target.
  /// If @a target is the end iterator, @a v is appended to the list.
  /// @return This list.
  self_type &insert_after(iterator const &target, value_type *v);

  /// Insert a new element @a v before @a target.
  /// If @a target is the end iterator, @a v is appended to the list.
  /// @return This list.
  self_type &insert_before(iterator const &target, value_type *v);

  /// Take @a elt out of this list.
  /// @return This list.
  self_type &erase(value_type *v);

  /// Remove all elements.
  /// @note @b No memory management is done!
  /// @return This container.
  self_type &clear();

  /// @return Number of elements in the list.
  size_t count() const;

  /// Get an iterator to the first element.
  iterator begin();

  /// Get an iterator to the first element.
  const_iterator begin() const;

  /// Get an iterator past the last element.
  iterator end();

  /// Get an iterator past the last element.
  const_iterator end() const;

  /** Get an iterator for the item @a v.
   *
   * It is the responsibility of the caller that @a v is in the list. The purpose is to make iteration starting
   * at a specific element easier (i.e. all of the link manipulation and checking is done by the iterator).
   *
   * @return An @c iterator that refers to @a v.
   */
  iterator iterator_for(value_type *v);
  const_iterator iterator_for(const value_type *v) const;

  /// Get the first element.
  value_type *head();

  /// Get the last element.
  value_type *tail();

protected:
  value_type *_head{nullptr}; ///< First element in list.
  value_type *_tail{nullptr}; ///< Last element in list.
  size_t _count{0};           ///< # of elements in list.
};

template <typename L> IntrusiveDList<L>::const_iterator::const_iterator() {}

template <typename L>
IntrusiveDList<L>::const_iterator::const_iterator(const list_type *list, value_type *v)
  : _list(const_cast<list_type *>(list)), _v(const_cast<typename list_type::value_type *>(v))
{
}

template <typename L> IntrusiveDList<L>::iterator::iterator() {}

template <typename L> IntrusiveDList<L>::iterator::iterator(IntrusiveDList *list, value_type *v) : super_type(list, v) {}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator++() -> self_type &
{
  _v = L::next_ptr(_v);
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator++() -> self_type &
{
  this->super_type::operator++();
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator++(int) -> self_type
{
  self_type tmp(*this);
  ++*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator++(int) -> self_type
{
  self_type tmp(*this);
  ++*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator--() -> self_type &
{
  if (_v) {
    _v = L::prev_ptr(_v);
  } else if (_list) {
    _v = _list->_tail;
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator--() -> self_type &
{
  this->super_type::operator--();
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator--(int) -> self_type
{
  self_type tmp(*this);
  --*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator--(int) -> self_type
{
  self_type tmp(*this);
  --*this;
  return tmp;
}

template <typename L> auto IntrusiveDList<L>::const_iterator::operator-> () const -> value_type *
{
  return _v;
}

template <typename L> auto IntrusiveDList<L>::iterator::operator-> () const -> value_type *
{
  return super_type::_v;
}

template <typename L> auto IntrusiveDList<L>::const_iterator::operator*() const -> value_type &
{
  return *_v;
}

template <typename L> auto IntrusiveDList<L>::iterator::operator*() const -> value_type &
{
  return *super_type::_v;
}

template <typename L>
bool
IntrusiveDList<L>::empty() const
{
  return _head == nullptr;
}

template <typename L>
bool
IntrusiveDList<L>::contains(value_type *v) const
{
  for (auto thing = _head; thing; thing = L::next_ptr(thing)) {
    if (thing == v)
      return true;
  }
  return false;
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::operator==(self_type const &that) const
{
  return this->_v == that._v;
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::operator!=(self_type const &that) const
{
  return this->_v != that._v;
}

template <typename L>
auto
IntrusiveDList<L>::prepend(value_type *v) -> self_type &
{
  L::prev_ptr(v) = nullptr;
  if (nullptr != (L::next_ptr(v) = _head)) {
    L::prev_ptr(_head) = v;
  } else {
    _tail = v; // transition empty -> non-empty
  }
  _head = v;
  ++_count;
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::append(value_type *v) -> self_type &
{
  L::next_ptr(v) = nullptr;
  if (nullptr != (L::prev_ptr(v) = _tail)) {
    L::next_ptr(_tail) = v;
  } else {
    _head = v; // transition empty -> non-empty
  }
  _tail = v;
  ++_count;
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::take_head() -> value_type *
{
  value_type *zret = _head;
  if (_head) {
    if (nullptr == (_head = L::next_ptr(_head))) {
      _tail = nullptr; // transition non-empty -> empty
    } else {
      L::prev_ptr(_head) = nullptr;
    }
    L::next_ptr(zret) = L::prev_ptr(zret) = nullptr;
    --_count;
  }
  return zret;
}

template <typename L>
auto
IntrusiveDList<L>::take_tail() -> value_type *
{
  value_type *zret = _tail;
  if (_tail) {
    if (nullptr == (_tail = L::prev_ptr(_tail))) {
      _head = nullptr; // transition non-empty -> empty
    } else {
      L::next_ptr(_tail) = nullptr;
    }
    L::next_ptr(zret) = L::prev_ptr(zret) = nullptr;
    --_count;
  }
  return zret;
}

template <typename L>
auto
IntrusiveDList<L>::insert_after(value_type *target, value_type *v) -> self_type &
{
  if (target) {
    if (nullptr != (L::next_ptr(v) = L::next_ptr(target))) {
      L::prev_ptr(L::next_ptr(v)) = v;
    } else if (_tail == target) {
      _tail = v;
    }
    L::prev_ptr(v)      = target;
    L::next_ptr(target) = v;

    ++_count;
  } else {
    this->append(v);
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::insert_after(iterator const &target, value_type *v) -> self_type &
{
  return this->insert_after(target._v, v);
}

template <typename L>
auto
IntrusiveDList<L>::insert_before(value_type *target, value_type *v) -> self_type &
{
  if (target) {
    if (nullptr != (L::prev_ptr(v) = L::prev_ptr(target))) {
      L::next_ptr(L::prev_ptr(v)) = v;
    } else if (target == _head) {
      _head = v;
    }
    L::next_ptr(v)      = target;
    L::prev_ptr(target) = v;

    ++_count;
  } else {
    this->append(v);
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::insert_before(iterator const &target, value_type *v) -> self_type &
{
  return this->insert_before(target._v, v);
}

template <typename L>
auto
IntrusiveDList<L>::erase(value_type *v) -> self_type &
{
  if (L::prev_ptr(v)) {
    L::next_ptr(L::prev_ptr(v)) = L::next_ptr(v);
  }
  if (L::next_ptr(v)) {
    L::prev_ptr(L::next_ptr(v)) = L::prev_ptr(v);
  }
  if (v == _head) {
    _head = L::next_ptr(v);
  }
  if (v == _tail) {
    _tail = L::prev_ptr(v);
  }
  L::prev_ptr(v) = L::next_ptr(v) = nullptr;
  --_count;

  return *this;
}

template <typename L>
size_t
IntrusiveDList<L>::count() const
{
  return _count;
};

template <typename L>
auto
IntrusiveDList<L>::begin() const -> const_iterator
{
  return const_iterator{this, _head};
};

template <typename L>
auto
IntrusiveDList<L>::begin() -> iterator
{
  return iterator{this, _head};
};

template <typename L>
auto
IntrusiveDList<L>::end() const -> const_iterator
{
  return const_iterator{this, nullptr};
};

template <typename L>
auto
IntrusiveDList<L>::end() -> iterator
{
  return iterator{this, nullptr};
};

template <typename L>
auto
IntrusiveDList<L>::iterator_for(value_type *v) -> iterator
{
  return iterator{this, v};
};

template <typename L>
auto
IntrusiveDList<L>::iterator_for(const value_type *v) const -> const_iterator
{
  return const_iterator{this, v};
};

template <typename L>
auto
IntrusiveDList<L>::tail() -> value_type *
{
  return _tail;
}

template <typename L>
auto
IntrusiveDList<L>::head() -> value_type *
{
  return _head;
}

template <typename L>
auto
IntrusiveDList<L>::clear() -> self_type &
{
  _head = _tail = nullptr;
  _count        = 0;
  return *this;
};
