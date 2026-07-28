/*
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
#include "url_query.h"

#include <algorithm>
#include <vector>

#include "swoc/TextView.h"

namespace
{

std::vector<std::string_view>
split(std::string_view text, char delimiter)
{
  std::vector<std::string_view> tokens;
  swoc::TextView                view(text);

  while (view) {
    tokens.push_back(view.take_prefix_at(delimiter));
  }

  return tokens;
}

std::string_view
param_name(std::string_view param)
{
  return param.substr(0, param.find('='));
}

} // namespace

std::string
sort_query(std::string_view query)
{
  if (query.empty()) {
    return {};
  }

  std::vector<std::string_view> params = split(query, '&');

  std::stable_sort(params.begin(), params.end(),
                   [](std::string_view a, std::string_view b) { return param_name(a) < param_name(b); });

  std::string result;
  result.reserve(query.size()); // same length as query, capped at 64KB by request_line_max_size

  for (const auto &param : params) {
    if (!result.empty()) {
      result += '&';
    }
    result.append(param);
  }

  return result;
}
