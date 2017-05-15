/** @file
    SSL dynamic certificate loader
    Loads certificates into a hash table as they are requested

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
#include <cstdio>
#include <memory.h>
#include <cinttypes>
#include "domain-tree.h"

// return true if comparable.  Return type of compare in relative parameter
// 0 if eq.  < 0 if node key is broader.  > 0 if parameter key is broader
bool
DomainNameTree::DomainNameNode::compare(std::string key, int &relative)
{
  size_t star_loc = key.find('*');
  bool is_wild    = false;

  if (star_loc != std::string::npos) {
    star_loc++;
    is_wild = true;
    key     = key.substr(star_loc);
  }

  return this->prunedCompare(key, relative, is_wild);
}

bool
DomainNameTree::DomainNameNode::prunedCompare(const std::string &key, int &relative, bool is_wild)
{
  if (key == this->key) {
    relative = 0;
    return true;
  } else {
    if (this->is_wild) {
      size_t loc = key.find(this->key);

      if (this->key == "") { // Match all
        relative = -1;
        return true;
      } else if (loc != std::string::npos) {
        // node key is in search key
        if ((key.length() - this->key.length()) == loc) {
          // And node key is at the end of search key
          relative = -1;
          return true;
        }
      }
    }
    if (is_wild) {
      if (key == "") { // Match all
        relative = 1;
        return true;
      } else {
        size_t loc = this->key.find(key);

        if (loc != std::string::npos) {
          if ((this->key.length() - key.length()) == loc) {
            relative = 1;
            return true;
          }
        }
      }
    }
  }

  return false;
}

DomainNameTree::DomainNameNode *
DomainNameTree::find(std::string key, bool best_match)
{
  DomainNameNode *retval = nullptr;
  DomainNameNode *first  = nullptr;
  size_t star_loc        = key.find('*');
  bool is_wild           = false;

  if (star_loc != std::string::npos) {
    key     = key.substr(star_loc + 1);
    is_wild = true;
  }

  bool set_iter                = false;
  DomainNameNode *current_node = root;
  std::deque<DomainNameNode *>::iterator sibPtr, endPtr;

  while (current_node != nullptr) {
    bool partial_match = false;
    int relative;

    if (current_node->prunedCompare(key, relative, is_wild)) {
      if (relative == 0) {
        retval = current_node;
        if (nullptr == first || retval->order < first->order) {
          first = retval;
        }
        current_node = nullptr;
        break;
      } else if (relative < 0) {
        retval        = current_node;
        partial_match = true;
        if (nullptr == first || retval->order < first->order) {
          first = retval;
        }
      }
    }
    if (partial_match) {
      // Check out the children, maybe there is something better there
      sibPtr   = current_node->children.begin();
      endPtr   = current_node->children.end();
      set_iter = true;
      if (sibPtr == endPtr) {
        break; // We are done
      }
      current_node = *(sibPtr++);
    } else { // No match here.  Look at next sibling?
      // Is there another sibling to look at?
      if (set_iter && sibPtr != endPtr) {
        current_node = *(sibPtr++);
      } else { // No more siblings to check, give it up.
        break;
      }
    }
  }

  return best_match ? retval : first;
}

DomainNameTree::DomainNameNode *
DomainNameTree::insert(std::string key, void *payload, int order)
{
  TSMutexLock(this->tree_mutex);
  DomainNameNode *retval = nullptr;
  DomainNameNode *node   = this->findBestMatch(key);
  int relative;

  if (node->compare(key, relative)) {
    size_t star_loc = key.find('*');
    bool is_wild    = false;

    if (star_loc != std::string::npos) {
      star_loc++;
      key     = key.substr(star_loc);
      is_wild = true;
    }
    if (relative < 0) {
      // Make a new node that is a child of node
      DomainNameNode *new_node = new DomainNameNode(key, payload, order, is_wild);

      new_node->parent = node;
      node->children.push_back(new_node);
      retval = new_node;
    } else if (relative > 0) {
      // Insert new node as parent of node
      DomainNameNode *new_node = new DomainNameNode(key, payload, order, is_wild);

      new_node->parent = node->parent;
      new_node->children.push_back(node);
      node->parent = new_node;

      // Replace the node with new_node in the child list of the parent;
      for (std::deque<DomainNameNode *>::iterator iter = new_node->parent->children.begin();
           iter != new_node->parent->children.end(); ++iter) {
        if (*(iter) == node) {
          *(iter) = new_node;
        }
      }
      retval = new_node;
    } else {
      // Will not replace in the equal case
      // Unless this is the root node
      if (node->key == "" && node->order == 0x7fffffff) {
        node->key     = key;
        node->payload = payload;
        node->order   = order;
        retval        = node;
      }
    }
  }
  TSMutexUnlock(this->tree_mutex);

  return retval;
}
