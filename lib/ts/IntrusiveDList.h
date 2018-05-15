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

/** Intrusive doubly linked list container.

    This holds items in a doubly linked list using members of the
    items.  Elements are copied in to the list. No memory management
    is done by the list implementation.

    To use this class a client should create the structure for
    elements of the list and ensure that it has two self pointers to
    be used by the list. For example,

    @code
      struct Elt {
        int _payload;
        Elt* _next;
        Elt* _prev;
      };
    @endcode

    The list is declared as
    @code
      typedef IntrusiveDList<Elt, &Elt::_next, &Elt::_prev> EltList;
    @endcode

    An element can be in multiple types of lists simultaneously as
    long as each list type uses distinct members. It is not possible
    for an element to be in more than one list of the same type
    simultaneously.  This is intrinsic to intrusive list support.

    Element access is done by using either STL style iteration, or
    direct access to the member pointers. A client can have its own
    mechanism for getting an element to start, or use the @c getHead
    and/or @c getTail methods to get the first and last elements in
    the list respectively.

    @note Due to bugs in various compilers or the C++ specification
    (or both) it is not possible in general to declare the element
    pointers in a super class. The template argument @c T must be
    exactly the same @c T as for the element pointers, even though a
    pointer to member of a superclass should be trivially coerced to a
    pointer to member of subclass. MSVC permits an explicit cast in
    this case, but gcc does not and therefore there is no way to do
    this. It is most vexing.

    P.S. I think it's a compiler bug personally with regard to the
    type of an expression of the form @c &T::M is not @c T::* if @c M
    is declared in a superclass S. In that case the type is @c S::*
    which seems very wrong to me.

  */
template <typename T, ///< Type of list element.
          T *(T::*N), ///< Member to use for pointer to next element.
          T *(T::*P)  ///< Member to use for pointer to previous element.
          >
class IntrusiveDList
{
  friend class iterator;

public:
  typedef IntrusiveDList self; ///< Self reference type.
  typedef T element_type;      ///< Type of list element.
                               /** STL style iterator for access to elements.
                                */
  class iterator
  {
    friend class IntrusiveDList;

  public:
    typedef iterator self;       ///< Self reference type.
    typedef T value_type;        ///< Referenced type for iterator.
    typedef int difference_type; ///< Distance type.
    typedef T *pointer;          ///< Pointer to referent.
    typedef T &reference;        ///< Reference to referent.
    typedef std::bidirectional_iterator_tag iterator_category;

    /// Default constructor.
    iterator() : _list(0), _elt(0) {}
    /// Equality test.
    /// @return @c true if @c this and @a that refer to the same object.
    bool
    operator==(self const &that) const
    {
      return _list == that._list && _elt == that._elt;
    }
    /// Pre-increment.
    /// Move to the next element in the list.
    /// @return The iterator.
    self &
    operator++()
    {
      if (_elt)
        _elt = _elt->*N;
      return *this;
    }
    /// Pre-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator.
    self &
    operator--()
    {
      if (_elt)
        _elt = _elt->*P;
      else if (_list)
        _elt = _list->_tail;
      return *this;
    }
    /// Post-increment.
    /// Move to the next element in the list.
    /// @return The iterator value before the increment.
    self
    operator++(int)
    {
      self tmp(*this);
      ++*this;
      return tmp;
    }
    /// Post-decrement.
    /// Move to the previous element in the list.
    /// @return The iterator value before the decrement.
    self
    operator--(int)
    {
      self tmp(*this);
      --*this;
      return tmp;
    }
    /// Inequality test.
    /// @return @c true if @c this and @a do not refer to the same object.
    bool
    operator!=(self const &that) const
    {
      return !(*this == that);
    }
    /// Dereference.
    /// @return A reference to the referent.
    reference operator*() { return *_elt; }
    /// Dereference.
    /// @return A pointer to the referent.
    pointer operator->() { return _elt; }

  protected:
    IntrusiveDList *_list; ///< List for this iterator.
    T *_elt;               ///< Referenced element.
    /// Internal constructor for containers.
    iterator(IntrusiveDList *container, ///< Container for iteration.
             T *elt                     ///< Initial referent
             )
      : _list(container), _elt(elt)
    {
    }
  };

  /// Default constructor (empty list).
  IntrusiveDList() : _head(nullptr), _tail(nullptr), _count(0) {}
  /// Empty check.
  /// @return @c true if the list is empty.
  bool
  isEmpty() const
  {
    return 0 == _head;
  }
  /// Add @a elt as the first element in the list.
  /// @return This container.
  self &
  prepend(T *elt ///< Element to add.
  )
  {
    elt->*N = _head;
    elt->*P = nullptr;
    if (_head)
      _head->*P = elt;
    _head = elt;
    if (!_tail)
      _tail = _head; // empty to non-empty transition
    ++_count;
    return *this;
  }
  /// Add @elt as the last element in the list.
  /// @return This container.
  self &
  append(T *elt ///< Element to add.
  )
  {
    elt->*N = nullptr;
    elt->*P = _tail;
    if (_tail)
      _tail->*N = elt;
    _tail = elt;
    if (!_head)
      _head = _tail; // empty to non-empty transition
    ++_count;
    return *this;
  }
  /// Remove the first element of the list.
  /// @return A poiner to the removed item, or @c nullptr if the list was empty.
  T *
  takeHead()
  {
    T *zret = 0;
    if (_head) {
      zret  = _head;
      _head = _head->*N;
      if (_head)
        _head->*P = 0;
      else
        _tail = 0;  // non-empty to empty transition.
      zret->*N = 0; // erase traces of list.
      zret->*P = 0;
      --_count;
    }
    return zret;
  }
  /// Remove the last element of the list.
  /// @return A poiner to the removed item, or @c nullptr if the list was empty.
  T *
  takeTail()
  {
    T *zret = 0;
    if (_tail) {
      zret  = _tail;
      _tail = _tail->*P = 0;
      if (_tail)
        _tail->*N = 0;
      else
        _head = 0;  // non-empty to empty transition.
      zret->*N = 0; // erase traces of list.
      zret->*P = 0;
      --_count;
    }
    return zret;
  }
  /// Insert a new element @a elt after @a target.
  /// The caller is responsible for ensuring @a target is in this list
  /// and @a elt is not in a list.
  /// @return This list.
  self &
  insertAfter(T *target, ///< Target element in list.
              T *elt     ///< Element to insert.
  )
  {
    // Should assert that !(elt->*N || elt->*P)
    elt->*N    = target->*N;
    elt->*P    = target;
    target->*N = elt;
    if (elt->*N)
      elt->*N->*P = elt;
    if (target == _tail)
      _tail = elt;
    ++_count;
    return *this;
  }
  /// Insert a new element @a elt before @a target.
  /// The caller is responsible for ensuring @a target is in this list
  /// and @a elt is not in a list.
  /// @return This list.
  self &
  insertBefore(T *target, ///< Target element in list.
               T *elt     ///< Element to insert.
  )
  {
    // Should assert that !(elt->*N || elt->*P)
    elt->*P    = target->*P;
    elt->*N    = target;
    target->*P = elt;
    if (elt->*P)
      elt->*P->*N = elt;
    if (target == _head)
      _head = elt;
    ++_count;
    return *this;
  }
  /// Take @a elt out of this list.
  /// @return This list.
  self &
  take(T *elt ///< Element to remove.
  )
  {
    if (elt->*P)
      elt->*P->*N = elt->*N;
    if (elt->*N)
      elt->*N->*P = elt->*P;
    if (elt == _head)
      _head = elt->*N;
    if (elt == _tail)
      _tail = elt->*P;
    elt->*P = elt->*N = nullptr;
    --_count;
    return *this;
  }
  /// Remove all elements.
  /// @note @b No memory management is done!
  /// @return This container.
  self &
  clear()
  {
    _head = _tail = nullptr;
    _count        = 0;
    return *this;
  }
  /// @return Number of elements in the list.
  size_t
  getCount() const
  {
    return _count;
  }

  /// Get an iterator to the first element.
  iterator
  begin()
  {
    return iterator(this, _head);
  }
  /// Get an iterator to past the last element.
  iterator
  end()
  {
    return iterator(this, 0);
  }
  /// Get the first element.
  T *
  getHead()
  {
    return _head;
  }
  /// Get the last element.
  T *
  getTail()
  {
    return _tail;
  }

protected:
  T *_head;      ///< First element in list.
  T *_tail;      ///< Last element in list.
  size_t _count; ///< # of elements in list.
};
