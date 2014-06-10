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

#ifndef TS_PROXY_LOGGING_QUERY_PARAMS_ESCAPER

#define TS_PROXY_LOGGING_QUERY_PARAMS_ESCAPER

#include <list>
#include <string>
#include <vector>

class QueryParamsEscaper
{
public:
  QueryParamsEscaper(const std::vector<std::string> &params_to_hide)
    : _targets(params_to_hide), _num_targets(_targets.size()) { };
  bool is_escaping_required_for_url(const char *immutable_url, size_t url_len);

  // arg should point to mutable version of url tested previously via
  // is_escaping_required_for_url()
  void escape_url(char *mutable_url);

  // resets state about URL currently being worked on
  void reset() { _ranges_to_escape.clear(); }
private:
  const std::vector<std::string> &_targets;
  const size_t _num_targets;
  typedef std::pair<size_t, size_t> IndexRange;
  std::list<IndexRange> _ranges_to_escape;
};

#endif
