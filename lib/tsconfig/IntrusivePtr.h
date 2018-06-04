/** @file

    This is an intrusive shared pointer class designed for internal use in various containers inside
    Traffic Server. It is not dependent on TS and could be used elsewhere. The design tries to
    follow that of @c std::shared_ptr as it performs a similar function. The key difference is
    this shared pointer requires the reference count to be in the target class. This is done by
    inheriting a class containing the counter. This has the benefits

    - improved locality for instances of the class and the reference count
    - the ability to reliably construct shared pointers from raw pointers.
    - lower overhead (a single reference counter).

    The requirement of modifying the target class limits the generality of this class but it is
    still quite useful in specific cases (particularly containers and their internal node classes).

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

#include <sys/types.h>
#include <type_traits>
#include <cassert>
#include <functional>
#include <atomic>

namespace ts
{

template < typename T > class IntrusivePtr; // forward declare

/* ----------------------------------------------------------------------- */
/** Reference counter mixin.

    To add support for @c IntrusivePtr to class @a T, it must publicly inherit
    @c IntrusivePtrCounter. Doing so

    - provides a reference count member
    - forces the reference count to initialize to zero
    - prevents reference count copying on copy construction or assignment
    - provides access to the reference counter from the shared pointer
    - provides the `use_count` method to check the reference count.

    @note You can use this with insulated (by name only) classes. The important thing is to make
    sure that any such class that uses @c IntrusivePtr has all of its constructors and destructors
    declared in the header and defined in the implementation translation unit. If the compiler
    generates any of those, it will not compile due to missing functions or methods

  */
class IntrusivePtrCounter
{
  template < typename T > friend class IntrusivePtr;

  using self_type = IntrusivePtrCounter;
public:
  /** Copy constructor.

      @internal We have to define this to explicitly _not_ copy the reference count. Otherwise any
      client that uses a default copy constructor will _copy the ref count into the new object_.
      That way lies madness.
  */
  IntrusivePtrCounter(self_type const & that);
  /// No move constructor - that won't work for an object that is the target of a shared pointer.
  IntrusivePtrCounter(self_type && that) = delete;

  /** Assignment operator.

      @internal We need this for the same reason as the copy constructor. The reference count must
      not be copied.
   */
  self_type &operator=(self_type const &that);
  /// No move assignment - that won't work for an object that is the target of a shared pointer.
  self_type &operator=(self_type &&that) = delete;

protected:
  /// The reference counter type is signed because detecting underflow is more important than having
  /// more than 2G references.
  using intrusive_ptr_reference_counter_type = long int;
  /// The reference counter.
  intrusive_ptr_reference_counter_type m_intrusive_pointer_reference_count{0};
  /// Default constructor (0 init counter).
  /// @internal Only subclasses can access this.
  IntrusivePtrCounter();
};

/** Atomic reference counting mixin.
 */
class IntrusivePtrAtomicCounter
{
  template < typename T > friend class IntrusivePtr;

  using self_type = IntrusivePtrAtomicCounter;

public:
  IntrusivePtrAtomicCounter(self_type const & that);
  /// No move constructor - that won't work for an object that is the target of a shared pointer.
  IntrusivePtrAtomicCounter(self_type && that) = delete;

  self_type &operator=(self_type const &that);
  /// No move assignment - that won't work for an object that is the target of a shared pointer.
  self_type &operator=(self_type &&that) = delete;

protected:
  /// The reference counter type is signed because detecting underflow is more important than having
  /// more than 2G references.
  using intrusive_ptr_reference_counter_type = std::atomic<long int>;
  /// The reference counter.
  intrusive_ptr_reference_counter_type m_intrusive_pointer_reference_count{0};
  /// Default constructor (0 init counter).
  /// @internal Only subclasses can access this.
  IntrusivePtrAtomicCounter();
};

/* ----------------------------------------------------------------------- */
/** Intrusive shared pointer.

    This is a reference counted smart pointer. A single object is jointly ownded by a set of
    pointers. When the last of the pointers is destructed the target object is also destructed.

    The smart pointer actions can be changed through class specific policy by specializing the @c
    IntrusivePtrPolicy template class.
*/
template <typename T> class IntrusivePtr
{
private:                          /* don't pollute client with these typedefs */
  using self_type = IntrusivePtr;      ///< Self reference type.
  template < typename U > friend class IntrusivePtr; /// Make friends wih our clones.
public:
  /// The underlying (integral) type of the reference counter.
  using count_type = long int;

  /// Default constructor (nullptr initialized).
  IntrusivePtr();
  /// Construct from instance.
  /// The instance becomes referenced and owned by the pointer.
  explicit IntrusivePtr(T *obj);
  /// Destructor.
  ~IntrusivePtr();

  /// Copy constructor
  IntrusivePtr(self_type const& that);
  /// Move constructor.
  IntrusivePtr(self_type && that);
  /// Self assignment.
  self_type& operator=(self_type const& that);
  /// Move assignment.
  self_type& operator=(self_type && that);

  /** Assign from instance.
      The instance becomes referenced and owned by the pointer.
      The reference to the current object is dropped.
  */
  void reset(T *obj = nullptr);

  /** Clear reference without cleanup.

      This unsets this smart pointer and decrements the reference count, but does @b not perform any
      finalization on the target object. This can easily lead to memory leaks and in some sense
      vitiates the point of this class, but it is occasionally the right thing to do. Use with
      caution.

      @return @c true if there are no references upon return,
      @c false if the reference count is not zero.
   */
  T* release();

  /// Test for not @c nullptr
  explicit operator bool() const;

  /// Member dereference.
  T *operator->() const;
  /// Dereference.
  T &operator*() const;
  /// Access raw pointer.
  T *get() const;

  /** User conversion to raw pointer.

      @internal allow implicit conversion to the underlying pointer which is one of the advantages
      of intrusive pointers because the reference count doesn't get lost.

  */
  operator T *() const;

  /** Cross type construction.
      This succeeds if an @a U* can be implicitly converted to a @a T*.
  */
  template <typename U>
  IntrusivePtr(IntrusivePtr<U> const &that);

  template <typename U>
  IntrusivePtr(IntrusivePtr<U> && that);

  /** Cross type assignment.
      This succeeds if an @a U* can be implicitily converted to a @a T*.
  */
  template <typename U>
  self_type &operator=(IntrusivePtr<U> const &that);

  template <typename U>
  self_type &operator=(IntrusivePtr<U> && that);

  /// Reference count.
  /// @return Number of references.
  count_type use_count() const;

private:
  T *m_obj{nullptr}; ///< Pointer to object.

  /// Reference @a obj.
  void set(T *obj);
  /// Drop the current reference.
  void unset();
};

/** Pointer dynamic cast.
    This allows a smart pointer to be cast from one type to another.
    It must be used when the types do not implicitly convert (generally
    a downcast).

    @code
    class A { ... };
    class B : public A { ... };
    IntrusivePtr<A> really_b(new B);
    InstruivePtr<B> the_b;
    the_b = dynamic_ptr_cast<B>(really_b);
    @endcode
*/
template <typename T, typename U>
IntrusivePtr<T>
dynamic_ptr_cast(IntrusivePtr<U> const &src ///< Source pointer.
                 )
{
  return IntrusivePtr<T>(dynamic_cast<T *>(src.get()));
}

/** Pointer cast.
    This allows a smart pointer to be cast from one type to another.
    It must be used when the types do not implicitly convert (generally
    a downcast). This uses @c static_cast and so performs only compile
    time checks.

    @code
    class A { ... };
    class B : public A { ... };
    IntrusivePtr<A> really_b(new B);
    IntrusivePtr<B> the_b;
    the_b = ptr_cast<B>(really_b);
    @endcode
*/
template <typename T, ///< Target type.
          typename U  ///< Source type.
          >
IntrusivePtr<T>
ptr_cast(IntrusivePtr<U> const &src ///< Source pointer.
         )
{
  return IntrusivePtr<T>(static_cast<T *>(src.get()));
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
/** Default policy class for intrusive pointers.

    This allows per type policy, although not per target instance.
    Clients can override policy by specializing this class for the
    target type.

    @code
    template <> IntrusivePtrPolicy<SomeType>
      : IntrusivePtrDefaultPolicy {
     ... Redefinition of methods and nested types ...
    };
    @endcode

    The inherited class will provide the default definitions so you can
    override only what is different. Although this can be omitted if you
    override everything, it is more robust for maintenance to inherit
    anyway.
*/

template <typename T> class IntrusivePtrPolicy
{
public:
  /// Called when the pointer is dereferenced.
  /// Default is empty (no action).
  static void dereferenceCheck(T * ///< Target object.
                               );

  /** Perform clean up on a target object that is no longer referenced.

      Default is calling @c delete. Any specialization that overrides this
      @b must clean up the object. The primary use of this is to perform
      a clean up other than @c delete.

      @note When this is called, the target object reference count
      is zero. If it is necessary to pass a smart pointer to the
      target object, it will be necessary to call
      @c IntrusivePtr::release to drop the reference without
      another finalization. Further care must be taken that none of
      the called logic keeps a copy of the smart pointer. Use with
      caution.
  */
  static void finalize(T *t ///< Target object.
                       );
  /// Strict weak order for STL containers.
  class Order : public std::binary_function<IntrusivePtr<T>, IntrusivePtr<T>, bool>
  {
  public:
    /// Default constructor.
    Order() {}
    /// Compare by raw pointer.
    bool operator()(IntrusivePtr<T> const &lhs, ///< Left hand operand.
                    IntrusivePtr<T> const &rhs  ///< Right hand operand.
                    ) const;
  };
};

struct IntrusivePtrDefaultPolicyTag {
};
typedef IntrusivePtrPolicy<IntrusivePtrDefaultPolicyTag> IntrusivePtrDefaultPolicy;
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
/* Inline Methods */
inline IntrusivePtrCounter::IntrusivePtrCounter() {}

inline IntrusivePtrCounter::IntrusivePtrCounter(self_type const &) {}

inline IntrusivePtrCounter &
IntrusivePtrCounter::operator=(self_type const &)
{
  return *this;
}

inline IntrusivePtrAtomicCounter::IntrusivePtrAtomicCounter() {}

inline IntrusivePtrAtomicCounter::IntrusivePtrAtomicCounter(self_type const &) {}

inline IntrusivePtrAtomicCounter &
IntrusivePtrAtomicCounter::operator=(self_type const &)
{
  return *this;
}

/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */

template <typename T>
void
IntrusivePtrPolicy<T>::dereferenceCheck(T *)
{
}

template <typename T>
void
IntrusivePtrPolicy<T>::finalize(T *obj)
{
  delete obj;
}

template <typename T>
bool
IntrusivePtrPolicy<T>::Order::operator()(IntrusivePtr<T> const &lhs, IntrusivePtr<T> const &rhs) const
{
  return lhs.get() < rhs.get();
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
template <typename T> IntrusivePtr<T>::IntrusivePtr() {}

template <typename T> IntrusivePtr<T>::IntrusivePtr(T *obj)
{
  this->set(obj);
}

template <typename T> IntrusivePtr<T>::~IntrusivePtr()
{
  this->unset();
}

template < typename T >
IntrusivePtr<T>::IntrusivePtr(self_type const &that)
{
  this->set(that.m_obj);
}

template <typename T>
template <typename U>
IntrusivePtr<T>::IntrusivePtr(IntrusivePtr<U> const &that)
{
  static_assert(std::is_convertible<U*, T*>::value, "The argument type is not implicitly convertible to the return type.");
  this->set(that.m_obj);
}

template < typename T >
IntrusivePtr<T>::IntrusivePtr(self_type && that)
{
  std::swap<T*>(m_obj, that.m_obj);
}

template <typename T>
template <typename U>
IntrusivePtr<T>::IntrusivePtr(IntrusivePtr<U> && that)
{
  static_assert(std::is_convertible<U*, T*>::value, "The argument type is not implicitly convertible to the return type.");
  std::swap<T*>(m_obj, that.get());
}

template < typename T >
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(self_type const& that)
{
  this->reset(that.get());
  return *this;
}


template <typename T>
template <typename U>
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(IntrusivePtr<U> const& that)
{
  static_assert(std::is_convertible<U*, T*>::value, "The argument type is not implicitly convertible to the return type.");
  this->reset(that.get());
  return *this;
}

template < typename T >
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(self_type && that)
{
  if (m_obj != that.m_obj) {
    this->unset();
    m_obj = that.m_obj;
    that.m_obj = nullptr;
  } else {
    that.unset();
  }
  return *this;
}

template <typename T>
template <typename U>
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(IntrusivePtr<U> && that)
{
  static_assert(std::is_convertible<U*, T*>::value, "The argument type is not implicitly convertible to the return type.");
  if (m_obj != that.m_obj) {
    this->unset();
    m_obj = that.m_obj;
    that.m_obj = nullptr;
  } else {
    that.unset();
  }
  return *this;
}

template <typename T> T *IntrusivePtr<T>::operator->() const
{
  IntrusivePtrPolicy<T>::dereferenceCheck(m_obj);
  return m_obj;
}

template <typename T> T &IntrusivePtr<T>::operator*() const
{
  IntrusivePtrPolicy<T>::dereferenceCheck(m_obj);
  return *m_obj;
}

template <typename T>
T *
IntrusivePtr<T>::get() const
{
  IntrusivePtrPolicy<T>::dereferenceCheck(m_obj);
  return m_obj;
}

/* The Set/Unset methods are the basic implementation of our
 * reference counting. The Reset method is the standard way
 * of invoking the pair, although splitting them allows some
 * additional efficiency in certain situations.
 */

/* @c set and @c unset are two half operations that don't do checks.
   It is the caller's responsibility to do that.
*/

template <typename T>
void
IntrusivePtr<T>::unset()
{
  if (nullptr != m_obj) {
    auto &cp = m_obj->m_intrusive_pointer_reference_count;

    if (0 == --cp) {
      IntrusivePtrPolicy<T>::finalize(m_obj);
    }
    m_obj = nullptr;
  }
}

template <typename T>
void
IntrusivePtr<T>::set(T *obj)
{
  m_obj = obj;    /* update to new object */
  if (nullptr != m_obj) /* if a real object, bump the ref count */
    ++(m_obj->m_intrusive_pointer_reference_count);
}

template <typename T>
void
IntrusivePtr<T>::reset(T *obj)
{
  if (obj != m_obj) {
    this->unset();
    this->set(obj);
  }
}

template <typename T>
T*
IntrusivePtr<T>::release()
{
  T* zret = m_obj;
  if (m_obj) {
    auto &cp = m_obj->m_intrusive_pointer_reference_count;
    // If the client is using this method, they're doing something funky
    // so be extra careful with the reference count.
    if (cp > 0)
      --cp;
    m_obj = nullptr;
  }
  return zret;
}

template <typename T>
IntrusivePtr<T>::operator bool() const
{
  return m_obj != nullptr;
}

/* Pointer comparison */
template <typename T>
bool
operator==(IntrusivePtr<T> const &lhs, IntrusivePtr<T> const &rhs)
{
  return lhs.get() == rhs.get();
}

template <typename T>
bool
operator!=(IntrusivePtr<T> const &lhs, IntrusivePtr<T> const &rhs)
{
  return lhs.get() != rhs.get();
}

template <typename T>
bool
operator<(IntrusivePtr<T> const &lhs, IntrusivePtr<T> const &rhs)
{
  return lhs.get() < rhs.get();
}

template <typename T> IntrusivePtr<T>::operator T *() const
{
  return m_obj;
}

template <typename T>
typename IntrusivePtr<T>::count_type
IntrusivePtr<T>::use_count() const
{
  return m_obj ? count_type{m_obj->m_intrusive_pointer_reference_count} : count_type{0};
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
} // namespace ts
