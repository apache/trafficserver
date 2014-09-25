/** @file 
    SSL dynamic certificate loader
    Loads certificates into a hash table as they are requested
*/

# include <stdio.h>
# include <memory.h>
# include <inttypes.h>
# include "domain-tree.h"

// return true if comparable.  Return type of compare in relative parameter
// 0 if eq.  < 0 if node key is broader.  > 0 if parameter key is broader
bool 
DomainNameTree::DomainNameNode::compare(std::string key, int &relative) {
  size_t star_loc = key.find("*");
  bool is_wild = false;
  if (star_loc != std::string::npos) {
    star_loc++;
    is_wild = true;
    key = key.substr(star_loc);
  }
  return this->prunedCompare(key, relative, is_wild);
}

bool 
DomainNameTree::DomainNameNode::prunedCompare(std::string key, int &relative, bool is_wild) {
  if (key == this->key) {
    relative = 0;
    return true;
  }
  else {
    if (this->is_wild) { 
      size_t loc = key.find(this->key);
      if (this->key == "") { // Match all
        relative =  -1;
        return true;
      }
      else if (loc != std::string::npos) {
        // node key is in search key
        if ((key.length() - this->key.length()) == loc) {
          // And node key is at the end of search key
          relative =  -1;
          return true;
        }
      }
    }
    if (is_wild) {
      if (key == "") { // Match all
        relative =  1;
        return true;
      }
      else {
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

DomainNameTree::DomainNameNode *DomainNameTree::find(std::string key, bool best_match) {
  DomainNameNode *retval = NULL;
  DomainNameNode *first = NULL;
  size_t star_loc = key.find("*");
  bool is_wild = false;
  if (star_loc != std::string::npos) {
    key = key.substr(star_loc+1);
    is_wild = true;
  }
  
  bool set_iter = false;
  std::deque<DomainNameNode *>::iterator sibPtr;
  DomainNameNode *current_node = root;
  while (current_node != NULL) {
    bool partial_match = false;
    int relative;
    if (current_node->prunedCompare(key, relative, is_wild)) {
      if (relative == 0) {
        retval = current_node;
        if (NULL == first || retval->order < first->order) {
          first = retval;
        }
        break;
      }
      else if (relative < 0) {
        retval = current_node;
        partial_match = true;
        if (NULL == first || retval->order < first->order) {
          first = retval;
        }
      }
    }
    if (partial_match) {
      // Check out the children, maybe there is something better there
      sibPtr = current_node->children.begin();
      set_iter = true;
      if (sibPtr == current_node->children.end()) break;  // We are done
      current_node = *(sibPtr++);
    } 
    else { // No match here.  Look at next sibling?
      // Is there another sibling to look at?
      if (set_iter && sibPtr != current_node->children.end()) {
        current_node = *(sibPtr++);
      }
      else {	// No more siblings to check, give it up.
        break;
      }
    }
  }
  return best_match ? retval : first;
}

DomainNameTree::DomainNameNode *
DomainNameTree::insert(std::string key, void *payload, int order) {
  TSMutexLock(this->tree_mutex);
  DomainNameNode *retval = NULL;
  DomainNameNode *node = this->findBestMatch(key);
  int relative;
  if (node->compare(key, relative)) {
    size_t star_loc = key.find("*");
    bool is_wild = false;
    if (star_loc != std::string::npos) {
      star_loc++;
      key = key.substr(star_loc);
      is_wild = true;
    }
    if (relative < 0) {
      // Make a new node that is a child of node
      DomainNameNode *new_node = new DomainNameNode(key, payload, order, is_wild);
      new_node->parent = node;
      node->children.push_back(new_node);
      retval = new_node;
    }
    else if (relative > 0) {
      // Insert new node as parent of node
      DomainNameNode *new_node = new DomainNameNode(key, payload, order, is_wild);
      new_node->parent = node->parent;
      new_node->children.push_back(node);
      // Replace the node with new_node in the child list of the parent; 
      std::deque<DomainNameNode *>::iterator iter = node->parent->children.begin();
      for (; iter != node->parent->children.end(); ++iter) {
        if (*(iter) == node) {
          *(iter) = new_node;
        }
      }
      retval =  new_node;
    }
    // Will not replace in the equal case
    // Unless this is the root node
    else {
      if (node->key == "" && node->order == 0x7fffffff){
        node->key = key;
        node->payload = payload;
        node->order = order;
        retval = node;
      }
    }
  }
  TSMutexUnlock(this->tree_mutex);
  return retval;
}

