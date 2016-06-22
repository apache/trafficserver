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

#include <iostream>
#include <string.h>

#include <ts/TestBox.h>

#include "PriorityQueue.h"

using namespace std;

class N
{
public:
  N(uint32_t w, string c) : weight(w), content(c) {}
  bool
  operator<(const N &n) const
  {
    return weight < n.weight;
  }

  uint32_t weight;
  string content;
};

typedef PriorityQueueEntry<N *> Entry;
typedef PriorityQueue<N *> PQ;

// For debug
void
dump(PQ *pq)
{
  Vec<Entry *> v = pq->dump();

  for (uint32_t i = 0; i < v.length(); i++) {
    cout << v[i]->index << "," << v[i]->node->weight << "," << v[i]->node->content << endl;
  }
  cout << "--------" << endl;
}

// Push, top, and pop a entry
REGRESSION_TEST(PriorityQueue_1)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq         = new PQ();
  N *a           = new N(6, "A");
  Entry *entry_a = new Entry(a);

  pq->push(entry_a);
  box.check(pq->top() == entry_a, "top should be entry_a");

  pq->pop();
  box.check(pq->top() == NULL, "top should be NULL");
}

// Increase weight
REGRESSION_TEST(PriorityQueue_2)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq = new PQ();

  N *a = new N(10, "A");
  N *b = new N(20, "B");
  N *c = new N(30, "C");

  Entry *entry_a = new Entry(a);
  Entry *entry_b = new Entry(b);
  Entry *entry_c = new Entry(c);

  pq->push(entry_a);
  pq->push(entry_b);
  pq->push(entry_c);

  box.check(pq->top() == entry_a, "top should be entry_a");

  a->weight = 40;
  pq->update(entry_a);

  box.check(pq->top() == entry_b, "top should be entry_b");

  b->weight = 50;
  pq->update(entry_b, true);

  box.check(pq->top() == entry_c, "top should be entry_c");
}

// Decrease weight
REGRESSION_TEST(PriorityQueue_3)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq = new PQ();

  N *a = new N(10, "A");
  N *b = new N(20, "B");
  N *c = new N(30, "C");

  Entry *entry_a = new Entry(a);
  Entry *entry_b = new Entry(b);
  Entry *entry_c = new Entry(c);

  pq->push(entry_a);
  pq->push(entry_b);
  pq->push(entry_c);

  box.check(pq->top() == entry_a, "top should be entry_a");

  b->weight = 5;
  pq->update(entry_b);

  box.check(pq->top() == entry_b, "top should be entry_b");

  c->weight = 3;
  pq->update(entry_c, false);

  box.check(pq->top() == entry_c, "top should be entry_c");
}

// Push, top, and pop 9 entries
REGRESSION_TEST(PriorityQueue_4)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq = new PQ();

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

  box.check(pq->top() == entry_b, "top should be entry_b"); // 1
  pq->pop();
  box.check(pq->top() == entry_g, "top should be entry_g"); // 2
  pq->pop();
  box.check(pq->top() == entry_f, "top should be entry_f"); // 3
  pq->pop();
  box.check(pq->top() == entry_e, "top should be entry_e"); // 4
  pq->pop();
  box.check(pq->top() == entry_i, "top should be entry_i"); // 5
  pq->pop();
  box.check(pq->top() == entry_a, "top should be entry_a"); // 6
  pq->pop();
  box.check(pq->top() == entry_h, "top should be entry_h"); // 7
  pq->pop();
  box.check(pq->top() == entry_d, "top should be entry_d"); // 8
  pq->pop();
  box.check(pq->top() == entry_c, "top should be entry_c"); // 9
  pq->pop();

  box.check(pq->top() == NULL, "top should be NULL");
}

// // Push, top, pop, and update 9 entries
REGRESSION_TEST(PriorityQueue_5)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq = new PQ();

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

  // Pop head and push it back again
  box.check(pq->top() == entry_b, "top should be entry_b"); // 1
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

  // Check
  box.check(pq->top() == entry_f, "top should be entry_f"); // 3
  pq->pop();
  box.check(pq->top() == entry_i, "top should be entry_i"); // 5
  pq->pop();
  box.check(pq->top() == entry_h, "top should be entry_h"); // 7
  pq->pop();
  box.check(pq->top() == entry_d, "top should be entry_d"); // 8
  pq->pop();
  box.check(pq->top() == entry_b, "top should be entry_b"); // 101
  pq->pop();
  box.check(pq->top() == entry_g, "top should be entry_g"); // 102
  pq->pop();
  box.check(pq->top() == entry_e, "top should be entry_e"); // 104
  pq->pop();
  box.check(pq->top() == entry_a, "top should be entry_a"); // 106
  pq->pop();
  box.check(pq->top() == entry_c, "top should be entry_c"); // 109
  pq->pop();

  box.check(pq->top() == NULL, "top should be NULL");
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  const char *name = "PriorityQueue";
  RegressionTest::run(name);

  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
}
