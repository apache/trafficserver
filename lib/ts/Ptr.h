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
  // https://en.wikibooks.org/wiki/More_C%2B%2B_Idioms/Safe_bool.
  typedef void (Ptr::*bool_type)() const;
  void
  this_type_does_not_support_comparisons() const
  {
  }

public:
  explicit Ptr(T *p = 0);
  Ptr(const Ptr<T> &);
  ~Ptr();

  void clear();
  Ptr<T> &operator=(const Ptr<T> &);
  Ptr<T> &operator=(T *);

  T *operator->() const { return (m_ptr); }
  T &operator*() const { return (*m_ptr); }
  operator bool_type() const { return m_ptr ? &Ptr::this_type_does_not_support_comparisons : 0; }
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

  // Return the raw pointer.
  T *
  get() const
  {
    return m_ptr;
  }

  // Return the raw pointer as a RefCount object. Typically
  // this is for keeping a collection of heterogenous objects.
  RefCountObj *
  object() const
  {
    return static_cast<RefCountObj *>(m_ptr);
  }

  // Return the stored pointer, storing NULL instead. Do not increment
  // the refcount; the caller is now responsible for owning the RefCountObj.
  T *
  detach()
  {
    T *tmp = m_ptr;
    m_ptr  = NULL;
    return tmp;
  }

  // XXX Clearly this is not safe. This is used in HdrHeap::unmarshal() to swizzle
  // the refcount of the managed heap pointers. That code needs to be cleaned up
  // so that this can be removed. Do not use this in new code.
  void
  swizzle(RefCountObj *ptr)
  {
    m_ptr = ptr;
  }

private:
  T *m_ptr;

  friend class CoreUtils;
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
  if (m_ptr) {
    m_ptr->refcount_inc();
  }
}

template <class T> inline Ptr<T>::Ptr(const Ptr<T> &src) : m_ptr(src.m_ptr)
{
  if (m_ptr) {
    m_ptr->refcount_inc();
  }
}

template <class T> inline Ptr<T>::~Ptr()
{
  if (m_ptr && m_ptr->refcount_dec() == 0) {
    m_ptr->free();
  }
}

template <class T>
inline Ptr<T> &
Ptr<T>::operator=(T *p)
{
  T *temp_ptr = m_ptr;

  if (m_ptr == p) {
    return (*this);
  }

  m_ptr = p;

  if (m_ptr) {
    m_ptr->refcount_inc();
  }

  if (temp_ptr && temp_ptr->refcount_dec() == 0) {
    temp_ptr->free();
  }

  return (*this);
}

template <class T>
inline void
Ptr<T>::clear()
{
  if (m_ptr) {
    if (!m_ptr->refcount_dec())
      m_ptr->free();
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
