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

#include "Utils.h"

#include <sstream>

using namespace EsiLib;

ComponentBase::Debug Utils::DEBUG_LOG(0);
ComponentBase::Error Utils::ERROR_LOG(0);

#define DEBUG_TAG "EsiUtils"

using std::string;

void
Utils::init(ComponentBase::Debug debug_func, ComponentBase::Error error_func)
{
  DEBUG_LOG = debug_func;
  ERROR_LOG = error_func;
}

bool
Utils::getAttribute(const string &data, const string &attr, size_t curr_pos, size_t end_pos, Attribute &attr_info,
                    size_t *term_pos /* = 0 */, char terminator /* = 0 */)
{
  size_t attr_start = data.find(attr, curr_pos);
  if (attr_start >= end_pos) {
    ERROR_LOG("[%s] Tag has no [%.*s] attribute", __FUNCTION__, attr.size(), attr.data());
    return false;
  }
  curr_pos          = attr_start + attr.size();
  bool equals_found = false;
  for (; curr_pos < end_pos; ++curr_pos) {
    if (data[curr_pos] == ' ') {
      continue;
    } else {
      if (data[curr_pos] == '=') {
        equals_found = true;
      }
      break;
    }
  }
  if (!equals_found) {
    ERROR_LOG("[%s] Attribute [%.*s] has no value", __FUNCTION__, attr.size(), attr.data());
    return false;
  }
  ++curr_pos;
  if (curr_pos == end_pos) {
    ERROR_LOG("[%s] No space for value after [%.*s] attribute", __FUNCTION__, attr.size(), attr.data());
    return false;
  }
  bool in_quoted_part = false;
  bool quoted         = false;
  size_t i;
  for (i = curr_pos; i < end_pos; ++i) {
    if (data[i] == '"') {
      quoted         = true;
      in_quoted_part = !in_quoted_part;
    } else if (data[i] == ' ') {
      if (!in_quoted_part) {
        break;
      }
    } else if (terminator && !in_quoted_part && (data[i] == terminator)) {
      break;
    }
  }
  const char *data_start_ptr = data.data();
  if (in_quoted_part) {
    ERROR_LOG("[%s] Unterminated quote in value for attribute [%.*s] starting at [%.10s]", __FUNCTION__, attr.size(), attr.data(),
              data_start_ptr + curr_pos);
    return false;
  }
  if (terminator && term_pos) {
    *term_pos = data.find(terminator, i);
    if (*term_pos >= end_pos) {
      ERROR_LOG("[%s] Unterminated attribute [%.*s]", __FUNCTION__, attr.size(), attr.data());
      return false;
    }
  }
  attr_info.name      = data_start_ptr + attr_start;
  attr_info.name_len  = attr.size();
  attr_info.value     = data_start_ptr + curr_pos;
  attr_info.value_len = i - curr_pos;
  if (quoted) {
    ++attr_info.value;
    attr_info.value_len -= 2;
  }
  return true;
}

void
Utils::parseKeyValueConfig(const std::list<string> &lines, KeyValueMap &kvMap)
{
  string key, value;
  std::istringstream iss;
  for (std::list<string>::const_iterator list_iter = lines.begin(); list_iter != lines.end(); ++list_iter) {
    const string &conf_line = *list_iter; // handy reference
    if (!conf_line.size() || (conf_line[0] == '#')) {
      continue;
    }
    iss.clear();
    iss.str(conf_line);
    if (iss.good()) {
      iss >> key;
      iss >> value;
      if (key.size() && value.size()) {
        kvMap.insert(KeyValueMap::value_type(key, value));
        DEBUG_LOG(DEBUG_TAG, "[%s] Read value [%s] for key [%s]", __FUNCTION__, value.c_str(), key.c_str());
      }
    }
    key.clear();
    value.clear();
  }
}

void
Utils::parseAttributes(const char *data, int data_len, AttributeList &attr_list, const char *pair_separators /* = " " */)
{
  attr_list.clear();
  if (!data || (data_len <= 0)) {
    return;
  }
  char separator_lookup[256] = {0};
  int i;
  for (i = 0; pair_separators[i]; ++i) {
    separator_lookup[static_cast<unsigned int>(pair_separators[i])] = 1;
  }
  Attribute attr;
  bool inside_quotes = false, end_of_attribute;
  bool escape_on     = false;
  for (i = 0; (i < data_len) && ((isspace(data[i]) || separator_lookup[static_cast<unsigned int>(data[i])])); ++i)
    ;
  attr.name  = data + i;
  attr.value = 0;
  for (; i <= data_len; ++i) {
    end_of_attribute = false;
    if (i == data_len) {
      end_of_attribute = true;
    } else if (separator_lookup[static_cast<unsigned int>(data[i])] && !inside_quotes) {
      end_of_attribute = true;
    } // else ignore separator when in quotes
    if (end_of_attribute) {
      if (!inside_quotes) {
        if (attr.value > attr.name) {
          attr.value_len = data + i - attr.value;
          trimWhiteSpace(attr.name, attr.name_len);
          trimWhiteSpace(attr.value, attr.value_len);
          if (attr.value[0] == '"') {
            ++attr.value;
            attr.value_len -= 2;
          }
          if (attr.name_len && attr.value_len) {
            DEBUG_LOG(DEBUG_TAG, "[%s] Added attribute with name [%.*s] and value [%.*s]", __FUNCTION__, attr.name_len, attr.name,
                      attr.value_len, attr.value);
            attr_list.push_back(attr);
          } // else ignore empty name/value
        }   // else ignore attribute with no value
      }     // else ignore variable with unterminated quotes
      for (; (i < data_len) && ((isspace(data[i]) || separator_lookup[static_cast<unsigned int>(data[i])])); ++i)
        ;
      attr.name     = data + i;
      attr.value    = 0;
      inside_quotes = false;
    } else if (data[i] == '"') {
      if (!escape_on) {
        inside_quotes = !inside_quotes;
      }
    } else if ((data[i] == '=') && !attr.value && !inside_quotes) {
      attr.value    = data + i + 1;
      attr.name_len = data + i - attr.name;
    }
    escape_on = (data[i] == '\\') ? true : false;
  }
}
