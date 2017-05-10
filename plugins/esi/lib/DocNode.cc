/** @file

  A brief file description

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

#include "DocNode.h"
#include "Utils.h"

using std::string;
using namespace EsiLib;

const char *DocNode::type_names_[] = {"UNKNOWN", "PRE",       "INCLUDE", "COMMENT", "REMOVE", "VARS",         "CHOOSE",
                                      "WHEN",    "OTHERWISE", "TRY",     "ATTEMPT", "EXCEPT", "HTML_COMMENT", "SPECIAL_INCLUDE"};

// helper functions

inline void
packString(const char *str, int32_t str_len, string &buffer)
{
  buffer.append(reinterpret_cast<const char *>(&str_len), sizeof(str_len));
  if (str_len) {
    buffer.append(str, str_len);
  }
}

inline void
unpackString(const char *&packed_data, const char *&item, int32_t &item_len)
{
  item_len = *(reinterpret_cast<const int32_t *>(packed_data));
  packed_data += sizeof(int32_t);
  item = item_len ? packed_data : nullptr;
  packed_data += item_len;
}

template <typename T>
inline void
unpackItem(const char *&packed_data, T &item)
{
  item = *(reinterpret_cast<const T *>(packed_data));
  packed_data += sizeof(T);
}

void
DocNode::pack(string &buffer) const
{
  int32_t orig_buf_size = buffer.size();
  buffer += DOCNODE_VERSION;
  buffer.append(sizeof(int32_t), ' '); // reserve space for length
  buffer.append(reinterpret_cast<const char *>(&type), sizeof(type));
  packString(data, data_len, buffer);
  int32_t n_elements = attr_list.size();
  buffer.append(reinterpret_cast<const char *>(&n_elements), sizeof(n_elements));
  for (const auto &iter : attr_list) {
    packString(iter.name, iter.name_len, buffer);
    packString(iter.value, iter.value_len, buffer);
  }
  child_nodes.packToBuffer(buffer);
  *(reinterpret_cast<int32_t *>(&buffer[orig_buf_size + 1])) = buffer.size() - orig_buf_size;
}

bool
DocNode::unpack(const char *packed_data, int packed_data_len, int &node_len)
{
  const char *packed_data_start = packed_data;

  if (!packed_data || (packed_data_len < static_cast<int>((sizeof(char) + sizeof(int32_t))))) {
    Utils::ERROR_LOG("[%s] Invalid arguments (%p, %d)", __FUNCTION__, packed_data, packed_data_len);
    return false;
  }
  if (*packed_data != DOCNODE_VERSION) {
    Utils::ERROR_LOG("[%s] Version %d not in supported set (%d)", __FUNCTION__, static_cast<int>(*packed_data),
                     static_cast<int>(DOCNODE_VERSION));
    return false;
  }
  ++packed_data;

  int32_t node_size;
  unpackItem(packed_data, node_size);
  if (node_size > packed_data_len) {
    Utils::ERROR_LOG("[%s] Data size (%d) not sufficient to hold node of size %d", __FUNCTION__, packed_data_len, node_size);
    return false;
  }
  node_len = node_size;

  unpackItem(packed_data, type);

  unpackString(packed_data, data, data_len);

  int32_t n_elements;
  unpackItem(packed_data, n_elements);
  Attribute attr;
  attr_list.clear();
  for (int i = 0; i < n_elements; ++i) {
    unpackString(packed_data, attr.name, attr.name_len);
    unpackString(packed_data, attr.value, attr.value_len);
    attr_list.push_back(attr);
  }

  if (!child_nodes.unpack(packed_data, packed_data_len - (packed_data - packed_data_start))) {
    Utils::ERROR_LOG("[%s] Could not unpack child nodes", __FUNCTION__);
    return false;
  }
  return true;
}

void
DocNodeList::packToBuffer(string &buffer) const
{
  int32_t n_elements = size();
  buffer.append(reinterpret_cast<const char *>(&n_elements), sizeof(n_elements));
  for (const auto &iter : *this) {
    iter.pack(buffer);
  }
}

bool
DocNodeList::unpack(const char *data, int data_len)
{
  if (!data || (data_len < static_cast<int>(sizeof(int32_t)))) {
    Utils::ERROR_LOG("[%s] Invalid arguments", __FUNCTION__);
    return false;
  }
  const char *data_start = data;
  int32_t n_elements;
  unpackItem(data, n_elements);
  clear();
  int data_offset = data - data_start, node_size;
  DocNode node;
  for (int i = 0; i < n_elements; ++i) {
    if (!node.unpack(data_start + data_offset, data_len - data_offset, node_size)) {
      Utils::ERROR_LOG("[%s] Could not unpack node", __FUNCTION__);
      return false;
    }
    data_offset += node_size;
    push_back(node);
  }
  return true;
}
