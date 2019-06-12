/** @file

    IntrusiveDList unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <iostream>
#include <string_view>
#include <string>
#include <algorithm>

#include "tscpp/util/IntrusiveDList.h"
#include "tscpp/util/bwf_base.h"

#include "catch.hpp"

using ts::IntrusiveDList;

// --------------------
// Code for documentation - placed here to guarantee the examples at least compile.
// First so that additional tests do not require updating the documentation source links.

class Message
{
  using self_type = Message; ///< Self reference type.

public:
  // Message severity level.
  enum Severity { LVL_DEBUG, LVL_INFO, LVL_WARN, LVL_ERROR };

protected:
  std::string _text; // Text of the message.
  Severity _severity{LVL_DEBUG};
  int _indent{0}; // indentation level for display.

  // Intrusive list support.
  struct Linkage {
    static self_type *&next_ptr(self_type *); // Link accessor.
    static self_type *&prev_ptr(self_type *); // Link accessor.

    self_type *_next{nullptr}; // Forward link.
    self_type *_prev{nullptr}; // Backward link.
  } _link;

  bool is_in_list() const;

  friend class Container;
};

auto
Message::Linkage::next_ptr(self_type *that) -> self_type *&
{
  return that->_link._next;
}
auto
Message::Linkage::prev_ptr(self_type *that) -> self_type *&
{
  return that->_link._prev;
}

bool
Message::is_in_list() const
{
  return _link._next || _link._prev;
}

class Container
{
  using self_type   = Container;
  using MessageList = ts::IntrusiveDList<Message::Linkage>;

public:
  ~Container();

  template <typename... Args> self_type &debug(std::string_view fmt, Args &&... args);

  size_t count() const;
  self_type &clear();
  Message::Severity max_severity() const;
  void print() const;

protected:
  MessageList _msgs;
};

Container::~Container()
{
  this->clear(); // clean up memory.
}

auto
Container::clear() -> self_type &
{
  Message *msg;
  while (nullptr != (msg = _msgs.take_head())) {
    delete msg;
  }
  _msgs.clear();
  return *this;
}

size_t
Container::count() const
{
  return _msgs.count();
}

template <typename... Args>
auto
Container::debug(std::string_view fmt, Args &&... args) -> self_type &
{
  Message *msg = new Message;
  ts::bwprintv(msg->_text, fmt, std::forward_as_tuple(args...));
  msg->_severity = Message::LVL_DEBUG;
  _msgs.append(msg);
  return *this;
}

Message::Severity
Container::max_severity() const
{
  auto spot = std::max_element(_msgs.begin(), _msgs.end(),
                               [](Message const &lhs, Message const &rhs) { return lhs._severity < rhs._severity; });
  return spot == _msgs.end() ? Message::Severity::LVL_DEBUG : spot->_severity;
}

void
Container::print() const
{
  for (auto &&elt : _msgs) {
    std::cout << static_cast<unsigned int>(elt._severity) << ": " << elt._text << std::endl;
  }
}

TEST_CASE("IntrusiveDList Example", "[libtscpputil][IntrusiveDList]")
{
  Container container;

  container.debug("This is message {}", 1);
  REQUIRE(container.count() == 1);
  // Destructor is checked for non-crashing as container goes out of scope.
}

struct Thing {
  Thing *_next{nullptr};
  Thing *_prev{nullptr};
  std::string _payload;

  Thing(std::string_view text) : _payload(text) {}

  struct Linkage {
    static Thing *&
    next_ptr(Thing *t)
    {
      return t->_next;
    }
    static Thing *&
    prev_ptr(Thing *t)
    {
      return t->_prev;
    }
  };
};

// Just for you, @maskit ! Demonstrating non-public links and subclassing.
class PrivateThing : protected Thing
{
  using self_type  = PrivateThing;
  using super_type = Thing;

public:
  PrivateThing(std::string_view text) : super_type(text) {}

  struct Linkage {
    static self_type *&
    next_ptr(self_type *t)
    {
      return ts::ptr_ref_cast<self_type>(t->_next);
    }
    static self_type *&
    prev_ptr(self_type *t)
    {
      return ts::ptr_ref_cast<self_type>(t->_prev);
    }
  };

  std::string const &
  payload() const
  {
    return _payload;
  }
};

// End of documentation example code.
// If any lines above here are changed, the documentation must be updated.
// --------------------

using ThingList        = ts::IntrusiveDList<Thing::Linkage>;
using PrivateThingList = ts::IntrusiveDList<PrivateThing::Linkage>;

TEST_CASE("IntrusiveDList", "[libtscpputil][IntrusiveDList]")
{
  ThingList list;
  int n;

  REQUIRE(list.count() == 0);
  REQUIRE(list.head() == nullptr);
  REQUIRE(list.tail() == nullptr);
  REQUIRE(list.begin() == list.end());
  REQUIRE(list.empty());

  n = 0;
  for ([[maybe_unused]] auto &thing : list)
    ++n;
  REQUIRE(n == 0);
  // Check const iteration (mostly compile checks here).
  for ([[maybe_unused]] auto &thing : static_cast<ThingList const &>(list))
    ++n;
  REQUIRE(n == 0);

  list.append(new Thing("one"));
  REQUIRE(list.begin() != list.end());
  REQUIRE(list.tail() == list.head());

  list.prepend(new Thing("two"));
  REQUIRE(list.count() == 2);
  REQUIRE(list.head()->_payload == "two");
  REQUIRE(list.tail()->_payload == "one");
  list.prepend(list.take_tail());
  REQUIRE(list.head()->_payload == "one");
  REQUIRE(list.tail()->_payload == "two");
  list.insert_after(list.head(), new Thing("middle"));
  list.insert_before(list.tail(), new Thing("muddle"));
  REQUIRE(list.count() == 4);
  auto spot = list.begin();
  REQUIRE((*spot++)._payload == "one");
  REQUIRE((*spot++)._payload == "middle");
  REQUIRE((*spot++)._payload == "muddle");
  REQUIRE((*spot++)._payload == "two");
  REQUIRE(spot == list.end());

  Thing *thing = list.take_head();
  REQUIRE(thing->_payload == "one");
  REQUIRE(list.count() == 3);
  REQUIRE(list.head() != nullptr);
  REQUIRE(list.head()->_payload == "middle");

  list.prepend(thing);
  list.erase(list.head());
  REQUIRE(list.count() == 3);
  REQUIRE(list.head() != nullptr);
  REQUIRE(list.head()->_payload == "middle");
  list.prepend(thing);

  thing = list.take_tail();
  REQUIRE(thing->_payload == "two");
  REQUIRE(list.count() == 3);
  REQUIRE(list.tail() != nullptr);
  REQUIRE(list.tail()->_payload == "muddle");

  list.append(thing);
  list.erase(list.tail());
  REQUIRE(list.count() == 3);
  REQUIRE(list.tail() != nullptr);
  REQUIRE(list.tail()->_payload == "muddle");
  REQUIRE(list.head()->_payload == "one");

  list.insert_before(list.end(), new Thing("trailer"));
  REQUIRE(list.count() == 4);
  REQUIRE(list.tail()->_payload == "trailer");

  PrivateThingList priv_list;
  for (int i = 1; i <= 23; ++i) {
    std::string name;
    ts::bwprint(name, "Item {}", i);
    priv_list.append(new PrivateThing(name));
    REQUIRE(priv_list.count() == i);
  }
  REQUIRE(priv_list.head()->payload() == "Item 1");
  REQUIRE(priv_list.tail()->payload() == "Item 23");
}
