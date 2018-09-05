/** @file

    Unit tests for Http2DependencyTree

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
#include <cstring>
#include <sstream>

#include "tscore/TestBox.h"

#include "Http2DependencyTree.h"

using namespace std;

using Tree = Http2DependencyTree::Tree<std::string *>;
using Node = Http2DependencyTree::Node;

/**
 * Exclusive Dependency Creation
 *
 *       A            A
 *      / \    =>     |
 *     B   C          D
 *                   / \
 *                  B   C
 */
REGRESSION_TEST(Http2DependencyTree_1)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D");

  tree->add(0, 1, 0, false, &b);
  tree->add(0, 3, 0, false, &c);

  Node *node_a = tree->find(0);
  Node *node_b = tree->find(1);
  Node *node_c = tree->find(3);

  box.check(node_b->parent == node_a, "parent of B should be A");
  box.check(node_c->parent == node_a, "parent of C should be A");

  // Add node with exclusive flag
  tree->add(0, 5, 0, true, &d);

  Node *node_d = tree->find(5);

  box.check(node_d->parent == node_a, "parent of D should be A");
  box.check(node_b->parent == node_d, "parent of B should be D");
  box.check(node_c->parent == node_d, "parent of C should be D");

  delete tree;
}

/**
 * Reprioritization (non-exclusive)
 *
 *    x                x
 *    |                |
 *    A                D
 *   / \              / \
 *  B   C     ==>    F   A
 *     / \              / \
 *    D   E            B   C
 *    |                    |
 *    F                    E
 */
REGRESSION_TEST(Http2DependencyTree_2)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  tree->reprioritize(1, 7, false);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_d = tree->find(7);
  Node *node_f = tree->find(11);

  box.check(node_a->parent == node_d, "parent of A should be D");
  box.check(node_d->parent == node_x, "parent of D should be X");
  box.check(node_f->parent == node_d, "parent of F should be D");

  delete tree;
}

/**
 * Reprioritization (exclusive)
 *
 *    x              x
 *    |              |
 *    A              D
 *   / \             |
 *  B   C     ==>    A
 *     / \          /|\
 *    D   E        B C F
 *    |              |
 *    F              E
 */
REGRESSION_TEST(Http2DependencyTree_3)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  tree->reprioritize(1, 7, true);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_d = tree->find(7);
  Node *node_f = tree->find(11);

  box.check(node_a->parent == node_d, "parent of A should be D");
  box.check(node_d->parent == node_x, "parent of D should be X");
  box.check(node_f->parent == node_a, "parent of F should be A");

  delete tree;
}

/**
 * Only One Node Tree
 *      ROOT
 *      /
 *    A(1)
 */
REGRESSION_TEST(Http2DependencyTree_4)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A");
  tree->add(0, 1, 0, false, &a);

  Node *node_a = tree->find(1);

  box.check(tree->top() == nullptr, "top should be nullptr");

  tree->activate(node_a);
  box.check(tree->top() == node_a, "top should be A");

  tree->deactivate(node_a, 0);
  box.check(tree->top() == nullptr, "top should be nullptr");

  delete tree;
}

/**
 * Simple Tree
 *      ROOT
 *      /
 *    A(3)
 *   /
 * B(5)
 *
 */
REGRESSION_TEST(Http2DependencyTree_5)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C");

  tree->add(0, 3, 15, false, &a);
  tree->add(3, 5, 15, false, &b);

  Node *node_a = tree->find(3);
  Node *node_b = tree->find(5);

  box.check(tree->top() == nullptr, "top should be nullptr");

  tree->activate(node_a);
  tree->activate(node_b);
  box.check(tree->top() == node_a, "top should be A");

  tree->deactivate(node_a, 0);
  box.check(tree->top() == node_b, "top should be B");

  delete tree;
}

/**
 * Basic Tree
 *      ROOT
 *      /  \
 *    A(3)  D(9)
 *   /  \
 * B(5) C(7)
 *
 */
REGRESSION_TEST(Http2DependencyTree_6)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C"), d("D");

  // NOTE, weight is actual weight - 1
  tree->add(0, 3, 20, false, &a); // node_a is unused
  Node *node_b = tree->add(3, 5, 10, false, &b);
  Node *node_c = tree->add(3, 7, 10, false, &c);
  Node *node_d = tree->add(0, 9, 20, false, &d);

  // Activate B, C and D
  tree->activate(node_b);
  tree->activate(node_c);
  tree->activate(node_d);

  ostringstream oss;

  for (int i = 0; i < 90; ++i) {
    Node *node = tree->top();
    oss << static_cast<string *>(node->t)->c_str();
    tree->update(node, 100);
  }

  const string expect = "BDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBDCDBD";
  box.check(oss.str() == expect, "\nExpect : %s\nActual : %s", expect.c_str(), oss.str().c_str());

  delete tree;
}

/**
 * Tree of Chrome 50
 *
 *       ROOT
 *     /   |       \
 *   A(3) B(5) ... I(19)
 *
 */
REGRESSION_TEST(Http2DependencyTree_Chrome_50)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F"), g("G"), h("H"), i("I");

  Node *node_a = tree->add(0, 3, 255, false, &a);
  Node *node_b = tree->add(0, 5, 255, false, &b);
  Node *node_c = tree->add(0, 7, 255, false, &c);
  Node *node_d = tree->add(0, 9, 182, false, &d);
  Node *node_e = tree->add(0, 11, 182, false, &e);
  Node *node_f = tree->add(0, 13, 182, false, &f);
  Node *node_g = tree->add(0, 15, 146, false, &g);
  Node *node_h = tree->add(0, 17, 146, false, &h);
  Node *node_i = tree->add(0, 19, 146, false, &i);

  // Activate nodes from A to I
  tree->activate(node_a);
  tree->activate(node_b);
  tree->activate(node_c);
  tree->activate(node_d);
  tree->activate(node_e);
  tree->activate(node_f);
  tree->activate(node_g);
  tree->activate(node_h);
  tree->activate(node_i);

  ostringstream oss;

  for (int i = 0; i < 108; ++i) {
    Node *node = tree->top();
    oss << static_cast<string *>(node->t)->c_str();

    tree->update(node, 16375);
  }

  const string expect =
    "ABCDEFGHIABCDEFGHIABCDEFABCGHIABCDEFABCGHIDEFABCGHIDEFABCABCDEFGHIABCDEFABCGHIABCDEFABCGHIDEFABCGHIDEFABCABC";

  box.check(oss.str() == expect, "\nExpect : %s\nActual : %s", expect.c_str(), oss.str().c_str());

  delete tree;
}

/**
 * Tree of Chrome 51
 *
 *   ROOT
 *    |
 *   A(3)
 *    |
 *   B(5)
 *    .
 *    .
 *    .
 *   I(19)
 *
 */
REGRESSION_TEST(Http2DependencyTree_Chrome_51)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F"), g("G"), h("H"), i("I");

  Node *node_a = tree->add(0, 3, 255, false, &a);
  Node *node_b = tree->add(3, 5, 255, false, &b);
  Node *node_c = tree->add(5, 7, 255, false, &c);
  Node *node_d = tree->add(7, 9, 182, false, &d);
  Node *node_e = tree->add(9, 11, 182, false, &e);
  Node *node_f = tree->add(11, 13, 182, false, &f);
  Node *node_g = tree->add(13, 15, 146, false, &g);
  Node *node_h = tree->add(15, 17, 146, false, &h);
  Node *node_i = tree->add(17, 19, 146, false, &i);

  // Activate nodes A, C, E, G, and I
  tree->activate(node_a);
  tree->activate(node_c);
  tree->activate(node_e);
  tree->activate(node_g);
  tree->activate(node_i);

  ostringstream oss;

  for (int i = 0; i < 9; ++i) {
    Node *node = tree->top();
    if (node != nullptr) {
      oss << static_cast<string *>(node->t)->c_str();

      tree->deactivate(node, 16384);
      tree->remove(node);
    }
  }

  // Activate nodes B, D, F, and H
  tree->activate(node_b);
  tree->activate(node_d);
  tree->activate(node_f);
  tree->activate(node_h);

  for (int i = 0; i < 9; ++i) {
    Node *node = tree->top();
    if (node != nullptr) {
      oss << static_cast<string *>(node->t)->c_str();

      tree->deactivate(node, 16384);
      tree->remove(node);
    }
  }

  const string expect = "ACEGIBDFH";

  box.check(oss.str() == expect, "\nExpect : %s\nActual : %s", expect.c_str(), oss.str().c_str());

  delete tree;
}

/**
 * Removing Node from tree 1
 *
 *    ROOT
 *     |
 *    A(3)
 *   /  \
 * B(5) C(7)
 *
 */
REGRESSION_TEST(Http2DependencyTree_remove_1)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C");

  // NOTE, weight is actual weight - 1
  Node *node_a = tree->add(0, 3, 30, false, &a);
  Node *node_b = tree->add(3, 5, 20, false, &b);
  Node *node_c = tree->add(3, 7, 10, false, &c);

  // Activate A, B, and C
  tree->activate(node_a);
  tree->activate(node_b);
  tree->activate(node_c);

  Node *top_node = nullptr;

  // Deactivate A and try to remove
  top_node = tree->top();
  box.check(top_node == node_a, "Top node should be node_a");
  tree->deactivate(node_a, 16);
  tree->remove(node_a);
  box.check(tree->find(3) == nullptr, "Node A should be removed");

  // Deactivate B and try to remove
  top_node = tree->top();
  box.check(top_node == node_b, "Top node should be node_b");
  tree->deactivate(node_b, 16);
  tree->remove(node_b);
  box.check(tree->find(5) == nullptr, "Node B should be removed");

  // Deactivate C and try to remove
  top_node = tree->top();
  box.check(top_node == node_c, "Top node should be node_c");
  tree->deactivate(node_c, 16);
  tree->remove(node_c);
  box.check(tree->find(7) == nullptr, "Node C should be removed");

  delete tree;
}

/**
 * Removing Node from tree 2
 *
 *    ROOT
 *     |
 *    A(3)
 *     |
 *    B(5)
 *     |
 *    C(7)
 */
REGRESSION_TEST(Http2DependencyTree_remove_2)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C");

  // NOTE, weight is actual weight - 1
  Node *node_a = tree->add(0, 3, 20, false, &a);
  Node *node_b = tree->add(3, 5, 10, false, &b);
  Node *node_c = tree->add(5, 7, 10, false, &c);

  // Activate, deactivate, and remove C
  tree->activate(node_c);
  box.check(tree->top() == node_c, "Top node should be node_c");
  tree->deactivate(node_c, 16384);
  tree->remove(node_c);

  // Activate, deactivate, and remove A
  tree->activate(node_a);
  box.check(tree->top() == node_a, "Top node should be node_a");
  tree->deactivate(node_a, 16384);
  tree->remove(node_a);

  // Activate, deactivate, and remove B
  tree->activate(node_b);
  box.check(tree->top() == node_b, "Top node should be node_b");
  tree->deactivate(node_b, 16384);
  tree->remove(node_b);

  box.check(tree->top() == nullptr, "Top node should be nullptr");
  box.check(tree->find(3) == nullptr, "Tree should be empty");
  box.check(tree->find(5) == nullptr, "Tree should be empty");
  box.check(tree->find(7) == nullptr, "Tree should be empty");

  delete tree;
}

/**
 * Exclusive Dependency Creation
 *
 *       A            A
 *      / \    =>     |
 *     B   C          D
 *                   / \
 *                  B   C
 */
REGRESSION_TEST(Http2DependencyTree_exclusive_node)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D");

  Node *B = tree->add(0, 1, 0, false, &b);
  tree->add(0, 3, 0, false, &c);

  tree->activate(B);
  // Add node with exclusive flag
  tree->add(0, 5, 0, true, &d);

  tree->deactivate(B, 0);
  tree->remove(B);

  box.check(tree->top() == nullptr, "Tree top should be nullptr");

  delete tree;
}

/** test for reprioritize with active node
 *
 *     root                  root                   root
 *    /    \                /    \   (remove A)    /    \
 *   A      B   =======>   C      B   =======>    C      B
 *           \            /
 *            C          A
 *
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C");

  Node *A = tree->add(0, 7, 70, false, &a);
  Node *B = tree->add(0, 3, 10, false, &b);
  Node *C = tree->add(3, 5, 30, false, &c);

  tree->activate(A);
  tree->activate(B);
  tree->activate(C);

  tree->reprioritize(A, 5, false);

  tree->deactivate(A, 0);
  tree->remove(A);

  box.check(tree->top()->t != nullptr, "should not core dump");

  delete tree;
}

/**
 * Reprioritization (exclusive)
 *
 *    x              x
 *    |              |
 *    A              D
 *   / \             |
 *  B   C     ==>    A
 *     / \          /|\
 *    D   E        B C F
 *    |              |
 *    F              E
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize_2)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_b = tree->find(3);
  Node *node_d = tree->find(7);

  tree->activate(node_b);
  box.check(node_x->queue->in(node_a->entry), "A should be in x's queue");

  tree->reprioritize(1, 7, true);

  box.check(!node_x->queue->in(node_a->entry), "A should not be in x's queue");
  box.check(node_x->queue->in(node_d->entry), "D should be in x's queue");
  box.check(node_d->queue->in(node_a->entry), "A should be in d's queue");

  delete tree;
}

/**
 * Reprioritization (exclusive)
 *
 *    x              x
 *    |              |
 *    A              D
 *   / \             |
 *  B   C     ==>    A
 *     / \          /|\
 *    D   E        B C F
 *    |              |
 *    F              E
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize_3)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_c = tree->find(5);
  Node *node_d = tree->find(7);
  Node *node_f = tree->find(11);

  tree->activate(node_f);
  tree->reprioritize(1, 7, true);

  box.check(node_a->queue->in(node_f->entry), "F should be in A's queue");
  box.check(node_d->queue->in(node_a->entry), "A should be in D's queue");
  box.check(node_x->queue->in(node_d->entry), "D should be in x's queue");
  box.check(!node_a->queue->in(node_c->entry), "C should not be in A's queue");
  box.check(node_c->queue->empty(), "C's queue should be empty");

  delete tree;
}

/**
 * https://github.com/apache/trafficserver/issues/4057
 * Reprioritization to root
 *
 *    x                x
 *    |               / \
 *    A              A   D
 *   / \            / \  |
 *  B   C     ==>  B   C F
 *     / \             |
 *    D   E            E
 *    |
 *    F
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize_4)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_c = tree->find(5);
  Node *node_d = tree->find(7);
  Node *node_f = tree->find(11);

  tree->activate(node_f);
  tree->reprioritize(7, 0, false);

  box.check(!node_a->queue->in(node_f->entry), "F should not be in A's queue");
  box.check(node_d->queue->in(node_f->entry), "F should be in D's queue");
  box.check(node_x->queue->in(node_d->entry), "D should be in x's queue");
  box.check(!node_a->queue->in(node_c->entry), "C should not be in A's queue");
  box.check(node_c->queue->empty(), "C's queue should be empty");

  delete tree;
}

/**
 * https://github.com/apache/trafficserver/issues/4057
 * Reprioritization to unrelated node
 *
 *    x                x
 *    |                |
 *    A                A
 *   / \              / \
 *  B   C     ==>    B   C
 *     / \           |   |
 *    D   E          D   E
 *    |              |
 *    F              F
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize_5)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  Node *node_x = tree->find(0);
  Node *node_a = tree->find(1);
  Node *node_b = tree->find(3);
  Node *node_c = tree->find(5);
  Node *node_d = tree->find(7);
  Node *node_f = tree->find(11);

  tree->activate(node_f);
  tree->reprioritize(7, 3, false);

  box.check(node_a->queue->in(node_b->entry), "B should be in A's queue");
  box.check(node_b->queue->in(node_d->entry), "D should be in B's queue");
  box.check(!node_c->queue->in(node_d->entry), "D should not be in C's queue");
  box.check(node_x->queue->in(node_a->entry), "A should be in x's queue");
  box.check(!node_a->queue->in(node_c->entry), "C should not be in A's queue");
  box.check(node_c->queue->empty(), "C's queue should be empty");

  delete tree;
}

/** test for https://github.com/apache/trafficserver/issues/2268
 *
 *    root            root                  root
 *    /     =====>   /    \     =======>   /    \
 *   A              A      shadow         A      shadow
 *                          \                    \
 *                           B                    B
 *                                                 \
 *                                                  C
 *
 *              root                      root
 *             /    \                    /
 *  ======>   A      shadow   =======>  A
 *                    \
 *                     C
 */
REGRESSION_TEST(Http2DependencyTree_insert_with_empty_parent)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B"), c("C");
  tree->add(0, 3, 20, false, &a);

  Node *b_n = tree->add(9, 7, 30, true, &b);

  box.check(b_n->parent->id == 9, "Node B's parent should be 9");
  box.check(tree->find(9) == nullptr, "The shadow nodes should not be found");
  box.check(tree->find_shadow(9)->is_shadow() == true, "nodes 9 should be the shadow node");

  Node *c_n = tree->add(7, 11, 30, false, &c);
  tree->remove(b_n);

  box.check(c_n->parent->id == 9, "Node C's parent should be 9");
  box.check(tree->find(7) == nullptr, "Nodes b should be removed");
  box.check(tree->find_shadow(9)->is_shadow() == true, "Nodes 9 should be existed after removing");

  tree->remove(c_n);
  box.check(tree->find_shadow(9) == nullptr, "Shadow nodes should be remove");

  delete tree;
}

/** test for https://github.com/apache/trafficserver/issues/2268
 *
 *    root            root                  root                root
 *    /     =====>   /    \     =======>   /    \   =======>   /    \
 *   A              A      shadow         A      B            A      B
 *                          \                     \
 *                           B                     shadow
 */
REGRESSION_TEST(Http2DependencyTree_shadow_reprioritize)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);

  string a("A"), b("B");
  tree->add(0, 3, 20, false, &a);
  tree->add(9, 7, 30, true, &b);

  Node *s_n = tree->find_shadow(9);
  box.check(s_n != nullptr && s_n->is_shadow() == true, "Shadow nodes should not be nullptr");

  tree->reprioritize(s_n, 7, false);
  box.check(tree->find_shadow(9) == nullptr, "Shadow nodes should be nullptr after reprioritizing");

  delete tree;
}

/** Test for https://github.com/apache/trafficserver/pull/4212
 *
 * Add child to parent that has already completed.
 *
 * root        root        root        root       root
 *  |           |           |           |          |
 *  A   ====>   A   ====>   A   ====>   A  ====>   A
 *  |                       |                      |
 *  B                       C                      E
 *                          |
 *                          D
 */
REGRESSION_TEST(Http2DependencyTree_delete_parent_before_child_arrives)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E");

  tree->add(0, 3, 20, false, &a);
  Node *node_b = tree->add(3, 5, 30, true, &b);

  tree->remove(node_b);

  // Tree should remember B, so C will be added to B's ancestor
  Node *node_c = tree->add(5, 7, 20, false, &c);
  box.check(node_c->parent->id == 3, "Node C's parent should be 3");

  // See if it remembers two missing ancestors
  Node *node_d = tree->add(7, 9, 20, false, &d);

  tree->remove(node_c);
  tree->remove(node_d);

  Node *node_e = tree->add(9, 11, 30, false, &e);
  box.check(node_e->parent->id == 3, "Node E's parent should be 3");

  delete tree;
}

/** Test for https://github.com/apache/trafficserver/pull/4212
 *
 * Make sure priority nodes stick around
 *
 *        root                 root
 *       / | \                / | \
 *      P1 P2 P3   ====>     P1 P2 P3
 *      |  |  |                 |  |
 *      A  B  C                 B  C
 *         |                    |
 *         D                    D
 */
REGRESSION_TEST(Http2DependencyTree_handle_priority_nodes)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C"), d("D"), e("E");

  // P1 node
  tree->add(0, 3, 20, false, nullptr);
  // P2 node
  tree->add(0, 5, 20, false, nullptr);
  // P3 node
  tree->add(0, 7, 20, false, nullptr);

  Node *node_a = tree->add(3, 9, 30, true, &a);
  Node *node_b = tree->add(5, 11, 30, true, &b);
  Node *node_c = tree->add(7, 13, 30, true, &c);
  Node *node_d = tree->add(11, 15, 30, true, &d);

  box.check(node_a->parent->id == 3, "Node A's parent should be 3");
  box.check(node_b->parent->id == 5, "Node B's parent should be 5");
  box.check(node_c->parent->id == 7, "Node C's parent should be 7");
  box.check(node_d->parent->id == 11, "Node D's parent should be 11");

  // Deleting the children should not make the priority node go away
  tree->remove(node_a);
  Node *node_p1 = tree->find(3);
  box.check(node_p1 != nullptr, "Priority node 1 should remain");

  delete tree;
}

/**
 * Shadow nodes should reprioritize when they vivify
 *
 *      root                root              root
 *      /  \                 |                 |
 *     A   Shadow  ====>     A          ====>  A
 *          |                |                 |
 *          B                C(was shadow)     C
 *                           |
 *                           B
 */
REGRESSION_TEST(Http2DependencyTree_reprioritize_shadow_node)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C");

  tree->add(0, 3, 20, false, &a);
  // 7 should be created as a shadow node
  tree->add(7, 5, 20, false, &b);

  Node *b_n        = tree->find(5);
  Node *c_n        = tree->find(7);
  Node *c_shadow_n = tree->find_shadow(7);

  box.check(b_n != nullptr && b_n->parent->id == 7, "B should be child of 7");
  box.check(c_n == nullptr && c_shadow_n != nullptr && c_shadow_n->parent->id == 0, "Node 7 is a shadow and a child of the root");

  // Now populate the shadow
  tree->add(3, 7, 30, false, &c);
  c_n = tree->find(7);
  box.check(c_n != nullptr && c_n->parent->id == 3 && c_n->weight == 30, "C is a child of 3 and no longer a shadow");

  // C should still exist when its child goes away
  tree->remove(b_n);
  c_n = tree->find(7);
  box.check(c_n != nullptr, "C is still present with no children");

  delete tree;
}

REGRESSION_TEST(Http2DependencyTree_missing_parent)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A"), b("B"), c("C");

  tree->add(0, 3, 20, false, &a);
  tree->add(5, 7, 30, true, &b);

  Node *c_n        = tree->find(5);
  Node *c_shadow_n = tree->find_shadow(5);
  box.check(c_n == nullptr && c_shadow_n != nullptr && c_shadow_n->is_shadow() == true, "Node 5 starts out as a shadow");

  tree->add(0, 5, 15, false, &c);

  c_n = tree->find(5);
  box.check(c_n != nullptr && c_n->is_shadow() == false, "Node 5 should not be shadow node");
  box.check(c_n->point == 5 && c_n->weight == 15, "The weight and point should be 15");

  delete tree;
}

REGRESSION_TEST(Http2DependencyTree_max_depth)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree(100);
  string a("A");
  for (int i = 0; i < 100; ++i) {
    tree->add(i, i + 1, 16, false, &a);
  }
  Node *node = tree->find(100);
  Node *leaf = tree->find(99);
  box.check(node->parent->id == 0, "100st node should be child root");
  box.check(leaf != nullptr && leaf->parent->id != 0, "99th node is not a child of the root");

  delete tree;
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  const char *name = "Http2DependencyTree";
  RegressionTest::run(name, REGRESSION_TEST_QUICK);

  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
}
