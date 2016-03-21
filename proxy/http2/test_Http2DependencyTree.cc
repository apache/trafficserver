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

#include <stdlib.h>
#include <iostream>
#include <assert.h>
#include <string.h>
#include <sstream>

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
bool
exclusive_dependency_creaion()
{
  Tree *tree = new Tree();
  string a("A"), b("B"), c("C"), d("D");

  tree->add(0, 1, 0, false, &b);
  tree->add(0, 3, 0, false, &c);

  Tree::Node *node_a = tree->find(0);
  Tree::Node *node_b = tree->find(1);
  Tree::Node *node_c = tree->find(3);

  ink_assert(node_b->parent == node_a);
  ink_assert(node_c->parent == node_a);

  // Add node with exclusive flag
  tree->add(0, 5, 0, true, &d);

  Tree::Node *node_d = tree->find(5);

  ink_assert(node_d->parent == node_a);
  ink_assert(node_b->parent == node_d);
  ink_assert(node_c->parent == node_d);

  return true;
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
bool
reprioritization()
{
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

  ink_assert(node_a->parent == node_d);
  ink_assert(node_d->parent == node_x);
  ink_assert(node_f->parent == node_d);

  return true;
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
bool
exclusive_reprioritization()
{
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

  ink_assert(node_a->parent == node_d);
  ink_assert(node_d->parent == node_x);
  ink_assert(node_f->parent == node_a);

  return true;
}


/**
 * Tree
 *      ROOT
 *      /
 *    A(1)
 */
bool
one_node()
{
  Tree *tree = new Tree();
  string a("A");
  tree->add(0, 1, 0, false, &a);

  Tree::Node *node_a = tree->find(1);

  ink_assert(tree->top() == NULL);

  tree->activate(node_a);
  ink_assert(tree->top() == node_a);

  tree->deactivate(node_a, 0);
  ink_assert(tree->top() == NULL);

  return true;
}

/**
 * Tree
 *      ROOT
 *      /
 *    A(3)
 *   /
 * B(5)
 *
 */
bool
active_intermediate_node()
{
  Tree *tree = new Tree();
  string a("A"), b("B"), c("C");

  tree->add(0, 3, 15, false, &a);
  tree->add(3, 5, 15, false, &b);

  Tree::Node *node_a = tree->find(3);
  Tree::Node *node_b = tree->find(5);

  ink_assert(tree->top() == NULL);

  tree->activate(node_a);
  tree->activate(node_b);
  ink_assert(tree->top() == node_a);

  tree->deactivate(node_a, 0);
  ink_assert(tree->top() == node_b);

  return true;
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
bool
basic_tree()
{
  Tree *tree = new Tree();

  string a("A"), b("B"), c("C"), d("D");

  // NOTE, weight is actual weight - 1
  tree->add(0, 3, 0, false, &a);
  tree->add(3, 5, 0, false, &b);
  tree->add(3, 7, 1, false, &c);
  tree->add(0, 9, 1, false, &d);

  Tree::Node *node_b = tree->find(5);
  Tree::Node *node_c = tree->find(7);
  Tree::Node *node_d = tree->find(9);

  // Activate B, C and D
  tree->activate(node_b);
  tree->activate(node_c);
  tree->activate(node_d);

  ostringstream oss;

  for (int i = 0; i < 30; ++i) {
    Tree::Node *node = tree->top();
    oss << node->t->c_str();
    tree->update(node, 100);
  }

  bool result = false;

  // TODO: check strictly
  if (oss.str() == "BDDDCDDCDDCDDBDDCDDCDDBDDCDDCD") {
    result = true;
  } else {
    cerr << "ERR: " << oss.str() << endl;
    result = false;
  }

  delete tree;

  return result;
}

/**
 * Chrome's Tree
 *      ROOT
 *      /  \
 *   A(3)   C(7)
 *   /        \
 * B(5)       D(9)
 *
 */
bool
chrome_tree()
{
  Tree *tree = new Tree();

  string a("A"), b("B"), c("C"), d("D");

  tree->add(0, 3, 15, false, &a);
  tree->add(3, 5, 15, true, &b);
  tree->add(0, 7, 15, false, &c);
  tree->add(7, 9, 15, true, &d);

  Tree::Node *node_a = tree->find(3);
  Tree::Node *node_b = tree->find(5);
  Tree::Node *node_c = tree->find(7);
  Tree::Node *node_d = tree->find(9);

  // Activate A and B
  tree->activate(node_a);
  tree->activate(node_b);
  tree->activate(node_c);
  tree->activate(node_d);

  ostringstream oss;

  for (int i = 0; i < 30; ++i) {
    Tree::Node *node = tree->top();
    oss << node->t->c_str();

    if (i == 14 || i == 15) {
      tree->deactivate(node, 100);
    } else {
      tree->update(node, 100);
    }
  }

  bool result = false;

  if (oss.str() == "ACCAACCAACCAACCABDDBBDDBBDDBBD") {
    result = true;
  } else {
    cerr << "ERR: " << oss.str() << endl;
    result = false;
  }

  delete tree;

  return result;
}

int
main()
{
  cout << "Exclusive Dependency Creation: ";
  if (exclusive_dependency_creaion()) {
    cout << "OK" << endl;
  }

  cout << "Reprioritization: ";
  if (reprioritization()) {
    cout << "OK" << endl;
  }

  cout << "Exclusive Reprioritization: ";
  if (exclusive_reprioritization()) {
    cout << "OK" << endl;
  }

  cout << "One node: ";
  if (one_node()) {
    cout << "OK" << endl;
  }

  cout << "Active Intermidiate Node: ";
  if (active_intermediate_node()) {
    cout << "OK" << endl;
  }

  cout << "Basic Tree: ";
  if (basic_tree()) {
    cout << "OK" << endl;
  }

  cout << "Chrome Tree: ";
  if (chrome_tree()) {
    cout << "OK" << endl;
  }

  return 0;
}
