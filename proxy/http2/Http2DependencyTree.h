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
  Node(void *t = nullptr) : t(t)
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

  bool
  is_shadow() const
  {
    return t == nullptr;
  }

  bool active     = false;
  bool queued     = false;
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
  Tree(uint32_t max_concurrent_streams) : _max_depth(MIN(max_concurrent_streams, HTTP2_DEPENDENCY_TREE_MAX_DEPTH)) {}

  ~Tree() { delete _root; }
  Node *find(uint32_t id);
  Node *find_shadow(uint32_t id);
  Node *add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t);
  void remove(Node *node);
  void reprioritize(uint32_t new_parent_id, uint32_t id, bool exclusive);
  void reprioritize(Node *node, uint32_t id, bool exclusive);
  Node *top();
  void activate(Node *node);
  void deactivate(Node *node, uint32_t sent);
  void update(Node *node, uint32_t sent);
  bool in(Node *current, Node *node);
  uint32_t size() const;

private:
  Node *_find(Node *node, uint32_t id, uint32_t depth = 1);
  Node *_top(Node *node);
  void _change_parent(Node *new_parent, Node *node, bool exclusive);
  bool in_parent_chain(Node *maybe_parent, Node *target);

  Node *_root = new Node(this);
  uint32_t _max_depth;
  uint32_t _node_count = 0;
};

template <typename T>
Node *
Tree<T>::_find(Node *node, uint32_t id, uint32_t depth)
{
  if (node->id == id) {
    return node;
  }

  if (node->children.empty() || depth >= _max_depth) {
    return nullptr;
  }

  Node *result = nullptr;
  for (Node *n = node->children.head; n; n = n->link.next) {
    result = _find(n, id, ++depth);
    if (result != nullptr) {
      break;
    }
  }

  return result;
}

template <typename T>
Node *
Tree<T>::find_shadow(uint32_t id)
{
  return _find(_root, id);
}

template <typename T>
Node *
Tree<T>::find(uint32_t id)
{
  Node *n = _find(_root, id);
  return n == nullptr ? nullptr : (n->is_shadow() ? nullptr : n);
}

template <typename T>
Node *
Tree<T>::add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t)
{
  Node *parent = find(parent_id);
  if (parent == nullptr) {
    parent = add(0, parent_id, HTTP2_PRIORITY_DEFAULT_WEIGHT, false, nullptr);
  }

  Node *node = find_shadow(id);
  if (node != nullptr && node->is_shadow()) {
    node->t      = t;
    node->point  = id;
    node->weight = weight;
    return node;
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
void
Tree<T>::reprioritize(uint32_t id, uint32_t new_parent_id, bool exclusive)
{
  Node *node = find(id);
  if (node == nullptr) {
    return;
  }

  reprioritize(node, new_parent_id, exclusive);
}

template <typename T>
void
Tree<T>::reprioritize(Node *node, uint32_t new_parent_id, bool exclusive)
{
  if (node == nullptr) {
    return;
  }

  Node *old_parent = node->parent;
  if (old_parent->id == new_parent_id) {
    // Do nothing
    return;
  }
  // should not change the root node
  ink_assert(node->parent);

  Node *new_parent = find(new_parent_id);
  if (new_parent == nullptr) {
    return;
  }
  // If node is dependent on the new parent, must move the new parent first
  if (new_parent_id != 0 && in_parent_chain(node, new_parent)) {
    _change_parent(new_parent, old_parent, false);
  }
  _change_parent(node, new_parent, exclusive);

  // delete the shadow node
  if (node->is_shadow() && node->children.empty() && node->queue->empty()) {
    remove(node);
  }
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
