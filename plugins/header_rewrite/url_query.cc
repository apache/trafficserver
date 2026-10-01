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

#include "swoc/MemSpan.h"
#include "swoc/TextView.h"
#include "swoc/Vectray.h"

namespace
{

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

  swoc::Vectray<std::string_view, 16> params;
  swoc::TextView                      view(query);

  while (view) {
    if (std::string_view param = view.take_prefix_at('&'); !param.empty()) {
      params.push_back(param);
    }
  }

  // Vectray::end() spans all N inline slots, not just the filled ones, so bound ranges by size().
  // Built after the last push_back: a later push can move the storage and leave this span dangling.
  swoc::MemSpan<std::string_view> filled(params.begin(), params.size());

  std::stable_sort(filled.begin(), filled.end(),
                   [](std::string_view a, std::string_view b) { return param_name(a) < param_name(b); });

  std::string result;
  result.reserve(query.size()); // same length as query, capped at 64KB by request_line_max_size

  for (const auto &param : filled) {
    if (!result.empty()) {
      result += '&';
    }
    result.append(param);
  }

  return result;
}

bool
is_query_sorted(std::string_view query)
{
  swoc::TextView   view(query);
  std::string_view prev;

  while (view) {
    std::string_view param = view.take_prefix_at('&');

    if (param.empty() || param_name(param) < param_name(prev)) {
      return false;
    }
    prev = param;
  }

  // take_prefix_at() swallows a trailing '&' without yielding an empty param.
  return query.empty() || query.back() != '&';
}
