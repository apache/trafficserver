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

#pragma once

#include <string>

#include "lib/ComponentBase.h"
#include "lib/DocNode.h"

class EsiParser : private EsiLib::ComponentBase
{
public:
  EsiParser(const char *debug_tag, EsiLib::ComponentBase::Debug debug_func, EsiLib::ComponentBase::Error error_func);

  /** clears state */
  void clear();

  /** parses a chunk of the document; adds complete nodes found;
   * data is assumed to be NULL-terminated is data_len is set to -1.
   *
   * Output nodes contain pointers to internal data; use with care */
  bool parseChunk(const char *data, EsiLib::DocNodeList &node_list, int data_len = -1);

  /** convenient alternative to method above */
  bool
  parseChunk(const std::string &data, EsiLib::DocNodeList &node_list)
  {
    return parseChunk(data.data(), node_list, data.size());
  };

  /** completes the parse including data from previous chunk(s);
   * adds newly found nodes to list. optionally a final or the
   * only chunk can be provided.
   *
   * Output nodes contain pointers to internal data; use with care. */
  bool
  completeParse(EsiLib::DocNodeList &node_list, const char *data = nullptr, int data_len = -1)
  {
    return _completeParse(_data, _parse_start_pos, _orig_output_list_size, node_list, data, data_len);
  }

  /** convenient alternative to method above */
  bool
  completeParse(EsiLib::DocNodeList &node_list, const std::string &data)
  {
    return completeParse(node_list, data.data(), data.size());
  }

  /** stateless method that parses data and returns output nodes that
   * point to addresses in caller space */
  bool parse(EsiLib::DocNodeList &node_list, const char *ext_data_ptr, int data_len = -1) const;

  /** convenient alternative to method above */
  bool
  parse(EsiLib::DocNodeList &node_list, const std::string &ext_data) const
  {
    return parse(node_list, ext_data.data(), ext_data.size());
  }

  virtual ~EsiParser();

private:
  struct EsiNodeInfo {
    EsiLib::DocNode::TYPE type;
    const char *tag_suffix;
    int tag_suffix_len;
    const char *closing_tag;
    int closing_tag_len;
    EsiNodeInfo(EsiLib::DocNode::TYPE t, const char *s, int s_len, const char *ct, int ct_len)
      : type(t), tag_suffix(s), tag_suffix_len(s_len), closing_tag(ct), closing_tag_len(ct_len){};
  };

  std::string _data;
  int _parse_start_pos;
  size_t _orig_output_list_size = 0;

  static const EsiNodeInfo ESI_NODES[];
  static const EsiNodeInfo HTML_COMMENT_NODE_INFO;

  static const char *ESI_TAG_PREFIX;
  static const int ESI_TAG_PREFIX_LEN;

  static const std::string SRC_ATTR_STR;
  static const std::string TEST_ATTR_STR;
  static const std::string HANDLER_ATTR_STR;

  static const unsigned int MAX_DOC_SIZE;

  enum MATCH_TYPE {
    NO_MATCH,
    COMPLETE_MATCH,
    PARTIAL_MATCH,
  };

  MATCH_TYPE _searchData(const std::string &data, size_t start_pos, const char *str, int str_len, size_t &pos) const;

  MATCH_TYPE _compareData(const std::string &data, size_t pos, const char *str, int str_len) const;

  MATCH_TYPE _findOpeningTag(const std::string &data, size_t start_pos, size_t &opening_tag_pos, bool &is_html_comment_node) const;

  bool _parse(const std::string &data, int &parse_start_pos, EsiLib::DocNodeList &node_list, bool last_chunk = false) const;

  bool _processIncludeTag(const std::string &data, size_t curr_pos, size_t end_pos, EsiLib::DocNodeList &node_list) const;

  bool _processSpecialIncludeTag(const std::string &data, size_t curr_pos, size_t end_pos, EsiLib::DocNodeList &node_list) const;

  inline bool _isWhitespace(const char *data, int data_len) const;

  bool _processWhenTag(const std::string &data, size_t curr_pos, size_t end_pos, EsiLib::DocNodeList &node_list) const;

  bool _processChooseTag(const std::string &data, size_t curr_pos, size_t end_pos, EsiLib::DocNodeList &node_list) const;

  bool _processTryTag(const std::string &data, size_t curr_pos, size_t end_pos, EsiLib::DocNodeList &node_list) const;

  inline bool _processSimpleContentTag(EsiLib::DocNode::TYPE node_type, const char *data, int data_len,
                                       EsiLib::DocNodeList &node_list) const;

  bool _setup(std::string &data, int &parse_start_pos, size_t &orig_output_list_size, EsiLib::DocNodeList &node_list,
              const char *data_ptr, int &data_len) const;

  bool _completeParse(std::string &data, int &parse_start_pos, size_t &orig_output_list_size, EsiLib::DocNodeList &node_list,
                      const char *data_ptr = nullptr, int data_len = -1) const;

  inline void _adjustPointers(EsiLib::DocNodeList::iterator node_iter, EsiLib::DocNodeList::iterator end, const char *ext_data_ptr,
                              const char *int_data_start) const;
};
