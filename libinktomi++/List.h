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

  list type		1-linked list	2-linked list	queue      plist
  ---------		-------------	-------------	-----      -----
  link cell		SLink<C>	Link<C>		Link<C>	   Link<C>
  list descriptor	SLL<C>		DLL<C>		Queue<C>   Plist<C>

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
#define GetSLinkNext(_c, _e, _o) (((SLink<_c> *)(void*)(((intptr_t)(void*)_e) + _o))->next)

//
//      Link cell for doubly-linked list of objects of type C.
//
template <class C> struct Link : SLink<C> {
  C *prev;
  Link() : prev(NULL) {}
};
#define GetLink(_c, _e, _o) (((Link<_c> *)(void*)(((intptr_t)(void*)_e) + _o)))
#define GetLinkNext(_c, _e, _o) (((Link<_c> *)(void*)(((intptr_t)(void*)_e) + _o))->next)
#define GetLinkPrev(_c, _e, _o) (((Link<_c> *)(void*)(((intptr_t)(void*)_e) + _o))->prev)

//
//      List descriptor for singly-linked list of objects of type C.
//
template <class C, int o = -1> class SLL {
 public:
  C *head;
  void push(C *e);
  C *pop();
  void clear() { head = NULL; }
  C *next_link(C *e) { return GetLinkNext(C, e, o); }
  // old manual link offset compatibiilty code
  SLink<C> &link(C * x, C * c, SLink<C> &cl);
  SLink<C> &head_link(C * c, SLink<C> &l) { return link(head, c, l); }
  void push(C * c, SLink<C> &l);
  C *pop(C * c, SLink<C> &l);
  // end compatibility code

  SLL() : head(NULL) {}
  SLL(C *c) : head(c) {}
};
#define SList(_c, _f)  SLL<_c, ink_offsetof(_c, _f)>
#define forl_LL(_c, _p, _l) for (_c *_p = (_l).head; _p; _p = (_l).next_link(_p))

template<class C, int o> inline void 
SLL<C,o>::push(C *c, SLink<C> &l) {
  l.next = head;
  head = c;
}

template <class C, int o> inline void 
SLL<C, o>::push(C *e) {
  if (o == -1) {
    push(e, *(SLink<C>*)&e->link);
    return;
  }
  GetSLinkNext(C, e, o) = head;
  head = e;
}

template<class C, int o> inline C * 
SLL<C, o>::pop(C * c, SLink<C> &l) {
  C *res = head;
  if (res) {
    head = l.next;
    link(res, c, l).next = NULL;
  }
  return res;
}

template <class C, int o> inline C *
SLL<C, o>::pop() {
  if (o == -1) {
    if (head)
      return pop(head, *(SLink<C>*)&head->link);
    else
      return NULL;
  }
  C *ret = head;
  if (ret) {
    head = GetSLinkNext(C, ret, o);
    GetSLinkNext(C, ret, o) = NULL;
  }
  return ret;
}

template<class C, int o> inline SLink<C> &SLL<C, o>::link(C * x, C * c, SLink<C> &cl) {
  return *(SLink<C> *)(((char *) x) + (((char *) &cl) - ((char *) c)));
}

//
//      List descriptor for doubly-linked list of objects of type C.
//
template <class C, int o = -1> struct DLL {
  C *head;
  bool empty() { return head == NULL; }
  void push(C *e);
  C *pop();
  void remove(C *e);
  void insert(C *e, C *after);
  bool in(C *e) { return head == e || GetLinkNext(C, e, o) || GetLinkPrev(C, e, o); }
  void clear() { head = NULL; }
  C *next_link(C *e) { return GetLinkNext(C, e, o); }
  C *prev_link(C *e) { return GetLinkPrev(C, e, o); }
  // old manual link offset compatibiilty code
  void push(C *c, Link<C> &l);
  void remove(C *c, Link<C> &l);
  void enqueue(C *c, Link<C> &l);
  void insert(C *c, Link<C> &l, C *after);
  bool in(C * c, Link<C> &l) { return l.prev || l.next || this->head == c; }
  C *pop(C *c, Link<C> &l);
  Link<C> &link(C *x, C *c, Link<C> &cl);
  Link<C> &head_link(C * c, Link<C> &cl);
  Link<C> &tail_link(C * c, Link<C> &cl);
  // end compatibility code

  DLL() : head(NULL) {}
};
#define DList(_c, _f)  DLL<_c, ink_offsetof(_c, _f)>

template<class C, int o> inline void 
DLL<C,o>::push(C *c, Link<C> &l)
{
  C *chead = (C *)head;
  l.next = chead;
  if (chead)
    link(chead, c, l).prev = c;
  head = c;
}

template <class C, int o> inline void 
DLL<C, o>::push(C *e) {
  if (o == -1) {
    push(e, *(Link<C>*)&e->link);
    return;
  }
  if (head)
    GetLinkPrev(C, head, o) = e;
  GetLinkNext(C, e, o) = head;
  head = e;
}

template<class C, int o> inline void 
DLL<C,o>::remove(C *c, Link<C> &l)
{
  if (!head)
    return;
  if (c == head)
    head = (C *) link(head, c, l).next;
  if (l.prev)
    link(l.prev, c, l).next = l.next;
  if (l.next)
    link(l.next, c, l).prev = l.prev;
  l.prev = NULL;
  l.next = NULL;
}

template <class C, int o> inline void
DLL<C, o>::remove(C *e) {
  if (o == -1) {
    remove(e, *(Link<C>*)&e->link);
    return;
  }
  if (!head) return;
  if (e == head) head = GetLinkNext(C, e, o);
  if (GetLinkPrev(C, e, o)) GetLinkNext(C, GetLinkPrev(C, e, o), o) = GetLinkNext(C, e, o);
  if (GetLinkNext(C, e, o)) GetLinkPrev(C, GetLinkNext(C, e, o), o) = GetLinkPrev(C, e, o);
  GetLinkPrev(C, e, o) = NULL;
  GetLinkNext(C, e, o) = NULL;
}

template<class C, int o> inline C *
DLL<C,o>::pop(C * c, Link<C> &l)
{
  C *res = (C*)head;
  if (res)
    remove(res, link(res, c, l));
  return res;
}

template <class C, int o> inline C *
DLL<C, o>::pop() {
  if (o == -1) {
    if (head)
      return pop(head, *(Link<C>*)&head->link);
    else
      return NULL;
  }
  C *ret = head;
  if (ret) {
    head = GetLinkNext(C, ret, o);
    if (head)
      GetLinkPrev(C, head, o) = NULL;
    GetLinkNext(C, ret, o) = NULL;
    return ret;
  } else
    return NULL;
}

template<class C, int o> inline void
DLL<C, o>::insert(C *c, Link<C> &l, C *after) {
  if (!after) {
    push(c, l);
    return;
  }
  l.prev = after;
  l.next = link(after, c, l).next;
  link(after, c, l).next = c;
  if (l.next)
    link(l.next, c, l).prev = c;
}

template <class C, int o> inline void
DLL<C, o>::insert(C *e, C *after) {
  if (o == -1) {
    insert(e, *(Link<C>*)&e->link, after);
    return;
  }
  if (!after) { push(e); return; }
  GetLinkPrev(C, e, o) = after; 
  GetLinkNext(C, e, o) = GetLinkNext(C, after, o);
  GetLinkNext(C, after, o) = e;
  if (GetLinkNext(C, e, o)) GetLinkPrev(C, GetLinkNext(C, e, o), o) = e;
}

template <class C, int o> inline Link<C> &
DLL<C,o>::link(C *x, C *c, Link<C> &cl) {
  return (*(Link<C>*)(((char*)x)+(((char*)&cl)-((char*)c))));
}

template <class C, int o> inline Link<C> &
DLL<C,o>::head_link(C * c, Link<C> &cl) {
  return link((C *) this->head, c, cl);
}

template <class C, int o> inline Link<C> &
DLL<C,o>::tail_link(C * c, Link<C> &cl) {
  return link((C *) this->tail, c, cl);
}

//
//      List descriptor for queue of objects of type C.
//
template <class C, int o = -1> class Queue : public DLL<C, o> {
 public:
  using DLL<C, o>::head;
  using DLL<C, o>::link;
  C *tail;
  void push(C *e);
  C *pop();
  void enqueue(C *e);
  void in_or_enqueue(C *e);
  C *dequeue();
  void remove(C *e);
  void insert(C *e, C *after);
  void append(Queue<C, o> q);
  void append(DLL<C, o> q);
  void clear() { head = NULL; tail = NULL; }
  // old manual link offset compatibiilty code
  void push(C *c, Link<C> &l);
  C *pop(C *c, Link<C> &l);
  void enqueue(C *e, Link<C> &l);
  C *dequeue(C *c, Link<C> &l);
  void remove(C *c, Link<C> &l);
  void insert(C *c, Link<C> &l, C * after);
  void append(Queue<C, o> q, Link<C> &l);
  void append(DLL<C, o> q, Link<C> &l);
  // end compatibility code
  
  Queue() : tail(NULL) {}
};
#define Que(_c, _f) Queue<_c, ink_offsetof(_c, _f)>

template <class C, int o> inline void 
Queue<C, o>::push(C * c, Link<C> &l) {
  DLL<C, o>::push(c, l);
  if (!tail)
    tail = head;
}

template <class C, int o> inline void 
Queue<C, o>::push(C *e) {
  if (o == -1) {
    push(e, *(Link<C>*)&e->link);
    return;
  }
  DLL<C, o>::push(e);
  if (!tail) tail = head;
}

template <class C, int o> inline C *
Queue<C, o>::pop(C *c, Link<C> &l) {
  C *ret = DLL<C,o>::pop(c, l);
  if (!head)
    tail = NULL;
  return ret;
}

template <class C, int o> inline C *
Queue<C, o>::pop() {
  if (o == -1) {
    if (head)
      return pop(head, *(Link<C>*)&head->link);
    else
      return NULL;
  }
  C *ret = DLL<C, o>::pop();
  if (!head) tail = NULL;
  return ret;
}

template <class C, int o> inline void
Queue<C, o>::insert(C *c, Link<C> &l, C *after) {
  DLL<C, o>::insert(c, l, after);
  if (!tail)
    tail = head;
  else if (tail == after)
    tail = c;
}

template <class C, int o> inline void
Queue<C, o>::insert(C *e, C *after) {
  if (o == -1) {
    insert(e, *(Link<C>*)&e->link, after);
    return;
  }
  DLL<C, o>::insert(e, after);
  if (!tail)
    tail = head;
  else if (tail == after)
    tail = e;
}

template<class C, int o> inline void 
Queue<C, o>::remove(C * c, Link<C> &l) {
  if (c == tail)
    tail = l.prev;
  DLL<C, o>::remove(c, l);
}

template <class C, int o> inline void
Queue<C, o>::remove(C *e) {
  if (o == -1) {
    remove(e, *(Link<C>*)&e->link);
    return;
  }
  if (tail == e)
    tail = GetLinkPrev(C, e, o);
  DLL<C, o>::remove(e);
}

template <class C, int o> inline void
Queue<C, o>::append(DLL<C, o> q, Link<C> &l) {
  C *qtail = q.head;
  if (qtail)
    while (link(qtail, q.head, l).next)
      qtail = link(qtail, q.head, l).next;
  if (!head) {
    head = q.head;
    tail = qtail;
  } else {
    if (q.head) {
      link(tail, q.head, l).next = q.head;
      link(q.head, q.head, l).prev = tail;
      tail = qtail;
    }
  }
}

template <class C, int o> inline void
Queue<C, o>::append(DLL<C, o> q) {
  if (o == -1) {
    if (q.head)
      append(q, *(Link<C>*)&q.head->link);
    return;
  }
  C *qtail = q.head;
  if (qtail)
    while (GetLinkNext(C, qtail, o))
      qtail = GetLinkNext(C, qtail, o);
  if (!head) {
    head = q.head;
    tail = qtail;
  } else {
    if (q.head) {
      GetLinkNext(C, tail, o) = q.head;
      GetLinkPrev(C, q.head, o) = tail;
      tail = qtail;
    }
  }
}

template <class C, int o> inline void
Queue<C, o>::append(Queue<C, o> q, Link<C> &l) {
  if (!head) {
    head = q.head;
    tail = q.tail;
  } else {
    if (q.head) {
      link(tail, q.head, l).next = q.head;
      link(q.head, q.head, l).prev = tail;
      tail = q.tail;
    }
  }
}

template <class C, int o> inline void
Queue<C, o>::append(Queue<C, o> q) {
  if (o == -1) {
    append(q, *(Link<C>*)&q.head->link);
    return;
  }
  if (!head) {
    head = q.head;
    tail = q.tail;
  } else {
    if (q.head) {
      GetLinkNext(C, tail, o) = q.head;
      GetLinkPrev(C, q.head, o) = tail;
      tail = q.tail;
    }
  }
}

template<class C, int o> inline void 
Queue<C,o>::enqueue(C *c, Link<C> &l) {
  C *ctail = (C*)tail;
  if (ctail)
    insert(c, l, ctail);
  else {
    ink_assert(!head);
    DLL<C,o>::push(c, l);
  }
  tail = c;
}

template <class C, int o> inline void 
Queue<C, o>::enqueue(C *e) {
  if (o == -1) {
    enqueue(e, *(Link<C>*)&e->link);
    return;
  }
  if (tail)
    insert(e, tail);
  else
    push(e);
}

template <class C, int o> inline void 
Queue<C, o>::in_or_enqueue(C *e) {
  if (!in(e)) enqueue(e);
}

template<class C, int o> inline C * 
Queue<C, o>::dequeue(C * c, Link<C> &l) {
  C *chead = (C*)head;
  if (!chead)
    return NULL;
  remove(chead, link(chead, c, l));
  return (C*)chead;
}

template <class C, int o> inline C *
Queue<C, o>::dequeue() {
  if (o == -1) {
    if (head)
      return dequeue(head, *(Link<C>*)&head->link);
    else
      return NULL;
  }
  return pop();
}

//
// Adds sorting, but requires that elements implement <
//

template<class C, int o = -1> struct SortableQueue: public Queue<C, o>
{
  using DLL<C, o>::head;
  using DLL<C, o>::link;
  using Queue<C, o>::tail;
  void sort() {
    if (!head) return;
    if (o == -1) {
      sort(head, *(Link<C>*)&head->link);
      return;
    }
    sort(head, *GetLink(C, head, o));
  }
  void sort(C * c, Link<C> &l)
  {
    bool clean = false;
    while (!clean) {
      clean = true;
      C *v = head;
      C *n = head_link(c, l).next;
      while (n) {
        C *f = link(n, c, l).next;
        if (*n < *v) {
          clean = false;
          // swap 'em
          if (head == v)
            head = n;
          if (tail == n)
            tail = v;
          // fix prev (p)
          C *p = link(v, c, l).prev;
          if (p) {
            link(p, c, l).next = n;
            link(n, c, l).prev = p;
          } else {
            link(n, c, l).prev = NULL;
          }
          // fix follow (f)
          if (f) {
            link(f, c, l).prev = v;
            link(v, c, l).next = f;
          } else {
            link(v, c, l).next = NULL;
          }
          // fix interior
          link(v, c, l).prev = n;
          link(n, c, l).next = v;
        } else {
          v = n;
        }
        n = f;
      }
    }
  }
};
#define SortableQue(_c, _l) SortableQueue<_c, ink_offsetof(_c, _l)>

//////////////////////////////////////////////////////////////////////////////
//
// perl-like interface to queues.
//      push() adds to the end                  - NOT LIKE DLL!
//      pop() removes from the end              - NOT LIKE DLL!
//      shift() removes from the beginning
//      unshift() adds to the beginning
//
//////////////////////////////////////////////////////////////////////////////

template<class C, int o> struct Plist:public Queue<C, o>
{
  using DLL<C,o>::head;
  using Queue<C,o>::tail;
  void push(C * c) {
    Queue<C, o>::enqueue(c, *GetLink(C, c, o));
  }
  void push(C * c, Link<C> &l) {
    Queue<C, o>::enqueue(c, l);
  }
  C *pop() {
    return pop(tail, *GetLink(C, tail, o));
  }
  C *pop(C * c, Link<C> &l) {
    C *v = tail;
    this->remove(v, tail_link(c, l));
    return v;
  }
  C *shift() {
    return Queue<C, o>::dequeue();
  }
  C *shift(C * c, Link<C> &l) {
    return Queue<C, o>::dequeue(c, l);
  }
  void unshift(C * c) {
    unshift(c, *GetLink(C, c, o));
  }
  void unshift(C * c, Link<C> &l) {
    Queue<C, o>::push(c, l);
    if (!tail)
      tail = c;
  }
private:
  void enqueue(C *) {
    ink_assert(!"no Plist::enqueue");
  }
  void enqueue(C *, Link<C> &) {
    ink_assert(!"no Plist::enqueue");
  }
  C *dequeue() {
    ink_assert(!"no Plist::dequeue");
    return 0;
  }
  C *dequeue(C *, Link<C> &) {
    ink_assert(!"no Plist::dequeue");
    return 0;
  }
};

//////////////////////////////////////////////////////////////////////////////
//
// counted perl-like interface to queue
//      push() adds to the end                  - NOT LIKE DLL!
//      pop() removes from the end              - NOT LIKE DLL!
//      shift() removes from the beginning
//      unshift() adds to the beginning
//
//      count - count of items in list
//
//  XXX - bug: count doesn't work if insert() and remove() are called
//        directly.
//
//////////////////////////////////////////////////////////////////////////////

template<class C, int o> struct CPlist:Plist<C, o>
{
  using DLL<C,o>::head;
  using Queue<C,o>::tail;

  int count;

  void push(C * c) {
    push(c, *GetLink(C, c, o));
  }
  void push(C * c, Link<C> &l) {
    Plist<C, o>::push(c, l);
    count++;
  }
  C *pop() {
    return pop(tail, *GetLink(C, tail, o));
  }
  C *pop(C * c, Link<C> &l) {
    C *t = Plist<C, o>::pop(c, l);
    if (t)
      count--;
    return t;
  }
  C *shift() {
    return shift(head, *GetLink(C, head, o));
  }
  C *shift(C * c, Link<C> &l) {
    C *t = Plist<C, o>::shift(c, l);
    if (t)
      count--;
    return t;
  }
  void unshift(C * c) {
    unshift(c, *GetLink(C, c, o));
  }
  void unshift(C * c, Link<C> &l) {
    Plist<C, o>::unshift(c, l);
    count++;
  }
  void clear() {
    Plist<C, o>::clear();
    count = 0;
  }

  CPlist():count(0) { }
};


//////////////////////////////////////////////////////////////////////////////
//
//      FOR_EACH(instance_name, list_descriptor, link_field_name)
//
//      This is a macro to simplify iteration along lists of various types.
//      It takes a list <list_descriptor>, the name of the link field inside
//      the constituent objects <link_field_name>, and loops in forward
//      order across the constituent objects, assigning each one to the
//      variable with name <instance_name>.
//
//////////////////////////////////////////////////////////////////////////////

#define FOR_EACH(_x,_q,_l) \
for(_x = _q.head; _x != NULL; _x = LINK(_x,_x,_x->_l).next)

#undef LINK


// atomic lists

template<class C, int o = -1> struct AtomicSLL
{
  SLink<C> &link(C * x, C * c, SLink<C> &cl);

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
  AtomicSLL(C * c, SLink<C> *l);
};

#define ASLL(_c, _l) AtomicSLL<_c, ink_offsetof(_c, _l)>

template<class C, int o> inline SLink<C> &AtomicSLL<C,o>::link(C * x, C * c, SLink<C> &cl) {
  return *(SLink<C> *)(((char *) x) + (((char *) &cl) - ((char *) c)));
}

template<class C, int o> inline AtomicSLL<C,o>::AtomicSLL(C * c, SLink<C> *l) {
  ink_atomiclist_init(&al, "AtomicSLL", (char *) l - (char *) c);
}

template<class C, int o> inline AtomicSLL<C,o>::AtomicSLL() {
  if (o < 0)
    ink_atomiclist_init(&al, "AtomicSLL", (inku32)(uintptr_t)&((C*)0)->link);
  else
    ink_atomiclist_init(&al, "AtomicSLL", (inku32)o);
}

#endif  /*_List_h_*/
