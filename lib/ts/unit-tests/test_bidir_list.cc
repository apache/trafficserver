/** @file

    Unit tests for bidir_list.h

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

// Unit testing for list.h and bidir_list.h .

#include "catch.hpp"

// Test include once protection by including twice.
//
#include <ts/bidir_list.h>
#include <ts/bidir_list.h>

namespace
{
using namespace abstract_container;

typedef p_bidir_list list_t;

list_t lst;

const unsigned num_e = 5;

typedef list_t::elem elem_t;

elem_t e[num_e];

// Purge test list, mark all elements as detached.
//
void
init()
{
  lst.purge();

  for (unsigned i = 0; i < num_e; ++i)
    lst.make_detached(e + i);
}

// Check if list structure is sane, and elements of list are in ascending
// order by address.
//
void
scan()
{
  elem_t *last = nullptr;

  for (unsigned i = 0; i < num_e; ++i)
    if (!lst.is_detached(e + i)) {
      REQUIRE(lst.link(e + i, reverse) == last);
      if (last)
        REQUIRE(lst.link(last) == (e + i));
      else
        REQUIRE(lst.start() == (e + i));
      last = e + i;
    }

  REQUIRE(lst.start(reverse) == last);
  REQUIRE(lst.empty() == (last == nullptr));
  if (last)
    REQUIRE(lst.link(last) == nullptr);
  else
    REQUIRE(lst.start() == nullptr);
}

} // end anonymous namespace

TEST_CASE("abstract_countainer::bidir_list", "[ACBL]")
{
  REQUIRE(sizeof(lst) == (2 * sizeof(void *)));

  REQUIRE(sizeof(e[0]) == (2 * sizeof(void *)));

  REQUIRE(lst.empty());

  init();
  scan();

  lst.push(e + 2);
  scan();
  lst.insert(e + 2, e + 4);
  scan();
  lst.insert(e + 2, e + 0, reverse);
  scan();
  lst.insert(e + 2, e + 3);
  scan();
  lst.insert(e + 2, e + 1, reverse);
  scan();

  lst.remove(e + 2);
  lst.make_detached(e + 2);
  scan();
  lst.remove(e + 0);
  lst.make_detached(e + 0);
  scan();
  lst.remove(e + 4);
  lst.make_detached(e + 4);
  scan();
  lst.remove(e + 3);
  lst.make_detached(e + 3);
  scan();
  lst.remove(e + 1);
  lst.make_detached(e + 1);
  scan();

  REQUIRE(lst.empty());

  lst.push(e + 2);
  scan();
  lst.pop();
  lst.make_detached(e + 2);
  scan();
  lst.push(e + 2, reverse);
  scan();
  lst.pop(reverse);
  lst.make_detached(e + 2);
  scan();
  lst.push(e + 2);
  scan();
  lst.push(e + 1);
  scan();
  lst.push(e + 3, reverse);
  scan();
  lst.pop(reverse);
  lst.make_detached(e + 3);
  scan();
  lst.pop();
  lst.make_detached(e + 1);
  scan();

  lst.purge();
  REQUIRE(lst.empty());
}
