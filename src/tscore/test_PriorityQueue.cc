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
#include <utility>
#include <cstring>

#include "tscore/TestBox.h"

#include "tscore/PriorityQueue.h"

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
  string content;
};

using Entry = PriorityQueueEntry<N *>;
using PQ    = PriorityQueue<N *>;

// For debug
void
dump(PQ *pq)
{
  std::vector<Entry *> v = pq->dump();

  for (auto &i : v) {
    cout << i->index << "," << i->node->weight << "," << i->node->content << endl;
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
  box.check(pq->top() == nullptr, "top should be NULL");

  delete pq;
  delete a;
  delete entry_a;
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

  delete pq;

  delete a;
  delete b;
  delete c;

  delete entry_a;
  delete entry_b;
  delete entry_c;
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

  delete pq;

  delete a;
  delete b;
  delete c;

  delete entry_a;
  delete entry_b;
  delete entry_c;
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

  box.check(pq->top() == nullptr, "top should be NULL");

  delete pq;

  delete a;
  delete b;
  delete c;
  delete d;
  delete e;
  delete f;
  delete g;
  delete h;
  delete i;

  delete entry_a;
  delete entry_b;
  delete entry_c;
  delete entry_d;
  delete entry_e;
  delete entry_f;
  delete entry_g;
  delete entry_h;
  delete entry_i;
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

  box.check(pq->top() == nullptr, "top should be NULL");

  delete pq;

  delete a;
  delete b;
  delete c;
  delete d;
  delete e;
  delete f;
  delete g;
  delete h;
  delete i;

  delete entry_a;
  delete entry_b;
  delete entry_c;
  delete entry_d;
  delete entry_e;
  delete entry_f;
  delete entry_g;
  delete entry_h;
  delete entry_i;
}

// Test erase method
REGRESSION_TEST(PriorityQueue_6)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
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

  uint32_t index;

  box.check(pq->top() == entry_a, "top should be entry_a");

  index = entry_a->index;
  pq->erase(entry_a);
  box.check(entry_a->index == index, "index should be the same");

  box.check(pq->top() == entry_b, "top should be entry_b");

  index = entry_c->index;
  pq->erase(entry_c);
  box.check(entry_c->index == index, "index should be the same");

  box.check(pq->top() == entry_b, "top should be entry_b");

  index = entry_b->index;
  pq->erase(entry_b);
  box.check(entry_b->index == index, "index should be the same");

  box.check(pq->top() == nullptr, "top should be NULL");
  box.check(pq->empty(), "should be empty");

  delete pq;

  delete a;
  delete b;
  delete c;

  delete entry_a;
  delete entry_b;
  delete entry_c;

  PQ *pq2 = new PQ();

  N *w = new N(10, "W");
  N *x = new N(20, "X");
  N *y = new N(30, "Y");
  N *z = new N(40, "Z");

  Entry *entry_w = new Entry(w);
  Entry *entry_x = new Entry(x);
  Entry *entry_y = new Entry(y);
  Entry *entry_z = new Entry(z);

  pq2->push(entry_z);
  pq2->push(entry_y);
  pq2->push(entry_x);
  pq2->push(entry_w);

  box.check(pq2->top() == entry_w, "top should be entry_w 1");
  pq2->erase(entry_x);
  box.check(pq2->top() == entry_w, "top should be entry_w 2");
  // The following two cases should test that erase preserves the index
  pq2->erase(entry_y);
  box.check(pq2->top() == entry_w, "top should be entry_w 3");
  pq2->erase(entry_z);
  box.check(pq2->top() == entry_w, "top should be entry_w 4");

  delete pq2;

  delete w;
  delete x;
  delete y;
  delete z;

  delete entry_w;
  delete entry_x;
  delete entry_y;
  delete entry_z;
}

// Test erase and pop method to ensure the index entries are updated (TS-4915)
REGRESSION_TEST(PriorityQueue_7)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq2 = new PQ();

  N *x = new N(20, "X");
  N *y = new N(30, "Y");
  N *z = new N(40, "Z");

  Entry *entry_x = new Entry(x);
  Entry *entry_y = new Entry(y);
  Entry *entry_z = new Entry(z);

  pq2->push(entry_z);
  pq2->push(entry_y);
  pq2->push(entry_x);

  box.check(pq2->top() == entry_x, "top should be entry_x");
  pq2->pop();
  box.check(pq2->top() == entry_y, "top should be entry_y");
  pq2->erase(entry_y);
  box.check(pq2->top() == entry_z, "top should be entry_z");

  delete pq2;

  delete x;
  delete y;
  delete z;

  delete entry_x;
  delete entry_y;
  delete entry_z;
}

// Test erase and pop method to ensure the index entries are correctly
REGRESSION_TEST(PriorityQueue_pop_and_erase)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq1 = new PQ();
  PQ *pq2 = new PQ();

  N *x = new N(20, "X");
  N *y = new N(30, "Y");
  N *z = new N(40, "Z");

  Entry *entry_x = new Entry(x);
  Entry *entry_y = new Entry(y);
  Entry *entry_z = new Entry(z);

  pq2->push(entry_z);
  pq2->push(entry_y);
  pq2->push(entry_x);

  x->weight = 40;
  y->weight = 30;
  z->weight = 20;

  pq1->push(pq2->top());
  pq2->pop();
  box.check(pq1->top()->index == 0, "Top index should be zero, but got %d", pq1->top()->index);

  pq1->push(pq2->top());
  pq2->pop();
  box.check(pq1->top()->index == 0, "Top index should be zero, but got %d", pq1->top()->index);

  pq1->push(pq2->top());
  pq2->pop();
  box.check(pq1->top()->index == 0, "Top index should be zero, but got %d", pq1->top()->index);

  delete pq1;
  delete pq2;

  delete x;
  delete y;
  delete z;

  delete entry_x;
  delete entry_y;
  delete entry_z;
}

REGRESSION_TEST(PriorityQueue_pop_and_erase_2)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  PQ *pq1 = new PQ();

  N *x = new N(20, "X");
  N *y = new N(30, "Y");

  Entry *X = new Entry(x);
  Entry *Y = new Entry(y);

  box.check(X->index == 0 && Y->index == 0, "X and Y index should be 0");

  pq1->push(X);

  pq1->erase(Y);

  box.check(pq1->top() == X, "X should be in queue");

  delete x;
  delete y;

  delete X;
  delete Y;

  delete pq1;
}
