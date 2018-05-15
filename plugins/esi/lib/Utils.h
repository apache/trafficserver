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

#ifndef _ESI_UTILS_H

#define _ESI_UTILS_H

#include "DocNode.h"
#include "ComponentBase.h"

#include <cstring>
#include <string>
#include <map>
#include <list>
#include <cctype>

namespace EsiLib
{
namespace Utils
{
  extern ComponentBase::Debug DEBUG_LOG;

  extern ComponentBase::Error ERROR_LOG;

  void init(ComponentBase::Debug debug_func, ComponentBase::Error error_func);

  // looks for the given attribute in given data; a terminator can
  // also be specified apart from the end_pos; attr_info will point
  // to data inside data string
  bool getAttribute(const std::string &data, const std::string &attr, size_t curr_pos, size_t end_pos, Attribute &attr_info,
                    size_t *term_pos = nullptr, char terminator = 0);

  // less specialized version of method above
  inline bool
  getAttribute(const std::string &data, const char *attr, Attribute &attr_info)
  {
    return getAttribute(data, std::string(attr), 0, data.size(), attr_info);
  }

  // trims leading and trailing white space; input arguments
  // will be modified to reflect trimmed data
  inline void
  trimWhiteSpace(const char *&data, int &data_len)
  {
    if (!data) {
      data_len = 0;
    } else {
      if (data_len == -1) {
        data_len = strlen(data);
      }
      int i, j;
      for (i = 0; ((i < data_len) && isspace(data[i])); ++i)
        ;
      for (j = (data_len - 1); ((j > i) && isspace(data[j])); --j)
        ;
      data += i;
      data_len = j - i + 1;
    }
  }

  // does case insensitive comparison
  inline bool
  areEqual(const char *str1, int str1_len, const char *str2, int str2_len)
  {
    if (str1_len == str2_len) {
      return (strncasecmp(str1, str2, str1_len) == 0);
    }
    return false;
  }

  inline bool
  areEqual(const char *str1, int str1_len, const std::string &str2)
  {
    return areEqual(str1, str1_len, str2.data(), static_cast<int>(str2.size()));
  }

  // parses a string of name=value attributes separated by any character in pair_separators;
  void parseAttributes(const char *data, int data_len, AttributeList &attr_list, const char *pair_separators = " ");

  inline void
  parseAttributes(const std::string &data, AttributeList &attr_list, const char *pair_separators = " ")
  {
    parseAttributes(data.data(), data.size(), attr_list, pair_separators);
  }

  typedef std::map<std::string, std::string> KeyValueMap;
  typedef std::list<std::string> HeaderValueList;

  // parses given lines (assumes <key><whitespace><value> format) and
  // stores them in supplied map; Lines beginning with '#' are ignored
  // also if line starts with "whitelistCookie", we store next token in a list
  void parseKeyValueConfig(const std::list<std::string> &lines, KeyValueMap &kvMap, HeaderValueList &whitelistCookies);

  inline std::string
  unescape(const char *str, int len = -1)
  {
    std::string retval("");
    if (str) {
      for (int i = 0; (((len == -1) && (str[i] != '\0')) || (i < len)); ++i) {
        if (str[i] != '\\') {
          retval += str[i];
        }
      }
    }
    return retval;
  }
}; // namespace Utils
}; // namespace EsiLib

#endif
