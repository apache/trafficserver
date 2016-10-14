/** @file

  A brief file description

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

#include "P_RecTree.h"
#include "stdio.h"

int
main(int argc, char **argv)
{
  RecTree *new_rec_tree = new RecTree(nullptr);

  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child.grandchild1");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child.grandchild2");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child.grandchild2.grandgrandchild1");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child.grandchild2.grandgrandchild2");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.first_child.grandchild3");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.second_child");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.second_child.grandchild1");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.1");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.process.librecords.2");
  printf("\n");
  new_rec_tree->rec_tree_insert("proxy.node.http.hitrate");
  printf("\n");
  new_rec_tree->print();

  /* Test getting subtree */
  printf("\nTest getting subtree(1)\n");
  printf("----------------------------------------------------------------\n");
  RecTree *sub_tree = new_rec_tree->rec_tree_get("proxy.process.librecords");
  sub_tree->print();

  printf("\nTest getting subtree(2)\n");
  printf("----------------------------------------------------------------\n");
  sub_tree = new_rec_tree->rec_tree_get("proxy.process.*");
  sub_tree->print();

  /* Test getting a list of variables */
  printf("\nTest getting variables list\n");
  printf("----------------------------------------------------------------\n");
  int count = 0;
  char **variable_list;
  new_rec_tree->rec_tree_get_list("proxy.process", &variable_list, &count);
  for (int i = 0; i < count; i++) {
    printf("[RecTreeTest] %s\n", variable_list[i]);
  }

  printf("\n -- Fin --\n");
}
