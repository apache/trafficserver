/** @file

  HTTP/2 Priority Queue

  Priority Queue Implimentation using Min Heap.
  Used by HTTP/2 Dependency Tree for WFQ Scheduling.

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

#ifndef __HTTP2_PRIORITY_QUEUE_H__
#define __HTTP2_PRIORITY_QUEUE_H__

#include "ts/Diags.h"
#include "ts/Vec.h"

template <typename T> struct Http2PriorityQueueEntry {
  Http2PriorityQueueEntry(T n) : index(0), node(n) {}

  uint32_t index;
  T node;
};

template <typename T> class Http2PriorityQueue
{
public:
  Http2PriorityQueue() {}
  ~Http2PriorityQueue() {}

  const bool empty();
  Http2PriorityQueueEntry<T> *top();
  void pop();
  void push(Http2PriorityQueueEntry<T> *);
  void update(Http2PriorityQueueEntry<T> *, bool);
  const Vec<Http2PriorityQueueEntry<T> *> &dump() const;

private:
  Vec<Http2PriorityQueueEntry<T> *> _v;

  void _swap(uint32_t, uint32_t);
  void _bubble_up(uint32_t);
  void _bubble_down(uint32_t);
};

template <typename T>
const Vec<Http2PriorityQueueEntry<T> *> &
Http2PriorityQueue<T>::dump() const
{
  return _v;
}

template <typename T>
const bool
Http2PriorityQueue<T>::empty()
{
  return _v.length() == 0;
}

template <typename T>
void
Http2PriorityQueue<T>::push(Http2PriorityQueueEntry<T> *entry)
{
  ink_release_assert(entry != NULL);

  int len = _v.length();
  _v.push_back(entry);
  entry->index = len;

  _bubble_up(len);
}

template <typename T>
Http2PriorityQueueEntry<T> *
Http2PriorityQueue<T>::top()
{
  if (empty()) {
    return NULL;
  } else {
    return _v[0];
  }
}

template <typename T>
void
Http2PriorityQueue<T>::pop()
{
  if (empty()) {
    return;
  }

  _v[0] = _v[_v.length() - 1];
  _v.pop();
  _bubble_down(0);
}

template <typename T>
void
Http2PriorityQueue<T>::update(Http2PriorityQueueEntry<T> *entry, bool increased = true)
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

template <typename T>
void
Http2PriorityQueue<T>::_swap(uint32_t i, uint32_t j)
{
  Http2PriorityQueueEntry<T> *tmp = _v[i];
  _v[i] = _v[j];
  _v[j] = tmp;

  _v[i]->index = i;
  _v[j]->index = j;
}


template <typename T>
void
Http2PriorityQueue<T>::_bubble_up(uint32_t index)
{
  if (empty()) {
    ink_release_assert(false);
  }

  uint32_t parent;
  while (index != 0) {
    parent = (index - 1) / 2;
    if (*(_v[index]->node) < *(_v[parent]->node)) {
      _swap(parent, index);
      index = parent;
      continue;
    }

    break;
  }
}


template <typename T>
void
Http2PriorityQueue<T>::_bubble_down(uint32_t index)
{
  if (empty()) {
    // Do nothing
    return;
  }

  uint32_t left, right, smaller;

  while (true) {
    if ((left = index * 2 + 1) >= _v.length()) {
      break;
    } else if ((right = index * 2 + 2) >= _v.length()) {
      smaller = left;
    } else {
      smaller = (*(_v[left]->node) < *(_v[right]->node)) ? left : right;
    }

    if (*(_v[smaller]->node) < *(_v[index]->node)) {
      _swap(smaller, index);
      index = smaller;
      continue;
    }

    break;
  }
}

#endif // __HTTP2_PRIORITY_QUEUE_H__
