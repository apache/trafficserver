/** @file

  HTTP/2 Dependency Tree

  The original idea of Stream Priority Algorithm using Weighted Fair Queue (WFQ)
  Scheduling is invented by Kazuho Oku (H2O project).

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

#pragma once

#include "tscore/List.h"
#include "tscore/Diags.h"
#include "tscore/PriorityQueue.h"

#include "HTTP2.h"

// TODO: K is a constant, 256 is temporal value.
const static uint32_t K                               = 256;
const static uint32_t HTTP2_DEPENDENCY_TREE_MAX_DEPTH = 256;

namespace Http2DependencyTree
{
class Node
{
public:
  explicit Node(void *t = nullptr) : t(t)
  {
    entry = new PriorityQueueEntry<Node *>(this);
    queue = new PriorityQueue<Node *>();
  }

  Node(uint32_t i, uint32_t w, uint32_t p, Node *n, void *t = nullptr) : id(i), weight(w), point(p), t(t), parent(n)
  {
    entry = new PriorityQueueEntry<Node *>(this);
    queue = new PriorityQueue<Node *>();
  }

  ~Node()
  {
    delete entry;
    delete queue;

    // delete all child nodes
    if (!children.empty()) {
      Node *node = children.head;
      Node *next = nullptr;
      while (node) {
        next = node->link.next;
        children.remove(node);
        delete node;
        node = next;
      }
    }
  }

  Node(const Node &) = delete;
  Node &operator=(const Node &) = delete;

  LINK(Node, link);

  bool
  operator<(const Node &n) const
  {
    return point < n.point;
  }

  bool
  operator>(const Node &n) const
  {
    return point > n.point;
  }

  /**
   * Added an explicit shadow flag.  The original logic
   * was using an null Http2Stream frame to mark a shadow node
   * but that would pull in Priority holder nodes too
   */
  bool
  is_shadow() const
  {
    return shadow == true;
  }

  bool active     = false;
  bool queued     = false;
  bool shadow     = false;
  uint32_t id     = HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY;
  uint32_t weight = HTTP2_PRIORITY_DEFAULT_WEIGHT;
  uint32_t point  = 0;
  void *t         = nullptr;
  Node *parent    = nullptr;
  DLL<Node> children;
  PriorityQueueEntry<Node *> *entry;
  PriorityQueue<Node *> *queue;
};

template <typename T> class Tree
{
public:
  explicit Tree(uint32_t max_concurrent_streams) : _max_depth(MIN(max_concurrent_streams, HTTP2_DEPENDENCY_TREE_MAX_DEPTH))
  {
    _ancestors.resize(_max_ancestors);
  }
  ~Tree() { delete _root; }
  Node *find(uint32_t id, bool *is_max_leaf = nullptr);
  Node *find_shadow(uint32_t id, bool *is_max_leaf = nullptr);
  Node *add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t, bool shadow = false);
  Node *reprioritize(uint32_t id, uint32_t new_parent_id, bool exclusive);
  Node *reprioritize(Node *node, uint32_t id, bool exclusive);
  Node *top();
  void remove(Node *node);
  void activate(Node *node);
  void deactivate(Node *node, uint32_t sent);
  void update(Node *node, uint32_t sent);
  bool in(Node *current, Node *node);
  uint32_t size() const;
  /*
   * Dump the priority tree relationships in JSON form for debugging
   */
  void
  dump_tree(std::ostream &output) const
  {
    _dump(_root, output);
  }
  void add_ancestor(Node *node);
  uint32_t was_ancestor(uint32_t id) const;

private:
  void _dump(Node *node, std::ostream &output) const;
  Node *_find(Node *node, uint32_t id, uint32_t depth = 1, bool *is_max_leaf = nullptr);
  Node *_top(Node *node);
  void _change_parent(Node *node, Node *new_parent, bool exclusive);
  bool in_parent_chain(Node *maybe_parent, Node *target);

  Node *_root = new Node(this);
  uint32_t _max_depth;
  uint32_t _node_count = 0;
  /*
   * _ancestors in a circular buffer tracking parent relationships for
   * recently completed nodes.  Without this new streams may not find their
   * parents and be inserted at the root, violating the client's desired
   * dependency relationship.  This addresses the issue identified in section
   * 5.3.4 of the HTTP/2 spec
   *
   * "It is possible for a stream to become closed while prioritization
   * information that creates a dependency on that stream is in transit.
   * If a stream identified in a dependency has no associated priority
   * information, then the dependent stream is instead assigned a default
   * priority (Section 5.3.5).  This potentially creates suboptimal
   * prioritization, since the stream could be given a priority that is
   * different from what is intended.
   * To avoid these problems, an endpoint SHOULD retain stream
   * prioritization state for a period after streams become closed.  The
   * longer state is retained, the lower the chance that streams are
   * assigned incorrect or default priority values."
   */
  static const uint32_t _max_ancestors = 64;
  uint32_t _ancestor_index             = 0;
  std::vector<std::pair<uint32_t, uint32_t>> _ancestors;
};

template <typename T>
void
Tree<T>::_dump(Node *node, std::ostream &output) const
{
  output << R"({ "id":")" << node->id << "/" << node->weight << "/" << node->point << "/" << ((node->t != nullptr) ? "1" : "0")
         << "/" << ((node->active) ? "a" : "d") << "\",";
  // Dump the children
  output << " \"c\":[";
  for (Node *n = node->children.head; n; n = n->link.next) {
    _dump(n, output);
    output << ",";
  }
  output << "] }";
}

template <typename T>
Node *
Tree<T>::_find(Node *node, uint32_t id, uint32_t depth, bool *is_max_leaf)
{
  if (node->id == id) {
    if (is_max_leaf) {
      *is_max_leaf = depth == _max_depth;
    }
    return node;
  }

  if (node->children.empty() || depth > _max_depth) {
    return nullptr;
  }

  Node *result = nullptr;
  for (Node *n = node->children.head; n; n = n->link.next) {
    result = _find(n, id, ++depth, is_max_leaf);
    if (result != nullptr) {
      break;
    }
  }

  return result;
}

template <typename T>
Node *
Tree<T>::find_shadow(uint32_t id, bool *is_max_leaf)
{
  return _find(_root, id, 1, is_max_leaf);
}

template <typename T>
Node *
Tree<T>::find(uint32_t id, bool *is_max_leaf)
{
  Node *n = _find(_root, id, 1, is_max_leaf);
  return n == nullptr ? nullptr : (n->is_shadow() ? nullptr : n);
}

template <typename T>
void
Tree<T>::add_ancestor(Node *node)
{
  if (node->parent != _root) {
    _ancestors[_ancestor_index].first  = node->id;
    _ancestors[_ancestor_index].second = node->parent->id;
    _ancestor_index++;
    if (_ancestor_index >= _max_ancestors) {
      _ancestor_index = 0;
    }
  }
}

template <typename T>
uint32_t
Tree<T>::was_ancestor(uint32_t pid) const
{
  uint32_t i = (_ancestor_index == 0) ? _max_ancestors - 1 : _ancestor_index - 1;
  while (i != _ancestor_index) {
    if (_ancestors[i].first == pid) {
      return _ancestors[i].second;
    }
    i = (i == 0) ? _max_ancestors - 1 : i - 1;
  }
  return 0;
}

template <typename T>
Node *
Tree<T>::add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t, bool shadow)
{
  // Can we vivify a shadow node?
  Node *node = find_shadow(id);
  if (node != nullptr && node->is_shadow()) {
    node->t      = t;
    node->point  = id;
    node->weight = weight;
    node->shadow = false;
    // Move the shadow node into the proper position in the tree
    node = reprioritize(node, parent_id, exclusive);
    return node;
  }

  bool is_max_leaf = false;
  Node *parent     = find_shadow(parent_id, &is_max_leaf); // Look for real and shadow nodes

  if (parent == nullptr) {
    if (parent_id < id) { // See if we still have a history of the parent
      uint32_t pid = parent_id;
      do {
        pid = was_ancestor(pid);
        if (pid != 0) {
          parent = find(pid);
        }
      } while (pid != 0 && parent == nullptr);
      if (parent == nullptr) {
        // Found no ancestor, just add to root at default weight
        weight    = HTTP2_PRIORITY_DEFAULT_WEIGHT;
        exclusive = false;
        parent    = _root;
      }
    }
    if (parent == nullptr || parent == _root) { // Create a shadow node
      parent    = add(0, parent_id, HTTP2_PRIORITY_DEFAULT_WEIGHT, false, nullptr, true);
      exclusive = false;
    }
  } else if (is_max_leaf) { // Chain too long, just add to root
    parent    = _root;
    exclusive = false;
  }

  // Use stream id as initial point
  node = new Node(id, weight, id, parent, t);

  if (exclusive) {
    while (Node *child = parent->children.pop()) {
      if (child->queued) {
        parent->queue->erase(child->entry);
        node->queue->push(child->entry);
      }
      node->children.push(child);
      child->parent = node;
    }
  }

  parent->children.push(node);
  if (!node->queue->empty()) {
    ink_release_assert(!node->queued);
    parent->queue->push(node->entry);
    node->queued = true;
  }
  node->shadow = shadow;
  ++_node_count;
  return node;
}

template <typename T>
bool
Tree<T>::in(Node *current, Node *node)
{
  bool retval = false;
  if (current == nullptr)
    current = _root;
  if (current->queue->in(node->entry)) {
    return true;
  } else {
    Node *child = current->children.head;
    while (child) {
      if (in(child, node)) {
        return true;
      }
      child = child->link.next;
    }
  }
  return retval;
}

template <typename T>
void
Tree<T>::remove(Node *node)
{
  if (node == _root || node->active) {
    return;
  }

  // Make a note of node's ancestory
  add_ancestor(node);

  Node *parent = node->parent;
  parent->children.remove(node);
  if (node->queued) {
    parent->queue->erase(node->entry);
  }

  // Push queue entries
  while (!node->queue->empty()) {
    parent->queue->push(node->queue->top());
    node->queue->pop();
  }

  // Push children
  while (!node->children.empty()) {
    Node *child = node->children.pop();
    parent->children.push(child);
    child->parent = parent;
  }

  // delete the shadow parent
  if (parent->is_shadow() && parent->children.empty() && parent->queue->empty()) {
    remove(parent);
  }

  // ink_release_assert(!this->in(nullptr, node));

  --_node_count;
  delete node;
}

template <typename T>
Node *
Tree<T>::reprioritize(uint32_t id, uint32_t new_parent_id, bool exclusive)
{
  Node *node = find(id);
  if (node == nullptr) {
    return node;
  }

  return reprioritize(node, new_parent_id, exclusive);
}

template <typename T>
Node *
Tree<T>::reprioritize(Node *node, uint32_t new_parent_id, bool exclusive)
{
  if (node == nullptr) {
    return node;
  }

  Node *old_parent = node->parent;
  if (old_parent->id == new_parent_id) {
    // Do nothing
    return node;
  }
  // should not change the root node
  ink_assert(node->parent);

  Node *new_parent = find(new_parent_id);
  if (new_parent == nullptr) {
    return node;
  }
  // If node is dependent on the new parent, must move the new parent first
  if (new_parent_id != 0 && in_parent_chain(node, new_parent)) {
    _change_parent(new_parent, old_parent, false);
  }
  _change_parent(node, new_parent, exclusive);

  // delete the shadow node
  if (node->is_shadow() && node->children.empty() && node->queue->empty()) {
    remove(node);
    return nullptr;
  }

  return node;
}

template <typename T>
bool
Tree<T>::in_parent_chain(Node *maybe_parent, Node *target)
{
  bool retval  = false;
  Node *parent = target->parent;
  while (parent != nullptr && !retval) {
    retval = maybe_parent == parent;
    parent = parent->parent;
  }
  return retval;
}

// Change node's parent to new_parent
template <typename T>
void
Tree<T>::_change_parent(Node *node, Node *new_parent, bool exclusive)
{
  ink_release_assert(node->parent != nullptr);
  node->parent->children.remove(node);
  if (node->queued) {
    node->parent->queue->erase(node->entry);
    node->queued = false;

    Node *current = node->parent;
    while (current->queue->empty() && !current->active && current->parent != nullptr) {
      current->parent->queue->erase(current->entry);
      current->queued = false;
      current         = current->parent;
    }
  }

  node->parent = nullptr;
  if (exclusive) {
    while (Node *child = new_parent->children.pop()) {
      if (child->queued) {
        child->parent->queue->erase(child->entry);
        node->queue->push(child->entry);
      }

      node->children.push(child);
      ink_release_assert(child != node);
      child->parent = node;
    }
  }

  new_parent->children.push(node);
  ink_release_assert(node != new_parent);
  node->parent = new_parent;

  if (node->active || !node->queue->empty()) {
    Node *current = node;
    while (current->parent != nullptr && !current->queued) {
      current->parent->queue->push(current->entry);
      current->queued = true;
      current         = current->parent;
    }
  }
}

template <typename T>
Node *
Tree<T>::_top(Node *node)
{
  Node *child = node;

  while (child != nullptr) {
    if (child->active) {
      return child;
    } else if (!child->queue->empty()) {
      child = child->queue->top()->node;
    } else {
      return nullptr;
    }
  }

  return child;
}

template <typename T>
Node *
Tree<T>::top()
{
  return _top(_root);
}

template <typename T>
void
Tree<T>::activate(Node *node)
{
  node->active = true;

  while (node->parent != nullptr && !node->queued) {
    node->parent->queue->push(node->entry);
    node->queued = true;
    node         = node->parent;
  }
}

template <typename T>
void
Tree<T>::deactivate(Node *node, uint32_t sent)
{
  node->active = false;

  while (!node->active && node->queue->empty() && node->parent != nullptr) {
    if (node->queued) {
      node->parent->queue->erase(node->entry);
      node->queued = false;
    }

    node = node->parent;
  }

  update(node, sent);
}

template <typename T>
void
Tree<T>::update(Node *node, uint32_t sent)
{
  while (node->parent != nullptr) {
    node->point += sent * K / (node->weight + 1);

    if (node->queued) {
      node->parent->queue->update(node->entry, true);
    } else {
      node->parent->queue->push(node->entry);
      node->queued = true;
    }

    node = node->parent;
  }
}

template <typename T>
uint32_t
Tree<T>::size() const
{
  return _node_count;
}
} // namespace Http2DependencyTree
