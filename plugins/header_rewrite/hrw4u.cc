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

#include "hrw4u.h"

#include <fstream>
#include <sstream>

#include "ts/ts.h"

#include "factory.h"
#include "ruleset.h"
#include "conditions.h"
#include "operators.h"

#ifdef ENABLE_HRW4U_NATIVE
#include "hrw4u/HRW4UVisitor.h"
#include "hrw4u/Visitor.h"
#include "hrw4u/Types.h"
#include "hrw4u/ObjTypes.h"
#endif

namespace hrw4u_integration
{
bool
is_hrw4u_file(std::string_view filename)
{
  constexpr std::string_view suffix = ".hrw4u";

  if (filename.size() < suffix.size()) {
    return false;
  }

  return filename.substr(filename.size() - suffix.size()) == suffix;
}

TSHttpHookID
section_to_hook(int section_type)
{
#ifdef ENABLE_HRW4U_NATIVE
  using hrw4u::SectionType;

  switch (static_cast<SectionType>(section_type)) {
  case SectionType::READ_REQUEST:
    return TS_HTTP_READ_REQUEST_HDR_HOOK;
  case SectionType::SEND_REQUEST:
    return TS_HTTP_SEND_REQUEST_HDR_HOOK;
  case SectionType::READ_RESPONSE:
    return TS_HTTP_READ_RESPONSE_HDR_HOOK;
  case SectionType::SEND_RESPONSE:
    return TS_HTTP_SEND_RESPONSE_HDR_HOOK;
  case SectionType::PRE_REMAP:
    return TS_HTTP_PRE_REMAP_HOOK;
  case SectionType::REMAP:
    return TS_REMAP_PSEUDO_HOOK;
  case SectionType::POST_REMAP:
    return TS_HTTP_POST_REMAP_HOOK;
  case SectionType::TXN_START:
    return TS_HTTP_TXN_START_HOOK;
  case SectionType::TXN_CLOSE:
    return TS_HTTP_TXN_CLOSE_HOOK;
  default:
    return TS_HTTP_READ_RESPONSE_HDR_HOOK;
  }
#else
  return static_cast<TSHttpHookID>(section_type);
#endif
}

int
hook_to_section(TSHttpHookID hook)
{
#ifdef ENABLE_HRW4U_NATIVE
  using hrw4u::SectionType;

  switch (hook) {
  case TS_HTTP_READ_REQUEST_HDR_HOOK:
    return static_cast<int>(SectionType::READ_REQUEST);
  case TS_HTTP_SEND_REQUEST_HDR_HOOK:
    return static_cast<int>(SectionType::SEND_REQUEST);
  case TS_HTTP_READ_RESPONSE_HDR_HOOK:
    return static_cast<int>(SectionType::READ_RESPONSE);
  case TS_HTTP_SEND_RESPONSE_HDR_HOOK:
    return static_cast<int>(SectionType::SEND_RESPONSE);
  case TS_HTTP_PRE_REMAP_HOOK:
    return static_cast<int>(SectionType::PRE_REMAP);
  case TS_REMAP_PSEUDO_HOOK:
    return static_cast<int>(SectionType::REMAP);
  case TS_HTTP_POST_REMAP_HOOK:
    return static_cast<int>(SectionType::POST_REMAP);
  case TS_HTTP_TXN_START_HOOK:
    return static_cast<int>(SectionType::TXN_START);
  case TS_HTTP_TXN_CLOSE_HOOK:
    return static_cast<int>(SectionType::TXN_CLOSE);
  default:
    return static_cast<int>(SectionType::READ_RESPONSE);
  }
#else
  return static_cast<int>(hook);
#endif
}

#ifdef ENABLE_HRW4U_NATIVE

namespace
{
  class FactoryBridge
  {
  public:
    static void *
    create_condition(const hrw4u::ParserContext &ctx)
    {
      std::string cond_spec = ctx.op;

      if (cond_spec.size() > 3 && cond_spec.substr(0, 2) == "%{" && cond_spec.back() == '}') {
        cond_spec = cond_spec.substr(2, cond_spec.size() - 3);
      }

      Condition *cond = condition_factory(cond_spec);

      if (!cond) {
        TSError("[header_rewrite:hrw4u] Failed to create condition: %s", cond_spec.c_str());
        return nullptr;
      }

      Dbg(pi_dbg_ctl, "    Creating condition: %%{%s} with arg: %s", cond_spec.c_str(), ctx.arg.c_str());

      Parser p;

      p.set_op(cond_spec);
      p.set_arg(ctx.arg);
      for (const auto &mod : ctx.mods) {
        p.add_mod(mod);
      }

      try {
        cond->initialize(p);
      } catch (const std::exception &e) {
        TSError("[header_rewrite:hrw4u] Failed to initialize condition %s: %s", cond_spec.c_str(), e.what());
        delete cond;
        return nullptr;
      }

      return cond;
    }

    static void *
    create_operator(const hrw4u::ParserContext &ctx)
    {
      Operator *op = operator_factory(ctx.op_type);

      if (!op) {
        TSError("[header_rewrite:hrw4u] Failed to create operator type %d (factory returned nullptr)",
                static_cast<int>(ctx.op_type));
        return nullptr;
      }

      Dbg(pi_dbg_ctl, "    Adding operator: %s, arg=\"%s\", val=\"%s\"", std::string(hrw::operator_type_name(ctx.op_type)).c_str(),
          ctx.arg.c_str(), ctx.val.c_str());

      Parser p(ctx.from_url, ctx.to_url);

      p.set_op("");
      p.set_arg(ctx.arg);
      p.set_val(ctx.val);
      for (const auto &mod : ctx.mods) {
        p.add_mod(mod);
      }

      try {
        op->initialize(p);
      } catch (const std::exception &e) {
        TSError("[header_rewrite:hrw4u] Failed to initialize operator type %d: %s", static_cast<int>(ctx.op_type), e.what());
        delete op;

        return nullptr;
      }

      return op;
    }

    static void *
    create_ruleset()
    {
      return new RuleSet();
    }

    static bool
    add_condition(void *rule, void *condition)
    {
      if (!rule || !condition) {
        return false;
      }

      auto *ruleset = static_cast<RuleSet *>(rule);
      auto *cond    = static_cast<Condition *>(condition);
      auto *group   = ruleset->get_group();

      if (group) {
        group->add_condition(cond);

        ruleset->require_resources(cond->get_resource_ids());
        return true;
      }

      return false;
    }

    static bool
    add_operator(void *rule, void *op)
    {
      if (!rule || !op) {
        return false;
      }

      auto *ruleset   = static_cast<RuleSet *>(rule);
      auto *operator_ = static_cast<Operator *>(op);

      return ruleset->add_operator(operator_);
    }

    static bool
    add_condition_to_if(void *op_if_ptr, void *condition)
    {
      if (!op_if_ptr || !condition) {
        return false;
      }

      auto *op_if = static_cast<OperatorIf *>(op_if_ptr);
      auto *cond  = static_cast<Condition *>(condition);
      auto *group = op_if->get_group();

      if (group) {
        group->add_condition(cond);

        op_if->require_resources(cond->get_resource_ids());
        return true;
      }

      return false;
    }

    static bool
    add_condition_to_group(void *group_ptr, void *condition)
    {
      if (!group_ptr || !condition) {
        return false;
      }

      auto *group = static_cast<ConditionGroup *>(group_ptr);
      auto *cond  = static_cast<Condition *>(condition);

      group->add_condition(cond);

      return true;
    }

    static bool
    add_operator_to_if(void *op_if_ptr, void *op)
    {
      if (!op_if_ptr || !op) {
        return false;
      }

      auto *op_if     = static_cast<OperatorIf *>(op_if_ptr);
      auto *operator_ = static_cast<Operator *>(op);
      auto *cur_sec   = op_if->cur_section();

      if (!cur_sec) {
        return false;
      }

      if (!cur_sec->ops.oper) {
        cur_sec->ops.oper.reset(operator_);
      } else {
        cur_sec->ops.oper->append(operator_);
      }

      cur_sec->ops.oper_mods = static_cast<OperModifiers>(cur_sec->ops.oper_mods | cur_sec->ops.oper->get_oper_modifiers());
      op_if->require_resources(operator_->get_resource_ids());

      return true;
    }

    static void *
    create_if_operator()
    {
      return new OperatorIf();
    }

    static Parser::CondClause
    to_parser_clause(hrw4u::CondClause clause)
    {
      switch (clause) {
      case hrw4u::CondClause::ELIF:
        return Parser::CondClause::ELIF;
      case hrw4u::CondClause::ELSE:
        return Parser::CondClause::ELSE;
      default:
        return Parser::CondClause::IF;
      }
    }

    static void *
    new_section(void *op_if_ptr, hrw4u::CondClause clause)
    {
      if (!op_if_ptr) {
        return nullptr;
      }
      return static_cast<OperatorIf *>(op_if_ptr)->new_section(to_parser_clause(clause));
    }

    static void *
    new_ruleset_section(void *ruleset_ptr, hrw4u::CondClause clause)
    {
      if (!ruleset_ptr) {
        return nullptr;
      }
      return static_cast<RuleSet *>(ruleset_ptr)->new_section(to_parser_clause(clause));
    }

    static void
    destroy(void *ptr, std::string_view type)
    {
      if (!ptr) {
        return;
      }

      if (type == "condition") {
        delete static_cast<Condition *>(ptr);
      } else if (type == "operator" || type == "operator_if") {
        delete static_cast<Operator *>(ptr);
      } else if (type == "ruleset") {
        delete static_cast<RuleSet *>(ptr);
      }
    }
  };

  hrw4u::FactoryCallbacks
  make_callbacks()
  {
    hrw4u::FactoryCallbacks callbacks;
    callbacks.create_condition       = FactoryBridge::create_condition;
    callbacks.create_operator        = FactoryBridge::create_operator;
    callbacks.create_ruleset         = FactoryBridge::create_ruleset;
    callbacks.add_condition          = FactoryBridge::add_condition;
    callbacks.add_operator           = FactoryBridge::add_operator;
    callbacks.add_condition_to_if    = FactoryBridge::add_condition_to_if;
    callbacks.add_operator_to_if     = FactoryBridge::add_operator_to_if;
    callbacks.add_condition_to_group = FactoryBridge::add_condition_to_group;
    callbacks.create_if_operator     = FactoryBridge::create_if_operator;
    callbacks.new_section            = FactoryBridge::new_section;
    callbacks.new_ruleset_section    = FactoryBridge::new_ruleset_section;
    callbacks.destroy                = FactoryBridge::destroy;

    return callbacks;
  }

} // namespace

HRW4UResult
parse_hrw4u_content(std::string_view content, const HRW4UConfig &config)
{
  HRW4UResult         result;
  hrw4u::ParserConfig parser_config;

  parser_config.default_hook = static_cast<hrw4u::SectionType>(hook_to_section(config.default_hook));
  parser_config.filename     = config.filename;
  parser_config.from_url     = config.from_url;
  parser_config.to_url       = config.to_url;

  hrw4u::HRW4UVisitor visitor(make_callbacks(), parser_config);
  auto                parse_result = visitor.parse(content);

  if (!parse_result.success) {
    std::ostringstream oss;

    result.success = false;
    for (const auto &err : parse_result.errors.errors()) {
      oss << err.format() << "\n";
    }
    result.error_message = oss.str();

    return result;
  }

  result.success = true;
  for (size_t i = 0; i < parse_result.rulesets.size(); ++i) {
    TSHttpHookID hook;
    auto        *ruleset = static_cast<RuleSet *>(parse_result.rulesets[i]);

    result.rulesets.emplace_back(ruleset);
    if (i < parse_result.sections.size()) {
      hook = section_to_hook(static_cast<int>(parse_result.sections[i]));
    } else {
      hook = config.default_hook;
    }
    result.hooks.push_back(hook);
  }

  Dbg(pi_dbg_ctl, "hrw4u: Parsed %zu rulesets from %s", parse_result.rulesets.size(),
      config.filename.empty() ? "<content>" : config.filename.c_str());

  return result;
}

HRW4UResult
parse_hrw4u_file(const std::string &filename, const HRW4UConfig &config)
{
  HRW4UResult   result;
  std::ifstream infile(filename);

  if (!infile.is_open()) {
    result.success       = false;
    result.error_message = "Cannot open file: " + filename;

    return result;
  }

  HRW4UConfig       updated_config = config;
  std::stringstream buffer;

  buffer << infile.rdbuf();
  updated_config.filename = filename;

  return parse_hrw4u_content(buffer.str(), updated_config);
}

#else // !ENABLE_HRW4U_NATIVE

HRW4UResult
parse_hrw4u_content(std::string_view, const HRW4UConfig &)
{
  HRW4UResult result;

  result.success       = false;
  result.error_message = "hrw4u parsing not enabled. Build with ANTLR4 support.";

  return result;
}

HRW4UResult
parse_hrw4u_file(const std::string &, const HRW4UConfig &)
{
  HRW4UResult result;

  result.success       = false;
  result.error_message = "hrw4u parsing not enabled. Build with ANTLR4 support.";

  return result;
}

#endif // ENABLE_HRW4U_NATIVE

} // namespace hrw4u_integration
