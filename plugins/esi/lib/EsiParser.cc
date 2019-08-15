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

#include "EsiParser.h"
#include "Utils.h"

#include <cctype>

using std::string;
using namespace EsiLib;

const char *EsiParser::ESI_TAG_PREFIX   = "<esi:";
const int EsiParser::ESI_TAG_PREFIX_LEN = 5;

const string EsiParser::SRC_ATTR_STR("src");
const string EsiParser::TEST_ATTR_STR("test");
const string EsiParser::HANDLER_ATTR_STR("handler");

const unsigned int EsiParser::MAX_DOC_SIZE = 1024 * 1024;

const EsiParser::EsiNodeInfo EsiParser::ESI_NODES[] = {
  EsiNodeInfo(DocNode::TYPE_INCLUDE, "include", 7, "/>", 2),
  EsiNodeInfo(DocNode::TYPE_REMOVE, "remove>", 7, "</esi:remove>", 13),
  EsiNodeInfo(DocNode::TYPE_COMMENT, "comment", 7, "/>", 2),
  EsiNodeInfo(DocNode::TYPE_VARS, "vars>", 5, "</esi:vars>", 11),
  EsiNodeInfo(DocNode::TYPE_CHOOSE, "choose>", 7, "</esi:choose>", 13),
  EsiNodeInfo(DocNode::TYPE_WHEN, "when", 4, "</esi:when>", 11),
  EsiNodeInfo(DocNode::TYPE_OTHERWISE, "otherwise>", 10, "</esi:otherwise>", 16),
  EsiNodeInfo(DocNode::TYPE_TRY, "try>", 4, "</esi:try>", 10),
  EsiNodeInfo(DocNode::TYPE_ATTEMPT, "attempt>", 8, "</esi:attempt>", 14),
  EsiNodeInfo(DocNode::TYPE_EXCEPT, "except>", 7, "</esi:except>", 13),
  EsiNodeInfo(DocNode::TYPE_SPECIAL_INCLUDE, "special-include", 15, "/>", 2),
  EsiNodeInfo(DocNode::TYPE_UNKNOWN, "", 0, "", 0) // serves as end marker
};

const EsiParser::EsiNodeInfo EsiParser::HTML_COMMENT_NODE_INFO(DocNode::TYPE_HTML_COMMENT, "<!--esi", 7, "-->", 3);

EsiParser::EsiParser(const char *debug_tag, ComponentBase::Debug debug_func, ComponentBase::Error error_func)
  : ComponentBase(debug_tag, debug_func, error_func), _parse_start_pos(-1)
{
  // do this so that object doesn't move around in memory;
  // (because we return pointers into this object)
  _data.reserve(MAX_DOC_SIZE);
}

bool
EsiParser::_setup(string &data, int &parse_start_pos, size_t &orig_output_list_size, DocNodeList &node_list, const char *data_ptr,
                  int &data_len) const
{
  bool retval = true;
  if (!data_ptr || !data_len) {
    _debugLog(_debug_tag, "[%s] Returning true for empty data", __FUNCTION__);
  } else {
    if (data_len == -1) {
      data_len = strlen(data_ptr);
    }
    if ((data.size() + data_len) > MAX_DOC_SIZE) {
      _errorLog("[%s] Cannot allow attempted doc of size %d; Max allowed size is %d", __FUNCTION__, data.size() + data_len,
                MAX_DOC_SIZE);
      retval = false;
    } else {
      data.append(data_ptr, data_len);
    }
  }
  if (parse_start_pos == -1) { // first time this cycle that input is being provided
    parse_start_pos       = 0;
    orig_output_list_size = node_list.size();
  }
  return retval;
}

bool
EsiParser::parseChunk(const char *data, DocNodeList &node_list, int data_len /* = -1 */)
{
  if (!_setup(_data, _parse_start_pos, _orig_output_list_size, node_list, data, data_len)) {
    return false;
  }
  if (!_parse(_data, _parse_start_pos, node_list)) {
    _errorLog("[%s] Failed to parse chunk of size %d starting with [%.5s]...", __FUNCTION__, data_len,
              (data_len ? data : "(null)"));
    return false;
  }
  return true;
}

bool
EsiParser::_completeParse(string &data, int &parse_start_pos, size_t &orig_output_list_size, DocNodeList &node_list,
                          const char *data_ptr /* = 0 */, int data_len /* = -1 */) const
{
  if (!_setup(data, parse_start_pos, orig_output_list_size, node_list, data_ptr, data_len)) {
    return false;
  }
  if (!data.size()) {
    _debugLog(_debug_tag, "[%s] No data to parse!", __FUNCTION__);
    return true;
  }
  if (!_parse(data, parse_start_pos, node_list, true)) {
    _errorLog("[%s] Failed to complete parse of data of total size %d starting with [%.5s]...", __FUNCTION__, data.size(),
              (data.size() ? data.data() : "(null)"));
    node_list.resize(orig_output_list_size);
    return false;
  }
  return true;
}

EsiParser::MATCH_TYPE
EsiParser::_searchData(const string &data, size_t start_pos, const char *str, int str_len, size_t &pos) const
{
  const char *data_ptr = data.data() + start_pos;
  int data_len         = data.size() - start_pos;
  int i_data = 0, i_str = 0;

  while (i_data < data_len) {
    if (data_ptr[i_data] == str[i_str]) {
      ++i_str;
      if (i_str == str_len) {
        break;
      }
    } else {
      i_data -= i_str;
      i_str = 0;
    }
    ++i_data;
  }

  if (i_str == str_len) {
    pos = start_pos + i_data + 1 - i_str;
    _debugLog(_debug_tag, "[%s] Found full match of %.*s in [%.5s...] at position %d", __FUNCTION__, str_len, str, data_ptr, pos);
    return COMPLETE_MATCH;
  } else if (i_str) {
    pos = start_pos + i_data - i_str;
    _debugLog(_debug_tag, "[%s] Found partial match of %.*s in [%.5s...] at position %d", __FUNCTION__, str_len, str, data_ptr,
              pos);
    return PARTIAL_MATCH;
  } else {
    _debugLog(_debug_tag, "[%s] Found no match of %.*s in [%.5s...]", __FUNCTION__, str_len, str, data_ptr);
    return NO_MATCH;
  }
}

EsiParser::MATCH_TYPE
EsiParser::_compareData(const string &data, size_t pos, const char *str, int str_len) const
{
  int i_str     = 0;
  size_t i_data = pos;
  for (; i_data < data.size(); ++i_data) {
    if (data[i_data] == str[i_str]) {
      ++i_str;
      if (i_str == str_len) {
        _debugLog(_debug_tag, "[%s] string [%.*s] is equal to data at position %d", __FUNCTION__, str_len, str, pos);
        return COMPLETE_MATCH;
      }
    } else {
      /*
      _debugLog(_debug_tag, "[%s] string [%.*s] is not equal to data at position %d",
                __FUNCTION__, str_len, str, pos);
      */
      return NO_MATCH;
    }
  }
  _debugLog(_debug_tag, "[%s] string [%.*s] is partially equal to data at position %d", __FUNCTION__, str_len, str, pos);
  return PARTIAL_MATCH;
}

/** This implementation is optimized but not completely correct.  If
 * the opening tag were to have a repeating opening sequence ('<e<esi'
 * or something like that), this will break. However that is not the
 * case for the two opening tags we are looking for */
EsiParser::MATCH_TYPE
EsiParser::_findOpeningTag(const string &data, size_t start_pos, size_t &opening_tag_pos, bool &is_html_comment_node) const
{
  size_t i_data = start_pos;
  int i_esi = 0, i_html_comment = 0;

  while (i_data < data.size()) {
    if (data[i_data] == ESI_TAG_PREFIX[i_esi]) {
      if (++i_esi == ESI_TAG_PREFIX_LEN) {
        is_html_comment_node = false;
        opening_tag_pos      = i_data - i_esi + 1;
        return COMPLETE_MATCH;
      }
    } else {
      if (i_esi) {
        i_esi = 0;
        --i_data; // we do this to reexamine the current char as target string might start from here
        if (i_html_comment) {
          --i_html_comment; // in case other target string has started matching, adjust it's index
        }
      }
    }
    // doing the exact same thing for the other target string
    if (i_html_comment < HTML_COMMENT_NODE_INFO.tag_suffix_len &&
        data[i_data] == HTML_COMMENT_NODE_INFO.tag_suffix[i_html_comment]) {
      if (++i_html_comment == HTML_COMMENT_NODE_INFO.tag_suffix_len && i_data + 1 < data.size()) {
        char ch = data[i_data + 1]; //<!--esi must follow by a space char
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
          is_html_comment_node = true;
          opening_tag_pos      = i_data - i_html_comment + 1;
          return COMPLETE_MATCH;
        }
      }
    } else {
      if (i_html_comment) {
        i_html_comment = 0;
        --i_data; // same comments from above applies
        if (i_esi) {
          --i_esi;
        }
      }
    }
    ++i_data;
  }
  // partial matches; with the nature of our current opening tags, the
  // only way we can have a partial match for both target strings is
  // if the last char of the input string is '<' and that is not
  // enough information to differentiate the tags; Anyway, the parser
  // takes no action for a partial match
  if (i_esi) {
    is_html_comment_node = false;
    opening_tag_pos      = i_data - i_esi;
    return PARTIAL_MATCH;
  }
  if (i_html_comment) {
    is_html_comment_node = true;
    opening_tag_pos      = i_data - i_html_comment;
    return PARTIAL_MATCH;
  }
  return NO_MATCH;
}

inline bool
EsiParser::_processSimpleContentTag(DocNode::TYPE node_type, const char *data, int data_len, DocNodeList &node_list) const
{
  DocNode new_node(node_type);
  if (!parse(new_node.child_nodes, data, data_len)) {
    _errorLog("[%s] Could not parse simple content of [%s] node", __FUNCTION__, DocNode::type_names_[node_type]);
    return false;
  }
  node_list.push_back(new_node);
  return true;
}

bool
EsiParser::_parse(const string &data, int &parse_start_pos, DocNodeList &node_list, bool last_chunk /* = false */) const
{
  size_t orig_list_size = node_list.size();
  size_t curr_pos, end_pos;
  const char *const data_start_ptr = data.data();
  size_t data_size                 = data.size();
  const EsiNodeInfo *node_info;
  MATCH_TYPE search_result;
  bool is_html_comment_node;
  bool parse_result;

  while (parse_start_pos < static_cast<int>(data_size)) {
    search_result = _findOpeningTag(data, static_cast<int>(parse_start_pos), curr_pos, is_html_comment_node);
    if (search_result == NO_MATCH) {
      // we could add this chunk as a PRE node, but it might be
      // possible that the next chunk is also a PRE node, in which
      // case it is more correct to create one PRE node than two PRE
      // nodes even though processing would result in the same final
      // output in either case.  we are sacrificing a little
      // performance (we'll have to parse this chunk again next time)
      // for correctness
      break;
    }
    if (search_result == PARTIAL_MATCH) {
      goto lPartialMatch;
    }

    // we have a complete match of the opening tag
    if ((curr_pos - parse_start_pos) > 0) {
      // add text till here as a PRE node
      _debugLog(_debug_tag, "[%s], Adding data of size %d before (newly found) ESI tag as PRE node", __FUNCTION__,
                curr_pos - parse_start_pos);
      node_list.push_back(DocNode(DocNode::TYPE_PRE, data_start_ptr + parse_start_pos, curr_pos - parse_start_pos));
      parse_start_pos = curr_pos;
    }

    if (is_html_comment_node) {
      _debugLog(_debug_tag, "[%s] Found html comment tag at position %d", __FUNCTION__, curr_pos);
      node_info = &HTML_COMMENT_NODE_INFO;
      ++curr_pos;
    } else {
      curr_pos += ESI_TAG_PREFIX_LEN;

      for (node_info = ESI_NODES; node_info->type != DocNode::TYPE_UNKNOWN; ++node_info) {
        search_result = _compareData(data, curr_pos, node_info->tag_suffix, node_info->tag_suffix_len);
        if (search_result == COMPLETE_MATCH) {
          if (node_info->tag_suffix[node_info->tag_suffix_len - 1] == '>') {
            _debugLog(_debug_tag, "[%s] Found [%s] tag at position %d", __FUNCTION__, DocNode::type_names_[node_info->type],
                      curr_pos - ESI_TAG_PREFIX_LEN);
            break;
          } else {
            if (curr_pos + node_info->tag_suffix_len < data_size) {
              char ch = data_start_ptr[curr_pos + node_info->tag_suffix_len];
              if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
                _debugLog(_debug_tag, "[%s] Found [%s] tag at position %d", __FUNCTION__, DocNode::type_names_[node_info->type],
                          curr_pos - ESI_TAG_PREFIX_LEN);
                ++curr_pos; // skip the space char
                break;
              } else if (ch == '/' || ch == '>') {
                _debugLog(_debug_tag, "[%s] Found [%s] tag at position %d", __FUNCTION__, DocNode::type_names_[node_info->type],
                          curr_pos - ESI_TAG_PREFIX_LEN);
                break;
              }
            } else {
              goto lPartialMatch;
            }
          }
        } else if (search_result == PARTIAL_MATCH) {
          goto lPartialMatch;
        }
      }
      if (node_info->type == DocNode::TYPE_UNKNOWN) {
        _errorLog("[%s] Unknown ESI tag starting with [%10s]...", __FUNCTION__, data.c_str());
        goto lFail;
      }
    }

    curr_pos += node_info->tag_suffix_len;
    search_result = _searchData(data, curr_pos, node_info->closing_tag, node_info->closing_tag_len, end_pos);

    if ((search_result == NO_MATCH) || (search_result == PARTIAL_MATCH)) {
      if (last_chunk) {
        _errorLog("[%s] ESI tag starting with [%10s]... has no matching closing tag [%.*s]", __FUNCTION__, data.c_str(),
                  node_info->closing_tag_len, node_info->closing_tag);
        goto lFail;
      } else {
        goto lPartialMatch;
      }
    }

    // now we process only complete nodes
    switch (node_info->type) {
    case DocNode::TYPE_INCLUDE:
      _debugLog(_debug_tag, "[%s] Handling include tag...", __FUNCTION__);
      parse_result = _processIncludeTag(data, curr_pos, end_pos, node_list);
      break;
    case DocNode::TYPE_COMMENT:
    case DocNode::TYPE_REMOVE:
      _debugLog(_debug_tag, "[%s] Adding node [%s]", __FUNCTION__, DocNode::type_names_[node_info->type]);
      node_list.push_back(DocNode(node_info->type)); // no data required
      parse_result = true;
      break;
    case DocNode::TYPE_WHEN:
      _debugLog(_debug_tag, "[%s] Handling when tag...", __FUNCTION__);
      parse_result = _processWhenTag(data, curr_pos, end_pos, node_list);
      break;
    case DocNode::TYPE_TRY:
      _debugLog(_debug_tag, "[%s] Handling try tag...", __FUNCTION__);
      parse_result = _processTryTag(data, curr_pos, end_pos, node_list);
      break;
    case DocNode::TYPE_CHOOSE:
      _debugLog(_debug_tag, "[%s] Handling choose tag...", __FUNCTION__);
      parse_result = _processChooseTag(data, curr_pos, end_pos, node_list);
      break;
    case DocNode::TYPE_OTHERWISE:
    case DocNode::TYPE_ATTEMPT:
    case DocNode::TYPE_EXCEPT:
      _debugLog(_debug_tag, "[%s] Handling %s tag...", __FUNCTION__, DocNode::type_names_[node_info->type]);
      parse_result = _processSimpleContentTag(node_info->type, data.data() + curr_pos, end_pos - curr_pos, node_list);
      break;
    case DocNode::TYPE_VARS:
    case DocNode::TYPE_HTML_COMMENT:
      _debugLog(_debug_tag, "[%s] added string of size %d starting with [%.5s] for node %s", __FUNCTION__, end_pos - curr_pos,
                data.data() + curr_pos, DocNode::type_names_[node_info->type]);
      node_list.push_back(DocNode(node_info->type, data.data() + curr_pos, end_pos - curr_pos));
      parse_result = true;
      break;
    case DocNode::TYPE_SPECIAL_INCLUDE:
      _debugLog(_debug_tag, "[%s] Handling special include tag...", __FUNCTION__);
      parse_result = _processSpecialIncludeTag(data, curr_pos, end_pos, node_list);
      break;
    default:
      parse_result = false;
      break;
    }

    if (!parse_result) {
      _errorLog("[%s] Cannot handle ESI tag [%.*s]", __FUNCTION__, node_info->tag_suffix_len, node_info->tag_suffix);
      goto lFail;
    }

    parse_start_pos = end_pos + node_info->closing_tag_len;
    continue;

  lPartialMatch:
    if (last_chunk) {
      _debugLog(_debug_tag, "[%s] Found a partial ESI tag - will be treated as PRE text", __FUNCTION__);
    } else {
      _debugLog(_debug_tag, "[%s] Deferring to next chunk to find complete tag", __FUNCTION__);
    }
    break;
  }
  if (last_chunk && (parse_start_pos < static_cast<int>(data_size))) {
    _debugLog(_debug_tag, "[%s] Adding trailing text of size %d starting at [%.5s] as a PRE node", __FUNCTION__,
              data_size - parse_start_pos, data_start_ptr + parse_start_pos);
    node_list.push_back(DocNode(DocNode::TYPE_PRE, data_start_ptr + parse_start_pos, data_size - parse_start_pos));
  }
  _debugLog(_debug_tag, "[%s] Added %d node(s) during parse", __FUNCTION__, node_list.size() - orig_list_size);
  return true;

lFail:
  node_list.resize(orig_list_size); // delete whatever nodes we have added so far
  return false;
}

bool
EsiParser::_processIncludeTag(const string &data, size_t curr_pos, size_t end_pos, DocNodeList &node_list) const
{
  Attribute src_info;
  if (!Utils::getAttribute(data, SRC_ATTR_STR, curr_pos, end_pos, src_info)) {
    _errorLog("[%s] Could not find src attribute", __FUNCTION__);
    return false;
  }
  node_list.push_back(DocNode(DocNode::TYPE_INCLUDE));
  node_list.back().attr_list.push_back(src_info);
  _debugLog(_debug_tag, "[%s] Added include tag with url [%.*s]", __FUNCTION__, src_info.value_len, src_info.value);
  return true;
}

bool
EsiParser::_processSpecialIncludeTag(const string &data, size_t curr_pos, size_t end_pos, DocNodeList &node_list) const
{
  Attribute handler_info;
  if (!Utils::getAttribute(data, HANDLER_ATTR_STR, curr_pos, end_pos, handler_info)) {
    _errorLog("[%s] Could not find handler attribute", __FUNCTION__);
    return false;
  }
  node_list.push_back(DocNode(DocNode::TYPE_SPECIAL_INCLUDE));
  DocNode &node = node_list.back();
  node.attr_list.push_back(handler_info);
  node.data     = data.data() + curr_pos;
  node.data_len = end_pos - curr_pos;
  _debugLog(_debug_tag, "[%s] Added special include tag with handler [%.*s] and data [%.*s]", __FUNCTION__, handler_info.value_len,
            handler_info.value, node.data_len, node.data);
  return true;
}

inline bool
EsiParser::_isWhitespace(const char *data, int data_len) const
{
  for (int i = 0; i < data_len; ++i) {
    if (!isspace(data[i])) {
      return false;
    }
  }
  return true;
}

bool
EsiParser::_processWhenTag(const string &data, size_t curr_pos, size_t end_pos, DocNodeList &node_list) const
{
  Attribute test_expr;
  size_t term_pos;
  if (!Utils::getAttribute(data, TEST_ATTR_STR, curr_pos, end_pos, test_expr, &term_pos, '>')) {
    _errorLog("[%s] Could not find test attribute", __FUNCTION__);
    return false;
  }
  ++term_pos; // go past the terminator
  const char *data_start_ptr = data.data() + term_pos;
  int data_size              = end_pos - term_pos;
  if (!_processSimpleContentTag(DocNode::TYPE_WHEN, data_start_ptr, data_size, node_list)) {
    _errorLog("[%s] Could not parse when node's content", __FUNCTION__);
    return false;
  }
  node_list.back().attr_list.push_back(test_expr);
  _debugLog(_debug_tag, "[%s] Added when tag with expression [%.*s] and data starting with [%.5s]", __FUNCTION__,
            test_expr.value_len, test_expr.value, data_start_ptr);
  return true;
}

bool
EsiParser::_processTryTag(const string &data, size_t curr_pos, size_t end_pos, DocNodeList &node_list) const
{
  const char *data_start_ptr = data.data() + curr_pos;
  int data_size              = end_pos - curr_pos;
  DocNode try_node(DocNode::TYPE_TRY);
  if (!parse(try_node.child_nodes, data_start_ptr, data_size)) {
    _errorLog("[%s] Could not parse try node's content", __FUNCTION__);
    return false;
  }

  DocNodeList::iterator iter, end_node, attempt_node, except_node, temp_iter;
  end_node     = try_node.child_nodes.end();
  attempt_node = except_node = end_node;
  iter                       = try_node.child_nodes.begin();
  while (iter != end_node) {
    if (iter->type == DocNode::TYPE_ATTEMPT) {
      if (attempt_node != end_node) {
        _errorLog("[%s] Can have exactly one attempt node in try block", __FUNCTION__);
        return false;
      }
      attempt_node = iter;
    } else if (iter->type == DocNode::TYPE_EXCEPT) {
      if (except_node != end_node) {
        _errorLog("[%s] Can have exactly one except node in try block", __FUNCTION__);
        return false;
      }
      except_node = iter;
    } else if (iter->type == DocNode::TYPE_PRE) {
      if (!_isWhitespace(iter->data, iter->data_len)) {
        _errorLog("[%s] Cannot have non-whitespace raw text as top level node in try block", __FUNCTION__);
        return false;
      }
      _debugLog(_debug_tag, "[%s] Ignoring top-level whitespace raw text", __FUNCTION__);
      temp_iter = iter;
      ++temp_iter;
      try_node.child_nodes.erase(iter);
      iter = temp_iter;
      continue; // skip the increment
    } else {
      _errorLog("[%s] Only attempt/except/text nodes allowed in try block; [%s] node invalid", __FUNCTION__,
                DocNode::type_names_[iter->type]);
      return false;
    }
    ++iter;
  }
  if ((attempt_node == end_node) || (except_node == end_node)) {
    _errorLog("[%s] try block must contain one each of attempt and except nodes", __FUNCTION__);
    return false;
  }
  node_list.push_back(try_node);
  _debugLog(_debug_tag, "[%s] Added try node successfully", __FUNCTION__);
  return true;
}

bool
EsiParser::_processChooseTag(const string &data, size_t curr_pos, size_t end_pos, DocNodeList &node_list) const
{
  const char *data_start_ptr = data.data() + curr_pos;
  size_t data_size           = end_pos - curr_pos;
  DocNode choose_node(DocNode::TYPE_CHOOSE);
  if (!parse(choose_node.child_nodes, data_start_ptr, data_size)) {
    _errorLog("[%s] Couldn't parse choose node content", __FUNCTION__);
    return false;
  }
  DocNodeList::iterator end_node       = choose_node.child_nodes.end();
  DocNodeList::iterator otherwise_node = end_node, iter, temp_iter;
  iter                                 = choose_node.child_nodes.begin();
  while (iter != end_node) {
    if (iter->type == DocNode::TYPE_OTHERWISE) {
      if (otherwise_node != end_node) {
        _errorLog("[%s] Cannot have more than one esi:otherwise node in an esi:choose node", __FUNCTION__);
        return false;
      }
      otherwise_node = iter;
    } else if (iter->type == DocNode::TYPE_PRE) {
      if (!_isWhitespace(iter->data, iter->data_len)) {
        _errorLog("[%s] Cannot have non-whitespace raw text as top-level node in choose data", __FUNCTION__,
                  DocNode::type_names_[iter->type]);
        return false;
      }
      _debugLog(_debug_tag, "[%s] Ignoring top-level whitespace raw text", __FUNCTION__);
      temp_iter = iter;
      ++temp_iter;
      choose_node.child_nodes.erase(iter);
      iter = temp_iter;
      continue; // skip the increment
    } else if (iter->type != DocNode::TYPE_WHEN) {
      _errorLog("[%s] Cannot have %s as top-level node in choose data; only when/otherwise/whitespace-text "
                "permitted",
                __FUNCTION__, DocNode::type_names_[iter->type]);
      return false;
    }
    ++iter;
  }
  node_list.push_back(choose_node);
  return true;
}

void
EsiParser::clear()
{
  _data.clear();
  _parse_start_pos = -1;
}

EsiParser::~EsiParser() = default;

inline void
EsiParser::_adjustPointers(DocNodeList::iterator node_iter, DocNodeList::iterator end, const char *ext_data_ptr,
                           const char *int_data_start) const
{
  AttributeList::iterator attr_iter;
  for (; node_iter != end; ++node_iter) {
    if (node_iter->data_len) {
      node_iter->data = ext_data_ptr + (node_iter->data - int_data_start);
    }
    for (attr_iter = node_iter->attr_list.begin(); attr_iter != node_iter->attr_list.end(); ++attr_iter) {
      if (attr_iter->name_len) {
        attr_iter->name = ext_data_ptr + (attr_iter->name - int_data_start);
      }
      if (attr_iter->value_len) {
        attr_iter->value = ext_data_ptr + (attr_iter->value - int_data_start);
      }
    }
    if (node_iter->child_nodes.size()) {
      _adjustPointers(node_iter->child_nodes.begin(), node_iter->child_nodes.end(), ext_data_ptr, int_data_start);
    }
  }
}

bool
EsiParser::parse(DocNodeList &node_list, const char *ext_data_ptr, int data_len /* = -1 */) const
{
  string data;
  size_t orig_output_list_size;
  int parse_start_pos = -1;
  bool retval         = _completeParse(data, parse_start_pos, orig_output_list_size, node_list, ext_data_ptr, data_len);
  if (retval && (node_list.size() - orig_output_list_size)) {
    // adjust all pointers to addresses in input parameter
    const char *int_data_start      = data.data();
    DocNodeList::iterator node_iter = node_list.begin();
    for (size_t i = 0; i < orig_output_list_size; ++i, ++node_iter) {
      ;
    }
    _adjustPointers(node_iter, node_list.end(), ext_data_ptr, int_data_start);
  }
  return retval;
}
