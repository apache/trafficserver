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
#include <vector>
#include <memory>
#include <set>

#include "ts/ts.h"

// Forward declarations
class RuleSet;
class Statement;
class Condition;
class Operator;
class OperatorIf;

namespace ConfigComparison
{

struct ParseStats {
  int                    rulesets   = 0;
  int                    conditions = 0;
  int                    operators  = 0;
  std::set<TSHttpHookID> hooks;
  bool                   is_hrw4u = false;

  std::string format_hooks() const;
};

struct ComparisonResult {
  bool                     success = true;
  std::vector<std::string> differences;
  int                      total_comparisons   = 0;
  int                      successful_compares = 0;

  void add_diff(const std::string &context, const std::string &msg);
  void add_diff(const std::string &context, const std::string &msg, const std::string &expected, const std::string &got);
  void add_success(const std::string &context);

  std::string
  summary() const
  {
    if (success) {
      return "✓ All comparisons passed (" + std::to_string(successful_compares) + "/" + std::to_string(total_comparisons) + ")";
    } else {
      return "✗ " + std::to_string(differences.size()) + " difference(s) found";
    }
  }
};

class ConfigComparator
{
public:
  ConfigComparator() = default;

  bool compare_rulesets_for_hook(RuleSet *rs1, RuleSet *rs2, TSHttpHookID hook);

  void set_debug(bool debug);

  const ComparisonResult &
  get_result() const
  {
    return _result;
  }

  // Statistics collection
  ParseStats collect_stats(RuleSet *config);

  const ParseStats &
  get_old_stats() const
  {
    return _old_stats;
  }

  const ParseStats &
  get_new_stats() const
  {
    return _new_stats;
  }

private:
  bool compare_ruleset_chain(RuleSet *rs1, RuleSet *rs2, const std::string &context);
  bool compare_single_ruleset(RuleSet *rs1, RuleSet *rs2, const std::string &context);
  bool compare_statement_chains(Statement *s1, Statement *s2, const std::string &context);
  bool compare_single_statement(Statement *s1, Statement *s2, const std::string &context);
  bool compare_operator_if_sections(const OperatorIf *op1, const OperatorIf *op2, const std::string &context);

  void debug_print_ruleset(RuleSet *rs, const std::string &label);

  // Helper functions for statistics
  void count_ruleset_stats(RuleSet *rs, ParseStats &stats);
  void count_statement_stats(Statement *stmt, ParseStats &stats);
  void count_operator_if_stats(const OperatorIf *op_if, ParseStats &stats);

  ComparisonResult _result;
  bool             _debug = false;
  ParseStats       _old_stats;
  ParseStats       _new_stats;
};

} // namespace ConfigComparison
