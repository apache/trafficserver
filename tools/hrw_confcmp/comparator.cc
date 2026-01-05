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

#include "comparator.h"
#include "ruleset.h"
#include "condition.h"
#include "operator.h"
#include "operators.h"
#include "conditions.h"
#include "matcher.h"

#include <iostream>
#include <sstream>

#include "hrw4u.h"
#include "hrw4u/Types.h"

namespace ConfigComparison
{

namespace
{
  // Helper to format hook ID as hrw4u section name for display
  const char *
  get_hrw4u_section_name(TSHttpHookID hook)
  {
    static char buf[64];
    int         section_type = hrw4u_integration::hook_to_section(hook);
    auto        section      = static_cast<hrw4u::SectionType>(section_type);
    auto        sv           = hrw4u::section_type_to_string(section);
    size_t      len          = sv.size();

    if (len >= sizeof(buf)) {
      len = sizeof(buf) - 1;
    }
    for (size_t i = 0; i < len; ++i) {
      buf[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(sv[i])));
    }
    buf[len] = '\0';
    return buf;
  }

  template <typename T, typename NextFn>
  int
  count_chain(T *head, NextFn next_fn)
  {
    int count = 0;

    for (auto *p = head; p; p = next_fn(p)) {
      count++;
    }

    return count;
  }

  inline CondModifiers
  mask_cond_modifier(CondModifiers mods, CondModifiers to_remove)
  {
    return static_cast<CondModifiers>(static_cast<unsigned>(mods) & ~static_cast<unsigned>(to_remove));
  }

  inline OperModifiers
  mask_oper_modifier(OperModifiers mods, OperModifiers to_remove)
  {
    return static_cast<OperModifiers>(static_cast<int>(mods) & ~static_cast<int>(to_remove));
  }
} // namespace

void
ComparisonResult::add_diff(const std::string &context, const std::string &msg)
{
  success              = false;
  std::string full_msg = context + ": " + msg;
  differences.push_back(full_msg);

  std::cout << "  ERROR: " << full_msg << "\n";
  total_comparisons++;
}

void
ComparisonResult::add_diff(const std::string &context, const std::string &msg, const std::string &expected, const std::string &got)
{
  success              = false;
  std::string full_msg = context + ": " + msg;
  differences.push_back(full_msg);

  std::cout << "  ERROR: " << full_msg << "\n";
  std::cout << "    Expected (hrw):  " << expected << "\n";
  std::cout << "    Got (hrw4u):     " << got << "\n";
  total_comparisons++;
}

void
ComparisonResult::add_success(const std::string & /* context */)
{
  successful_compares++;
  total_comparisons++;
}

bool
ConfigComparator::compare_rulesets_for_hook(RuleSet *rs1, RuleSet *rs2, TSHttpHookID hook)
{
  std::string context = std::string("Hook[") + TSHttpHookNameLookup(hook) + "]";

  if (!rs1 && !rs2) {
    return true;
  }

  if (!rs1 || !rs2) {
    if (!rs1) {
      _result.add_diff(context, "hrw config has NO rules for this hook, but hrw4u config DOES have rules");
    } else {
      _result.add_diff(context, "hrw config HAS rules for this hook, but hrw4u config DOES NOT");
    }
    return false;
  }

  return compare_ruleset_chain(rs1, rs2, context);
}

bool
ConfigComparator::compare_ruleset_chain(RuleSet *rs1, RuleSet *rs2, const std::string &context)
{
  int  index     = 0;
  bool all_match = true;

  int count1 = count_chain(rs1, [](RuleSet *r) { return r->next.get(); });
  int count2 = count_chain(rs2, [](RuleSet *r) { return r->next.get(); });

  while (rs1 || rs2) {
    std::string ctx = context + ".RuleSet[" + std::to_string(index) + "]";

    if (!rs1 || !rs2) {
      std::ostringstream oss;
      oss << "RuleSet chain length mismatch: expected " << count1 << " rulesets, got " << count2;
      _result.add_diff(context, oss.str());
      return false;
    }

    if (!compare_single_ruleset(rs1, rs2, ctx)) {
      all_match = false;
    }

    rs1 = rs1->next.get();
    rs2 = rs2->next.get();
    index++;
  }

  return all_match;
}

bool
ConfigComparator::compare_single_ruleset(RuleSet *rs1, RuleSet *rs2, const std::string &context)
{
  bool all_match = true;

  if (_debug) {
    debug_print_ruleset(rs1, context + " OLD");
    debug_print_ruleset(rs2, context + " NEW");
  }

  if (rs1->get_hook() != rs2->get_hook()) {
    std::ostringstream oss;
    oss << "Hook mismatch: expected " << TSHttpHookNameLookup(rs1->get_hook()) << ", got " << TSHttpHookNameLookup(rs2->get_hook());
    _result.add_diff(context, oss.str());
    all_match = false;
  }

  if (rs1->get_resource_ids() != rs2->get_resource_ids()) {
    std::ostringstream oss;
    oss << "Resource IDs differ: expected 0x" << std::hex << static_cast<unsigned>(rs1->get_resource_ids()) << ", got 0x"
        << static_cast<unsigned>(rs2->get_resource_ids()) << std::dec;
    _result.add_diff(context, oss.str());
    all_match = false;
  }

  ConditionGroup *g1                  = rs1->get_group();
  ConditionGroup *g2                  = rs2->get_group();
  bool            has_cond1           = g1 && g1->has_conditions();
  bool            has_cond2           = g2 && g2->has_conditions();
  bool            conditions_deferred = false;

  if (has_cond1 && has_cond2) {
    if (!compare_statement_chains(g1->get_conditions(), g2->get_conditions(), context + ".conditions")) {
      all_match = false;
    }
  } else if (has_cond1 || has_cond2) {
    conditions_deferred = true;
    Dbg(dbg_ctl, "Top-level condition mismatch, will check in OperatorIf sections");
  }

  const OperatorIf *op_if1 = rs1->get_operator_if();
  const OperatorIf *op_if2 = rs2->get_operator_if();

  if (op_if1 && op_if2) {
    if (!compare_operator_if_sections(op_if1, op_if2, context + ".OperatorIf")) {
      all_match = false;
    }
  } else if (op_if1 || op_if2) {
    _result.add_diff(context, "One RuleSet has OperatorIf structure but the other does not");
    all_match = false;
  } else if (conditions_deferred) {
    if (has_cond1) {
      _result.add_diff(context, "hrw has top-level conditions but hrw4u has none");
    } else {
      _result.add_diff(context, "hrw4u has top-level conditions but hrw has none");
    }
    all_match = false;
  }

  if (all_match) {
    _result.add_success(context);
  }

  return all_match;
}

bool
ConfigComparator::compare_operator_if_sections(const OperatorIf *op1, const OperatorIf *op2, const std::string &context)
{
  const auto *sec1      = op1->get_sections();
  const auto *sec2      = op2->get_sections();
  int         sec_index = 0;
  bool        all_match = true;
  int         count1 = 0, count2 = 0;

  for (auto *s = sec1; s; s = s->next.get()) {
    count1++;
  }
  for (auto *s = sec2; s; s = s->next.get()) {
    count2++;
  }

  while (sec1 || sec2) {
    std::string ctx = context + ".Section[" + std::to_string(sec_index) + "]";

    if (!sec1 || !sec2) {
      std::ostringstream oss;

      oss << "OperatorIf section count mismatch: expected " << count1 << " sections, got " << count2;
      _result.add_diff(context, oss.str());

      return false;
    }

    Statement *cond1 = sec1->group.get_conditions();
    Statement *cond2 = sec2->group.get_conditions();
    Operator  *oper1 = sec1->ops.oper.get();
    Operator  *oper2 = sec2->ops.oper.get();

    if (!cond1 && oper1 && oper1->type_name() == "OperatorIf" && !oper1->next()) {
      auto       *nested_op  = static_cast<OperatorIf *>(oper1);
      const auto *nested_sec = nested_op->get_sections();

      if (nested_sec && !nested_sec->next.get()) {
        cond1 = nested_sec->group.get_conditions();
        oper1 = nested_sec->ops.oper.get();
      }
    }

    if (!cond2 && oper2 && oper2->type_name() == "OperatorIf" && !oper2->next()) {
      auto       *nested_op  = static_cast<OperatorIf *>(oper2);
      const auto *nested_sec = nested_op->get_sections();

      if (nested_sec && !nested_sec->next.get()) {
        cond2 = nested_sec->group.get_conditions();
        oper2 = nested_sec->ops.oper.get();
      }
    }

    if (!compare_statement_chains(cond1, cond2, ctx + ".conditions")) {
      all_match = false;
    }

    if (oper1 && oper2) {
      if (!compare_statement_chains(oper1, oper2, ctx + ".operators")) {
        all_match = false;
      }
    } else if (oper1 || oper2) {
      if (!oper1) {
        _result.add_diff(ctx, "Expected operators present, but new config section has none");
      } else {
        _result.add_diff(ctx, "Expected no operators, but new config section has some");
      }
      all_match = false;
    }

    sec1 = sec1->next.get();
    sec2 = sec2->next.get();
    sec_index++;
  }

  return all_match;
}

bool
ConfigComparator::compare_statement_chains(Statement *s1, Statement *s2, const std::string &context)
{
  int                      index     = 0;
  bool                     all_match = true;
  int                      count1 = 0, count2 = 0;
  std::vector<std::string> types1, types2;

  for (Statement *s = s1; s; s = s->next()) {
    count1++;
    types1.push_back(std::string(s->type_name()));
  }
  for (Statement *s = s2; s; s = s->next()) {
    count2++;
    types2.push_back(std::string(s->type_name()));
  }

  while (s1 || s2) {
    std::string ctx = context + "[" + std::to_string(index) + "]";

    if (!s1 || !s2) {
      std::ostringstream oss;

      oss << "Statement chain length mismatch: expected " << count1 << " statements, got " << count2;
      _result.add_diff(context, oss.str());

      std::cout << "    Expected chain: ";
      for (size_t i = 0; i < types1.size(); i++) {
        std::cout << types1[i] << (i < types1.size() - 1 ? " -> " : "");
      }
      std::cout << "\n";

      std::cout << "    Got chain:      ";
      for (size_t i = 0; i < types2.size(); i++) {
        std::cout << types2[i] << (i < types2.size() - 1 ? " -> " : "");
      }
      std::cout << "\n";

      return false;
    }

    if (!compare_single_statement(s1, s2, ctx)) {
      all_match = false;
    }

    s1 = s1->next();
    s2 = s2->next();
    index++;
  }

  return all_match;
}

bool
ConfigComparator::compare_single_statement(Statement *s1, Statement *s2, const std::string &context)
{
  if (s1->type_name() != s2->type_name()) {
    std::ostringstream oss;

    oss << "Statement type mismatch: expected '" << s1->type_name() << "', got '" << s2->type_name() << "'";
    _result.add_diff(context, oss.str());

    return false;
  }

  if (s1->type_name() == "OperatorIf") {
    auto *op_if1 = static_cast<OperatorIf *>(s1);
    auto *op_if2 = static_cast<OperatorIf *>(s2);

    return compare_operator_if_sections(op_if1, op_if2, context + ".OperatorIf");
  }

  bool  semantic_match = true;
  auto *cond1          = dynamic_cast<Condition *>(s1);
  auto *cond2          = dynamic_cast<Condition *>(s2);

  if (cond1 && cond2) {
    CondModifiers mods1_masked = mask_cond_modifier(cond1->mods(), CondModifiers::AND);
    CondModifiers mods2_masked = mask_cond_modifier(cond2->mods(), CondModifiers::AND);

    if (cond1->get_qualifier() != cond2->get_qualifier() || cond1->get_cond_op() != cond2->get_cond_op() ||
        mods1_masked != mods2_masked) {
      semantic_match = false;
    }
    if (cond1->get_matcher() && cond2->get_matcher()) {
      if (cond1->get_matcher()->op() != cond2->get_matcher()->op()) {
        semantic_match = false;
      }
    } else if ((cond1->get_matcher() == nullptr) != (cond2->get_matcher() == nullptr)) {
      semantic_match = false;
    }
  } else {
    auto *op1 = dynamic_cast<Operator *>(s1);
    auto *op2 = dynamic_cast<Operator *>(s2);

    if (op1 && op2) {
      OperModifiers mods1_masked = mask_oper_modifier(op1->get_oper_modifiers(), OPER_LAST);
      OperModifiers mods2_masked = mask_oper_modifier(op2->get_oper_modifiers(), OPER_LAST);
      auto         *redirect1    = dynamic_cast<OperatorSetRedirect *>(s1);
      auto         *redirect2    = dynamic_cast<OperatorSetRedirect *>(s2);

      if (redirect1 && redirect2) {
        const std::string query_suffix = "?%{CLIENT-URL:QUERY}";
        bool              op1_has_qsa  = (mods1_masked & OPER_QSA) != 0;
        bool              op2_has_qsa  = (mods2_masked & OPER_QSA) != 0;
        std::string       loc1         = redirect1->get_location();
        std::string       loc2         = redirect2->get_location();

        if (op1_has_qsa && !op2_has_qsa) {
          if (loc2.size() >= query_suffix.size() && loc2.substr(loc2.size() - query_suffix.size()) == query_suffix) {
            mods2_masked = static_cast<OperModifiers>(mods2_masked | OPER_QSA);
            loc2         = loc2.substr(0, loc2.size() - query_suffix.size());
          }
        } else if (!op1_has_qsa && op2_has_qsa) {
          if (loc1.size() >= query_suffix.size() && loc1.substr(loc1.size() - query_suffix.size()) == query_suffix) {
            mods1_masked = static_cast<OperModifiers>(mods1_masked | OPER_QSA);
            loc1         = loc1.substr(0, loc1.size() - query_suffix.size());
          }
        }

        if (mods1_masked != mods2_masked || redirect1->get_status() != redirect2->get_status() || loc1 != loc2) {
          semantic_match = false;
        }
      } else {
        if (mods1_masked != mods2_masked) {
          semantic_match = false;
        }
        if (!op1->equals(op2)) {
          semantic_match = false;
        }
      }
    } else {
      semantic_match = s1->equals(s2);
    }
  }

  if (semantic_match) {
    _result.add_success(context + "." + std::string(s1->type_name()));
    return true;
  } else {
    if (_debug) {
      std::cerr << "DEBUG: Statement comparison failed for " << s1->type_name() << "\n";
      std::cerr << "  Statement 1: hook=" << TSHttpHookNameLookup(s1->get_hook()) << ", rsrc=0x" << std::hex
                << static_cast<unsigned>(s1->get_resource_ids()) << std::dec << "\n";
      std::cerr << "  Statement 2: hook=" << TSHttpHookNameLookup(s2->get_hook()) << ", rsrc=0x" << std::hex
                << static_cast<unsigned>(s2->get_resource_ids()) << std::dec << "\n";

      if (cond1 && cond2) {
        unsigned mods1 = static_cast<unsigned>(cond1->mods());
        unsigned mods2 = static_cast<unsigned>(cond2->mods());

        std::cerr << "  Condition 1: op=" << static_cast<int>(cond1->get_cond_op()) << ", qualifier='" << cond1->get_qualifier()
                  << "', mods=" << mods1 << " (" << cond_modifiers_to_string(cond1->mods()) << ")\n";
        std::cerr << "  Condition 2: op=" << static_cast<int>(cond2->get_cond_op()) << ", qualifier='" << cond2->get_qualifier()
                  << "', mods=" << mods2 << " (" << cond_modifiers_to_string(cond2->mods()) << ")\n";

        if (cond1->get_matcher() && cond2->get_matcher()) {
          std::cerr << "  Matcher 1: op=" << static_cast<int>(cond1->get_matcher()->op()) << "\n";
          std::cerr << "  Matcher 2: op=" << static_cast<int>(cond2->get_matcher()->op()) << "\n";
        } else {
          std::cerr << "  Matcher 1: " << (cond1->get_matcher() ? "present" : "nullptr") << "\n";
          std::cerr << "  Matcher 2: " << (cond2->get_matcher() ? "present" : "nullptr") << "\n";
        }
      }

      auto *op1 = dynamic_cast<Operator *>(s1);
      auto *op2 = dynamic_cast<Operator *>(s2);

      if (op1 && op2) {
        std::cerr << "  Operator 1: mods=" << static_cast<int>(op1->get_oper_modifiers()) << "\n";
        std::cerr << "  Operator 2: mods=" << static_cast<int>(op2->get_oper_modifiers()) << "\n";
      }
    }

    std::string msg = std::string(s1->type_name()) + " value mismatch";
    _result.add_diff(context, msg, s1->debug_string(), s2->debug_string());
    return false;
  }
}

void
ConfigComparator::debug_print_ruleset(RuleSet *rs, const std::string &label)
{
  if (!rs) {
    std::cerr << "DEBUG: " << label << ": nullptr\n";
    return;
  }

  std::cerr << "DEBUG: " << label << " RuleSet:\n";
  std::cerr << "  Hook: " << TSHttpHookNameLookup(rs->get_hook()) << "\n";
  std::cerr << "  Resource IDs: 0x" << std::hex << static_cast<unsigned>(rs->get_resource_ids()) << std::dec << "\n";

  ConditionGroup *g = rs->get_group();
  if (g) {
    std::cerr << "  Condition Group: present\n";
    Statement *cond  = g->get_conditions();
    int        count = 0;
    std::cerr << "  Conditions: ";
    while (cond) {
      std::cerr << cond->type_name() << " ";
      count++;
      cond = cond->next();
    }
    std::cerr << "(" << count << " total)\n";
  } else {
    std::cerr << "  Condition Group: nullptr\n";
  }

  const OperatorIf *op_if = rs->get_operator_if();
  if (op_if) {
    const auto *sections = op_if->get_sections();
    int         sec_num  = 0;

    std::cerr << "  OperatorIf sections:\n";
    for (const auto *sec = sections; sec; sec = sec->next.get(), sec_num++) {
      Statement *sec_cond   = sec->group.get_conditions();
      int        cond_count = 0;

      std::cerr << "    Section[" << sec_num << "]:\n";
      std::cerr << "      Conditions: ";
      while (sec_cond) {
        std::cerr << sec_cond->type_name() << " ";
        cond_count++;
        sec_cond = sec_cond->next();
      }
      std::cerr << "(" << cond_count << " total)\n";

      if (sec->ops.oper) {
        int op_count = 0;

        std::cerr << "      Operators: ";
        for (Operator *op = sec->ops.oper.get(); op; op = static_cast<Operator *>(op->next())) {
          std::cerr << op->type_name() << " ";
          op_count++;
        }
        std::cerr << "(" << op_count << " total)\n";
      } else {
        std::cerr << "      Operators: (0 total)\n";
      }
    }
  }
}

void
ConfigComparator::set_debug(bool debug)
{
  _debug = debug;
}

std::string
ParseStats::format_hooks() const
{
  std::string result;

  for (auto hook : hooks) {
    if (!result.empty()) {
      result += ", ";
    }
    result += is_hrw4u ? get_hrw4u_section_name(hook) : TSHttpHookNameLookup(hook);
  }

  return result.empty() ? "(none)" : result;
}

ParseStats
ConfigComparator::collect_stats(RuleSet *config)
{
  ParseStats stats;

  count_ruleset_stats(config, stats);
  return stats;
}

void
ConfigComparator::count_ruleset_stats(RuleSet *rs, ParseStats &stats)
{
  while (rs) {
    ConditionGroup *group = rs->get_group();

    stats.rulesets++;
    stats.hooks.insert(rs->get_hook());

    if (group && group->has_conditions()) {
      Statement *cond = group->get_conditions();

      count_statement_stats(cond, stats);
    }

    const OperatorIf *op_if = rs->get_operator_if();

    if (op_if) {
      count_operator_if_stats(op_if, stats);
    }

    rs = rs->next.get();
  }
}

void
ConfigComparator::count_statement_stats(Statement *stmt, ParseStats &stats)
{
  while (stmt) {
    if (dynamic_cast<Condition *>(stmt)) {
      stats.conditions++;
    } else if (auto *op = dynamic_cast<Operator *>(stmt)) {
      stats.operators++;
      if (op->type_name() == "OperatorIf") {
        auto *op_if = static_cast<OperatorIf *>(op);

        count_operator_if_stats(op_if, stats);
      }
    }

    stmt = stmt->next();
  }
}

void
ConfigComparator::count_operator_if_stats(const OperatorIf *op_if, ParseStats &stats)
{
  const auto *section = op_if->get_sections();

  while (section) {
    Statement *cond = section->group.get_conditions();
    Operator  *oper = section->ops.oper.get();

    count_statement_stats(cond, stats);
    count_statement_stats(oper, stats);
    section = section->next.get();
  }
}

} // namespace ConfigComparison
