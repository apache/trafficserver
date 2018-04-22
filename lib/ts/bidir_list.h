/** @file

  Bidirectional (doubly linked) list intrusive container template.

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

// Originally copied from https://github.com/wkaras/C-plus-plus-intrusive-container-templates .

/*
Copyright (c) 2016 Walter William Karas
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Include once.
#pragma once

#include <utility>

#if (__cplusplus < 201100) && !defined(nullptr)
#define nullptr 0
#endif

namespace abstract_container
{
#ifndef ABSTRACT_CONTAINER_DIRECTIONS_DEFIFINED_
#define ABSTRACT_CONTAINER_DIRECTIONS_DEFIFINED_

const bool forward = true;
const bool reverse = false;

#endif

/*
Bidirectional list intrusive container class template
The 'abstractor' template parameter class must have the following public
or protected members, or behave as though it does:
type handle -- must be copyable.  Each element to be contained in a list
  must have a unique value of this type associated with it.
Member functions:
handle null() -- must always return the same value, which is a handle value
  that is never associated with any element.  The returned value is called
  the null value.  Must be a static member.
void link(handle h, handle link_h, bool is_forward) -- causes the handle
  value link_h to be stored withing the element associated with the
  handle value h.  The element must be able to store a forward and a
  reverse link handle.  If is_forward is true, link_h is stored as the
  forward link handle, otherwise it is stored as the reverse link handle.
handle link(handle h, bool is_forward) -- for the specified direction,
  must return the stored handle value that was most recently stored in
  the element associated with handle h by the other link() member function.
*/
template <class abstractor> class bidir_list : protected abstractor
{
public:
  typedef typename abstractor::handle handle;

  static handle
  null()
  {
    return (abstractor::null());
  }

#if __cplusplus >= 201100

  template <typename... args_t> bidir_list(args_t &&... args) : abstractor(std::forward<args_t>(args)...), head{null(), null()} {}

  bidir_list(const bidir_list &) = delete;

  bidir_list &operator=(const bidir_list &) = delete;

#else

  bidir_list()
  {
    head[0] = null();
    head[1] = null();
  }

#endif

  handle
  link(handle h, bool is_forward = true)
  {
    return (abstractor::link(h, is_forward));
  }

  // Put the specied element (which must not be part of any list) into
  // a state that it can only be in when not in any list.
  //
  void
  make_detached(handle h)
  {
    link(h, h, forward);
  }

  // Returns true if make_detach() was called for the specified element,
  // and it has not since been put in any list.
  //
  bool
  is_detached(handle h)
  {
    return (link(h, forward) == h);
  }

  // FUTURE
  // bidir_list(
  //   bidir_list &to_split, handle first_in_list, handle last_in_list)

  // Returns the handle of first element in the list in the given direction.
  // Returns the null value if the list is empty.
  //
  handle
  start(bool is_forward = true)
  {
    return (head[is_forward]);
  }

  // For the element in_list (already in the list), inserts the element
  // to_insert after it in the given direction.
  //
  void
  insert(handle in_list, handle to_insert, bool is_forward = true)
  {
    handle ilf = link(in_list, is_forward);
    link(to_insert, ilf, is_forward);
    link(to_insert, in_list, !is_forward);
    if (ilf == null())
      // New head in reverse direction.
      head[!is_forward] = to_insert;
    else
      link(ilf, to_insert, !is_forward);
    link(in_list, to_insert, is_forward);
  }

#if 0
    // FUTURE
    void insert(handle in_list, bidir_list &to_insert, bool is_forward = true)
      {
        if (to_insert.head[forward] == null())
          return;
        ...
      }
#endif

  // Remorves the specified element (initially in the list) from the list.
  //
  void
  remove(handle in_list)
  {
    handle f = link(in_list, forward);
    handle r = link(in_list, reverse);

    if (head[forward] == in_list)
      head[forward] = f;

    if (head[reverse] == in_list)
      head[reverse] = r;

    if (f != null())
      link(f, r, reverse);

    if (r != null())
      link(r, f, forward);
  }

  // FUTURE
  // void remove(handle first_in_list, handle last_in_list))

  // Make the specified element (not initially in the list) the new first
  // element in the list, in the specified direction.
  //
  void
  push(handle to_push, bool is_forward = true)
  {
    link(to_push, null(), !is_forward);
    link(to_push, head[is_forward], is_forward);

    if (head[is_forward] != null())
      link(head[is_forward], to_push, !is_forward);
    else
      head[!is_forward] = to_push;

    head[is_forward] = to_push;
  }

  // FUTURE
  // void push(bidir_list &to_push, bool is_forward = true)

  // Removes and returns the first element (in the given direction) in the
  // list.
  //
  handle
  pop(bool is_forward = true)
  {
    handle p = head[is_forward];

    head[is_forward] = link(p, is_forward);

    if (head[is_forward] == null())
      head[!is_forward] = null();
    else
      link(head[is_forward], null(), !is_forward);

    return (p);
  }

  // Returns true if the list is empty.
  //
  bool
  empty()
  {
    return (head[forward] == null());
  }

  // Initialized the list to the empty state.
  //
  void
  purge()
  {
    head[forward] = null();
    head[reverse] = null();
  }

private:
  handle head[2];

  void
  link(handle h, handle link_h, bool is_forward = true)
  {
    abstractor::link(h, link_h, is_forward);
  }

}; // end bidir_list

namespace impl
{
  struct p_bidir_list_abs;

  class p_bidir_list_elem
  {
  public:
    const p_bidir_list_elem *
    link(bool is_forward = true) const
    {
      return (link_[is_forward]);
    }

  private:
    p_bidir_list_elem *link_[2];

    friend struct impl::p_bidir_list_abs;
  };

  struct p_bidir_list_abs {
    typedef p_bidir_list_elem *handle;

    static handle
    null()
    {
      return (nullptr);
    }

    static handle
    link(handle h, bool is_forward)
    {
      return (h->link_[is_forward]);
    }

    static void
    link(handle h, handle link_h, bool is_forward)
    {
      h->link_[is_forward] = link_h;
    }
  };

} // end namespace impl

class p_bidir_list : public bidir_list<impl::p_bidir_list_abs>
{
public:
  typedef impl::p_bidir_list_elem elem;
};

} // end namespace abstract_container
