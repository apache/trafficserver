/** @file

    Unit tests for Http2PriorityQueue

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

#include <stdlib.h>
#include <iostream>
#include <assert.h>
#include <string.h>
#include <sstream>

#include "Http2PriorityQueue.h"

using namespace std;

class N
{
public:
  N(uint32_t w, string c) : weight(w), content(c) {}

  bool operator<(const N &n) const { return weight < n.weight; }
  bool operator>(const N &n) const { return weight > n.weight; }

  uint32_t weight;
  string content;
};

typedef Http2PriorityQueueEntry<N *> Entry;
typedef Http2PriorityQueue<N *> PQ;

// For debug
void
dump(PQ *pq)
{
  Vec<Entry *> v = pq->dump();

  for (uint32_t i = 0; i < v.length(); i++) {
    std::cout << v[i]->index << "," << v[i]->node->weight << "," << v[i]->node->content << std::endl;
  }
  std::cout << "--------" << std::endl;
}

// Push, top, and pop a entry
void
test_pq_scenario_1()
{
  PQ *pq = new PQ();
  N *a = new N(6, "A");
  Entry *entry_a = new Entry(a);

  pq->push(entry_a);
  ink_assert(pq->top() == entry_a);

  pq->pop();
  ink_assert(pq->top() == NULL);
}

// Update weight
void
test_pq_scenario_2()
{
  PQ *pq = new PQ();

  N *a = new N(10, "A");
  N *b = new N(20, "B");

  Entry *entry_a = new Entry(a);
  Entry *entry_b = new Entry(b);

  pq->push(entry_a);
  pq->push(entry_b);

  ink_assert(pq->top() == entry_a);

  a->weight = 30;
  pq->update(entry_a);

  ink_assert(pq->top() == entry_b);
}

// Push, top, and pop 9 entries
void
test_pq_scenario_3()
{
  PQ *pq = new PQ();

  ink_assert(pq->empty());
  ink_assert(pq->top() == NULL);

  N *a = new N(6, "A");
  N *b = new N(1, "B");
  N *c = new N(9, "C");
  N *d = new N(8, "D");
  N *e = new N(4, "E");
  N *f = new N(3, "F");
  N *g = new N(2, "G");
  N *h = new N(7, "H");
  N *i = new N(5, "I");

  Entry *entry_a = new Entry(a);
  Entry *entry_b = new Entry(b);
  Entry *entry_c = new Entry(c);
  Entry *entry_d = new Entry(d);
  Entry *entry_e = new Entry(e);
  Entry *entry_f = new Entry(f);
  Entry *entry_g = new Entry(g);
  Entry *entry_h = new Entry(h);
  Entry *entry_i = new Entry(i);

  pq->push(entry_a);
  pq->push(entry_b);
  pq->push(entry_c);
  pq->push(entry_d);
  pq->push(entry_e);
  pq->push(entry_f);
  pq->push(entry_g);
  pq->push(entry_h);
  pq->push(entry_i);

  ink_assert(pq->top() == entry_b); // 1
  pq->pop();
  ink_assert(pq->top() == entry_g); // 2
  pq->pop();
  ink_assert(pq->top() == entry_f); // 3
  pq->pop();
  ink_assert(pq->top() == entry_e); // 4
  pq->pop();
  ink_assert(pq->top() == entry_i); // 5
  pq->pop();
  ink_assert(pq->top() == entry_a); // 6
  pq->pop();
  ink_assert(pq->top() == entry_h); // 7
  pq->pop();
  ink_assert(pq->top() == entry_d); // 8
  pq->pop();
  ink_assert(pq->top() == entry_c); // 9
  pq->pop();

  ink_assert(pq->top() == NULL);
}

// Push, top, pop, and update 9 entries
void
test_pq_scenario_4()
{
  PQ *pq = new PQ();

  ink_assert(pq->empty());
  ink_assert(pq->top() == NULL);

  N *a = new N(6, "A");
  N *b = new N(1, "B");
  N *c = new N(9, "C");
  N *d = new N(8, "D");
  N *e = new N(4, "E");
  N *f = new N(3, "F");
  N *g = new N(2, "G");
  N *h = new N(7, "H");
  N *i = new N(5, "I");

  Entry *entry_a = new Entry(a);
  Entry *entry_b = new Entry(b);
  Entry *entry_c = new Entry(c);
  Entry *entry_d = new Entry(d);
  Entry *entry_e = new Entry(e);
  Entry *entry_f = new Entry(f);
  Entry *entry_g = new Entry(g);
  Entry *entry_h = new Entry(h);
  Entry *entry_i = new Entry(i);

  pq->push(entry_a);
  pq->push(entry_b);
  pq->push(entry_c);
  pq->push(entry_d);
  pq->push(entry_e);
  pq->push(entry_f);
  pq->push(entry_g);
  pq->push(entry_h);
  pq->push(entry_i);
  dump(pq);

  // Pop head and push it back again
  ink_assert(pq->top() == entry_b); // 1
  pq->pop();
  b->weight += 100;
  pq->push(entry_b);
  // Update weight
  a->weight += 100;
  pq->update(entry_a);
  c->weight += 100;
  pq->update(entry_d);
  e->weight += 100;
  pq->update(entry_e);
  g->weight += 100;
  pq->update(entry_g);
  dump(pq);

  // Check
  ink_assert(pq->top() == entry_f); // 3
  pq->pop();
  ink_assert(pq->top() == entry_i); // 5
  pq->pop();
  ink_assert(pq->top() == entry_h); // 7
  pq->pop();
  ink_assert(pq->top() == entry_d); // 8
  pq->pop();
  ink_assert(pq->top() == entry_b); // 101
  pq->pop();
  ink_assert(pq->top() == entry_g); // 102
  pq->pop();
  ink_assert(pq->top() == entry_e); // 104
  pq->pop();
  ink_assert(pq->top() == entry_a); // 106
  pq->pop();
  ink_assert(pq->top() == entry_c); // 109
  pq->pop();

  ink_assert(pq->top() == NULL);
}

int
main()
{
  test_pq_scenario_1();
  test_pq_scenario_2();
  test_pq_scenario_3();
  test_pq_scenario_4();

  return 0;
}
