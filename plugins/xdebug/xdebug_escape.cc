/** @file
 *
 * XDebug plugin JSON escaping implementation.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "xdebug_escape.h"

namespace xdebug
{

std::string_view
EscapeCharForJson::operator()(char const &c)
{
  if ((_state != IN_VALUE) && ((' ' == c) || ('\t' == c))) {
    return {""};
  }
  if ((IN_NAME == _state) && (':' == c)) {
    _state = BEFORE_VALUE;
    if (_full_json) {
      return {R"(":")"};
    } else {
      return {"' : '"};
    }
  }
  if ('\r' == c) {
    return {""};
  }
  if ('\n' == c) {
    std::string_view result{_after_value(_full_json)};

    if (BEFORE_NAME == _state) {
      return {""};
    } else if (BEFORE_VALUE == _state) {
      result = _handle_empty_value(_full_json);
    }
    _state = BEFORE_NAME;
    return result;
  }
  if (BEFORE_NAME == _state) {
    _state = IN_NAME;
  } else if (BEFORE_VALUE == _state) {
    _state = IN_VALUE;
  }
  switch (c) {
  case '"':
    return {"\\\""};
  case '\\':
    return {"\\\\"};
  case '\b':
    return {"\\b"};
  case '\f':
    return {"\\f"};
  case '\t':
    return {"\\t"};
  default:
    return {&c, 1};
  }
}

std::size_t
EscapeCharForJson::backup(bool full_json)
{
  return _after_value(full_json).size() - 1;
}

std::string_view
EscapeCharForJson::_after_value(bool full_json)
{
  if (full_json) {
    return {R"(",")"};
  } else {
    return {"',\n\t'"};
  }
}

std::string_view
EscapeCharForJson::_handle_empty_value(bool full_json)
{
  if (full_json) {
    return {R"(",")"};
  } else {
    return {"',\n\t'"};
  }
}

} // namespace xdebug
