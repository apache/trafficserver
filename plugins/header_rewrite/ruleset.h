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
  struct OperatorPair {
    OperatorPair() = default;

    OperatorPair(const OperatorPair &)            = delete;
    OperatorPair &operator=(const OperatorPair &) = delete;

    Operator     *oper      = nullptr;
    OperModifiers oper_mods = OPER_NONE;
  };

  RuleSet() { Dbg(dbg_ctl, "RuleSet CTOR"); }

  ~RuleSet()
  {
    Dbg(dbg_ctl, "RulesSet DTOR");
    delete _operators[0].oper; // These are pointers
    delete _operators[1].oper;
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
    return (nullptr != _operators[0].oper) || (nullptr != _operators[1].oper);
  }

  void
  set_hook(TSHttpHookID hook)
  {
    _hook = hook;
  }

  ConditionGroup *
  get_group()
  {
    return &_group;
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

  bool
  last() const
  {
    return _last;
  }

  void
  switch_branch()
  {
    _is_else = !_is_else;
  }

  OperModifiers
  exec(const OperatorPair &ops, const Resources &res) const
  {
    if (nullptr == ops.oper) {
      return ops.oper_mods;
    }

    auto no_reenable_count{ops.oper->do_exec(res)};

    ink_assert(no_reenable_count < 2);
    if (no_reenable_count) {
      return static_cast<OperModifiers>(ops.oper_mods | OPER_NO_REENABLE);
    }

    return ops.oper_mods;
  }

  const OperatorPair &
  eval(const Resources &res)
  {
    if (_group.eval(res)) {
      return _operators[0]; // IF conditions
    } else {
      return _operators[1]; // ELSE conditions
    }
  }

  RuleSet *next = nullptr; // Linked list

private:
  ConditionGroup _group;        // All conditions are now wrapped in a group
  OperatorPair   _operators[2]; // Holds both the IF and the ELSE set of operators

  // State values (updated when conds / operators are added)
  TSHttpHookID _hook    = TS_HTTP_READ_RESPONSE_HDR_HOOK; // Which hook is this rule for
  ResourceIDs  _ids     = RSRC_NONE;
  bool         _last    = false;
  bool         _is_else = false; // Are we in the else clause of the new rule? For parsing.
};
