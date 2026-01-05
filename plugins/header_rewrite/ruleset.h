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
//////////////////////////////////////////////////////////////////////////////////////////////
//
// Implement the classes for the various types of hash keys we support.
//
#pragma once

#include <string>

#include <tscore/ink_assert.h>

#include "matcher.h"
#include "factory.h"
#include "resources.h"
#include "parser.h"
#include "conditions.h"
#include "operators.h"

///////////////////////////////////////////////////////////////////////////////
// RuleSet: Represents a complete wrapping a single OperatorIf.
//
class RuleSet
{
public:
  RuleSet();
  ~RuleSet();

  // noncopyable
  RuleSet(const RuleSet &)        = delete;
  void operator=(const RuleSet &) = delete;

  void        append(std::unique_ptr<RuleSet> rule);
  Condition  *make_condition(Parser &p, const char *filename, int lineno);
  ResourceIDs get_all_resource_ids() const;
  bool        add_operator(Parser &p, const char *filename, int lineno);
  bool        add_operator(Operator *op);

  ConditionGroup *
  get_group()
  {
    return _op_if.get_group();
  }

  Parser::CondClause
  get_clause() const
  {
    return _op_if.get_clause();
  }

  ConditionGroup *
  new_section(Parser::CondClause clause)
  {
    return _op_if.new_section(clause);
  }

  bool
  has_operator() const
  {
    return _op_if.has_operator();
  }

  bool
  section_has_condition() const
  {
    auto *sec = _op_if.cur_section();
    return sec ? sec->group.has_conditions() : false;
  }

  bool
  section_has_operator() const
  {
    auto *sec = _op_if.cur_section();
    return sec ? sec->has_operator() : false;
  }

  void
  set_hook(TSHttpHookID hook)
  {
    _hook = hook;
  }

  TSHttpHookID
  get_hook() const
  {
    return _hook;
  }

  ResourceIDs
  get_resource_ids() const
  {
    return _ids;
  }

  void
  require_resources(const ResourceIDs ids)
  {
    _ids = static_cast<ResourceIDs>(_ids | ids);
  }

  bool
  last() const
  {
    return _last;
  }

  OperModifiers exec(const Resources &res) const;

  const OperatorIf *
  get_operator_if() const
  {
    return &_op_if;
  }

  // Linked list of RuleSets
  std::unique_ptr<RuleSet> next;

private:
  OperatorIf   _op_if;
  TSHttpHookID _hook = TS_HTTP_READ_RESPONSE_HDR_HOOK; // Which hook is this rule for
  ResourceIDs  _ids  = RSRC_NONE;
  bool         _last = false;
};
