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

#ifndef _FETCHER_DOC_NODE_H
#define _FETCHER_DOC_NODE_H

#include <stdint.h>
#include <list>
#include <string>

#include "Attribute.h"

namespace EsiLib
{
class DocNode;

class DocNodeList : public std::list<DocNode>
{
public:
  inline void
  pack(std::string &buffer, bool retain_buffer_data = false) const
  {
    if (!retain_buffer_data) {
      buffer.clear();
    }
    packToBuffer(buffer);
  }

  inline std::string
  pack() const
  {
    std::string buffer("");
    pack(buffer);
    return buffer;
  }

  bool unpack(const char *data, int data_len);

  inline bool
  unpack(const std::string &data)
  {
    return unpack(data.data(), data.size());
  }

private:
  void packToBuffer(std::string &buffer) const;

  friend class DocNode; // to use the method above
};

class DocNode
{
public:
  enum TYPE {
    TYPE_UNKNOWN         = 0,
    TYPE_PRE             = 1,
    TYPE_INCLUDE         = 2,
    TYPE_COMMENT         = 3,
    TYPE_REMOVE          = 4,
    TYPE_VARS            = 5,
    TYPE_CHOOSE          = 6,
    TYPE_WHEN            = 7,
    TYPE_OTHERWISE       = 8,
    TYPE_TRY             = 9,
    TYPE_ATTEMPT         = 10,
    TYPE_EXCEPT          = 11,
    TYPE_HTML_COMMENT    = 12,
    TYPE_SPECIAL_INCLUDE = 13,
  };

  // Use with care - only types defined above will have valid names
  static const char *type_names_[];

  TYPE type;
  const char *data;
  int32_t data_len;

  AttributeList attr_list;

  DocNodeList child_nodes;

  DocNode(TYPE _type = TYPE_UNKNOWN, const char *_data = 0, int32_t _data_len = 0)
    : type(_type), data(_data), data_len(_data_len){};

  void pack(std::string &buffer) const;

  bool unpack(const char *data, int data_len, int &node_len);

private:
  static const char DOCNODE_VERSION = 1;
};
};

#endif // _FETCHER_DOC_NODE_H
