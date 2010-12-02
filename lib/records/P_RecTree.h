/** @file

  Private RecTree and RecTreeNode declarations

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

#ifndef _P_REC_TREE_H_
#define _P_REC_TREE_H_

#include "P_RecDefs.h"
#include "P_RecUtils.h"
#include "List.h"

class RecTree;

/*************************************************************************
 *
 * RecTreeNode
 *
 *************************************************************************/
class RecTreeNode
{
public:
  RecTreeNode(const char *name = NULL);
   ~RecTreeNode();

  RecRecord *record_ptr;
  RecTree *subtree_ptr;
  char *node_name;
  const char *var_name_ptr;
  int num_leaf;
  LINK(RecTreeNode, link);

  void print();
};


/*************************************************************************
 *
 * RecTree
 *
 *************************************************************************/
class RecTree
{
public:
  RecTree(RecTreeNode *);
  ~RecTree();

  inline RecTreeNode *first()
  {
    return m_root.head;
  };

  inline RecTreeNode *last()
  {
    return m_root.tail;
  };

  inline RecTreeNode *next(RecTreeNode * current)
  {
    return (current->link).next;
  };

  void rec_tree_insert(const char *, const char *full_name = NULL);
  RecTree *rec_tree_get(char *);
  void rec_tree_get_list(char *, char ***, int *);

  void print();
  RecTreeNode *this_node;

private:
  void p_rec_tree_get_list(char *, char ***, int *);
  Queue<RecTreeNode> m_root;
};

#endif
