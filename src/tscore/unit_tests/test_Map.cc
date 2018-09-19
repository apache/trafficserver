/** @file

  Test code for the Map templates.

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

#include "catch.hpp"

#include <cstdint>
#include "tscore/Map.h"
#include <list>

using cchar = const char;

struct Item {
  LINK(Item, m_link);
  struct Hash {
    using ID       = uint32_t;
    using Key      = uint32_t;
    using Value    = Item;
    using ListHead = DLL<Item, Item::Link_m_link>;

    static ID
    hash(Key key)
    {
      return key;
    }
    static Key key(Value *);
    static bool equal(Key lhs, Key rhs);
  };

  uint32_t _key;
  uint32_t _value;

  Item(uint32_t x) : _key(x), _value(x) {}
  Item(uint32_t key, uint32_t value) : _key(key), _value(value) {}
};

uint32_t
Item::Hash::key(Value *v)
{
  return v->_key;
}
bool
Item::Hash::equal(Key lhs, Key rhs)
{
  return lhs == rhs;
}

using Table = TSHashTable<Item::Hash>;

class testHashMap
{
private:
  HashMap<cchar *, StringHashFns, int> testsh;

public:
  int
  get(cchar *ch) const
  {
    return testsh.get(ch);
  }

  void
  put(cchar *key, int v)
  {
    testsh.put(key, v);
  }
};

TEST_CASE("test Map", "[libts][Map]")
{
  typedef Map<cchar *, cchar *> SSMap;
  typedef MapElem<cchar *, cchar *> SSMapElem;
  testHashMap testsh;
#define form_SSMap(_p, _v) form_Map(SSMapElem, _p, _v)
  SSMap ssm;
  ssm.put("a", "A");
  ssm.put("b", "B");
  ssm.put("c", "C");
  ssm.put("d", "D");
  form_SSMap(x, ssm)
  { /* nop */
  }

  /*
    if ((ssm).n)
      for (SSMapElem *qq__x = (SSMapElem*)0, *x = &(ssm).v[0];
               ((intptr_t)(qq__x) < (ssm).n) && ((x = &(ssm).v[(intptr_t)qq__x]) || 1);
               qq__x = (SSMapElem*)(((intptr_t)qq__x) + 1))
            if ((x)->key) {
              // nop
            }
            */

  cchar *hi = "hi", *ho = "ho", *hum = "hum", *hhi = "hhi";

  ++hhi;
  HashMap<cchar *, StringHashFns, int> sh;
  sh.put(hi, 1);
  sh.put(ho, 2);
  sh.put(hum, 3);
  sh.put(hhi, 4);
  REQUIRE(sh.get(hi) == 4);
  REQUIRE(sh.get(ho) == 2);
  REQUIRE(sh.get(hum) == 3);
  sh.put("aa", 5);
  sh.put("ab", 6);
  sh.put("ac", 7);
  sh.put("ad", 8);
  sh.put("ae", 9);
  sh.put("af", 10);
  REQUIRE(sh.get(hi) == 4);
  REQUIRE(sh.get(ho) == 2);
  REQUIRE(sh.get(hum) == 3);
  REQUIRE(sh.get("af") == 10);
  REQUIRE(sh.get("ac") == 7);

  HashMap<cchar *, StringHashFns, int> sh2(-99); // return -99 if key not found
  sh2.put("aa", 15);
  sh2.put("ab", 16);
  testsh.put("aa", 15);
  testsh.put("ab", 16);
  REQUIRE(sh2.get("aa") == 15);
  REQUIRE(sh2.get("ac") == -99);
  REQUIRE(testsh.get("aa") == 15);

  // test_TSHashTable
  static uint32_t const N = 270;
  Table t;
  Item *item = nullptr;
  Table::Location loc;
  std::list<Item *> to_delete;

  for (uint32_t i = 1; i <= N; ++i) {
    item = new Item(i);
    t.insert(item);
    to_delete.push_back(item);
  }

  for (uint32_t i = 1; i <= N; ++i) {
    Table::Location l = t.find(i);
    REQUIRE(l.isValid());
    REQUIRE(i == l->_value);
  }

  REQUIRE(!(t.find(N * 2).isValid()));

  loc = t.find(N / 2 | 1);
  if (loc) {
    t.remove(loc);
  } else {
    REQUIRE(!"Did not find expected value");
  }

  if (!loc) {
    ; // compiler check.
  }

  REQUIRE(!(t.find(N / 2 | 1).isValid()));

  for (uint32_t i = 1; i <= N; i += 2) {
    t.remove(i);
  }

  for (uint32_t i = 1; i <= N; ++i) {
    Table::Location l = t.find(i);
    if (1 & i) {
      REQUIRE(!l.isValid());
    } else {
      REQUIRE(l.isValid());
    }
  }

  int n = 0;
  for (Table::iterator spot = t.begin(), limit = t.end(); spot != limit; ++spot) {
    ++n;
    REQUIRE((spot->_value & 1) == 0);
  }
  REQUIRE(n == N / 2);

  for (auto it : to_delete) {
    delete it;
  }
}
