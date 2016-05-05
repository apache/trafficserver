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

#ifndef PTR_H_FBBD7DC3_CA5D_4715_9162_5E4DDA93353F
#define PTR_H_FBBD7DC3_CA5D_4715_9162_5E4DDA93353F

#include "ts/ink_atomic.h"

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

  // Increment the reference count, returning the new count.
  int
  refcount_inc()
  {
    return ink_atomic_increment((int *)&m_refcount, 1) + 1;
  }

  // Decrement the reference count, returning the new count.
  int
  refcount_dec()
  {
    return ink_atomic_increment((int *)&m_refcount, -1) - 1;
  }

  int
  refcount() const
  {
    return m_refcount;
  }

  virtual void
  free()
  {
    delete this;
  }

private:
  volatile int m_refcount;
};

#define REF_COUNT_OBJ_REFCOUNT_INC(_x) (_x)->refcount_inc()
#define REF_COUNT_OBJ_REFCOUNT_DEC(_x) (_x)->refcount_dec()

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
      T *ptr = m_ptr;
      m_ptr = 0;
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

#endif /* PTR_H_FBBD7DC3_CA5D_4715_9162_5E4DDA93353F */
