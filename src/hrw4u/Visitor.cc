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

#include "hrw4u/Visitor.h"
#include "hrw4u/HRW4UVisitor.h"
#include "hrw4u/Tables.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace hrw4u
{

bool
ParserContext::consume_mod(const std::string &m)
{
  auto it = std::find(mods.begin(), mods.end(), m);
  if (it != mods.end()) {
    mods.erase(it);
    return true;
  }
  return false;
}

bool
ParserContext::validate_mods() const
{
  return mods.empty();
}

void
ParseResult::cleanup(const DestroyCallback &destroy)
{
  if (!destroy) {
    return;
  }
  for (void *rs : rulesets) {
    if (rs) {
      destroy(rs, "ruleset");
    }
  }
  rulesets.clear();
  sections.clear();
}

ParseResult
parse_hrw4u(std::string_view input, const FactoryCallbacks &callbacks, const ParserConfig &config)
{
  ParseResult result;

  if (!callbacks.valid()) {
    result.errors.error("Invalid factory callbacks provided");
    return result;
  }

  if (input.empty()) {
    result.errors.error("Empty input");
    return result;
  }

  HRW4UVisitor visitor(callbacks, config);

  result.errors.set_filename(config.filename);

  return visitor.parse(input);
}

ParseResult
parse_hrw4u_file(std::string_view filename, const FactoryCallbacks &callbacks, ParserConfig config)
{
  config.filename = std::string(filename);

  HRW4UVisitor visitor(callbacks, config);
  return visitor.parse_file(filename);
}

} // namespace hrw4u
