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
#include <string.h>
#include <sstream>

#include "ts/TestBox.h"

#include "Http2DependencyTree.h"

using namespace std;

typedef Http2DependencyTree<string *> Tree;

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

  Tree *tree = new Tree();
  string a("A"), b("B"), c("C"), d("D");

  tree->add(0, 1, 0, false, &b);
  tree->add(0, 3, 0, false, &c);

  Tree::Node *node_a = tree->find(0);
  Tree::Node *node_b = tree->find(1);
  Tree::Node *node_c = tree->find(3);

  box.check(node_b->parent == node_a, "parent of B should be A");
  box.check(node_c->parent == node_a, "parent of C should be A");

  // Add node with exclusive flag
  tree->add(0, 5, 0, true, &d);

  Tree::Node *node_d = tree->find(5);

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

  Tree *tree = new Tree();
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  tree->reprioritize(1, 7, false);

  Tree::Node *node_x = tree->find(0);
  Tree::Node *node_a = tree->find(1);
  Tree::Node *node_d = tree->find(7);
  Tree::Node *node_f = tree->find(11);

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

  Tree *tree = new Tree();
  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F");

  tree->add(0, 1, 0, false, &a);
  tree->add(1, 3, 0, false, &b);
  tree->add(1, 5, 0, false, &c);
  tree->add(5, 7, 0, false, &d);
  tree->add(5, 9, 0, false, &e);
  tree->add(7, 11, 0, false, &f);

  tree->reprioritize(1, 7, true);

  Tree::Node *node_x = tree->find(0);
  Tree::Node *node_a = tree->find(1);
  Tree::Node *node_d = tree->find(7);
  Tree::Node *node_f = tree->find(11);

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

  Tree *tree = new Tree();
  string a("A");
  tree->add(0, 1, 0, false, &a);

  Tree::Node *node_a = tree->find(1);

  box.check(tree->top() == NULL, "top should be NULL");

  tree->activate(node_a);
  box.check(tree->top() == node_a, "top should be A");

  tree->deactivate(node_a, 0);
  box.check(tree->top() == NULL, "top should be NULL");

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

  Tree *tree = new Tree();
  string a("A"), b("B"), c("C");

  tree->add(0, 3, 15, false, &a);
  tree->add(3, 5, 15, false, &b);

  Tree::Node *node_a = tree->find(3);
  Tree::Node *node_b = tree->find(5);

  box.check(tree->top() == NULL, "top should be NULL");

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

  Tree *tree = new Tree();

  string a("A"), b("B"), c("C"), d("D");

  // NOTE, weight is actual weight - 1
  tree->add(0, 3, 20, false, &a); // node_a is unused
  Tree::Node *node_b = tree->add(3, 5, 10, false, &b);
  Tree::Node *node_c = tree->add(3, 7, 10, false, &c);
  Tree::Node *node_d = tree->add(0, 9, 20, false, &d);

  // Activate B, C and D
  tree->activate(node_b);
  tree->activate(node_c);
  tree->activate(node_d);

  ostringstream oss;

  for (int i = 0; i < 90; ++i) {
    Tree::Node *node = tree->top();
    oss << node->t->c_str();
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
REGRESSION_TEST(Http2DependencyTree_7)(RegressionTest *t, int /* atype ATS_UNUSED */, int *pstatus)
{
  TestBox box(t, pstatus);
  box = REGRESSION_TEST_PASSED;

  Tree *tree = new Tree();

  string a("A"), b("B"), c("C"), d("D"), e("E"), f("F"), g("G"), h("H"), i("I");

  Tree::Node *node_a = tree->add(0, 3, 255, false, &a);
  Tree::Node *node_b = tree->add(0, 5, 255, false, &b);
  Tree::Node *node_c = tree->add(0, 7, 255, false, &c);
  Tree::Node *node_d = tree->add(0, 9, 182, false, &d);
  Tree::Node *node_e = tree->add(0, 11, 182, false, &e);
  Tree::Node *node_f = tree->add(0, 13, 182, false, &f);
  Tree::Node *node_g = tree->add(0, 15, 146, false, &g);
  Tree::Node *node_h = tree->add(0, 17, 146, false, &h);
  Tree::Node *node_i = tree->add(0, 19, 146, false, &i);

  // Activate A and B
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
    Tree::Node *node = tree->top();
    oss << node->t->c_str();

    tree->update(node, 16375);
  }

  const string expect =
    "ABCDEFGHIABCDEFGHIABCDEFABCGHIABCDEFABCGHIDEFABCGHIDEFABCABCDEFGHIABCDEFABCGHIABCDEFABCGHIDEFABCGHIDEFABCABC";

  box.check(oss.str() == expect, "\nExpect : %s\nActual : %s", expect.c_str(), oss.str().c_str());

  delete tree;
}

int
main(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  const char *name = "Http2DependencyTree";
  RegressionTest::run(name);

  return RegressionTest::final_status == REGRESSION_TEST_PASSED ? 0 : 1;
}
