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
#include "ipranges_helper.h"

#include <string_view>
#include <vector>

static std::vector<std::string_view>
splitter(std::string_view input, char delim)
{
  std::vector<std::string_view> output;
  size_t first = 0;

  while (first < input.size()) {
    const auto second = input.find_first_of(delim, first);

    if (first != second) {
      output.emplace_back(input.substr(first, second - first));
    }
    if (second == std::string_view::npos) {
      break;
    }
    first = second + 1;
  }

  return output; // RVO
}

bool
ipRangesHelper::addIpRanges(const std::string &s)
{
  auto ranges = splitter(s, ',');

  for (auto &it : ranges) {
    IpAddr start, end;

    ats_ip_range_parse(it, start, end);
    _ipRanges.mark(start, end);
  }

  if (_ipRanges.count() > 0) {
    TSDebug(PLUGIN_NAME, "    Added %zu IP ranges while parsing", _ipRanges.count());
    return true;
  } else {
    TSDebug(PLUGIN_NAME, "    No IP ranges added, possibly bad input");
    return false;
  }
}
