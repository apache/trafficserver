/** @file

    IntrusivePtr tests.

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

#include "tscore/IntrusivePtr.h"

#include <string>
#include <sstream>
#include <catch.hpp>

struct Thing : public ts::IntrusivePtrCounter {
  Thing() { ++_count; }
  virtual ~Thing() { --_count; }
  std::string _name;
  static int _count; // instance count.
};

struct Stuff : public Thing {
  int _value{0};
};

struct Obscure : private ts::IntrusivePtrCounter {
  std::string _text;

  friend class ts::IntrusivePtr<Obscure>;
};

struct Item : public ts::IntrusivePtrCounter {
  ts::IntrusivePtr<Item> _next;
  Item() { ++_count; }
  ~Item() { --_count; }
  static int _count; // instance count.
};

struct Atomic : ts::IntrusivePtrAtomicCounter {
  int _q{0};
};

int Thing::_count{0};
int Item::_count{0};

TEST_CASE("IntrusivePtr", "[libts][IntrusivePtr]")
{
  using Ptr = ts::IntrusivePtr<Thing>;

  Ptr p1{new Thing};
  REQUIRE(p1.use_count() == 1);
  REQUIRE(Thing::_count == 1);
  p1.reset(nullptr);
  REQUIRE(Thing::_count == 0);

  p1.reset(new Thing);
  Ptr p2 = p1;
  REQUIRE(Thing::_count == 1);
  REQUIRE(p1.use_count() == p2.use_count());
  REQUIRE(p2.use_count() == 2);

  Ptr p3{new Thing};
  REQUIRE(Thing::_count == 2);
  p1 = p3;
  p2 = p3;
  REQUIRE(Thing::_count == 1);

  Ptr p4;

  REQUIRE(static_cast<bool>(p4) == false);
  REQUIRE(!p4 == true);

  REQUIRE(static_cast<bool>(p3) == true);
  REQUIRE(!p3 == false);

  // This is a compile check to make sure IntrusivePtr can be used with private inheritance
  // of ts::InstrusivePtrCounter if IntrusivePtr is declared a friend.
  ts::IntrusivePtr<Obscure> op{new Obscure};
  op->_text.assign("Text");
}

// List test.
TEST_CASE("IntrusivePtr List", "[libts][IntrusivePtr]")
{
  // The clang analyzer claims this type of list manipulation leads to use after free because of
  // premature class destruction but these tests verify that is a false positive.

  using LP = ts::IntrusivePtr<Item>;

  LP list{new Item}; // start a list
  {
    // Add an item to the front of the list.
    LP item{new Item};

    REQUIRE(Item::_count == 2);
    REQUIRE(list.use_count() == 1);
    REQUIRE(item.use_count() == 1);
    item->_next = list;
    REQUIRE(Item::_count == 2);
    REQUIRE(list.use_count() == 2);
    REQUIRE(item.use_count() == 1);
    list = item;
    REQUIRE(Item::_count == 2);
    REQUIRE(list.use_count() == 2);
  }
  REQUIRE(Item::_count == 2);
  REQUIRE(list.use_count() == 1);
  REQUIRE(list->_next.use_count() == 1);

  {
    // add an item after the first element in a non-empty list.
    LP item{new Item};

    REQUIRE(Item::_count == 3);
    REQUIRE(list.use_count() == 1);
    REQUIRE(item.use_count() == 1);
    item->_next = list->_next;
    REQUIRE(Item::_count == 3);
    REQUIRE(list.use_count() == 1);
    REQUIRE(item.use_count() == 1);
    REQUIRE(item->_next.use_count() == 2);
    list->_next = item;
    REQUIRE(Item::_count == 3);
    REQUIRE(item.use_count() == 2);
    REQUIRE(list->_next.get() == item.get());
    REQUIRE(item->_next.use_count() == 1);
  }
  REQUIRE(Item::_count == 3);
  REQUIRE(list.use_count() == 1);
  REQUIRE(list->_next.use_count() == 1);
  list.reset();
  REQUIRE(Item::_count == 0);

  list.reset(new Item); // start a list
  REQUIRE(Item::_count == 1);
  {
    // Add item after first in singleton list.
    LP item{new Item};
    REQUIRE(Item::_count == 2);
    REQUIRE(list.use_count() == 1);
    REQUIRE(item.use_count() == 1);
    REQUIRE(!list->_next);
    item->_next = list->_next;
    REQUIRE(!item->_next);
    REQUIRE(Item::_count == 2);
    REQUIRE(list.use_count() == 1);
    REQUIRE(item.use_count() == 1);
    list->_next = item;
    REQUIRE(Item::_count == 2);
    REQUIRE(item.use_count() == 2);
    REQUIRE(list->_next.get() == item.get());
  }
  REQUIRE(Item::_count == 2);
  REQUIRE(list.use_count() == 1);
  REQUIRE(list->_next.use_count() == 1);
  list.reset();
  REQUIRE(Item::_count == 0);
}

// Cross type tests.
TEST_CASE("IntrusivePtr CrossType", "[libts][IntrusivePtr]")
{
  using ThingPtr = ts::IntrusivePtr<Thing>;
  using StuffPtr = ts::IntrusivePtr<Stuff>;

  ThingPtr tp1{new Stuff};
  StuffPtr sp1{new Stuff};

  ThingPtr tp2{sp1};
  REQUIRE(tp2.get() == sp1.get());
  REQUIRE(Thing::_count == 2);
  REQUIRE(tp2.use_count() == 2);
  tp2 = sp1; // should be a no-op, verify it compiles.
  REQUIRE(tp2.get() == sp1.get());
  REQUIRE(Thing::_count == 2);
  REQUIRE(sp1.use_count() == 2);
  sp1 = ts::ptr_cast<Stuff>(tp1); // move assign
  REQUIRE(sp1.get() == tp1.get());
  REQUIRE(sp1.use_count() == 2);
  REQUIRE(tp2.use_count() == 1);
  tp1 = ts::ptr_cast<Stuff>(tp2); // cross type move assign
  REQUIRE(sp1.use_count() == 1);
  REQUIRE(tp1.get() == tp2.get());
  REQUIRE(tp1.use_count() == 2);
  sp1 = ts::ptr_cast<Stuff>(tp1);
  REQUIRE(Thing::_count == 1);
  REQUIRE(sp1.use_count() == 3);
  tp1 = tp2; // same object assign check.
  {
    StuffPtr sp2{sp1};
    REQUIRE(sp1.use_count() == 4);
  }
  sp1.reset();
  tp1 = std::move(tp2); // should clear tp2
  tp1.reset();
  REQUIRE(Thing::_count == 0);
}

TEST_CASE("IntrusiveAtomicPtr", "[libts][IntrusivePtr]")
{
  using Ptr = ts::IntrusivePtr<Atomic>;

  Ptr p1{new Atomic};
  REQUIRE(p1.use_count() == 1);
  p1.reset(nullptr);
}
