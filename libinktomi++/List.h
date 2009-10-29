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

#include "ink_bool.h"
#include "ink_assert.h"
#include "ink_queue.h"
#include "ink_resource.h"

//
//      Link cell for singly-linked list of objects of type C.
//
template<class C> struct SLink
{
  C *next;
    SLink();
};

template<class C> inline SLink<C>::SLink():next(NULL)
{
}

//
//      Link cell for doubly-linked list of objects of type C.
//
template<class C> struct Link:SLink<C>
{
  C *prev;
    Link():prev(NULL)
  {
  }
};



//
//      List descriptor for singly-linked list of objects of type C.
//
template<class C> struct SLL
{
  C *head;

  // given an object c and its link cl, find the link for object x

    SLink<C> &link(C * x, C * c, SLink<C> &cl);

  // given an object c and its link cl, compute the link for the head object

    SLink<C> &head_link(C * c, SLink<C> &l)
  {
    return link(head, c, l);
  }

  // push objects onto the SLL

  void push(C * c);
  void push(C * c, SLink<C> &l);

  // pop objects off the SLL

  C *pop();
  C *pop(C * c, SLink<C> &l);

SLL():head(NULL) {
  }
};


template<class C> inline SLink<C> &SLL<C>::link(C * x, C * c, SLink<C> &cl)
{
  return *(SLink<C> *)(((char *) x) + (((char *) &cl) - ((char *) c)));
}

template<class C> inline void SLL<C>::push(C * c)
{
  push(c, *(SLink<C> *) & c->link);
}

template<class C> inline void SLL<C>::push(C * c, SLink<C> &l)
{
  l.next = this->head;
  this->head = c;
}

template<class C> inline C * SLL<C>::pop()
{
  if (this->head)
    return pop(this->head, *(SLink<C> *) & this->head->link);
  else
    return NULL;
}

template<class C> inline C * SLL<C>::pop(C * c, SLink<C> &l)
{
  ink_assert(c == this->head);
  C *res = this->head;
  if (res) {
    this->head = l.next;
    link(res, c, l).next = NULL;
  }
  return res;
}

//
//      List descriptor for doubly-linked list of objects of type C.
//
#define LINK(_x,_c,_cl) (*(Link<C>*)(((char*)_x)+(((char*)&_cl)-((char*)_c))))
#define HEAD_LINK(_c,_cl) LINK(this->head,_c,_cl)

template<class C> struct DLL
{
  C *head;

  // given an object c and its link cl, compute the link for object x
  // Stupid SunCC can't seem to inline this at the highest optimiation level
  // hence the macro.
    Link<C> &link(C * x, C * c, Link<C> &cl)
  {
    return *(Link<C> *)(((char *) x) + (((char *) &cl) - ((char *) c)));
  }

  // given an object c and its link cl, compute the link for the head object.

  Link<C> &head_link(C * c, Link<C> &cl)
  {
    return LINK((C *) this->head, c, cl);
  }

  // push objects onto the DLL

  void push(C * c)
  {
    push(c, *(Link<C> *) & c->link);
  }
  void push(C * c, Link<C> &l);

  // pop objects off the DLL

  C *pop();
  C *pop(C * c, Link<C> &l);

  // remove object <c> from any place in the DLL
  //   (<c> must be a member of the list)

  void remove(C * c)
  {
    remove(c, *(Link<C> *) & c->link);
  }
  void remove(C * c, Link<C> &l);

  // insert object <c> after object <after> in the DLL
  //   (<after> must be a member of the list, and <c> must not be)

  void insert(C * c, C * after)
  {
    insert(c, *(Link<C> *) & c->link, after);
  }
  void insert(C * c, Link<C> &l, C * after);

  // determine if object <c> is in the list
  //   (currently does a weak test)

  bool in(C * c)
  {
    return in(c, *(Link<C> *) & c->link);
  }
  bool in(C * c, Link<C> &l)
  {
    return l.prev || l.next || this->head == c;
  }

DLL():head(NULL) {
  }
};

template<class C> inline void DLL<C>::push(C * c, Link<C> &l)
{
  C *chead = (C *) this->head;
  l.next = chead;
  if (chead)
    LINK(chead, c, l).prev = c;
  this->head = c;
}

template<class C> inline C * DLL<C>::pop()
{
  C *chead = (C *) this->head;
  if (chead)
    return pop(chead, *(Link<C> *) & chead->link);
  else
    return NULL;
}

template<class C> inline C * DLL<C>::pop(C * c, Link<C> &l)
{
  C *res = (C *) this->head;
  if (res)
    remove(res, LINK(res, c, l));
  return res;
}

template<class C> inline void DLL<C>::remove(C * c, Link<C> &l)
{
  if (!this->head)
    return;
  if (c == this->head)
    this->head = (C *) LINK(this->head, c, l).next;
  if (l.prev)
    LINK(l.prev, c, l).next = l.next;
  if (l.next)
    LINK(l.next, c, l).prev = l.prev;
  l.prev = NULL;
  l.next = NULL;
}

template<class C> inline void DLL<C>::insert(C * c, Link<C> &l, C * after)
{
  ink_assert(l.prev == NULL);
  ink_assert(l.next == NULL);
  if (!after) {
    push(c, l);
    return;
  }
  l.prev = after;
  l.next = LINK(after, c, l).next;
  LINK(after, c, l).next = c;
  if (l.next)
    LINK(l.next, c, l).prev = c;
}

//
//      List descriptor for queue of objects of type C.
//
#define TAIL_LINK(_c,_cl) LINK(this->tail,_c,_cl)

template<class C> struct Queue:DLL<C>
{
  C *tail;

  // Stupid SunCC can't seem to inline this at the highest optimiation level
  // hence the macro.
    Link<C> &tail_link(C * c, Link<C> &l)
  {
    return LINK(this->tail, c, l);
  }

  // push objects onto the DLL

  void push(C * c)
  {
    push(c, *(Link<C> *) & c->link);
  }
  void push(C * c, Link<C> &l)
  {
    DLL<C>::push(c, l);
    if (!this->tail)
      this->tail = this->head;
  }

  // pop objects off the DLL

  C *pop()
  {
    C *ret = DLL<C>::pop();
    if (!this->head)
      this->tail = NULL;
    return ret;
  }
  C *pop(C * c, Link<C> &l)
  {
    C *ret = DLL<C>::pop(c, l);
    if (!this->head)
      this->tail = NULL;
    return ret;
  }

  // enqueue object <c> at end of the Queue

  void enqueue(C * c)
  {
    enqueue(c, *(Link<C> *) & c->link);
  }
  void enqueue(C * c, Link<C> &l);

  void remove(C * c)
  {
    remove(c, *(Link<C> *) & c->link);
  }
  void remove(C * c, Link<C> &l);

  void insert(C * c, C * after)
  {
    insert(c, *(Link<C> *) & c->link, after);
  }
  void insert(C * c, Link<C> &l, C * after)
  {
    DLL<C>::insert(c, l, after);
    if (!this->tail)
      this->tail = this->head;
    else if (this->tail == after)
      this->tail = c;
  }

  void append(Queue<C> q)
  {
    append(q, *(Link<C> *) & q.head->link);
  }
  void append(Queue<C> q, Link<C> &l)
  {
    if (!this->head) {
      this->head = q.head;
      this->tail = q.tail;
    } else {
      if (q.head) {
        LINK(this->tail, q.head, l).next = q.head;
        LINK(q.head, q.head, l).prev = this->tail;
        this->tail = q.tail;
      }
    }
  }

  void append(DLL<C> q)
  {
    append(q, *(Link<C> *) & q.head->link);
  }
  void append(DLL<C> q, Link<C> &l)
  {
    C *qtail = q.head;
    if (qtail)
      while (LINK(qtail, q.head, l).next)
        qtail = LINK(qtail, q.head, l).next;
    if (!this->head) {
      this->head = q.head;
      this->tail = qtail;
    } else {
      if (q.head) {
        LINK(this->tail, q.head, l).next = q.head;
        LINK(q.head, q.head, l).prev = this->tail;
        this->tail = qtail;
      }
    }
  }

  // dequeue the object from the front of the Queue

  C *dequeue();
  C *dequeue(C * c, Link<C> &l);

  bool in(C * c)
  {
    return DLL<C>::in(c);
  }
  void clear()
  {
    this->head = NULL;
    this->tail = NULL;
  }
  bool empty()
  {
    return this->head == NULL && this->tail == NULL;
  }
Queue():tail(NULL) {
  }
};

template<class C> inline void Queue<C>::enqueue(C * c, Link<C> &l)
{
  C *ctail = (C *) this->tail;
  if (ctail)
    insert(c, l, ctail);
  else {
    ink_assert(!this->head);
    DLL<C>::push(c, l);
  }
  this->tail = c;
}

template<class C> inline void Queue<C>::remove(C * c, Link<C> &l)
{
  if (c == this->tail)
    this->tail = l.prev;
  DLL<C>::remove(c, l);
}

template<class C> inline C * Queue<C>::dequeue()
{
  C *chead = (C *) this->head;
  if (chead)
    return dequeue(chead, *(Link<C> *) & chead->link);
  else
    return NULL;
}

template<class C> inline C * Queue<C>::dequeue(C * c, Link<C> &l)
{
  C *chead = (C *) this->head;
  if (!chead)
    return NULL;
  remove(chead, LINK(chead, c, l));
  return (C *) chead;
}

template<class C> struct SortableQueue:Queue<C>
{
  void sort()
  {
    sort(this->head, *(Link<C> *) & this->head->link);
  }
  void sort(C * c, Link<C> &l)
  {
    bool clean = false;
    while (!clean) {
      clean = true;
      C *v = this->head;
      C *n = HEAD_LINK(c, l).next;
      while (n) {
        C *f = LINK(n, c, l).next;
        if (*n < *v) {
          clean = false;
          // swap 'em
          if (this->head == v)
            this->head = n;
          if (this->tail == n)
            this->tail = v;
          // fix prev (p)
          C *p = LINK(v, c, l).prev;
          if (p) {
            LINK(p, c, l).next = n;
            LINK(n, c, l).prev = p;
          } else {
            LINK(n, c, l).prev = NULL;
          }
          // fix follow (f)
          if (f) {
            LINK(f, c, l).prev = v;
            LINK(v, c, l).next = f;
          } else {
            LINK(v, c, l).next = NULL;
          }
          // fix interior
          LINK(v, c, l).prev = n;
          LINK(n, c, l).next = v;
        } else {
          v = n;
        }
        n = f;
      }
    }
  }
};

//////////////////////////////////////////////////////////////////////////////
//
// perl-like interface to queues.
//      push() adds to the end                  - NOT LIKE DLL!
//      pop() removes from the end              - NOT LIKE DLL!
//      shift() removes from the beginning
//      unshift() adds to the beginning
//
//////////////////////////////////////////////////////////////////////////////

template<class C> struct Plist:public Queue<C>
{
  void push(C * c)
  {
    Queue<C>::enqueue(c, *(Link<C> *) & c->link);
  }
  void push(C * c, Link<C> &l)
  {
    Queue<C>::enqueue(c, l);
  }
  C *pop()
  {
    return pop(this->tail, *(Link<C> *) & this->tail->link);
  }
  C *pop(C * c, Link<C> &l)
  {
    C *v = this->tail;
    this->remove(v, TAIL_LINK(c, l));
    return v;
  }
  C *shift()
  {
    return Queue<C>::dequeue();
  }
  C *shift(C * c, Link<C> &l)
  {
    return Queue<C>::dequeue(c, l);
  }
  void unshift(C * c)
  {
    unshift(c, *(Link<C> *) & c->link);
  }
  void unshift(C * c, Link<C> &l)
  {
    Queue<C>::push(c, l);
    if (!this->tail)
      this->tail = c;
  }
private:
  void enqueue(C *)
  {
    ink_assert(!"no Plist::enqueue");
  }
  void enqueue(C *, Link<C> &)
  {
    ink_assert(!"no Plist::enqueue");
  }
  C *dequeue()
  {
    ink_assert(!"no Plist::dequeue");
    return 0;
  }
  C *dequeue(C *, Link<C> &)
  {
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

template<class C> struct CPlist:Plist<C>
{

  int count;

  void push(C * c)
  {
    push(c, *(Link<C> *) & c->link);
  }
  void push(C * c, Link<C> &l)
  {
    Plist<C>::push(c, l);
    count++;
  }
  C *pop()
  {
    return pop(this->tail, *(Link<C> *) & this->tail->link);
  }
  C *pop(C * c, Link<C> &l)
  {
    C *t = Plist<C>::pop(c, l);
    if (t)
      count--;
    return t;
  }
  C *shift()
  {
    return shift(this->head, *(Link<C> *) & this->head->link);
  }
  C *shift(C * c, Link<C> &l)
  {
    C *t = Plist<C>::shift(c, l);
    if (t)
      count--;
    return t;
  }
  void unshift(C * c)
  {
    unshift(c, *(Link<C> *) & c->link);
  }
  void unshift(C * c, Link<C> &l)
  {
    Plist<C>::unshift(c, l);
    count++;
  }
  void clear()
  {
    Plist<C>::clear();
    count = 0;
  }

CPlist():count(0) {
  }
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

template<class C> struct AtomicSLL
{
  SLink<C> &link(C * x, C * c, SLink<C> &cl);

  void push(C * c)
  {
    ink_atomiclist_push(&al, c);
  }
  C *pop()
  {
    return (C *) ink_atomiclist_pop(&al);
  }
  C *popall()
  {
    return (C *) ink_atomiclist_popall(&al);
  }

  /*
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   * only if only one thread is doing pops it is possible to have a "remove"
   * which only that thread can use as well.
   * WARNING WARNING WARNING WARNING WARNING WARNING WARNING
   */
  C *remove(C * c)
  {
    return (C *) ink_atomiclist_remove(&al, c);
  }

  C *head()
  {
    return (C *) TO_PTR(FREELIST_POINTER(al.head));
  }
  C *next(C * c)
  {
    return (C *) TO_PTR(c);
  }

  InkAtomicList al;

  AtomicSLL();
  AtomicSLL(C * c, SLink<C> *l);
};

template<class C> inline SLink<C> &AtomicSLL<C>::link(C * x, C * c, SLink<C> &cl)
{
  return *(SLink<C> *)(((char *) x) + (((char *) &cl) - ((char *) c)));
}

template<class C> inline AtomicSLL<C>::AtomicSLL(C * c, SLink<C> *l)
{
  ink_atomiclist_init(&al, "AtomicSLL", (char *) l - (char *) c);
}

template<class C> inline AtomicSLL<C>::AtomicSLL()
{
  C *c = (C *) alloca(sizeof(C));
  ink_atomiclist_init(&al, "AtomicSLL", (char *) &c->link.next - (char *) c);
}

#endif  /*_List_h_*/
