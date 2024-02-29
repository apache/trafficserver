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

#include "swoc/IntrusiveDList.h"
#include "swoc/bwf_base.h"

#include "catch.hpp"

using swoc::IntrusiveDList;
using swoc::bwprint;

namespace {
struct Thing {
  std::string _payload;
  Thing *_next{nullptr};
  Thing *_prev{nullptr};

  Thing(std::string_view text) : _payload(text) {}

  struct Linkage {
    static Thing *&
    next_ptr(Thing *t) {
      return t->_next;
    }

    static Thing *&
    prev_ptr(Thing *t) {
      return t->_prev;
    }
  };
};

using ThingList = IntrusiveDList<Thing::Linkage>;

} // namespace

TEST_CASE("IntrusiveDList", "[libswoc][IntrusiveDList]") {
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
  spot = list.begin(); // verify assignment works.

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
}

TEST_CASE("IntrusiveDList list prefix", "[libswoc][IntrusiveDList]") {
  ThingList list;

  std::string tmp;
  for (unsigned idx = 1; idx <= 20; ++idx) {
    list.append(new Thing(bwprint(tmp, "{}", idx)));
  }

  auto x = list.nth(0);
  REQUIRE(x->_payload == "1");
  x = list.nth(19);
  REQUIRE(x->_payload == "20");

  auto list_none = list.take_prefix(0);
  REQUIRE(list_none.count() == 0);
  REQUIRE(list_none.head() == nullptr);
  REQUIRE(list.count() == 20);

  auto v      = list.head();
  auto list_1 = list.take_prefix(1);
  REQUIRE(list_1.count() == 1);
  REQUIRE(list_1.head() == v);
  REQUIRE(list.count() == 19);

  v           = list.head();
  auto list_5 = list.take_prefix(5);
  REQUIRE(list_5.count() == 5);
  REQUIRE(list_5.head() == v);
  REQUIRE(list.count() == 14);
  REQUIRE(list.head() != nullptr);
  REQUIRE(list.head()->_payload == "7");

  v              = list.head();
  auto list_most = list.take_prefix(9); // more than half.
  REQUIRE(list_most.count() == 9);
  REQUIRE(list_most.head() == v);
  REQUIRE(list.count() == 5);
  REQUIRE(list.head() != nullptr);

  v              = list.head();
  auto list_rest = list.take_prefix(20);
  REQUIRE(list_rest.count() == 5);
  REQUIRE(list_rest.head() == v);
  REQUIRE(list_rest.head()->_payload == "16");
  REQUIRE(list.count() == 0);
  REQUIRE(list.head() == nullptr);
}

TEST_CASE("IntrusiveDList list suffix", "[libswoc][IntrusiveDList]") {
  ThingList list;

  std::string tmp;
  for (unsigned idx = 1; idx <= 20; ++idx) {
    list.append(new Thing(bwprint(tmp, "{}", idx)));
  }

  auto list_none = list.take_suffix(0);
  REQUIRE(list_none.count() == 0);
  REQUIRE(list_none.head() == nullptr);
  REQUIRE(list.count() == 20);

  auto *v     = list.tail();
  auto list_1 = list.take_suffix(1);
  REQUIRE(list_1.count() == 1);
  REQUIRE(list_1.tail() == v);
  REQUIRE(list.count() == 19);

  v           = list.tail();
  auto list_5 = list.take_suffix(5);
  REQUIRE(list_5.count() == 5);
  REQUIRE(list_5.tail() == v);
  REQUIRE(list.count() == 14);
  REQUIRE(list.head() != nullptr);
  REQUIRE(list.tail()->_payload == "14");

  v              = list.tail();
  auto list_most = list.take_suffix(9); // more than half.
  REQUIRE(list_most.count() == 9);
  REQUIRE(list_most.tail() == v);
  REQUIRE(list.count() == 5);
  REQUIRE(list.tail() != nullptr);

  v              = list.head();
  auto list_rest = list.take_suffix(20);
  REQUIRE(list_rest.count() == 5);
  REQUIRE(list_rest.head() == v);
  REQUIRE(list_rest.head()->_payload == "1");
  REQUIRE(list_rest.tail()->_payload == "5");
  REQUIRE(list.count() == 0);
  REQUIRE(list.tail() == nullptr);

  // reassemble the list.
  list.append(list_most);  // middle 6..14
  list_1.prepend(list_5);  // -> last 15..20
  list.prepend(list_rest); // initial, 1..5 -> 1..14
  list.append(list_1);

  REQUIRE(list.count() == 20);
  REQUIRE(list.head()->_payload == "1");
  REQUIRE(list.tail()->_payload == "20");
  REQUIRE(list.nth(7)->_payload == "8");
  REQUIRE(list.nth(17)->_payload == "18");
}

TEST_CASE("IntrusiveDList Extra", "[libswoc][IntrusiveDList]") {
  struct S {
    std::string name;
    swoc::IntrusiveLinks<S> _links;
  };

  using S_List = swoc::IntrusiveDList<swoc::IntrusiveLinkDescriptor<S, &S::_links>>;
  [[maybe_unused]] S_List s_list;

  ThingList list, list_b, list_a;

  std::string tmp;
  list.append(new Thing(bwprint(tmp, "{}", 0)));
  list.append(new Thing(bwprint(tmp, "{}", 1)));
  list.append(new Thing(bwprint(tmp, "{}", 2)));
  list.append(new Thing(bwprint(tmp, "{}", 6)));
  list.append(new Thing(bwprint(tmp, "{}", 11)));
  list.append(new Thing(bwprint(tmp, "{}", 12)));

  for (unsigned idx = 3; idx <= 5; ++idx) {
    list_b.append(new Thing(bwprint(tmp, "{}", idx)));
  }
  for (unsigned idx = 7; idx <= 10; ++idx) {
    list_a.append(new Thing(bwprint(tmp, "{}", idx)));
  }

  auto v = list.nth(3);
  REQUIRE(v->_payload == "6");

  list.insert_before(v, list_b);
  list.insert_after(v, list_a);

  auto spot = list.begin();
  for (unsigned idx = 0; idx <= 12; ++idx, ++spot) {
    bwprint(tmp, "{}", idx);
    REQUIRE(spot->_payload == tmp);
  }
}
