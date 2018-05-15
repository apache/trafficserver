#if !defined(TS_INTRUSIVE_PTR_HEADER)
#define TS_INTRUSIVE_PTR_HEADER

/** @file

    This is a simple shared pointer class for restricted use. It is not a
    completely general class. The most significant missing feature is the
    lack of thread safety. For its intended use, this is acceptable and
    provides a performance improvement. However, it does restrict how the
    class may be used.

    This style of shared pointer also requires explicit support from the
    target class, which must provide an internal reference counter.

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

#include <sys/types.h>
#include <cassert>
#include <functional>

namespace ts
{
class IntrusivePtrCounter;

/** This class exists solely to be declared a friend of @c IntrusivePtrCounter.

    @internal This is done because we can't declare the template a
    friend, so rather than burden the client with the declaration we
    do it here. It provides a single method that allows the smart pointer
    to get access to the protected reference count.

 */
class IntrusivePtrBase
{
public:
  /// Type used for reference counter.
  typedef long Counter;

protected:
  Counter *getCounter(IntrusivePtrCounter *c ///< Cast object with reference counter.
                      ) const;
};
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
/** Reference counter mixin.

    To add support for @c IntrusivePtr to class @a T, it
    should inherit from @c IntrusivePtrCounter<T> in order to

    - provide a reference count member
    - force the reference count to initialize to zero
    - define the add and release global functions required by @c IntrusivePtr

    In general this class should be inherited publicly. This will
    provide methods which mimic the @c Boost.shared_ptr interface ( @c
    unique() , @c use_count() ).

    If this class is not inherited publically or the destructor is
    non-public then the host class (@a T) must declare this class ( @c
    reference_counter<T> ) as a friend.

    @internal Due to changes in the C++ standard and design decisions
    in gcc, it is no longer possible to declare a template parameter
    as a friend class.  (Basically, you can't use a typedef in a
    friend declaration and gcc treats template parameters as
    typedefs).

    @note You can use this with insulated (by name only) classes. The
    important thing is to make sure that any such class that uses @c
    IntrusivePtr has all of its constructors and destructors declared
    in the header and defined in the implementation translation
    unit. If the compiler generates any of those, it will not compile
    due to missing functions or methods

  */
class IntrusivePtrCounter
{
  friend class IntrusivePtrBase;

public:
  /** Copy constructor.

      @internal We have to define this to explicitly _not_ copy the
      reference count. Otherwise any client that uses a default copy
      constructor will _copy the ref count into the new object_. That
      way lies madness.
  */

  IntrusivePtrCounter(IntrusivePtrCounter const & ///< Source object.
                      );

  /** Assignment operator.

      @internal We need this for the same reason as the copy
      constructor. The reference counter must not participate in
      assignment.
   */
  IntrusivePtrCounter &operator=(IntrusivePtrCounter const &);

protected:
  IntrusivePtrBase::Counter m_intrusive_pointer_reference_count;
  /// Default constructor (0 init counter).
  /// @internal Only subclasses can access this.
  IntrusivePtrCounter();
};
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
/** Shared pointer.

    This is a reference counted smart pointer. A single object is jointly
    ownded by a set of pointers. When the last of the pointers is destructed
    the target object is also destructed.

    The smart pointer actions can be changed through class specific policy
    by specializing the @c IntrusivePtrPolicy template class.
*/
template <typename T> class IntrusivePtr : private IntrusivePtrBase
{
private:                          /* don't pollute client with these typedefs */
  typedef IntrusivePtrBase super; ///< Parent type.
  typedef IntrusivePtr self;      ///< Self reference type.

public:
  /// Promote type for reference counter.
  typedef super::Counter Counter;

  /// Default constructor (0 initialized).
  IntrusivePtr();
  /// Construct from instance.
  /// The instance becomes referenced and owned by the pointer.
  IntrusivePtr(T *obj);
  /// Destructor.
  ~IntrusivePtr();

  /// Copy constructor.
  IntrusivePtr(const self &src);
  /// Self assignement.
  self &operator=(const self &src);
  /** Assign from instance.
      The instance becomes referenced and owned by the pointer.
      The reference to the current object is dropped.
  */
  self &operator=(T *obj ///< Target instance.
                  );

  /** Assign from instance.
      The instance becomes referenced and owned by the pointer.
      The reference to the current object is dropped.
      @note A synonym for @c operator= for compatibility.
  */
  self &assign(T *obj ///< Target instance.
               );

  /** Assign from instance.
      The instance becomes referenced and owned by the pointer.
      The reference to the current object is dropped.
  */
  void reset(T *obj);
  /** Clear reference without cleanup.

      This unsets this smart pointer and decrements the reference
      count, but does @b not perform any finalization on the
      target object. This can easily lead to memory leaks and
      in some sense vitiates the point of this class, but it is
      occasionally the right thing to do. Use with caution.

      @return @c true if there are no references upon return,
      @c false if the reference count is not zero.
   */
  bool release();

  /// Test if the pointer is zero (@c NULL).
  bool isNull() const;

  /// Member dereference.
  T *operator->() const;
  /// Dereference.
  T &operator*() const;
  /// Access raw pointer.
  T *get() const;

  /** User conversion to raw pointer.

      @internal allow implicit conversion to the underlying
      pointer. This allows for the form "if (handle)" and is not
      particularly dangerous (as it would be for a scope_ptr or
      std::shared_ptr) because the counter is carried with the object and
      so can't get lost or duplicated.

  */
  operator T *() const;

  /** Cross type construction.
      This succeeds if an @a X* can be implicitly converted to a @a T*.
  */
  template <typename X ///< Foreign pointer type.
            >
  IntrusivePtr(IntrusivePtr<X> const &that ///< Foreign pointer.
               );

  /** Cross type assignment.
      This succeeds if an @a X* can be implicitily converted to a @a T*.
  */
  template <typename X ///< Foreign pointer type.
            >
  self &operator=(IntrusivePtr<X> const &that ///< Foreign pointer.
                  );

  /// Check for multiple references.
  /// @return @c true if more than one smart pointer references the object,
  /// @c false otherwise.
  bool isShared() const;
  /// Check for a single reference (@c std::shared_ptr compatibility)
  /// @return @c true if this object is not shared.
  bool unique() const;
  /// Reference count.
  /// @return Number of references.
  Counter useCount() const;

private:
  T *m_obj; ///< Pointer to object.

  /// Reference @a obj.
  void set(T *obj ///< Target object.
           );
  /// Drop the current reference.
  void unset();

  /// Get a pointer to the reference counter of the target object.
  Counter *getCounter() const;
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
template <typename T, ///< Target type.
          typename X  ///< Source type.
          >
IntrusivePtr<T>
dynamic_ptr_cast(IntrusivePtr<X> const &src ///< Source pointer.
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
          typename X  ///< Source type.
          >
IntrusivePtr<T>
ptr_cast(IntrusivePtr<X> const &src ///< Source pointer.
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
inline IntrusivePtrCounter::IntrusivePtrCounter() : m_intrusive_pointer_reference_count(0)
{
}

inline IntrusivePtrCounter::IntrusivePtrCounter(IntrusivePtrCounter const &) : m_intrusive_pointer_reference_count(0)
{
}

inline IntrusivePtrCounter &
IntrusivePtrCounter::operator=(IntrusivePtrCounter const &)
{
  return *this;
}

inline IntrusivePtrBase::Counter *
IntrusivePtrBase::getCounter(IntrusivePtrCounter *c) const
{
  return &(c->m_intrusive_pointer_reference_count);
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
template <typename T> IntrusivePtr<T>::IntrusivePtr() : m_obj(nullptr)
{
}

template <typename T> IntrusivePtr<T>::IntrusivePtr(T *obj)
{
  this->set(obj);
}

template <typename T> IntrusivePtr<T>::~IntrusivePtr()
{
  this->unset();
}

template <typename T> IntrusivePtr<T>::IntrusivePtr(const self &that)
{
  this->set(that.m_obj);
}

template <typename T>
template <typename X>
IntrusivePtr<T>::IntrusivePtr(IntrusivePtr<X> const &that ///< Foreign pointer.
                              )
  : super(that.get())
{
}

template <typename T>
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(const self &that)
{
  this->reset(that.m_obj);
  return *this;
}

template <typename T>
template <typename X>
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(IntrusivePtr<X> const &that ///< Foreign pointer.
                           )
{
  this->reset(that.get());
  return *this;
}

template <typename T>
IntrusivePtr<T> &
IntrusivePtr<T>::operator=(T *obj)
{
  this->reset(obj);
  return *this;
}

template <typename T>
IntrusivePtr<T> &
IntrusivePtr<T>::assign(T *obj)
{
  return *this = obj;
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

template <typename T>
typename IntrusivePtr<T>::Counter *
IntrusivePtr<T>::getCounter() const
{
  return super::getCounter(static_cast<IntrusivePtrCounter *>(m_obj));
}

/* The Set/Unset methods are the basic implementation of our
 * reference counting. The Reset method is the standard way
 * of invoking the pair, although splitting them allows some
 * additional efficiency in certain situations.
 */

/* set and unset are two half operations that don't do checks.
   It is the callers responsibility to do that.
*/

template <typename T>
void
IntrusivePtr<T>::unset()
{
  if (nullptr != m_obj) {
    /* magic: our target is required to inherit from IntrusivePtrCounter,
     * which provides a protected counter variable and access via our
     * super class. We call the super class method to get a raw pointer
     * to the counter variable.
     */
    Counter *cp = this->getCounter();

    /* If you hit this assert you've got a cycle of objects that
       reference each other. A delete in the cycle will eventually
       result in one of the objects getting deleted twice, which is
       what this assert indicates.
    */
    assert(*cp);

    if (0 == --*cp) {
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
    ++(*(this->getCounter()));
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
bool
IntrusivePtr<T>::release()
{
  bool zret = true;
  if (m_obj) {
    Counter *cp = this->getCounter();
    zret        = *cp <= 1;
    // If the client is using this method, they're doing something funky
    // so be extra careful with the reference count.
    if (*cp > 0)
      --*cp;
    m_obj = nullptr;
  }
  return zret;
}

/* Simple method to check for invalid pointer */
template <typename T>
bool
IntrusivePtr<T>::isNull() const
{
  return 0 == m_obj;
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

template <typename T>
bool
operator==(IntrusivePtr<T> const &lhs, int rhs)
{
  assert(0 == rhs);
  return lhs.get() == 0;
}

template <typename T>
bool
operator==(int lhs, IntrusivePtr<T> const &rhs)
{
  assert(0 == lhs);
  return rhs.get() == nullptr;
}

template <typename T>
bool
operator!=(int lhs, IntrusivePtr<T> const &rhs)
{
  return !(lhs == rhs);
}

template <typename T>
bool
operator!=(IntrusivePtr<T> const &lhs, int rhs)
{
  return !(lhs == rhs);
}

template <typename T> IntrusivePtr<T>::operator T *() const
{
  return m_obj;
}

template <typename T>
bool
IntrusivePtr<T>::isShared() const
{
  return m_obj && *(this->getCounter()) > 1;
}

template <typename T>
bool
IntrusivePtr<T>::unique() const
{
  return 0 == m_obj || *(this->getCounter()) <= 1;
}

template <typename T>
typename IntrusivePtr<T>::Counter
IntrusivePtr<T>::useCount() const
{
  return m_obj ? *(this->getCounter()) : 0;
}
/* ----------------------------------------------------------------------- */
/* ----------------------------------------------------------------------- */
} // namespace ats
/* ----------------------------------------------------------------------- */
#endif // TS_INTRUSIVE_PTR_HEADER
