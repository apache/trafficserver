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

#ifndef _List_h_
#define	_List_h_

#include <stdint.h>

#include "ink_bool.h"
#include "ink_assert.h"
#include "ink_queue.h"
#include "ink_resource.h"
#include "Compatability.h"

//
//      Link cell for singly-linked list of objects of type C.
//
template <class C> class SLink {
 public:
  C *next;
  SLink() : next(NULL) {};
};
#define SLINK(_c,_f) class Link##_##_f : public SLink<_c> { public:    \
    static _c *& next_link(_c *c) { return c->_f.next; }                \
  }; SLink<_c> _f
#define SLINKM(_c,_m,_f) class Link##_##_m##_##_f : public SLink<_c> { public: \
    static _c *& next_link(_c *c) { return c->_m._f.next; }             \
  };

//
//      Link cell for doubly-linked list of objects of type C.
//
template <class C> struct Link : SLink<C> {
  C *prev;
  Link() : prev(NULL) {}
};
#define LINK(_c,_f) class Link##_##_f : public Link<_c> { public:       \
    static _c *& next_link(_c *c) { return c->_f.next; }                \
    static _c *& prev_link(_c *c) { return c->_f.prev; }                \
  }; Link<_c> _f
#define LINKM(_c,_m,_f) class Link##_##_m##_##_f : public Link<_c> { public:  \
    static _c *& next_link(_c *c) { return c->_m._f.next; }             \
    static _c *& prev_link(_c *c) { return c->_m._f.prev; }             \
  };
#define LINK_FORWARD_DECLARATION(_c,_f) class Link##_##_c##_##_f : public Link<_c> { public:     \
    static _c *& next_link(_c *c);                                      \
    static _c *& prev_link(_c *c);                                      \
  };
#define LINK_DEFINITION(_c,_f)  \
  inline _c *& Link##_##_c##_##_f::next_link(_c *c) { return c->_f.next; } \
  inline _c *& Link##_##_c##_##_f::prev_link(_c *c) { return c->_f.prev; } \

//
//      List descriptor for singly-linked list of objects of type C.
//
template <class C, class L = typename C::Link_link> class SLL {
 public:
  C *head;
  void push(C *e);
  C *pop();
  void clear() { head = NULL; }
  C *& next(C *e) { return L::next_link(e); }

  SLL() : head(NULL) {}
  SLL(C *c) : head(c) {}
};
#define SList(_c, _f)  SLL<_c, _c::Link##_##_f>
#define SListM(_c, _m, _ml, _l) SLL<_c, _c::Link##_##_ml##_##_l>
#define forl_LL(_c, _p, _l) for (_c *_p = (_l).head; _p; _p = (_l).next(_p))

template <class C, class L> inline void 
SLL<C,L>::push(C *e) {
  next(e) = head;
  head = e;
}

template <class C, class L> inline C *
SLL<C,L>::pop() {
  C *ret = head;
  if (ret) {
    head = next(ret);
    next(ret) = NULL;
  }
  return ret;
}

//
//      List descriptor for doubly-linked list of objects of type C.
//
template <class C, class L = typename C::Link_link> struct DLL {
  C *head;
  bool empty() { return head == NULL; }
  void push(C *e);
  C *pop();
  void remove(C *e);
  void insert(C *e, C *after);
  bool in(C *e) { return head == e || next(e) || prev(e); }
  void clear() { head = NULL; }
  C *&next(C *e) { return *(C**)&L::next_link(e); }
  C *&prev(C *e) { return *(C**)&L::prev_link(e); }

  DLL() : head(NULL) {}
};
#define DList(_c, _f)  DLL<_c, _c::Link##_##_f>
#define DListM(_c, _m, _ml, _l) DLL<_c, _c::Link##_##_ml##_##_l>

template <class C, class L> inline void 
DLL<C,L>::push(C *e) {
  if (head)
    prev(head) = e;
  next(e) = head;
  head = e;
}

template <class C, class L> inline void
DLL<C,L>::remove(C *e) {
  if (!head) return;
  if (e == head) head = next(e);
  if (prev(e)) next(prev(e)) = next(e);
  if (next(e)) prev(next(e)) = prev(e);
  prev(e) = NULL;
  next(e) = NULL;
}

template <class C, class L> inline C *
DLL<C,L>::pop() {
  C *ret = head;
  if (ret) {
    head = next(ret);
    if (head)
      prev(head) = NULL;
    next(ret) = NULL;
    return ret;
  } else
    return NULL;
}

template <class C, class L> inline void
DLL<C,L>::insert(C *e, C *after) {
  if (!after) { push(e); return; }
  prev(e) = after; 
  next(e) = next(after);
  next(after) = e;
  if (next(e)) prev(next(e)) = e;
}

//
//      List descriptor for queue of objects of type C.
//
template <class C, class L = typename C::Link_link> class Queue : public DLL<C,L> {
 public:
  using DLL<C,L>::head;
  C *tail;
  void push(C *e);
  C *pop();
  void enqueue(C *e);
  void in_or_enqueue(C *e);
  C *dequeue();
  void remove(C *e);
  void insert(C *e, C *after);
  void append(Queue<C,L> q);
  void append(DLL<C,L> q);
  void clear() { head = NULL; tail = NULL; }
  
  Queue() : tail(NULL) {}
};
#define Que(_c, _f) Queue<_c, _c::Link##_##_f>
#define QueM(_c, _m, _mf, _f) Queue<_c, _c::Link##_##_mf##_##_f>

template <class C, class L> inline void 
Queue<C,L>::push(C *e) {
  DLL<C,L>::push(e);
  if (!tail) tail = head;
}

template <class C, class L> inline C *
Queue<C,L>::pop() {
  C *ret = DLL<C,L>::pop();
  if (!head) tail = NULL;
  return ret;
}

template <class C, class L> inline void
Queue<C,L>::insert(C *e, C *after) {
  DLL<C,L>::insert(e, after);
  if (!tail)
    tail = head;
  else if (tail == after)
    tail = e;
}

template <class C, class L> inline void
Queue<C,L>::remove(C *e) {
  if (tail == e)
    tail = (C*)prev(e);
  DLL<C,L>::remove(e);
}

template <class C, class L> inline void
Queue<C,L>::append(DLL<C,L> q) {
  C *qtail = q.head;
  if (qtail)
    while (next(qtail))
      qtail = next(qtail);
  if (!head) {
    head = q.head;
    tail = qtail;
  } else {
    if (q.head) {
      next(tail) = q.head;
      prev(q.head) = tail;
      tail = qtail;
    }
  }
}

template <class C, class L> inline void
Queue<C,L>::append(Queue<C,L> q) {
  if (!head) {
    head = q.head;
    tail = q.tail;
  } else {
    if (q.head) {
      next(tail) = q.head;
      prev(q.head) = tail;
      tail = q.tail;
    }
  }
}

template <class C, class L> inline void 
Queue<C,L>::enqueue(C *e) {
  if (tail)
    insert(e, tail);
  else
    push(e);
}

template <class C, class L> inline void 
Queue<C,L>::in_or_enqueue(C *e) {
  if (!in(e)) enqueue(e);
}

template <class C, class L> inline C *
Queue<C,L>::dequeue() {
  return pop();
}

//
// Adds sorting, but requires that elements implement <
//

template<class C, class L = typename C::Link_link> struct SortableQueue: public Queue<C,L>
{
  using DLL<C,L>::head;
  using Queue<C,L>::tail;
  void sort() {
    if (!head) return;
    bool clean = false;
    while (!clean) {
      clean = true;
      C *v = head;
      C *n = next(head);
      while (n) {
        C *f = next(n);
        if (*n < *v) {
          clean = false;
          // swap 'em
          if (head == v)
            head = n;
          if (tail == n)
            tail = v;
          // fix prev (p)
          C *p = prev(v);
          if (p) {
            next(p) = n;
            prev(n) = p;
          } else
            prev(n) = NULL;
          // fix follow (f)
          if (f) {
            prev(f) = v;
            next(v) = f;
          } else
            next(v) = NULL;
          // fix interior
          prev(v) = n;
          next(n) = v;
        } else
          v = n;
        n = f;
      }
    }
  }
};
#define SortableQue(_c, _l) SortableQueue<_c, _c::Link##_##_f>


// atomic lists

template<class C, class L = typename C::Link_link> struct AtomicSLL
{
  void push(C * c) { ink_atomiclist_push(&al, c); }
  C *pop() { return (C *) ink_atomiclist_pop(&al); }
  C *popall() { return (C *) ink_atomiclist_popall(&al); }
  bool empty() { return INK_ATOMICLIST_EMPTY(al); }

  /*
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   * only if only one thread is doing pops it is possible to have a "remove"
   * which only that thread can use as well.
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   */
  C *remove(C * c) { return (C *) ink_atomiclist_remove(&al, c); }
  C *head() { return (C *) TO_PTR(FREELIST_POINTER(al.head)); }
  C *next(C * c) { return (C *) TO_PTR(c); }

  InkAtomicList al;

  AtomicSLL();
};

#define ASLL(_c, _l) AtomicSLL<_c, _c::Link##_##_l>
#define ASLLM(_c, _m, _ml, _l) AtomicSLL<_c, _c::Link##_##_ml##_##_l>

template<class C, class L> inline AtomicSLL<C,L>::AtomicSLL() {
  ink_atomiclist_init(&al, "AtomicSLL", (inku32)(uintptr_t)&L::next_link((C*)0));
}

#endif  /*_List_h_*/
