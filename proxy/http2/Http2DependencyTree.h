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

#ifndef __HTTP2_DEP_TREE_H__
#define __HTTP2_DEP_TREE_H__

#include "ts/List.h"
#include "ts/Diags.h"
#include "ts/PriorityQueue.h"

#include "HTTP2.h"

// TODO: K is a constant, 256 is temporal value.
const static uint32_t K                               = 256;
const static uint32_t HTTP2_DEPENDENCY_TREE_MAX_DEPTH = 256;

template <typename T> class Http2DependencyTree
{
public:
  class Node
  {
  public:
    Node()
      : active(false),
        queued(false),
        id(HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY),
        weight(HTTP2_PRIORITY_DEFAULT_WEIGHT),
        point(0),
        parent(NULL),
        t(NULL)
    {
      entry = new PriorityQueueEntry<Node *>(this);
      queue = new PriorityQueue<Node *>();
    }
    Node(uint32_t i, uint32_t w, uint32_t p, Node *n, T t)
      : active(false), queued(false), id(i), weight(w), point(p), parent(n), t(t)
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
        Node *next = NULL;
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

    bool active;
    bool queued;
    uint32_t id;
    uint32_t weight;
    uint32_t point;
    Node *parent;
    DLL<Node> children;
    PriorityQueueEntry<Node *> *entry;
    PriorityQueue<Node *> *queue;
    T t;
  };

  Http2DependencyTree(uint32_t max_concurrent_streams)
    : _root(new Node()), _max_depth(MIN(max_concurrent_streams, HTTP2_DEPENDENCY_TREE_MAX_DEPTH)), _node_count(0)
  {
  }
  ~Http2DependencyTree() { delete _root; }
  Node *find(uint32_t id);
  Node *add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t);
  void remove(Node *node);
  void reprioritize(uint32_t new_parent_id, uint32_t id, bool exclusive);
  void reprioritize(Node *node, uint32_t id, bool exclusive);
  Node *top();
  void activate(Node *node);
  void deactivate(Node *node, uint32_t sent);
  void update(Node *node, uint32_t sent);
  uint32_t size() const;

private:
  Node *_find(Node *node, uint32_t id, uint32_t depth = 1);
  Node *_top(Node *node);
  void _change_parent(Node *new_parent, Node *node, bool exclusive);

  Node *_root;
  uint32_t _max_depth;
  uint32_t _node_count;
};

template <typename T>
typename Http2DependencyTree<T>::Node *
Http2DependencyTree<T>::_find(Node *node, uint32_t id, uint32_t depth)
{
  if (node->id == id) {
    return node;
  }

  if (node->children.empty() || depth >= _max_depth) {
    return NULL;
  }

  Node *result = NULL;
  for (Node *n = node->children.head; n; n = n->link.next) {
    result = _find(n, id, ++depth);
    if (result != NULL) {
      break;
    }
  }

  return result;
}

template <typename T>
typename Http2DependencyTree<T>::Node *
Http2DependencyTree<T>::find(uint32_t id)
{
  return _find(_root, id);
}

template <typename T>
typename Http2DependencyTree<T>::Node *
Http2DependencyTree<T>::add(uint32_t parent_id, uint32_t id, uint32_t weight, bool exclusive, T t)
{
  Node *parent = find(parent_id);
  if (parent == NULL) {
    parent = _root;
  }

  // Use stream id as initial point
  Node *node = new Node(id, weight, id, parent, t);

  if (exclusive) {
    while (Node *child = parent->children.pop()) {
      node->children.push(child);
      child->parent = node;
    }
  }

  parent->children.push(node);

  ++_node_count;
  return node;
}

template <typename T>
void
Http2DependencyTree<T>::remove(Node *node)
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

  --_node_count;
  delete node;
}

template <typename T>
void
Http2DependencyTree<T>::reprioritize(uint32_t id, uint32_t new_parent_id, bool exclusive)
{
  Node *node = find(id);
  if (node == NULL) {
    return;
  }

  reprioritize(node, new_parent_id, exclusive);
}

template <typename T>
void
Http2DependencyTree<T>::reprioritize(Node *node, uint32_t new_parent_id, bool exclusive)
{
  if (node == NULL) {
    return;
  }

  Node *old_parent = node->parent;
  if (old_parent->id == new_parent_id) {
    // Do nothing
    return;
  }

  Node *new_parent = find(new_parent_id);
  if (new_parent == NULL) {
    return;
  }
  _change_parent(new_parent, old_parent, false);
  _change_parent(node, new_parent, exclusive);
}

// Change node's parent to new_parent
template <typename T>
void
Http2DependencyTree<T>::_change_parent(Node *node, Node *new_parent, bool exclusive)
{
  node->parent->children.remove(node);
  node->parent = NULL;

  if (exclusive) {
    while (Node *child = new_parent->children.pop()) {
      node->children.push(child);
      child->parent = node;
    }
  }

  new_parent->children.push(node);
  node->parent = new_parent;
}

template <typename T>
typename Http2DependencyTree<T>::Node *
Http2DependencyTree<T>::_top(Node *node)
{
  Node *child = node;

  while (child != NULL) {
    if (child->active) {
      return child;
    } else if (!child->queue->empty()) {
      child = child->queue->top()->node;
    } else {
      return NULL;
    }
  }

  return child;
}

template <typename T>
typename Http2DependencyTree<T>::Node *
Http2DependencyTree<T>::top()
{
  return _top(_root);
}

template <typename T>
void
Http2DependencyTree<T>::activate(Node *node)
{
  node->active = true;

  while (node->parent != NULL && !node->queued) {
    node->parent->queue->push(node->entry);
    node->queued = true;
    node         = node->parent;
  }
}

template <typename T>
void
Http2DependencyTree<T>::deactivate(Node *node, uint32_t sent)
{
  node->active = false;

  while (node->queue->empty() && node->parent != NULL) {
    ink_assert(node->parent->queue->top() == node->entry);

    node->parent->queue->pop();
    node->queued = false;

    node = node->parent;
  }

  update(node, sent);
}

template <typename T>
void
Http2DependencyTree<T>::update(Node *node, uint32_t sent)
{
  while (node->parent != NULL) {
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
Http2DependencyTree<T>::size() const
{
  return _node_count;
}

#endif // __HTTP2_DEP_TREE_H__
