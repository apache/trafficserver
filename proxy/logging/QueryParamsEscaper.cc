/** @file

 This file contains a set of utility routines that are used throughout the
 logging implementation.

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

#include "QueryParamsEscaper.h"

using std::list;
using std::string;
using std::vector;

bool QueryParamsEscaper::is_escaping_required_for_url(const char *url, size_t url_len)
{
  if (!_num_targets) {
    return false;
  }
  size_t start;
  for (start = 0; (start < url_len) && (url[start] != '?'); ++start); // find start of query
  if (start == url_len) { // no query
    return false;
  }
  ++start;
  string param_name;
  bool save_value_range = false;
  for (size_t i = start; i < url_len; ++i) {
    if (url[i] == '=') {
      if (i - start) {
        param_name.assign(url + start /* start */, i - start /* num bytes */);
        for (size_t j = 0; j < _num_targets; ++j) {
          if (param_name.find(_targets[j]) != string::npos) {
            save_value_range = true;
            break; // no need to check for other targets
          }
        }
      }
      start = i + 1;
    }
    else if ((url[i] == '&') || (i == url_len - 1) || (url[i] == '#')) { // end of query/value
      if (save_value_range) {
        size_t end = (url[i] == '&') || (url[i] == '#') ? i - 1 : i;
        if (end >= start) {
          _ranges_to_escape.push_back(IndexRange(start, end));
        }
        save_value_range = false;
      }
      start = i + 1;
      if (url[i] == '#') {
        break;
      }
    }
  }
  return !_ranges_to_escape.empty();
}

void QueryParamsEscaper::escape_url(char *mutable_url)
{
  for (list<IndexRange>::iterator iter = _ranges_to_escape.begin(); iter != _ranges_to_escape.end(); ++iter) {
    for (size_t i = iter->first; i <= iter->second; ++i) {
      mutable_url[i] = '*';
    }
  }
}
