/** @file

    Utilities for @c std::string_view

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more
    contributor license agreements.  See the NOTICE file distributed with this
    work for additional information regarding copyright ownership.  The ASF
    licenses this file to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
    License for the specific language governing permissions and limitations under
    the License.
*/
#include "tscpp/util/string_view_util.h"

int
memcmp(std::string_view const &lhs, std::string_view const &rhs)
{
  int zret = 0;
  size_t n = rhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the memcmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1;
    n    = lhs.size();
  } else if (lhs.size() > rhs.size()) {
    zret = -1;
  } else if (lhs.data() == rhs.data()) { // same memory, obviously equal.
    return 0;
  }

  int r = ::memcmp(lhs.data(), rhs.data(), n);
  return r ? r : zret;
}

int
strcasecmp(const std::string_view &lhs, const std::string_view &rhs)
{
  int zret = 0;
  size_t n = rhs.size();

  // Seems a bit ugly but size comparisons must be done anyway to get the @c strncasecmp args.
  if (lhs.size() < rhs.size()) {
    zret = 1;
    n    = lhs.size();
  } else if (lhs.size() > rhs.size()) {
    zret = -1;
  } else if (lhs.data() == rhs.data()) { // the same memory, obviously equal.
    return 0;
  }

  int r = ::strncasecmp(lhs.data(), rhs.data(), n);

  return r ? r : zret;
}
