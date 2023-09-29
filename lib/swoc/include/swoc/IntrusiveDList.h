// SPDX-License-Identifier: Apache-2.0
// Copyright Apach Software Foundation 2019
/** @file

    Intrusive double linked list container.

    This provides support for a doubly linked list container. Items in the list must provide links
    inside the class and accessor functions for those links.

    @note This is a header only library.
*/

#pragma once

/// Clang doesn't like just declaring the tag struct we need so we have to include the file.
#include <iterator>
#include <type_traits>

#include "swoc/swoc_version.h"

namespace swoc { inline namespace SWOC_VERSION_NS {
/** Intrusive doubly linked list container.

    This holds items in a doubly linked list using links in the items. Items are placed in the list
    by changing the pointers. An item can be in only one list for a set of links, but an item can
    contain multiple sets of links. This requires different specializations of this template because
    link access is part of the type specification. Memory for items is not managed by this class -
    instances must be allocated and released elsewhere. In particular removing an item from the list
    does not destruct or free the item.

    Access to the links is described by a linkage class which is required to contain the following
    members:

    - The static method @c next_ptr which returns a reference to the pointer to the next item.

    - The static method @c prev_ptr which returns a reference to the pointer to the previous item.

    The pointer methods take a single argument of @c Item* and must return a reference to a pointer
    instance. This type is deduced from the methods and is not explicitly specified. It must be
    cheaply copyable and stateless.

    It is the responsibility of the item class to initialize the link pointers. When an item is
    removed from the list the link pointers are set to @c nullptr.

    An example declaration woudl be

    @code
      // Item in the list.
      struct Thing {
        Thing* _next {nullptr};
        Thing* _prev {nullptr};
        Data _payload;

        // Linkage descriptor.
        struct Linkage {
          static Thing*& next_ptr(Thing* Thing) { return Thing->_next; }
          static Thing*& prev_ptr(Thing* Thing) { return Thing->_prev; }
        };
      };

      using ThingList = IntrusiveDList<Thing::Linkage>;
    @endcode

    Item access is done by using either STL style iteration, or direct access to the member
    pointers. A client can have its own mechanism for getting an element to start, or use the @c
    head and/or @c tail methods to get the first and last elements in the list respectively. Note if
    the list is empty then @c nullptr will be returned. There are simple and fast conversions
    between item pointers and iterators.

  */
template <typename L> class IntrusiveDList {
  friend class iterator;

public:
  using self_type = IntrusiveDList; ///< Self reference type.
  /// The list item type.
  // Get the result of calling the pointer access function, then strip off reference and pointer
  // qualifiers. @c nullptr is used instead of the pointer type because this is done precisely
  // to find that type.
  using value_type = typename std::remove_pointer<
    typename std::remove_reference<typename std::invoke_result<decltype(L::next_ptr), std::nullptr_t>::type>::type>::type;

  /// Const iterator.
  class const_iterator {
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

    /// Convenience conversion to pointer type
    /// Because of how this list is normally used, being able to pass an iterator as a pointer is quite convienent.
    /// If the iterator isn't valid, it converts to @c nullptr.
    operator value_type *() const;

    /// Equality
    bool operator==(self_type const &that) const;

    /// Inequality
    bool operator!=(self_type const &that) const;

    /// Check if @c std::prev would be valid.
    /// @return @c true if calling @c std::prev would yield a valid iterator.
    /// @note Identical to @c has_predecessor.
    bool has_prev() const;

    /// Check if @c std::next would be valid.
    /// @return @c true if calling @c std::next would yield a valid iterator.
    bool has_next() const;

    /// Check if there is a predecessor.
    /// @return @c true if there is a predecessor element for the current element.
    /// @note Identical to @c has_prev.
    bool has_predecessor() const;

    /// Check if there is a successor.
    /// @return @c true if after incrementing, the iterator will reference a value.
    /// @note This is subtly different from @c has_net. It is false for the last element in the
    /// iteration. Even though incrementing such an iterator would valid, there would not be a
    /// successor element.
    bool has_successor() const;

  protected:
    // These are stored non-const to make implementing @c iterator easier. This class provides the required @c const
    // protection.
    list_type *_list{nullptr};                   ///< Needed to decrement from @c end() position.
    typename list_type::value_type *_v{nullptr}; ///< Referenced element.

    /// Internal constructor for containers.
    const_iterator(const list_type *list, value_type *v);
  };

  /// Iterator for the list.
  class iterator : public const_iterator {
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

    /// Convenience conversion to pointer type
    /// Because of how this list is normally used, being able to pass an iterator as a pointer is quite convenient.
    /// If the iterator isn't valid, it converts to @c nullptr.
    operator value_type *() const;

  protected:
    /// Internal constructor for containers.
    iterator(list_type *list, value_type *v);
  };

  /// Construct to empty list.
  IntrusiveDList() = default;

  IntrusiveDList(self_type const &that) = delete;

  /// Move list to @a this and leave @a that empty.
  IntrusiveDList(self_type &&that);

  /// No copy assignment because items can't be in two lists and can't copy items.
  self_type &operator=(const self_type &that) = delete;

  /// Move @a that to @a this.
  self_type &operator=(self_type &&that);

  /// Empty check.
  /// @return @c true if the list is empty.
  bool empty() const;

  /// Presence check (linear time).
  /// @return @c true if @a v is in the list, @c false if not.
  bool contains(const value_type *v) const;

  /// Add @a elt as the first element in the list.
  /// @return This container.
  self_type &prepend(value_type *v);

  /// Add @a elt as the last element in the list.
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

  /// Take @a v out of this list.
  /// @return The element after @a v.
  value_type *erase(value_type *v);

  /// Take the element at @a loc out of this list.
  /// @return Iterator for the next element.
  iterator erase(const iterator &loc);

  /// Take elements out of the list.
  /// Remove elements start with @a start up to but not including @a limit.
  /// @return @a limit
  iterator erase(const iterator &start, const iterator &limit);

  /** The nth element of the list.
   *
   * @param n Index of target element.
   * @return An iterator to the element, an empty iterator if @a n is too large.
   *
   * @note This is linear in @a n, use with caution.
   */
  iterator nth(unsigned n);

  /** Remove and return an initial subsequence.
   *
   * @param n Number of elements.
   * @return The list of elements.
   *
   * If @a n is more than the length of the list, the entire list is returned.
   */
  self_type take_prefix(unsigned n);

  /** Remove and return an initial subsequence.
   *
   * @param n Number of elements.
   * @return The list of elements.
   *
   * If @a n is more than the length of the list @a this is unchanged and an empty list returned.
   */
  self_type split_prefix(unsigned n);

  /** Remove and return a trailing subsequence.
   *
   * @param n Number of elements.
   * @return The list of elements.
   *
   * If @a n is more than the length of the list, the entire list is returned.
   */
  self_type take_suffix(unsigned n);

  /** Remove and return a trailing subsequence.
   *
   * @param n Number of elements.
   * @return The list of elements.
   *
   * If @a n is more than the length of the list, @a this is unchanged and an empty list returned.
   */
  self_type split_suffix(unsigned n);

  /** Append a list.
   *
   * @param src List to append.
   * @return @a this
   *
   * The elements are removed from @a src, which becomes empty.
   */
  self_type & append(self_type & src);

  /** Prepend a list.
   *
   * @param src List to prepend.
   * @return @a this
   *
   * The elements are removed from @a src, which becomes empty.
   */
  self_type & prepend(self_type & src);

  /** Splice in a list after an existing element.
   *
   * @param target Element in the lsit.
   * @param src List to splice.
   * @return @a this
   */
  self_type &insert_after(value_type *target, self_type & src);

  /** Splice in a list before an existing element.
   *
   * @param target Element in the lsit.
   * @param src List to splice.
   * @return @a this
   */
  self_type &insert_before(value_type *target, self_type & src);

  /** Splice in a list after an existing element.
   *
   * @param target Element in the lsit.
   * @param src List to splice.
   * @return @a this
   */
  self_type &insert_after(iterator const &target, self_type & src);

  /** Splice in a list before an existing element.
   *
   * @param target Element in the lsit.
   * @param src List to splice.
   * @return @a this
   */
  self_type &insert_before(iterator const &target, self_type & src);

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
   * It is the responsibility of the caller that @a v is in the list. The purpose is to make
   * iteration starting at a specific element easier (i.e. all of the link manipulation and checking
   * is done by the iterator).
   *
   * @return An @c iterator that refers to @a v.
   */
  iterator iterator_for(value_type *v);

  const_iterator iterator_for(const value_type *v) const;

  /// Get the first element.
  value_type *head();

  const value_type *head() const;

  /// Get the last element.
  value_type *tail();

  const value_type *tail() const;

  /** Apply a functor to every element in the list.
   * This iterates over the list correctly even if the functor destroys or removes elements.
   */
  template <typename F> self_type &apply(F &&f);

protected:
  value_type *_head{nullptr}; ///< First element in list.
  value_type *_tail{nullptr}; ///< Last element in list.
  size_t _count{0};           ///< Number of elements in list.
};

/** Utility class to provide intrusive links.
 *
 * @tparam T Class to link.
 *
 * The normal use is to declare this as a member to provide the links and the linkage functions.
 * @code
 * class Thing {
 *   // blah blah
 *   Thing* _next{nullptr};
 *   Thing* _prev{nullptr};
 *   using Linkage = swoc::IntrusiveLinkage<Thing, &Thing::_next, &Thing::_prev>;
 * };
 * using ThingList = swoc::IntrusiveDList<Thing::Linkage>;
 * @endcode
 * The template will default to the names '_next' and '_prev' therefore in the example it could
 * have been done as
 * @code
 *   using Linkage = swoc::IntrusiveLinkage<Thing>;
 * @endcode
 */
template <typename T, T *(T::*NEXT) = &T::_next, T *(T::*PREV) = &T::_prev> struct IntrusiveLinkage {
  static T *&next_ptr(T *thing); ///< Retrieve reference to next pointer.
  static T *&prev_ptr(T *thing); ///< Retrive reference to previous pointer.
};

template <typename T, T *(T::*NEXT), T *(T::*PREV)>
T *&
IntrusiveLinkage<T, NEXT, PREV>::next_ptr(T *thing) {
  return thing->*NEXT;
}

template <typename T, T *(T::*NEXT), T *(T::*PREV)>
T *&
IntrusiveLinkage<T, NEXT, PREV>::prev_ptr(T *thing) {
  return thing->*PREV;
}

/** Utility cast to change the underlying type of a pointer reference.
 *
 * @tparam T The resulting pointer reference type.
 * @tparam P The starting pointer reference type.
 * @param p A reference to pointer to @a P.
 * @return A reference to the same pointer memory of type @c T*&.
 *
 * This changes a reference to a pointer to @a P to a reference to a pointer to @a T. This is useful
 * for intrusive links that are inherited. For instance
 *
 * @code
 * class Thing { Thing* _next; ... }
 * class BetterThing : public Thing { ... };
 * @endcode
 *
 * To make @c BetterThing work with an intrusive container without making new link members,
 *
 * @code
 * static BetterThing*& next_ptr(BetterThing* bt) {
 *   return swoc::ptr_ref_cast<BetterThing>(_next);
 * }
 * @endcode
 *
 * This is both convenient and gets around aliasing warnings from the compiler that can arise from
 * using @c reinterpret_cast.
 */
template <typename T, typename P>
T *&
ptr_ref_cast(P *&p) {
  union {
    P **_p;
    T **_t;
  } u{&p};
  return *(u._t);
}

/** Utility class to provide intrusive links.
 *
 * @tparam T Class to link.
 *
 * The normal use is to declare this as a member to provide the links and the linkage functions.
 * @code
 * class Thing {
 *   // blah blah
 *   Thing* _next{nullptr};
 *   Thing* _prev{nullptr};
 *   using Linkage = swoc::IntrusiveLinkage<Thing, &Thing::_next, &Thing::_prev>;
 * };
 * using ThingList = swoc::IntrusiveDList<Thing::Linkage>;
 * @endcode
 * The template will default to the names '_next' and '_prev' therefore in the example it could
 * have been done as
 * @code
 *   using Linkage = swoc::IntrusiveLinkage<Thing>;
 * @endcode
 */
template <typename T, typename L> struct IntrusiveLinkageRebind {
  static T *&next_ptr(T *thing); ///< Retrieve reference to next pointer.
  static T *&prev_ptr(T *thing); ///< Retrive reference to previous pointer.
};

template <typename T, typename L>
T *&
IntrusiveLinkageRebind<T, L>::next_ptr(T *thing) {
  return ptr_ref_cast<T>(L::next_ptr(thing));
}

template <typename T, typename L>
T *&
IntrusiveLinkageRebind<T, L>::prev_ptr(T *thing) {
  return ptr_ref_cast<T>(L::prev_ptr(thing));
}

template < typename T > struct IntrusiveLinks {
  T * _next = nullptr;
  T * _prev = nullptr;
};

template < typename T, IntrusiveLinks<T> (T::* links) > struct IntrusiveLinkDescriptor {
  static T *&next_ptr(T *thing); ///< Retrieve reference to next pointer.
  static T *&prev_ptr(T *thing); ///< Retrive reference to previous pointer.
};

// --- Implementation ---

template <typename L> IntrusiveDList<L>::const_iterator::const_iterator() {}

template <typename L>
IntrusiveDList<L>::const_iterator::const_iterator(const list_type *list, value_type *v)
  : _list(const_cast<list_type *>(list)), _v(const_cast<typename list_type::value_type *>(v)) {}

template <typename L> IntrusiveDList<L>::iterator::iterator() {}

template <typename L> IntrusiveDList<L>::iterator::iterator(list_type *list, value_type *v) : super_type(list, v) {}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator++() -> self_type & {
  _v = L::next_ptr(_v);
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator++() -> self_type & {
  this->super_type::operator++();
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator++(int) -> self_type {
  self_type tmp(*this);
  ++*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator++(int) -> self_type {
  self_type tmp(*this);
  ++*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator--() -> self_type & {
  if (_v) {
    _v = L::prev_ptr(_v);
  } else if (_list) {
    _v = _list->_tail;
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator--() -> self_type & {
  this->super_type::operator--();
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator--(int) -> self_type {
  self_type tmp(*this);
  --*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator--(int) -> self_type {
  self_type tmp(*this);
  --*this;
  return tmp;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator->() const -> value_type * {
  return _v;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator->() const -> value_type * {
  return super_type::_v;
}

template <typename L> IntrusiveDList<L>::const_iterator::operator value_type *() const {
  return _v;
}

template <typename L>
auto
IntrusiveDList<L>::const_iterator::operator*() const -> value_type & {
  return *_v;
}

template <typename L>
auto
IntrusiveDList<L>::iterator::operator*() const -> value_type & {
  return *super_type::_v;
}

template <typename L> IntrusiveDList<L>::iterator::operator value_type *() const {
  return super_type::_v;
}

/// --- Main class

template <typename L>
IntrusiveDList<L>::IntrusiveDList(self_type &&that) : _head(that._head), _tail(that._tail), _count(that._count) {
  that.clear();
}

template <typename L>
bool
IntrusiveDList<L>::empty() const {
  return _head == nullptr;
}

template <typename L>
bool
IntrusiveDList<L>::contains(const value_type *v) const {
  for (auto thing = _head; thing; thing = L::next_ptr(thing)) {
    if (thing == v)
      return true;
  }
  return false;
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::operator==(self_type const &that) const {
  return this->_v == that._v;
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::operator!=(self_type const &that) const {
  return this->_v != that._v;
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::has_predecessor() const {
  return _v ? nullptr != L::prev_ptr(_v) : !_list->empty();
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::has_prev() const {
  return _v ? nullptr != L::prev_ptr(_v) : !_list->empty();
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::has_successor() const {
  return _v && nullptr != L::next_ptr(_v);
}

template <typename L>
bool
IntrusiveDList<L>::const_iterator::has_next() const {
  return nullptr != _v;
}

template <typename L>
auto
IntrusiveDList<L>::prepend(value_type *v) -> self_type & {
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
IntrusiveDList<L>::append(value_type *v) -> self_type & {
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
IntrusiveDList<L>::take_head() -> value_type * {
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
IntrusiveDList<L>::take_tail() -> value_type * {
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
IntrusiveDList<L>::insert_after(value_type *target, value_type *v) -> self_type & {
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
IntrusiveDList<L>::insert_after(iterator const &target, value_type *v) -> self_type & {
  return this->insert_after(target._v, v);
}

template <typename L>
auto
IntrusiveDList<L>::insert_before(value_type *target, value_type *v) -> self_type & {
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
IntrusiveDList<L>::insert_before(iterator const &target, value_type *v) -> self_type & {
  return this->insert_before(target._v, v);
}

template <typename L>
auto
IntrusiveDList<L>::insert_after(value_type *target, self_type & src) -> self_type & {
  if (target && src._count > 0) {
    if (_tail == target) {
      this->append(src);
    } else { // invariant - @a target is not tail therefore has a successor.
      L::next_ptr(src._tail) = L::next_ptr(target); // link @a src tail to @a target successor
      L::prev_ptr(L::next_ptr(src._tail)) = src._tail;

      L::prev_ptr(src._head) = target; // link @a src head to @target
      L::next_ptr(target) = src._head;

      _count += src._count;
      src.clear();
    }
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::insert_after(iterator const &target, self_type & src) -> self_type & {
  return this->insert_after(target._v, src);
}

template <typename L>
auto
IntrusiveDList<L>::insert_before(value_type *target, self_type & src) -> self_type & {
  if (target && src._count > 0) {
    if (_head == target) {
      this->prepend(src);
    } else { // invariant - @a target is not head and therefore has a predecessor.
      L::prev_ptr(src._head) = L::prev_ptr(target); // link @a src head to @a target predecessor
      L::next_ptr(L::prev_ptr(target)) = src._head;

      L::next_ptr(src._tail) = target; // link @a src tail to @a target
      L::prev_ptr(target) = src._tail;

      _count += src._count;
      src.clear();
    }
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::insert_before(iterator const &target, self_type & src) -> self_type & {
  return this->insert_before(target._v, src);
}

template <typename L>
auto
IntrusiveDList<L>::erase(value_type *v) -> value_type * {
  value_type *zret{nullptr};

  if (L::prev_ptr(v)) {
    L::next_ptr(L::prev_ptr(v)) = L::next_ptr(v);
  }
  if (L::next_ptr(v)) {
    zret                        = L::next_ptr(v);
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

  return zret;
}

template <typename L>
auto
IntrusiveDList<L>::erase(const iterator &loc) -> iterator {
  return this->iterator_for(this->erase(loc._v));
}

template <typename L>
auto
IntrusiveDList<L>::erase(const iterator &first, const iterator &limit) -> iterator {
  value_type *spot = first;
  value_type *prev{L::prev_ptr(spot)};
  if (prev) {
    L::next_ptr(prev) = limit;
  }
  if (spot == _head) {
    _head = limit;
  }
  // tail is only updated if @a limit is @a end (e.g., @c nullptr).
  if (nullptr == limit) {
    _tail = prev;
  } else {
    L::prev_ptr(limit) = prev;
  }
  // Clear links in removed elements.
  while (spot != limit) {
    value_type *target{spot};
    spot                = L::next_ptr(spot);
    L::prev_ptr(target) = L::next_ptr(target) = nullptr;
  };

  return {limit._v, this};
}

template <typename L>
auto
IntrusiveDList<L>::operator=(self_type &&that) -> self_type & {
  if (this != &that) {
    this->_head  = that._head;
    this->_tail  = that._tail;
    this->_count = that._count;
    that.clear();
  }
  return *this;
}

template <typename L>
size_t
IntrusiveDList<L>::count() const {
  return _count;
}

template <typename L>
auto
IntrusiveDList<L>::begin() const -> const_iterator {
  return const_iterator{this, _head};
}

template <typename L>
auto
IntrusiveDList<L>::begin() -> iterator {
  return iterator{this, _head};
}

template <typename L>
auto
IntrusiveDList<L>::end() const -> const_iterator {
  return const_iterator{this, nullptr};
}

template <typename L>
auto
IntrusiveDList<L>::end() -> iterator {
  return iterator{this, nullptr};
}

template <typename L>
auto
IntrusiveDList<L>::iterator_for(value_type *v) -> iterator {
  return iterator{this, v};
}

template <typename L>
auto
IntrusiveDList<L>::iterator_for(const value_type *v) const -> const_iterator {
  return const_iterator{this, v};
}

template <typename L>
auto
IntrusiveDList<L>::tail() -> value_type * {
  return _tail;
}

template <typename L>
auto
IntrusiveDList<L>::tail() const -> const value_type * {
  return _tail;
}

template <typename L>
auto
IntrusiveDList<L>::head() -> value_type * {
  return _head;
}

template <typename L>
auto
IntrusiveDList<L>::head() const -> const value_type * {
  return _head;
}

template <typename L>
auto
IntrusiveDList<L>::clear() -> self_type & {
  _head = _tail = nullptr;
  _count        = 0;
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::nth(unsigned int n) -> iterator {
  if (n >= _count) {
    return {};
  }

  value_type * spot;
  if (n < _count/2) { // closer to head, count from there..
    spot = _head;
    while (n-- > 0) { spot = L::next_ptr(spot); }
  } else { // count from tail
    spot = _tail;
    unsigned idx = _count - 1;
    while (idx-- > n) { spot = L::prev_ptr(spot); }
  }
  return iterator_for(spot);
}

template <typename L>
auto
IntrusiveDList<L>::take_prefix(unsigned int n) -> self_type {

  if (n == 0) {
    return {};
  }

  if (_count <= n) {
    return std::move(*this);
  }

  // Invariant - there is at least one element that will not be taken.

  self_type zret;
  value_type * spot = this->nth(n); // new @a head for @a this

  zret._count = n;
  zret._head = _head;
  zret._tail = L::prev_ptr(spot);
  L::next_ptr(zret._tail) = nullptr;

  _count -= n;
  _head = spot;
  L::prev_ptr(_head) = nullptr;

  return zret;
}

template <typename L>
auto
IntrusiveDList<L>::split_prefix(unsigned int n) -> self_type {
  if (n <= _count) {
    return this->take_prefix(n);
  }
  return {};
}

template <typename L>
auto
IntrusiveDList<L>::take_suffix(unsigned int n) -> self_type {

  if (n == 0) {
    return {};
  }

  if (_count <= n) {
    return std::move(*this);
  }

  // Invariant - there is at least one element that will not be taken.

  self_type zret;
  value_type * spot = this->nth(_count - n - 1); // new @a tail for @a this

  zret._count = n;
  zret._head = L::next_ptr(spot);
  L::prev_ptr(zret._head) = nullptr;
  zret._tail = _tail;

  _count -= n;
  _tail = spot;
  L::next_ptr(_tail) = nullptr;

  return zret;
}

template <typename L>
auto
IntrusiveDList<L>::split_suffix(unsigned int n) -> self_type {
  if (n <= _count) {
    return this->take_suffix(n);
  }
  return {};
}

template <typename L>
auto
IntrusiveDList<L>::prepend(IntrusiveDList::self_type &src) -> self_type & {
  if (src._count > 0) {
    if (_count == 0) {
      *this = std::move(src);
    } else {
      L::prev_ptr(_head)     = src._tail;
      L::next_ptr(src._tail) = _head;
      _count += src._count;
      _head = src._head;
      src.clear();
    }
  }
  return *this;
}

template <typename L>
auto
IntrusiveDList<L>::append(IntrusiveDList::self_type &src) -> self_type & {
  if (src._count > 0) {
    if (_count == 0) {
      *this = std::move(src);
    } else {
      L::next_ptr(_tail)     = src._head;
      L::prev_ptr(src._head) = _tail;
      _count += src._count;
      _tail = src._tail;
      src.clear();
    }
  }
  return *this;
}

namespace detail {
/// @cond INTERNAL_DETAIL
// Make @c apply more convenient by allowing the function to take a reference type or pointer type
// to the container elements. The pointer type is the base, plus a shim to convert from a reference
// type functor to a pointer type. The complex return type definition forces only one, but
// not both, to be valid for a particular functor. This also must be done via free functions and not
// method overloads because the compiler forces a match up of method definitions and declarations
// before any template instantiation.

template <typename L, typename F>
auto
Intrusive_DList_Apply(IntrusiveDList<L> &list, F &&f)
  -> decltype(f(*static_cast<typename IntrusiveDList<L>::value_type *>(nullptr)), list) {
  return list.apply([&f](typename IntrusiveDList<L>::value_type *v) { return f(*v); });
}

template <typename L, typename F>
auto
Intrusive_DList_Apply(IntrusiveDList<L> &list, F &&f)
  -> decltype(f(static_cast<typename IntrusiveDList<L>::value_type *>(nullptr)), list) {
  auto spot{list.begin()};
  auto limit{list.end()};
  while (spot != limit) {
    f(spot++); // post increment means @a spot is updated before @a f is applied.
  }
  return list;
}
} // namespace detail

template <typename L>
template <typename F>
auto
IntrusiveDList<L>::apply(F &&f) -> self_type & {
  return detail::Intrusive_DList_Apply(*this, f);
}
/// @endcond
}} // namespace swoc::SWOC_VERSION_NS
