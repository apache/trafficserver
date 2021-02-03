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
#include <random>
#include <tscore/IntrusiveHashMap.h>
#include <tscore/BufferWriter.h>
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

using Map = IntrusiveHashMap<ThingMapDescriptor>;

} // namespace

TEST_CASE("IntrusiveHashMap", "[libts][IntrusiveHashMap]")
{
  Map map;
  map.insert(new Thing("bob"));
  REQUIRE(map.count() == 1);
  map.insert(new Thing("dave"));
  map.insert(new Thing("persia"));
  REQUIRE(map.count() == 3);
  // Need to be bit careful cleaning up, since the link pointers are in the objects and deleting
  // the object makes it unsafe to use an iterator referencing that object. For a full cleanup,
  // the best option is to first delete everything, then clean up the map.
  map.apply([](Thing *thing) { delete thing; });
  map.clear();
  REQUIRE(map.count() == 0);

  size_t nb = map.bucket_count();
  std::bitset<64> marks;
  for (unsigned int i = 1; i <= 63; ++i) {
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
    marks[thing._n] = true;
  }
  marks[0] = true;
  REQUIRE(marks.all());
  map.insert(new Thing("dup"sv, 79));
  map.insert(new Thing("dup"sv, 80));
  map.insert(new Thing("dup"sv, 81));

  auto r = map.equal_range("dup"sv);
  REQUIRE(r.first != r.second);
  REQUIRE(r.first->_payload == "dup"sv);
  REQUIRE(r.first->_n == 81);

  Map::iterator idx;

  // Erase all the non-"dup" and see if the range is still correct.
  map.apply([&map](Thing *thing) {
    if (thing->_payload != "dup"sv) {
      map.erase(map.iterator_for(thing));
      delete thing;
    }
  });
  r = map.equal_range("dup"sv);
  REQUIRE(r.first != r.second);
  idx = r.first;
  REQUIRE(idx->_payload == "dup"sv);
  REQUIRE(idx->_n == 81);
  REQUIRE((++idx)->_payload == "dup"sv);
  REQUIRE(idx->_n != r.first->_n);
  REQUIRE(idx->_n == 79);
  REQUIRE((++idx)->_payload == "dup"sv);
  REQUIRE(idx->_n != r.first->_n);
  REQUIRE(idx->_n == 80);
  REQUIRE(++idx == map.end());
  // Verify only the "dup" items are left.
  for (auto &&elt : map) {
    REQUIRE(elt._payload == "dup"sv);
  }
  // clean up the last bits.
  map.apply([](Thing *thing) { delete thing; });
};

// Some more involved tests.
TEST_CASE("IntrusiveHashMapManyStrings", "[IntrusiveHashMap]")
{
  std::vector<std::string> strings;

  std::uniform_int_distribution<short> char_gen{'a', 'z'};
  std::uniform_int_distribution<short> length_gen{20, 40};
  std::minstd_rand randu;
  constexpr int N = 1009;

  Map ihm;

  strings.reserve(N);
  for (int i = 0; i < N; ++i) {
    auto len = length_gen(randu);
    std::string s;
    s.reserve(len);
    for (decltype(len) j = 0; j < len; ++j) {
      s += char_gen(randu);
    }
    strings.push_back(s);
  }

  // Fill the IntrusiveHashMap
  for (int i = 0; i < N; ++i) {
    ihm.insert(new Thing{strings[i], i});
  }

  REQUIRE(ihm.count() == N);

  // Do some lookups - just require the whole loop, don't artificially inflate the test count.
  bool miss_p = false;
  for (int j = 0, idx = 17; j < N; ++j, idx = (idx + 17) % N) {
    if (auto spot = ihm.find(strings[idx]); spot == ihm.end() || spot->_n != idx) {
      miss_p = true;
    }
  }
  REQUIRE(miss_p == false);

  // Let's try some duplicates when there's a lot of data in the map.
  miss_p = false;
  for (int idx = 23; idx < N; idx += 23) {
    ihm.insert(new Thing(strings[idx], 2000 + idx));
  }
  for (int idx = 23; idx < N; idx += 23) {
    auto spot = ihm.find(strings[idx]);
    if (spot == ihm.end() || spot->_n != 2000 + idx || ++spot == ihm.end() || spot->_n != idx) {
      miss_p = true;
    }
  }
  REQUIRE(miss_p == false);

  // Do a different stepping, special cases the intersection with the previous stepping.
  miss_p = false;
  for (int idx = 31; idx < N; idx += 31) {
    ihm.insert(new Thing(strings[idx], 3000 + idx));
  }
  for (int idx = 31; idx < N; idx += 31) {
    auto spot = ihm.find(strings[idx]);
    if (spot == ihm.end() || spot->_n != 3000 + idx || ++spot == ihm.end() || (idx != (23 * 31) && spot->_n != idx) ||
        (idx == (23 * 31) && spot->_n != 2000 + idx)) {
      miss_p = true;
    }
  }
  REQUIRE(miss_p == false);

  // Check for misses.
  miss_p = false;
  for (int i = 0; i < 99; ++i) {
    char s[41];
    auto len = length_gen(randu);
    for (decltype(len) j = 0; j < len; ++j) {
      s[j] = char_gen(randu);
    }
    std::string_view name(s, len);
    auto spot = ihm.find(name);
    if (spot != ihm.end() && name != spot->_payload) {
      miss_p = true;
    }
  }
  REQUIRE(miss_p == false);

  ihm.apply([](Thing *thing) { delete thing; });
};
