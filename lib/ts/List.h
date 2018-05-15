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

/****************************************************************************

  List.h

  This file implements singly and doubly linked list templates for
  homomorphic lists.

  There are two main data structures defined for each list, a link cell
  and a list descriptor.  Both are parameterized by template object class.
  The link cell and list descriptor are named as follows:

  list type		1-linked list	2-linked list	queue
  ---------		-------------	-------------	-----
  link cell		SLink<C>	Link<C>	        Link<C>
  list descriptor	SLL<C>	        DLL<C>	        Queue<C>

  The list descriptor contains state about the lists (for example: head and
  tail pointers) and supports list manipulation methods.

  The link cell strings objects together in the list, and is normally part
  of the object itself.  An SLink only points to the next object.  A Link
  points both to the previous and the next object in a list.

  The link() method can help find the location of the location of the link
  cell within an object, given the location of the link cell in another
  object.  This is useful when iterating along lists.


 ****************************************************************************/

#pragma once

#include <cstdint>

#include "ts/ink_assert.h"
#include "ts/ink_queue.h"
#include "ts/defalloc.h"

//
//      Link cell for singly-linked list of objects of type C.
//
template <class C> class SLink
{
public:
  C *next;
  SLink() : next(nullptr){};
};
#define SLINK(_c, _f)                  \
  class Link##_##_f : public SLink<_c> \
  {                                    \
  public:                              \
    static _c *&                       \
    next_link(_c *c)                   \
    {                                  \
      return c->_f.next;               \
    }                                  \
    static const _c *                  \
    next_link(const _c *c)             \
    {                                  \
      return c->_f.next;               \
    }                                  \
  };                                   \
  SLink<_c> _f
#define SLINKM(_c, _m, _f)                    \
  class Link##_##_m##_##_f : public SLink<_c> \
  {                                           \
  public:                                     \
    static _c *&                              \
    next_link(_c *c)                          \
    {                                         \
      return c->_m._f.next;                   \
    }                                         \
  };

//
//      Link cell for doubly-linked list of objects of type C.
//
template <class C> struct Link : public SLink<C> {
  C *prev;
  Link() : prev(nullptr) {}
};
#define LINK(_c, _f)                  \
  class Link##_##_f : public Link<_c> \
  {                                   \
  public:                             \
    static _c *&                      \
    next_link(_c *c)                  \
    {                                 \
      return c->_f.next;              \
    }                                 \
    static _c *&                      \
    prev_link(_c *c)                  \
    {                                 \
      return c->_f.prev;              \
    }                                 \
    static const _c *                 \
    next_link(const _c *c)            \
    {                                 \
      return c->_f.next;              \
    }                                 \
    static const _c *                 \
    prev_link(const _c *c)            \
    {                                 \
      return c->_f.prev;              \
    }                                 \
  };                                  \
  Link<_c> _f
#define LINKM(_c, _m, _f)                    \
  class Link##_##_m##_##_f : public Link<_c> \
  {                                          \
  public:                                    \
    static _c *&                             \
    next_link(_c *c)                         \
    {                                        \
      return c->_m._f.next;                  \
    }                                        \
    static _c *&                             \
    prev_link(_c *c)                         \
    {                                        \
      return c->_m._f.prev;                  \
    }                                        \
  };
#define LINK_FORWARD_DECLARATION(_c, _f)     \
  class Link##_##_c##_##_f : public Link<_c> \
  {                                          \
  public:                                    \
    static _c *&next_link(_c *c);            \
    static _c *&prev_link(_c *c);            \
  };
#define LINK_DEFINITION(_c, _f)                                           \
  inline _c *&Link##_##_c##_##_f::next_link(_c *c) { return c->_f.next; } \
  inline _c *&Link##_##_c##_##_f::prev_link(_c *c) { return c->_f.prev; }
//
//      List descriptor for singly-linked list of objects of type C.
//
template <class C, class L = typename C::Link_link> class SLL
{
public:
  C *head;
  bool
  empty() const
  {
    return head == nullptr;
  }
  void push(C *e);
  C *pop();
  void
  clear()
  {
    head = nullptr;
  }
  C *&
  next(C *e)
  {
    return L::next_link(e);
  }
  const C *
  next(const C *e) const
  {
    return L::next_link(e);
  }

  SLL() : head(nullptr) {}
  SLL(C *c) : head(c) {}
};
#define SList(_c, _f) SLL<_c, _c::Link##_##_f>
#define SListM(_c, _m, _ml, _l) SLL<_c, _c::Link##_##_ml##_##_l>
#define forl_LL(_c, _p, _l) for (_c *_p = (_l).head; _p; _p = (_l).next(_p))

template <class C, class L>
inline void
SLL<C, L>::push(C *e)
{
  next(e) = head;
  head    = e;
}

template <class C, class L>
inline C *
SLL<C, L>::pop()
{
  C *ret = head;
  if (ret) {
    head      = next(ret);
    next(ret) = nullptr;
  }
  return ret;
}

//
//      List descriptor for doubly-linked list of objects of type C.
//
template <class C, class L = typename C::Link_link> struct DLL {
  C *head;
  bool
  empty() const
  {
    return head == nullptr;
  }
  void push(C *e);
  C *pop();
  void remove(C *e);
  void insert(C *e, C *after);
  bool
  in(C *e)
  {
    return head == e || next(e) || prev(e);
  }
  void
  clear()
  {
    head = nullptr;
  }
  static C *&
  next(C *e)
  {
    return reinterpret_cast<C *&>(L::next_link(e));
  }
  static C *&
  prev(C *e)
  {
    return reinterpret_cast<C *&>(L::prev_link(e));
  }
  static C const *
  next(const C *e)
  {
    return L::next_link(e);
  }
  static C const *
  prev(const C *e)
  {
    return L::prev_link(e);
  }

  DLL() : head(nullptr) {}
};
#define DList(_c, _f) DLL<_c, _c::Link##_##_f>
#define DListM(_c, _m, _ml, _l) DLL<_c, _c::Link##_##_ml##_##_l>

template <class C, class L>
inline void
DLL<C, L>::push(C *e)
{
  if (head)
    prev(head) = e;
  next(e) = head;
  head    = e;
}

template <class C, class L>
inline void
DLL<C, L>::remove(C *e)
{
  if (!head)
    return;
  if (e == head)
    head = next(e);
  if (prev(e))
    next(prev(e)) = next(e);
  if (next(e))
    prev(next(e)) = prev(e);
  prev(e) = nullptr;
  next(e) = nullptr;
}

template <class C, class L>
inline C *
DLL<C, L>::pop()
{
  C *ret = head;
  if (ret) {
    head = next(ret);
    if (head)
      prev(head) = nullptr;
    next(ret) = nullptr;
    return ret;
  } else
    return nullptr;
}

template <class C, class L>
inline void
DLL<C, L>::insert(C *e, C *after)
{
  if (!after) {
    push(e);
    return;
  }
  prev(e)     = after;
  next(e)     = next(after);
  next(after) = e;
  if (next(e))
    prev(next(e)) = e;
}

//
//      List descriptor for queue of objects of type C.
//
template <class C, class L = typename C::Link_link> class Queue : public DLL<C, L>
{
public:
  using DLL<C, L>::head;
  C *tail;
  void push(C *e);
  C *pop();
  void enqueue(C *e);
  void in_or_enqueue(C *e);
  C *dequeue();
  void remove(C *e);
  void insert(C *e, C *after);
  void append(Queue<C, L> q);
  void append(DLL<C, L> q);
  void
  clear()
  {
    head = nullptr;
    tail = nullptr;
  }
  bool
  empty() const
  {
    return head == nullptr;
  }

  Queue() : tail(nullptr) {}
};
#define Que(_c, _f) Queue<_c, _c::Link##_##_f>
#define QueM(_c, _m, _mf, _f) Queue<_c, _c::Link##_##_mf##_##_f>

template <class C, class L>
inline void
Queue<C, L>::push(C *e)
{
  DLL<C, L>::push(e);
  if (!tail)
    tail = head;
}

template <class C, class L>
inline C *
Queue<C, L>::pop()
{
  C *ret = DLL<C, L>::pop();
  if (!head)
    tail = nullptr;
  return ret;
}

template <class C, class L>
inline void
Queue<C, L>::insert(C *e, C *after)
{
  DLL<C, L>::insert(e, after);
  if (!tail)
    tail = head;
  else if (tail == after)
    tail = e;
}

template <class C, class L>
inline void
Queue<C, L>::remove(C *e)
{
  if (tail == e)
    tail = (C *)this->prev(e);
  DLL<C, L>::remove(e);
}

template <class C, class L>
inline void
Queue<C, L>::append(DLL<C, L> q)
{
  C *qtail = q.head;
  if (qtail)
    while (this->next(qtail))
      qtail = this->next(qtail);
  if (!head) {
    head = q.head;
    tail = qtail;
  } else {
    if (q.head) {
      this->next(tail)   = q.head;
      this->prev(q.head) = tail;
      tail               = qtail;
    }
  }
}

template <class C, class L>
inline void
Queue<C, L>::append(Queue<C, L> q)
{
  if (!head) {
    head = q.head;
    tail = q.tail;
  } else {
    if (q.head) {
      this->next(tail)   = q.head;
      this->prev(q.head) = tail;
      tail               = q.tail;
    }
  }
}

template <class C, class L>
inline void
Queue<C, L>::enqueue(C *e)
{
  if (tail)
    insert(e, tail);
  else
    push(e);
}

template <class C, class L>
inline void
Queue<C, L>::in_or_enqueue(C *e)
{
  if (!this->in(e))
    enqueue(e);
}

template <class C, class L>
inline C *
Queue<C, L>::dequeue()
{
  return pop();
}

//
// Adds sorting, but requires that elements implement <
//

template <class C, class L = typename C::Link_link> struct SortableQueue : public Queue<C, L> {
  using DLL<C, L>::head;
  using Queue<C, L>::tail;
  void
  sort()
  {
    if (!head)
      return;
    bool clean = false;
    while (!clean) {
      clean = true;
      C *v  = head;
      C *n  = this->next(head);
      while (n) {
        C *f = this->next(n);
        if (*n < *v) {
          clean = false;
          // swap 'em
          if (head == v)
            head = n;
          if (tail == n)
            tail = v;
          // fix prev (p)
          C *p = this->prev(v);
          if (p) {
            this->next(p) = n;
            this->prev(n) = p;
          } else
            this->prev(n) = nullptr;
          // fix follow (f)
          if (f) {
            this->prev(f) = v;
            this->next(v) = f;
          } else
            this->next(v) = nullptr;
          // fix interior
          this->prev(v) = n;
          this->next(n) = v;
        } else
          v = n;
        n = f;
      }
    }
  }
};
#define SortableQue(_c, _l) SortableQueue<_c, _c::Link##_##_f>

//
// Adds counting to the Queue
//

template <class C, class L = typename C::Link_link> struct CountQueue : public Queue<C, L> {
  int size;
  inline CountQueue(void) : size(0) {}
  inline void push(C *e);
  inline C *pop();
  inline void enqueue(C *e);
  inline C *dequeue();
  inline void remove(C *e);
  inline void insert(C *e, C *after);
  inline void append(CountQueue<C, L> &q);
  inline void append_clear(CountQueue<C, L> &q);
};
#define CountQue(_c, _f) CountQueue<_c, _c::Link##_##_f>
#define CountQueM(_c, _m, _mf, _f) CountQueue<_c, _c::Link##_##_mf##_##_f>

template <class C, class L>
inline void
CountQueue<C, L>::push(C *e)
{
  Queue<C, L>::push(e);
  size++;
}

template <class C, class L>
inline C *
CountQueue<C, L>::pop()
{
  C *ret = Queue<C, L>::pop();
  if (ret)
    size--;
  return ret;
}

template <class C, class L>
inline void
CountQueue<C, L>::remove(C *e)
{
  Queue<C, L>::remove(e);
  size--;
}

template <class C, class L>
inline void
CountQueue<C, L>::enqueue(C *e)
{
  Queue<C, L>::enqueue(e);
  size++;
}

template <class C, class L>
inline C *
CountQueue<C, L>::dequeue()
{
  return pop();
}

template <class C, class L>
inline void
CountQueue<C, L>::insert(C *e, C *after)
{
  Queue<C, L>::insert(e, after);
  size++;
}

template <class C, class L>
inline void
CountQueue<C, L>::append(CountQueue<C, L> &q)
{
  Queue<C, L>::append(q);
  size += q.size;
}

template <class C, class L>
inline void
CountQueue<C, L>::append_clear(CountQueue<C, L> &q)
{
  append(q);
  q.head = q.tail = 0;
  q.size          = 0;
}

//
// List using cons cells
//

template <class C, class A = DefaultAlloc> struct ConsCell {
  C car;
  ConsCell *cdr;
  ConsCell(C acar, ConsCell *acdr) : car(acar), cdr(acdr) {}
  ConsCell(C acar) : car(acar), cdr(nullptr) {}
  ConsCell(ConsCell *acdr) : cdr(acdr) {}
  static void *
  operator new(size_t size)
  {
    return A::alloc(size);
  }
  static void
  operator delete(void *p, size_t /* size ATS_UNUSED */)
  {
    A::free(p);
  }
};

template <class C, class A = DefaultAlloc> struct List {
  ConsCell<C, A> *head;
  C
  first()
  {
    if (head)
      return head->car;
    else
      return 0;
  }
  C
  car()
  {
    return first();
  }
  ConsCell<C, A> *
  rest()
  {
    if (head)
      return head->cdr;
    else
      return 0;
  }
  ConsCell<C, A> *
  cdr()
  {
    return rest();
  }
  void
  push(C a)
  {
    head = new ConsCell<C, A>(a, head);
  }
  void
  push()
  {
    head = new ConsCell<C, A>(head);
  }
  C
  pop()
  {
    C a  = car();
    head = cdr();
    return a;
  }
  void
  clear()
  {
    head = nullptr;
  }
  void reverse();
  List(C acar) : head(new ConsCell<C, A>(acar)) {}
  List(C a, C b) : head(new ConsCell<C, A>(a, new ConsCell<C, A>(b))) {}
  List(C a, C b, C c) : head(new ConsCell<C, A>(a, new ConsCell<C, A>(b, new ConsCell<C, A>(c)))) {}
  List() : head(0) {}
};
#define forc_List(_c, _p, _l) \
  if ((_l).head)              \
    for (_c *_p = (_l).head; _p; _p = _p->cdr)

template <class C, class A>
void
List<C, A>::reverse()
{
  ConsCell<C, A> *n, *t;
  for (ConsCell<C, A> *p = head; p; p = n) {
    n      = p->cdr;
    p->cdr = t;
    t      = p;
  }
  head = t;
}

//
// Atomic lists
//

template <class C, class L = typename C::Link_link> struct AtomicSLL {
  void
  push(C *c)
  {
    ink_atomiclist_push(&al, c);
  }
  C *
  pop()
  {
    return (C *)ink_atomiclist_pop(&al);
  }
  C *
  popall()
  {
    return (C *)ink_atomiclist_popall(&al);
  }
  bool
  empty()
  {
    return INK_ATOMICLIST_EMPTY(al);
  }

  /*
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   * only if only one thread is doing pops it is possible to have a "remove"
   * which only that thread can use as well.
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   */
  C *
  remove(C *c)
  {
    return (C *)ink_atomiclist_remove(&al, c);
  }
  C *
  head()
  {
    return (C *)TO_PTR(FREELIST_POINTER(al.head));
  }
  C *
  next(C *c)
  {
    return (C *)TO_PTR(c);
  }

  InkAtomicList al;

  AtomicSLL();
};

#define ASLL(_c, _l) AtomicSLL<_c, _c::Link##_##_l>
#define ASLLM(_c, _m, _ml, _l) AtomicSLL<_c, _c::Link##_##_ml##_##_l>

template <class C, class L> inline AtomicSLL<C, L>::AtomicSLL()
{
  ink_atomiclist_init(&al, "AtomicSLL", (uint32_t)(uintptr_t)&L::next_link((C *)nullptr));
}
