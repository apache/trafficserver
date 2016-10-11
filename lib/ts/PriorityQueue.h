/** @file

  Priority Queue Implementation using Binary Heap.

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

#ifndef __PRIORITY_QUEUE_H__
#define __PRIORITY_QUEUE_H__

#include "ts/ink_assert.h"
#include "ts/Vec.h"

template <typename T> struct PriorityQueueEntry {
  PriorityQueueEntry(T n) : index(0), node(n){};
  PriorityQueueEntry() : index(0), node(NULL){};
  uint32_t index;
  T node;
};

template <typename T> struct PriorityQueueLess {
  bool
  operator()(const T &a, const T &b)
  {
    return *a < *b;
  }
};

template <typename T, class Comp = PriorityQueueLess<T>> class PriorityQueue
{
public:
  PriorityQueue() {}
  ~PriorityQueue() {}
  const bool empty();
  PriorityQueueEntry<T> *top();
  void pop();
  void push(PriorityQueueEntry<T> *);
  void update(PriorityQueueEntry<T> *);
  void update(PriorityQueueEntry<T> *, bool);
  void erase(PriorityQueueEntry<T> *);
  const Vec<PriorityQueueEntry<T> *> &dump() const;

private:
  Vec<PriorityQueueEntry<T> *> _v;

  void _swap(uint32_t, uint32_t);
  void _bubble_up(uint32_t);
  void _bubble_down(uint32_t);
};

template <typename T, typename Comp>
const Vec<PriorityQueueEntry<T> *> &
PriorityQueue<T, Comp>::dump() const
{
  return _v;
}

template <typename T, typename Comp>
const bool
PriorityQueue<T, Comp>::empty()
{
  return _v.length() == 0;
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::push(PriorityQueueEntry<T> *entry)
{
  ink_release_assert(entry != NULL);

  int len = _v.length();
  _v.push_back(entry);
  entry->index = len;

  _bubble_up(len);
}

template <typename T, typename Comp>
PriorityQueueEntry<T> *
PriorityQueue<T, Comp>::top()
{
  if (empty()) {
    return NULL;
  } else {
    return _v[0];
  }
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::pop()
{
  if (empty()) {
    return;
  }

  _swap(0, _v.length() - 1);
  _v.pop();
  _bubble_down(0);
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::erase(PriorityQueueEntry<T> *entry)
{
  if (empty()) {
    return;
  }

  ink_release_assert(entry->index < _v.length());
  const uint32_t original_index = entry->index;
  if (original_index != (_v.length() - 1)) {
    // Move the erased item to the end to be popped off
    _swap(original_index, _v.length() - 1);
    _v.pop();
    _bubble_down(original_index);
    _bubble_up(original_index);
  } else { // Otherwise, we are already at the end, just pop
    _v.pop();
  }
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::update(PriorityQueueEntry<T> *entry)
{
  ink_release_assert(entry != NULL);

  if (empty()) {
    return;
  }

  // One of them will works whether weight is increased or decreased
  _bubble_down(entry->index);
  _bubble_up(entry->index);
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::update(PriorityQueueEntry<T> *entry, bool increased)
{
  ink_release_assert(entry != NULL);

  if (empty()) {
    return;
  }

  if (increased) {
    _bubble_down(entry->index);
  } else {
    _bubble_up(entry->index);
  }
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::_swap(uint32_t i, uint32_t j)
{
  PriorityQueueEntry<T> *tmp = _v[i];
  _v[i]                      = _v[j];
  _v[j]                      = tmp;

  _v[i]->index = i;
  _v[j]->index = j;
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::_bubble_up(uint32_t index)
{
  if (empty()) {
    ink_release_assert(false);
  }

  Comp comp;

  uint32_t parent;
  while (index != 0) {
    parent = (index - 1) / 2;
    if (comp(_v[index]->node, _v[parent]->node)) {
      _swap(parent, index);
      index = parent;
      continue;
    }

    break;
  }
}

template <typename T, typename Comp>
void
PriorityQueue<T, Comp>::_bubble_down(uint32_t index)
{
  if (empty()) {
    // Do nothing
    return;
  }

  uint32_t left, right, smaller;

  Comp comp;

  while (true) {
    if ((left = index * 2 + 1) >= _v.length()) {
      break;
    } else if ((right = index * 2 + 2) >= _v.length()) {
      smaller = left;
    } else {
      smaller = comp(_v[left]->node, _v[right]->node) ? left : right;
    }

    if (comp(_v[smaller]->node, _v[index]->node)) {
      _swap(smaller, index);
      index = smaller;
      continue;
    }

    break;
  }
}

#endif // __PRIORITY_QUEUE_H__
