/** @file

    IntrusiveHashMap unit tests.

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

#include <iostream>
#include <string_view>
#include <string>
#include <bitset>
#include <ts/IntrusiveHashMap.h>
#include <ts/BufferWriter.h>
#include <catch.hpp>
#include "../../../tests/include/catch.hpp"

// -------------
// --- TESTS ---
// -------------

using namespace std::literals;

namespace
{
struct Thing {
  std::string _payload;
  int _n{0};

  Thing(std::string_view text) : _payload(text) {}
  Thing(std::string_view text, int x) : _payload(text), _n(x) {}

  Thing *_next{nullptr};
  Thing *_prev{nullptr};
};

struct ThingMapDescriptor {
  static Thing *&
  next_ptr(Thing *thing)
  {
    return thing->_next;
  }
  static Thing *&
  prev_ptr(Thing *thing)
  {
    return thing->_prev;
  }
  static std::string_view
  key_of(Thing *thing)
  {
    return thing->_payload;
  }
  static constexpr std::hash<std::string_view> hasher{};
  static auto
  hash_of(std::string_view s) -> decltype(hasher(s))
  {
    return hasher(s);
  }
  static bool
  equal(std::string_view const &lhs, std::string_view const &rhs)
  {
    return lhs == rhs;
  }
};

using Map = ts::IntrusiveHashMap<ThingMapDescriptor>;

} // namespace

TEST_CASE("IntrusiveHashMap", "[libts][IntrusiveHashMap]")
{
  Map map;
  map.insert(new Thing("bob"));
  REQUIRE(map.count() == 1);
  map.insert(new Thing("dave"));
  map.insert(new Thing("persia"));
  REQUIRE(map.count() == 3);
  for (auto &thing : map) {
    delete &thing;
  }
  map.clear();
  REQUIRE(map.count() == 0);

  size_t nb = map.bucket_count();
  std::bitset<64> marks;
  for (int i = 1; i <= 63; ++i) {
    std::string name;
    ts::bwprint(name, "{} squared is {}", i, i * i);
    Thing *thing = new Thing(name);
    thing->_n    = i;
    map.insert(thing);
    REQUIRE(map.count() == i);
    REQUIRE(map.find(name) != map.end());
  }
  REQUIRE(map.count() == 63);
  REQUIRE(map.bucket_count() > nb);
  for (auto &thing : map) {
    REQUIRE(0 == marks[thing._n]);
    marks[thing._n] = 1;
  }
  marks[0] = 1;
  REQUIRE(marks.all());
  map.insert(new Thing("dup"sv, 79));
  map.insert(new Thing("dup"sv, 80));
  map.insert(new Thing("dup"sv, 81));

  auto r = map.equal_range("dup"sv);
  REQUIRE(r.first != r.last);
  REQUIRE(r.first->_payload == "dup"sv);

  Map::iterator idx;

  // Erase all the non-"dup" and see if the range is still correct.
  map.apply([&map](Thing &thing) {
    if (thing._payload != "dup"sv)
      map.erase(map.iterator_for(&thing));
  });
  r = map.equal_range("dup"sv);
  REQUIRE(r.first != r.last);
  idx = r.first;
  REQUIRE(idx->_payload == "dup"sv);
  REQUIRE((++idx)->_payload == "dup"sv);
  REQUIRE(idx->_n != r.first->_n);
  REQUIRE((++idx)->_payload == "dup"sv);
  REQUIRE(idx->_n != r.first->_n);
  REQUIRE(++idx == map.end());
  // Verify only the "dup" items are left.
  for (auto &&elt : map) {
    REQUIRE(elt._payload == "dup"sv);
  }
  // clean up the last bits.
  map.apply([](Thing &thing) { delete &thing; });
};
