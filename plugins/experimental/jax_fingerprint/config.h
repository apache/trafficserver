/** @file

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

#pragma once

#include "ts/ts.h"
#include "method.h"

#include <string>
#include <unordered_set>

enum class Mode : int {
  OVERWRITE,
  KEEP,
  APPEND,
};

enum class PluginType : int {
  GLOBAL,
  REMAP,
};

// This hash function enables looking up the set by a string_view without making a temporal string object.
struct StringHash {
  // Enable heterogeneous lookup
  using is_transparent = void;

  size_t
  operator()(std::string_view sv) const
  {
    return std::hash<std::string_view>{}(sv);
  }
};

struct PluginConfig {
  PluginType    plugin_type     = PluginType::GLOBAL;
  Mode          mode            = Mode::OVERWRITE;
  struct Method method          = {"uninitialized", Method::Type::CONNECTION_BASED, nullptr, nullptr};
  std::string   header_name     = "";
  std::string   via_header_name = "";
  std::string   log_filename    = "";
  int           user_arg_index  = -1;
  TSCont        handler         = nullptr; // For remap plugin
  bool          standalone      = false;
  std::unordered_set<std::string, StringHash, std::equal_to<>> servernames;
  TSTextLogObject                                              log_handle = nullptr;
};
