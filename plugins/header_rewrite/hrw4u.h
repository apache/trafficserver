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

// Integration layer for native hrw4u parsing in header_rewrite plugin.

#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>

#include "ts/ts.h"
#include "parser.h"

class RuleSet;
class Condition;
class Operator;

namespace hrw4u_integration
{

bool is_hrw4u_file(std::string_view filename);

struct HRW4UConfig {
  TSHttpHookID default_hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  char        *from_url     = nullptr;
  char        *to_url       = nullptr;
  std::string  filename;
};

struct HRW4UResult {
  bool                                  success = false;
  std::vector<std::unique_ptr<RuleSet>> rulesets;
  std::vector<TSHttpHookID>             hooks;
  std::string                           error_message;

  explicit
  operator bool() const
  {
    return success;
  }
};

HRW4UResult parse_hrw4u_file(const std::string &filename, const HRW4UConfig &config);
HRW4UResult parse_hrw4u_content(std::string_view content, const HRW4UConfig &config);

TSHttpHookID section_to_hook(int section_type);
int          hook_to_section(TSHttpHookID hook);
} // namespace hrw4u_integration
