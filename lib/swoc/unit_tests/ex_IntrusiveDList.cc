/** @file

    IntrusiveDList documentation examples.

    This code is run during unit tests to verify that it compiles and runs correctly, but the primary
    purpose of the code is for documentation, not testing per se. This means editing the file is
    almost certain to require updating documentation references to code in this file.

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

#include "swoc/IntrusiveDList.h"
#include "swoc/bwf_base.h"

#include "catch.hpp"

using swoc::IntrusiveDList;

class Message {
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
Message::Linkage::next_ptr(self_type *that) -> self_type *& {
  return that->_link._next;
}
auto
Message::Linkage::prev_ptr(self_type *that) -> self_type *& {
  return that->_link._prev;
}

bool
Message::is_in_list() const {
  return _link._next || _link._prev;
}

class Container {
  using self_type   = Container;
  using MessageList = IntrusiveDList<Message::Linkage>;

public:
  ~Container();

  template <typename... Args> self_type &debug(std::string_view fmt, Args &&...args);

  size_t count() const;
  self_type &clear();
  Message::Severity max_severity() const;
  void print() const;

protected:
  MessageList _msgs;
};

Container::~Container() {
  this->clear(); // clean up memory.
}

auto
Container::clear() -> self_type & {
  Message *msg;
  while (nullptr != (msg = _msgs.take_head())) {
    delete msg;
  }
  _msgs.clear();
  return *this;
}

size_t
Container::count() const {
  return _msgs.count();
}

template <typename... Args>
auto
Container::debug(std::string_view fmt, Args &&...args) -> self_type & {
  Message *msg = new Message;
  swoc::bwprint_v(msg->_text, fmt, std::forward_as_tuple(args...));
  msg->_severity = Message::LVL_DEBUG;
  _msgs.append(msg);
  return *this;
}

Message::Severity
Container::max_severity() const {
  auto spot = std::max_element(_msgs.begin(), _msgs.end(),
                               [](Message const &lhs, Message const &rhs) { return lhs._severity < rhs._severity; });
  return spot == _msgs.end() ? Message::Severity::LVL_DEBUG : spot->_severity;
}

void
Container::print() const {
  for (auto &&elt : _msgs) {
    std::cout << static_cast<unsigned int>(elt._severity) << ": " << elt._text << std::endl;
  }
}

TEST_CASE("IntrusiveDList Example", "[libswoc][IntrusiveDList]") {
  Container container;

  container.debug("This is message {}", 1);
  REQUIRE(container.count() == 1);
  // Destructor is checked for non-crashing as container goes out of scope.
}

struct Thing {
  std::string _payload;
  Thing *_next{nullptr};
  Thing *_prev{nullptr};
  using Linkage = swoc::IntrusiveLinkage<Thing, &Thing::_next, &Thing::_prev>;

  Thing(std::string_view text) : _payload(text) {}
};

// Just for you, @maskit ! Demonstrating non-public links and subclassing.
class PrivateThing : protected Thing {
  using self_type  = PrivateThing;
  using super_type = Thing;

public:
  PrivateThing(std::string_view text) : super_type(text) {}

  struct Linkage {
    static self_type *&
    next_ptr(self_type *t) {
      return swoc::ptr_ref_cast<self_type>(t->_next);
    }
    static self_type *&
    prev_ptr(self_type *t) {
      return swoc::ptr_ref_cast<self_type>(t->_prev);
    }
  };

  std::string const &
  payload() const {
    return _payload;
  }
};

class PrivateThing2 : protected Thing {
  using self_type  = PrivateThing2;
  using super_type = Thing;

public:
  PrivateThing2(std::string_view text) : super_type(text) {}
  using Linkage = swoc::IntrusiveLinkageRebind<self_type, super_type::Linkage>;
  friend Linkage;

  std::string const &
  payload() const {
    return _payload;
  }
};

TEST_CASE("IntrusiveDList Inheritance", "[libswoc][IntrusiveDList][example]") {
  IntrusiveDList<PrivateThing::Linkage> priv_list;
  for (size_t i = 1; i <= 23; ++i) {
    swoc::LocalBufferWriter<16> w;
    w.print("Item {}", i);
    priv_list.append(new PrivateThing(w.view()));
    REQUIRE(priv_list.count() == i);
  }
  REQUIRE(priv_list.head()->payload() == "Item 1");
  REQUIRE(priv_list.tail()->payload() == "Item 23");

  IntrusiveDList<PrivateThing2::Linkage> priv2_list;
  for (size_t i = 1; i <= 23; ++i) {
    swoc::LocalBufferWriter<16> w;
    w.print("Item {}", i);
    priv2_list.append(new PrivateThing2(w.view()));
    REQUIRE(priv2_list.count() == i);
  }
  REQUIRE(priv2_list.head()->payload() == "Item 1");
  REQUIRE(priv2_list.tail()->payload() == "Item 23");
}
