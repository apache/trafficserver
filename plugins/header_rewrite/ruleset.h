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

///////////////////////////////////////////////////////////////////////////////
// Class holding one ruleset. A ruleset is one (or more) pre-conditions, and
// one (or more) operators.
//
class RuleSet
{
public:
  // Holding the IF and ELSE operators and mods, in two separate linked lists.
  struct OperatorAndMods {
    OperatorAndMods() = default;

    OperatorAndMods(const OperatorAndMods &)            = delete;
    OperatorAndMods &operator=(const OperatorAndMods &) = delete;

    Operator     *oper      = nullptr;
    OperModifiers oper_mods = OPER_NONE;
  };

  struct CondOpSection {
    CondOpSection() = default;

    ~CondOpSection()
    {
      delete ops.oper;
      delete next;
    }

    CondOpSection(const CondOpSection &)            = delete;
    CondOpSection &operator=(const CondOpSection &) = delete;

    bool
    has_operator() const
    {
      return ops.oper != nullptr;
    }

    ConditionGroup  group;
    OperatorAndMods ops;
    CondOpSection  *next = nullptr; // For elif / else sections.
  };

  RuleSet() { Dbg(dbg_ctl, "RuleSet CTOR"); }

  ~RuleSet()
  {
    Dbg(dbg_ctl, "RulesSet DTOR");
    delete next;
  }

  // noncopyable
  RuleSet(const RuleSet &)        = delete;
  void operator=(const RuleSet &) = delete;

  // No reason to inline these
  void        append(RuleSet *rule);
  Condition  *make_condition(Parser &p, const char *filename, int lineno);
  bool        add_operator(Parser &p, const char *filename, int lineno);
  ResourceIDs get_all_resource_ids() const;

  bool
  has_operator() const
  {
    const CondOpSection *section = &_sections;

    while (section != nullptr) {
      if (section->has_operator()) {
        return true;
      }
      section = section->next;
    }
    return false;
  }

  void
  set_hook(TSHttpHookID hook)
  {
    _hook = hook;
  }

  ConditionGroup *
  get_group()
  {
    return &_cur_section->group;
  }

  TSHttpHookID
  get_hook() const
  {
    return _hook;
  }

  Parser::CondClause
  get_clause() const
  {
    return _clause;
  }

  CondOpSection *
  cur_section()
  {
    return _cur_section;
  }

  ConditionGroup *
  new_section(Parser::CondClause clause)
  {
    TSAssert(_cur_section && !_cur_section->next);
    _clause            = clause;
    _cur_section->next = new CondOpSection();
    _cur_section       = _cur_section->next;

    return &_cur_section->group;
  }

  ResourceIDs
  get_resource_ids() const
  {
    return _ids;
  }

  bool
  last() const
  {
    return _last;
  }

  OperModifiers
  exec(const OperatorAndMods &ops, const Resources &res) const
  {
    if (nullptr == ops.oper) {
      return ops.oper_mods;
    }

    auto no_reenable_count = ops.oper->do_exec(res);

    ink_assert(no_reenable_count < 2);
    if (no_reenable_count) {
      return static_cast<OperModifiers>(ops.oper_mods | OPER_NO_REENABLE);
    }

    return ops.oper_mods;
  }

  const RuleSet::OperatorAndMods &
  eval(const Resources &res)
  {
    for (CondOpSection *sec = &_sections; sec != nullptr; sec = sec->next) {
      if (sec->group.eval(res)) {
        return sec->ops;
      }
    }

    // No matching condition found, return empty operator set.
    static OperatorAndMods empty_ops;
    return empty_ops;
  }

  // Linked list of RuleSets
  RuleSet *next = nullptr;

private:
  // This holds one condition group, and the ops and optional else_ops, there's
  // aways at least one of these in the vector (no "elif" sections).
  CondOpSection  _sections;
  CondOpSection *_cur_section = &_sections;

  // State values (updated when conds / operators are added)
  TSHttpHookID       _hook   = TS_HTTP_READ_RESPONSE_HDR_HOOK; // Which hook is this rule for
  ResourceIDs        _ids    = RSRC_NONE;
  bool               _last   = false;
  Parser::CondClause _clause = Parser::CondClause::OPER;
};
