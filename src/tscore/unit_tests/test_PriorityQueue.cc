/** @file

    Unit tests for PriorityQueue

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

#include <cstdint>
#include <memory>

#include "tscore/PriorityQueue.h"
#include "catch.hpp"

using namespace std;

class N
{
public:
  N(uint32_t w, string c) : weight(w), content(std::move(c)) {}
  bool
  operator<(const N &n) const
  {
    return weight < n.weight;
  }

  uint32_t weight;
  string   content;
};

using Entry = PriorityQueueEntry<N *>;
using PQ    = PriorityQueue<N *>;

// Push, top, and pop a entry
TEST_CASE("PriorityQueue1", "[libts][PriorityQueue]")
{
  auto pq      = std::make_unique<PQ>();
  auto a       = std::make_unique<N>(6, "A");
  auto entry_a = std::make_unique<Entry>(a.get());

  pq->push(entry_a.get());
  REQUIRE(pq->top() == entry_a.get());

  pq->pop();
  REQUIRE(pq->top() == nullptr);
}

// Increase weight
TEST_CASE("PriorityQueue2", "[libts][PriorityQueue]")
{
  auto pq = std::make_unique<PQ>();

  auto a = std::make_unique<N>(10, "A");
  auto b = std::make_unique<N>(20, "B");
  auto c = std::make_unique<N>(30, "C");

  auto entry_a = std::make_unique<Entry>(a.get());
  auto entry_b = std::make_unique<Entry>(b.get());
  auto entry_c = std::make_unique<Entry>(c.get());

  pq->push(entry_a.get());
  pq->push(entry_b.get());
  pq->push(entry_c.get());

  REQUIRE(pq->top() == entry_a.get());

  a->weight = 40;
  pq->update(entry_a.get());

  REQUIRE(pq->top() == entry_b.get());

  b->weight = 50;
  pq->update(entry_b.get(), true);

  REQUIRE(pq->top() == entry_c.get());
}

// Decrease weight
TEST_CASE("PriorityQueue3", "[libts][PriorityQueue]")
{
  auto pq = std::make_unique<PQ>();

  auto a = std::make_unique<N>(10, "A");
  auto b = std::make_unique<N>(20, "B");
  auto c = std::make_unique<N>(30, "C");

  auto entry_a = std::make_unique<Entry>(a.get());
  auto entry_b = std::make_unique<Entry>(b.get());
  auto entry_c = std::make_unique<Entry>(c.get());

  pq->push(entry_a.get());
  pq->push(entry_b.get());
  pq->push(entry_c.get());

  REQUIRE(pq->top() == entry_a.get());

  b->weight = 5;
  pq->update(entry_b.get());

  REQUIRE(pq->top() == entry_b.get());

  c->weight = 3;
  pq->update(entry_c.get(), false);

  REQUIRE(pq->top() == entry_c.get());
}

// Push, top, and pop 9 entries
TEST_CASE("PriorityQueue4", "[libts][PriorityQueue]")
{
  auto pq = std::make_unique<PQ>();

  auto a = std::make_unique<N>(6, "A");
  auto b = std::make_unique<N>(1, "B");
  auto c = std::make_unique<N>(9, "C");
  auto d = std::make_unique<N>(8, "D");
  auto e = std::make_unique<N>(4, "E");
  auto f = std::make_unique<N>(3, "F");
  auto g = std::make_unique<N>(2, "G");
  auto h = std::make_unique<N>(7, "H");
  auto i = std::make_unique<N>(5, "I");

  auto entry_a = std::make_unique<Entry>(a.get());
  auto entry_b = std::make_unique<Entry>(b.get());
  auto entry_c = std::make_unique<Entry>(c.get());
  auto entry_d = std::make_unique<Entry>(d.get());
  auto entry_e = std::make_unique<Entry>(e.get());
  auto entry_f = std::make_unique<Entry>(f.get());
  auto entry_g = std::make_unique<Entry>(g.get());
  auto entry_h = std::make_unique<Entry>(h.get());
  auto entry_i = std::make_unique<Entry>(i.get());

  pq->push(entry_a.get());
  pq->push(entry_b.get());
  pq->push(entry_c.get());
  pq->push(entry_d.get());
  pq->push(entry_e.get());
  pq->push(entry_f.get());
  pq->push(entry_g.get());
  pq->push(entry_h.get());
  pq->push(entry_i.get());

  REQUIRE(pq->top() == entry_b.get()); // 1
  pq->pop();
  REQUIRE(pq->top() == entry_g.get()); // 2
  pq->pop();
  REQUIRE(pq->top() == entry_f.get()); // 3
  pq->pop();
  REQUIRE(pq->top() == entry_e.get()); // 4
  pq->pop();
  REQUIRE(pq->top() == entry_i.get()); // 5
  pq->pop();
  REQUIRE(pq->top() == entry_a.get()); // 6
  pq->pop();
  REQUIRE(pq->top() == entry_h.get()); // 7
  pq->pop();
  REQUIRE(pq->top() == entry_d.get()); // 8
  pq->pop();
  REQUIRE(pq->top() == entry_c.get()); // 9
  pq->pop();

  REQUIRE(pq->top() == nullptr);
}

// Push, top, pop, and update 9 entries
TEST_CASE("PriorityQueue5", "[libts][PriorityQueue]")
{
  auto pq = std::make_unique<PQ>();

  auto a = std::make_unique<N>(6, "A");
  auto b = std::make_unique<N>(1, "B");
  auto c = std::make_unique<N>(9, "C");
  auto d = std::make_unique<N>(8, "D");
  auto e = std::make_unique<N>(4, "E");
  auto f = std::make_unique<N>(3, "F");
  auto g = std::make_unique<N>(2, "G");
  auto h = std::make_unique<N>(7, "H");
  auto i = std::make_unique<N>(5, "I");

  auto entry_a = std::make_unique<Entry>(a.get());
  auto entry_b = std::make_unique<Entry>(b.get());
  auto entry_c = std::make_unique<Entry>(c.get());
  auto entry_d = std::make_unique<Entry>(d.get());
  auto entry_e = std::make_unique<Entry>(e.get());
  auto entry_f = std::make_unique<Entry>(f.get());
  auto entry_g = std::make_unique<Entry>(g.get());
  auto entry_h = std::make_unique<Entry>(h.get());
  auto entry_i = std::make_unique<Entry>(i.get());

  pq->push(entry_a.get());
  pq->push(entry_b.get());
  pq->push(entry_c.get());
  pq->push(entry_d.get());
  pq->push(entry_e.get());
  pq->push(entry_f.get());
  pq->push(entry_g.get());
  pq->push(entry_h.get());
  pq->push(entry_i.get());

  // Pop head and push it back again
  REQUIRE(pq->top() == entry_b.get()); // 1
  pq->pop();
  b->weight += 100;
  pq->push(entry_b.get());
  // Update weight
  a->weight += 100;
  pq->update(entry_a.get());
  c->weight += 100;
  pq->update(entry_d.get());
  e->weight += 100;
  pq->update(entry_e.get());
  g->weight += 100;
  pq->update(entry_g.get());

  // Check
  REQUIRE(pq->top() == entry_f.get()); // 3
  pq->pop();
  REQUIRE(pq->top() == entry_i.get()); // 5
  pq->pop();
  REQUIRE(pq->top() == entry_h.get()); // 7
  pq->pop();
  REQUIRE(pq->top() == entry_d.get()); // 8
  pq->pop();
  REQUIRE(pq->top() == entry_b.get()); // 101
  pq->pop();
  REQUIRE(pq->top() == entry_g.get()); // 102
  pq->pop();
  REQUIRE(pq->top() == entry_e.get()); // 104
  pq->pop();
  REQUIRE(pq->top() == entry_a.get()); // 106
  pq->pop();
  REQUIRE(pq->top() == entry_c.get()); // 109
  pq->pop();

  REQUIRE(pq->top() == nullptr);
}

// Test erase method
TEST_CASE("PriorityQueue6", "[libts][PriorityQueue]")
{
  auto pq = std::make_unique<PQ>();

  auto a = std::make_unique<N>(10, "A");
  auto b = std::make_unique<N>(20, "B");
  auto c = std::make_unique<N>(30, "C");

  auto entry_a = std::make_unique<Entry>(a.get());
  auto entry_b = std::make_unique<Entry>(b.get());
  auto entry_c = std::make_unique<Entry>(c.get());

  pq->push(entry_a.get());
  pq->push(entry_b.get());
  pq->push(entry_c.get());

  uint32_t index;

  REQUIRE(pq->top() == entry_a.get());

  index = entry_a->index;
  pq->erase(entry_a.get());
  REQUIRE(entry_a->index == index);

  REQUIRE(pq->top() == entry_b.get());

  index = entry_c->index;
  pq->erase(entry_c.get());
  REQUIRE(entry_c->index == index);

  REQUIRE(pq->top() == entry_b.get());

  index = entry_b->index;
  pq->erase(entry_b.get());
  REQUIRE(entry_b->index == index);

  REQUIRE(pq->top() == nullptr);
  REQUIRE(pq->empty());

  auto pq2 = std::make_unique<PQ>();

  auto w = std::make_unique<N>(10, "W");
  auto x = std::make_unique<N>(20, "X");
  auto y = std::make_unique<N>(30, "Y");
  auto z = std::make_unique<N>(40, "Z");

  auto entry_w = std::make_unique<Entry>(w.get());
  auto entry_x = std::make_unique<Entry>(x.get());
  auto entry_y = std::make_unique<Entry>(y.get());
  auto entry_z = std::make_unique<Entry>(z.get());

  pq2->push(entry_z.get());
  pq2->push(entry_y.get());
  pq2->push(entry_x.get());
  pq2->push(entry_w.get());

  REQUIRE(pq2->top() == entry_w.get());
  pq2->erase(entry_x.get());
  REQUIRE(pq2->top() == entry_w.get());
  // The following two cases should test that erase preserves the index
  pq2->erase(entry_y.get());
  REQUIRE(pq2->top() == entry_w.get());
  pq2->erase(entry_z.get());
  REQUIRE(pq2->top() == entry_w.get());
}

// Test erase and pop method to ensure the index entries are updated (TS-4915)
TEST_CASE("PriorityQueue7", "[libts][PriorityQueue]")
{
  auto pq2 = std::make_unique<PQ>();

  auto x = std::make_unique<N>(20, "X");
  auto y = std::make_unique<N>(30, "Y");
  auto z = std::make_unique<N>(40, "Z");

  auto entry_x = std::make_unique<Entry>(x.get());
  auto entry_y = std::make_unique<Entry>(y.get());
  auto entry_z = std::make_unique<Entry>(z.get());

  pq2->push(entry_z.get());
  pq2->push(entry_y.get());
  pq2->push(entry_x.get());

  REQUIRE(pq2->top() == entry_x.get());
  pq2->pop();
  REQUIRE(pq2->top() == entry_y.get());
  pq2->erase(entry_y.get());
  REQUIRE(pq2->top() == entry_z.get());
}

// Test erase and pop method to ensure the index entries are correctly
TEST_CASE("PriorityQueue8", "[libts][PriorityQueue]")
{
  auto pq1 = std::make_unique<PQ>();
  auto pq2 = std::make_unique<PQ>();

  auto x = std::make_unique<N>(20, "X");
  auto y = std::make_unique<N>(30, "Y");
  auto z = std::make_unique<N>(40, "Z");

  auto entry_x = std::make_unique<Entry>(x.get());
  auto entry_y = std::make_unique<Entry>(y.get());
  auto entry_z = std::make_unique<Entry>(z.get());

  pq2->push(entry_z.get());
  pq2->push(entry_y.get());
  pq2->push(entry_x.get());

  x->weight = 40;
  y->weight = 30;
  z->weight = 20;

  pq1->push(pq2->top());
  pq2->pop();
  REQUIRE(pq1->top()->index == 0);

  pq1->push(pq2->top());
  pq2->pop();
  REQUIRE(pq1->top()->index == 0);

  pq1->push(pq2->top());
  pq2->pop();
  REQUIRE(pq1->top()->index == 0);
}

TEST_CASE("PriorityQueue9", "[libts][PriorityQueue]")
{
  auto pq1 = std::make_unique<PQ>();

  auto x = std::make_unique<N>(20, "X");
  auto y = std::make_unique<N>(30, "Y");

  auto X = std::make_unique<Entry>(x.get());
  auto Y = std::make_unique<Entry>(y.get());

  REQUIRE(X->index == 0);
  REQUIRE(Y->index == 0);

  pq1->push(X.get());

  pq1->erase(Y.get());

  REQUIRE(pq1->top() == X.get());
}
