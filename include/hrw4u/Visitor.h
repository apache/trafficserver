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

#include <memory>
#include <string>
#include <string_view>
#include <functional>
#include <optional>
#include <vector>

#include "hrw4u/Types.h"
#include "hrw4u/Error.h"
#include "hrw4u/ObjTypes.h"

namespace antlr4
{
class ParserRuleContext;
namespace tree
{
  class ParseTree;
}
} // namespace antlr4

namespace hrw4u
{

class SymbolResolver;
struct ParseError;

class ParserContext
{
public:
  ParserContext() = default;

  ParserContext(const ParserContext &)            = delete;
  ParserContext &operator=(const ParserContext &) = delete;
  ParserContext(ParserContext &&)                 = default;
  ParserContext &operator=(ParserContext &&)      = default;

  std::string              op;
  std::string              arg;
  std::string              val;
  std::vector<std::string> mods;
  char                    *from_url  = nullptr;
  char                    *to_url    = nullptr;
  hrw::ConditionType       cond_type = hrw::ConditionType::NONE;
  hrw::OperatorType        op_type   = hrw::OperatorType::NONE;

  bool consume_mod(const std::string &m);
  bool validate_mods() const;

  const std::vector<std::string> &
  get_mods() const
  {
    return mods;
  }
};

enum class CondClause { IF, ELIF, ELSE };

using ConditionFactory            = std::function<void *(const ParserContext &ctx)>;
using OperatorFactory             = std::function<void *(const ParserContext &ctx)>;
using RuleSetFactory              = std::function<void *()>;
using AddConditionCallback        = std::function<bool(void *rule, void *condition)>;
using AddOperatorCallback         = std::function<bool(void *rule, void *op)>;
using AddConditionToIfCallback    = std::function<bool(void *op_if, void *condition)>;
using AddOperatorToIfCallback     = std::function<bool(void *op_if, void *op)>;
using AddConditionToGroupCallback = std::function<bool(void *group, void *condition)>;
using CreateIfOperatorCallback    = std::function<void *()>;
using NewSectionCallback          = std::function<void *(void *op_if, CondClause clause)>;
using NewRuleSetSectionCallback   = std::function<void *(void *ruleset, CondClause clause)>;
using SetRuleSetHookCallback      = std::function<void(void *ruleset, int section_type)>;
using DestroyCallback             = std::function<void(void *ptr, std::string_view type)>;

struct FactoryCallbacks {
  ConditionFactory            create_condition;
  OperatorFactory             create_operator;
  RuleSetFactory              create_ruleset;
  AddConditionCallback        add_condition;
  AddOperatorCallback         add_operator;
  AddConditionToIfCallback    add_condition_to_if;
  AddOperatorToIfCallback     add_operator_to_if;
  AddConditionToGroupCallback add_condition_to_group;
  CreateIfOperatorCallback    create_if_operator;
  NewSectionCallback          new_section;
  NewRuleSetSectionCallback   new_ruleset_section;
  SetRuleSetHookCallback      set_ruleset_hook;
  DestroyCallback             destroy;

  [[nodiscard]] bool
  valid() const
  {
    return create_condition && create_operator && create_ruleset && add_condition && add_operator;
  }
};

struct ParseResult {
  bool                     success = false;
  std::vector<void *>      rulesets;
  std::vector<SectionType> sections;
  ErrorCollector           errors;

  void cleanup(const DestroyCallback &destroy);

  explicit
  operator bool() const
  {
    return success;
  }
};

struct ParserConfig {
  SectionType default_hook = SectionType::READ_RESPONSE;
  bool        strict_mode  = true;
  bool        allow_break  = true;
  std::string filename;
  char       *from_url = nullptr;
  char       *to_url   = nullptr;
};

[[nodiscard]] ParseResult parse_hrw4u(std::string_view input, const FactoryCallbacks &callbacks, const ParserConfig &config);
[[nodiscard]] ParseResult parse_hrw4u_file(std::string_view filename, const FactoryCallbacks &callbacks, ParserConfig config);

} // namespace hrw4u
