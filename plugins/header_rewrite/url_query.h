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
#pragma once

#include <string>
#include <string_view>

// Sort the '&'-separated parameters of a URL query string by parameter
// name. Sorting is stable: parameters that share the same name keep their
// relative order. Empty parameters (from leading, trailing, or consecutive
// '&') are dropped. An empty input returns an empty string.
std::string sort_query(std::string_view query);

// True when sort_query(query) would return query unchanged: parameters are
// already in name order and there are no empty parameters to drop. Callers
// use this to skip the allocation and URL rewrite in the common case.
bool is_query_sorted(std::string_view query);
