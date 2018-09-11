/** @file

    Trie implementation for 8-bit string keys.

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

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "tscore/List.h"

// Note that you should provide the class to use here, but we'll store
// pointers to such objects internally.
template <typename T> class Trie
{
public:
  Trie() { m_root.Clear(); }
  // will return false for duplicates; key should be nullptr-terminated
  // if key_len is defaulted to -1
  bool Insert(const char *key, T *value, int rank, int key_len = -1);

  // will return false if not found; else value_ptr will point to found value
  T *Search(const char *key, int key_len = -1) const;
  void Clear();
  void Print();

  bool
  Empty() const
  {
    return m_value_list.empty();
  }

  virtual ~Trie() { Clear(); }

private:
  static const int N_NODE_CHILDREN = 256;

  class Node
  {
  public:
    T *value;
    bool occupied;
    int rank;

    void
    Clear()
    {
      value    = nullptr;
      occupied = false;
      rank     = 0;
      ink_zero(children);
    }

    void Print(const char *debug_tag) const;
    inline Node *
    GetChild(char index) const
    {
      return children[static_cast<unsigned char>(index)];
    }
    inline Node *
    AllocateChild(char index)
    {
      Node *&child = children[static_cast<unsigned char>(index)];
      ink_assert(child == nullptr);
      child = static_cast<Node *>(ats_malloc(sizeof(Node)));
      child->Clear();
      return child;
    }

  private:
    Node *children[N_NODE_CHILDREN];
  };

  Node m_root;
  Queue<T> m_value_list;

  void _CheckArgs(const char *key, int &key_len) const;
  void _Clear(Node *node);

  // make copy-constructor and assignment operator private
  // till we properly implement them
  Trie(const Trie<T> &rhs){};
  Trie &
  operator=(const Trie<T> &rhs)
  {
    return *this;
  }
};

template <typename T>
void
Trie<T>::_CheckArgs(const char *key, int &key_len) const
{
  if (!key) {
    key_len = 0;
  } else if (key_len == -1) {
    key_len = strlen(key);
  }
}

template <typename T>
bool
Trie<T>::Insert(const char *key, T *value, int rank, int key_len /* = -1 */)
{
  _CheckArgs(key, key_len);

  Node *next_node;
  Node *curr_node = &m_root;
  int i           = 0;

  while (true) {
    if (is_debug_tag_set("Trie::Insert")) {
      Debug("Trie::Insert", "Visiting Node...");
      curr_node->Print("Trie::Insert");
    }

    if (i == key_len) {
      break;
    }

    next_node = curr_node->GetChild(key[i]);
    if (!next_node) {
      while (i < key_len) {
        Debug("Trie::Insert", "Creating child node for char %c (%d)", key[i], key[i]);
        curr_node = curr_node->AllocateChild(key[i]);
        ++i;
      }
      break;
    }
    curr_node = next_node;
    ++i;
  }

  if (curr_node->occupied) {
    Debug("Trie::Insert", "Cannot insert duplicate!");
    return false;
  }

  curr_node->occupied = true;
  curr_node->value    = value;
  curr_node->rank     = rank;
  m_value_list.enqueue(curr_node->value);
  Debug("Trie::Insert", "inserted new element!");
  return true;
}

template <typename T>
T *
Trie<T>::Search(const char *key, int key_len /* = -1 */) const
{
  _CheckArgs(key, key_len);

  const Node *found_node = nullptr;
  const Node *curr_node  = &m_root;
  int i                  = 0;

  while (curr_node) {
    if (is_debug_tag_set("Trie::Search")) {
      Debug("Trie::Search", "Visiting node...");
      curr_node->Print("Trie::Search");
    }
    if (curr_node->occupied) {
      if (!found_node || curr_node->rank <= found_node->rank) {
        found_node = curr_node;
      }
    }
    if (i == key_len) {
      break;
    }
    curr_node = curr_node->GetChild(key[i]);
    ++i;
  }

  if (found_node) {
    Debug("Trie::Search", "Returning element with rank %d", found_node->rank);
    return found_node->value;
  }

  return nullptr;
}

template <typename T>
void
Trie<T>::_Clear(Node *node)
{
  Node *child;

  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    child = node->GetChild(i);
    if (child) {
      _Clear(child);
      ats_free(child);
    }
  }
}

template <typename T>
void
Trie<T>::Clear()
{
  T *iter;
  while (nullptr != (iter = m_value_list.pop())) {
    delete iter;
  }

  _Clear(&m_root);
  m_root.Clear();
}

template <typename T>
void
Trie<T>::Print()
{
  // The class we contain must provide a ::Print() method.
  forl_LL(T, iter, m_value_list) iter->Print();
}

template <typename T>
void
Trie<T>::Node::Print(const char *debug_tag) const
{
  if (occupied) {
    Debug(debug_tag, "Node is occupied");
    Debug(debug_tag, "Node has rank %d", rank);
  } else {
    Debug(debug_tag, "Node is not occupied");
  }

  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    if (GetChild(i)) {
      Debug(debug_tag, "Node has child for char %c", static_cast<char>(i));
    }
  }
}
