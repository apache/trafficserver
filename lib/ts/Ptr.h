/** @file

  A brief file description

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

/*************************** -*- Mod: C++ -*- ******************************

   Atmic and non-atomic smart pointers.

   Note: it would have been nice to have one 'Ptr' class, but the
   templating system on some compilers is so broken that it cannot
   correctly compile Ptr without downcasting the m_ptr object to
   a RefCountObj.


 ****************************************************************************/
#if !defined(_Ptr_h_)
#define _Ptr_h_

#include "ts/ink_atomic.h"

////////////////////////////////////////////////////////////////////////
//
// class NonAtomicRefCountObj
// prototypical class for reference counting
//
////////////////////////////////////////////////////////////////////////
class NonAtomicRefCountObj
{
public:
  NonAtomicRefCountObj() : m_refcount(0) { return; }
  NonAtomicRefCountObj(const NonAtomicRefCountObj &s) : m_refcount(0)
  {
    (void)s;
    return;
  }
  virtual ~NonAtomicRefCountObj() { return; }
  NonAtomicRefCountObj &
  operator=(const NonAtomicRefCountObj &s)
  {
    (void)s;
    return (*this);
  }

  int refcount_inc();
  int refcount_dec();
  int refcount() const;

  virtual void
  free()
  {
    delete this;
  }

  volatile int m_refcount;
};

inline int
NonAtomicRefCountObj::refcount_inc()
{
  return ++m_refcount;
}

#define NONATOMIC_REF_COUNT_OBJ_REFCOUNT_INC(_x) (_x)->refcount_inc()

inline int
NonAtomicRefCountObj::refcount_dec()
{
  return --m_refcount;
}

#define NONATOMIC_REF_COUNT_OBJ_REFCOUNT_DEC(_x) (_x)->refcount_dec()

inline int
NonAtomicRefCountObj::refcount() const
{
  return m_refcount;
}

////////////////////////////////////////////////////////////////////////
//
// class NonAtomicPtr
//
////////////////////////////////////////////////////////////////////////
template <class T> class NonAtomicPtr
{
public:
  explicit NonAtomicPtr(T *ptr = 0);
  NonAtomicPtr(const NonAtomicPtr<T> &);
  ~NonAtomicPtr();

  NonAtomicPtr<T> &operator=(const NonAtomicPtr<T> &);
  NonAtomicPtr<T> &operator=(T *);

  void clear();

  operator T *() const { return (m_ptr); }
  T *operator->() const { return (m_ptr); }
  T &operator*() const { return (*m_ptr); }
  int
  operator==(const T *p)
  {
    return (m_ptr == p);
  }
  int
  operator==(const NonAtomicPtr<T> &p)
  {
    return (m_ptr == p.m_ptr);
  }
  int
  operator!=(const T *p)
  {
    return (m_ptr != p);
  }
  int
  operator!=(const NonAtomicPtr<T> &p)
  {
    return (m_ptr != p.m_ptr);
  }

  NonAtomicRefCountObj *
  _ptr()
  {
    return (NonAtomicRefCountObj *)m_ptr;
  }

  T *m_ptr;
};

template <typename T>
NonAtomicPtr<T>
make_nonatomic_ptr(T *p)
{
  return NonAtomicPtr<T>(p);
}

////////////////////////////////////////////////////////////////////////
//
// inline functions definitions
//
////////////////////////////////////////////////////////////////////////
template <class T> inline NonAtomicPtr<T>::NonAtomicPtr(T *ptr /* = 0 */) : m_ptr(ptr)
{
  if (m_ptr)
    _ptr()->refcount_inc();
  return;
}

template <class T> inline NonAtomicPtr<T>::NonAtomicPtr(const NonAtomicPtr<T> &src) : m_ptr(src.m_ptr)
{
  if (m_ptr)
    _ptr()->refcount_inc();
  return;
}

template <class T> inline NonAtomicPtr<T>::~NonAtomicPtr()
{
  if ((m_ptr) && _ptr()->refcount_dec() == 0) {
    _ptr()->free();
  }
  return;
}

template <class T>
inline NonAtomicPtr<T> &
NonAtomicPtr<T>::operator=(T *p)
{
  T *temp_ptr = m_ptr;

  if (m_ptr == p)
    return (*this);

  m_ptr = p;

  if (m_ptr != 0) {
    _ptr()->refcount_inc();
  }

  if ((temp_ptr) && ((NonAtomicRefCountObj *)temp_ptr)->refcount_dec() == 0) {
    ((NonAtomicRefCountObj *)temp_ptr)->free();
  }

  return (*this);
}
template <class T>
inline void
NonAtomicPtr<T>::clear()
{
  if (m_ptr) {
    if (!((NonAtomicRefCountObj *)m_ptr)->refcount_dec())
      ((NonAtomicRefCountObj *)m_ptr)->free();
    m_ptr = NULL;
  }
}
template <class T>
inline NonAtomicPtr<T> &
NonAtomicPtr<T>::operator=(const NonAtomicPtr<T> &src)
{
  return (operator=(src.m_ptr));
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
//////              ATOMIC VERSIONS
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////

struct ForceVFPTToTop {
  virtual ~ForceVFPTToTop() {}
};

////////////////////////////////////////////////////////////////////////
//
// class RefCountObj
// prototypical class for reference counting
//
////////////////////////////////////////////////////////////////////////
class RefCountObj : public ForceVFPTToTop
{
public:
  RefCountObj() : m_refcount(0) {}
  RefCountObj(const RefCountObj &s) : m_refcount(0)
  {
    (void)s;
    return;
  }
  virtual ~RefCountObj() {}
  RefCountObj &
  operator=(const RefCountObj &s)
  {
    (void)s;
    return (*this);
  }

  int refcount_inc();
  int refcount_dec();
  int refcount() const;

  virtual void
  free()
  {
    delete this;
  }

  volatile int m_refcount;
};

// Increment the reference count, returning the new count.
inline int
RefCountObj::refcount_inc()
{
  return ink_atomic_increment((int *)&m_refcount, 1) + 1;
}

#define REF_COUNT_OBJ_REFCOUNT_INC(_x) (_x)->refcount_inc()

// Decrement the reference count, returning the new count.
inline int
RefCountObj::refcount_dec()
{
  return ink_atomic_increment((int *)&m_refcount, -1) - 1;
}

#define REF_COUNT_OBJ_REFCOUNT_DEC(_x) (_x)->refcount_dec()

inline int
RefCountObj::refcount() const
{
  return m_refcount;
}

////////////////////////////////////////////////////////////////////////
//
// class Ptr
//
////////////////////////////////////////////////////////////////////////
template <class T> class Ptr
{
public:
  explicit Ptr(T *p = 0);
  Ptr(const Ptr<T> &);
  ~Ptr();

  void clear();
  Ptr<T> &operator=(const Ptr<T> &);
  Ptr<T> &operator=(T *);

  T *
  to_ptr()
  {
    if (m_ptr && m_ptr->m_refcount == 1) {
      T *ptr          = m_ptr;
      m_ptr           = 0;
      ptr->m_refcount = 0;
      return ptr;
    }
    return 0;
  }
  operator T *() const { return (m_ptr); }
  T *operator->() const { return (m_ptr); }
  T &operator*() const { return (*m_ptr); }
  int
  operator==(const T *p)
  {
    return (m_ptr == p);
  }
  int
  operator==(const Ptr<T> &p)
  {
    return (m_ptr == p.m_ptr);
  }
  int
  operator!=(const T *p)
  {
    return (m_ptr != p);
  }
  int
  operator!=(const Ptr<T> &p)
  {
    return (m_ptr != p.m_ptr);
  }

  RefCountObj *
  _ptr()
  {
    return (RefCountObj *)m_ptr;
  }

  T *m_ptr;
};

template <typename T>
Ptr<T>
make_ptr(T *p)
{
  return Ptr<T>(p);
}

////////////////////////////////////////////////////////////////////////
//
// inline functions definitions
//
////////////////////////////////////////////////////////////////////////
template <class T> inline Ptr<T>::Ptr(T *ptr /* = 0 */) : m_ptr(ptr)
{
  if (m_ptr)
    _ptr()->refcount_inc();
  return;
}

template <class T> inline Ptr<T>::Ptr(const Ptr<T> &src) : m_ptr(src.m_ptr)
{
  if (m_ptr)
    _ptr()->refcount_inc();
  return;
}

template <class T> inline Ptr<T>::~Ptr()
{
  if ((m_ptr) && _ptr()->refcount_dec() == 0) {
    _ptr()->free();
  }
  return;
}

template <class T>
inline Ptr<T> &
Ptr<T>::operator=(T *p)
{
  T *temp_ptr = m_ptr;

  if (m_ptr == p)
    return (*this);

  m_ptr = p;

  if (m_ptr != 0) {
    _ptr()->refcount_inc();
  }

  if ((temp_ptr) && ((RefCountObj *)temp_ptr)->refcount_dec() == 0) {
    ((RefCountObj *)temp_ptr)->free();
  }

  return (*this);
}
template <class T>
inline void
Ptr<T>::clear()
{
  if (m_ptr) {
    if (!((RefCountObj *)m_ptr)->refcount_dec())
      ((RefCountObj *)m_ptr)->free();
    m_ptr = NULL;
  }
}
template <class T>
inline Ptr<T> &
Ptr<T>::operator=(const Ptr<T> &src)
{
  return (operator=(src.m_ptr));
}

#endif
