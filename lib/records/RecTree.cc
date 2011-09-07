/** @file

  RecTree and RecTreeNode definitions

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

#include "libts.h"
#include "P_RecTree.h"

#define RecTreeDebug printf
bool rec_debug = false;


/*************************************************************************
 *
 * RecTreeNode
 *
 *************************************************************************/
RecTreeNode::RecTreeNode(const char *t):
record_ptr(NULL),
subtree_ptr(new RecTree(this)),
var_name_ptr(NULL),
num_leaf(0)
{
  if (t) {
    node_name = ats_strdup(t);
  } else {
    node_name = ats_strdup("root");
  }
}



RecTreeNode::~RecTreeNode()
{
  ats_free(node_name);
}

/**
  Print the current node's content. If this node is a leaf, that is,
  the subtree_list print count is zero, print the full variable name.

*/
void
RecTreeNode::print()
{
  if (num_leaf == 0) {
    RecTreeDebug("\t Leaf: %s\n", var_name_ptr);
  } else {
    subtree_ptr->print();
    RecTreeDebug("Node: %s\n", node_name);
  }
}


/*************************************************************************
 *
 * RecTree
 *
 *************************************************************************/
RecTree::RecTree(RecTreeNode * n)
{
  if (n == NULL) {
    n = new RecTreeNode("base");
  }
  this_node = n;
}


/*************************************************************************
 *
 *************************************************************************/
void
RecTree::rec_tree_insert(const char *var_name, const char *var_name_ptr)
{
  if ((var_name == NULL) || (!strlen(var_name))) {
    return;
  }

  if ((var_name_ptr == NULL) || (!strlen(var_name_ptr))) {
    var_name_ptr = var_name;
  }

  Tokenizer targetTok(".");
  targetTok.Initialize(var_name);
  tok_iter_state targetTok_state;
  const char *first_token = targetTok.iterFirst(&targetTok_state);

  const char *rest_token = strchr(var_name, REC_VAR_NAME_DELIMITOR);
  if (rest_token) {
    rest_token++;
    if (rec_debug) {
      RecTreeDebug("%s %s\n", first_token, rest_token);
    }
  }

  RecTreeNode *subtree = NULL;

  // First subtree who has the first token
  for (subtree = first(); subtree; subtree = next(subtree)) {
    if (!strcmp(subtree->node_name, first_token)) {
      if (rec_debug) {
        //      RecTreeDebug("RecTree::insert() -- found subtree %s\n", first_token);
      }
      break;
    }
  }

  // no subtree has the first token
  if (!subtree) {
    if (rec_debug) {
      RecTreeDebug("RecTree::insert() -- add subtree with %s\n", first_token);
    }
    subtree = new RecTreeNode((char *) first_token);
    ink_assert(subtree);
    m_root.enqueue(subtree);
  }

  if (rest_token) {
    if (rec_debug) {
      RecTreeDebug("RecTree::insert() -- insert the rest %s\n", rest_token);
    }
    subtree->subtree_ptr->rec_tree_insert(rest_token, var_name_ptr);
  } else {
    subtree->var_name_ptr = var_name_ptr;
    if (rec_debug) {
      RecTreeDebug("RecTree:insert() -- leaf node: %s\n", subtree->var_name_ptr);
    }
  }

  if (this_node) {
    (this_node->num_leaf)++;
  }

}


/*************************************************************************
 *
 *************************************************************************/
void
RecTree::print()
{
  for (RecTreeNode * node = first(); node; node = next(node)) {
    node->print();
  }
}


/*************************************************************************
 *
 *************************************************************************/
RecTree *
RecTree::rec_tree_get(char *path_name)
{

  Tokenizer targetTok(".");
  targetTok.Initialize(path_name);
  tok_iter_state targetTok_state;
  const char *first_token = targetTok.iterFirst(&targetTok_state);

  char *rest_token = strchr(path_name, REC_VAR_NAME_DELIMITOR);
  if (rest_token != NULL) {
    rest_token++;
  }

  RecTreeNode *subtree = NULL;

  // First subtree who has the first token
  for (subtree = first(); subtree; subtree = next(subtree)) {
    if (!strcmp(subtree->node_name, first_token)) {
      if (rec_debug) {
        RecTreeDebug("RecTree::get() -- found subtree %s\n", first_token);
      }
      break;
    }
  }

  if (!subtree) {
    if (rec_debug) {
      RecTreeDebug("RecTree::get() -- can't find subtree %s\n", first_token);
    }
    return NULL;
  } else {
    char wildcard[2];
    memset(wildcard, 0, 2);
    snprintf(wildcard, sizeof(wildcard), "%c", REC_VAR_NAME_WILDCARD);
    if (rest_token == NULL || !strcmp(rest_token, wildcard)) {
      return subtree->subtree_ptr;
    } else {
      if (rec_debug) {
        RecTreeDebug("RecTree::get() -- getting the rest %s\n", rest_token);
      }
      return subtree->subtree_ptr->rec_tree_get(rest_token);
    }
  }

}


/*************************************************************************
 * rec_tree_get_list
 *
 *************************************************************************/
void
RecTree::rec_tree_get_list(char *path_name, char ***buf, int *count)
{

  int i = 0;
  RecTree *subtree = rec_tree_get(path_name);

  if (!subtree) {
    (*count) = 0;
    return;
  }

  (*count) = subtree->this_node->num_leaf;
  if (rec_debug) {
    RecTreeDebug("RecTreeGetList subtree %s has %d leafs\n", subtree->this_node->node_name, (*count));
  }

  *buf = (char **)ats_malloc(sizeof(char *) * (*count));
  for (i = 0; i < (*count); i++) {
    (*buf)[i] = (char *)ats_malloc(sizeof(char));
  }

  int index = 0;
  subtree->p_rec_tree_get_list(path_name, &(*buf), &index);
  ink_assert((*count) == index);

  if (rec_debug) {
    for (i = 0; i < (*count); i++) {
      RecTreeDebug("[%d] %s\n", i, (*buf)[i]);
    }
  }

}


/** Recursive/private version of RecTreeGetList(). */
void
RecTree::p_rec_tree_get_list(char *path_name, char ***buffer, int *index)
{

  if (this_node->var_name_ptr) {
    (*buffer)[(*index)] = (char*)this_node->var_name_ptr;
    if (rec_debug) {
      RecTreeDebug("%d %s\n", (*index), (*buffer)[(*index)]);
    }
    (*index)++;
  }

  for (RecTreeNode * subtree = first(); subtree; subtree = next(subtree)) {
    if (rec_debug) {
      RecTreeDebug("current node: %s, subtree node: %s\n", this_node->node_name, subtree->node_name);
    }
    subtree->subtree_ptr->p_rec_tree_get_list(&(*path_name), &(*buffer), &(*index));
  }

}
