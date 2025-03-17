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
// ruleset.cc: implementation of the ruleset class
//
//
#include <string>

#include "ruleset.h"
#include "factory.h"

///////////////////////////////////////////////////////////////////////////////
// Class implementation (no reason to have these inline)
//
void
RuleSet::append(RuleSet *rule)
{
  RuleSet *tmp = this;

  TSReleaseAssert(rule->next == nullptr);

  while (tmp->next) {
    tmp = tmp->next;
  }
  tmp->next = rule;
}

// This stays here, since the condition, albeit owned by a group, is tightly couple to the ruleset.
Condition *
RuleSet::make_condition(Parser &p, const char *filename, int lineno)
{
  Condition *c = condition_factory(p.get_op());

  if (nullptr == c) {
    return nullptr; // Complete failure in the factory
  }

  Dbg(pi_dbg_ctl, "    Adding condition: %%{%s} with arg: %s", p.get_op().c_str(), p.get_arg().c_str());
  c->initialize(p);
  if (!c->set_hook(_hook)) {
    delete c;
    TSError("[%s] in %s:%d: can't use this condition in hook=%s: %%{%s} with arg: %s", PLUGIN_NAME, filename, lineno,
            TSHttpHookNameLookup(_hook), p.get_op().c_str(), p.get_arg().c_str());
    return nullptr;
  }

  if (c->get_cond_op() == MATCH_ERROR) {
    delete c;
    TSError("[%s] in %s:%d: Invalid operator", PLUGIN_NAME, filename, lineno);
    return nullptr;
  }

  // Update some ruleset state based on this new condition;
  _last |= c->last();
  _ids   = static_cast<ResourceIDs>(_ids | c->get_resource_ids());

  return c;
}

bool
RuleSet::add_operator(Parser &p, const char *filename, int lineno)
{
  Operator *op = operator_factory(p.get_op());

  if (nullptr != op) {
    Dbg(pi_dbg_ctl, "    Adding operator: %s(%s)=\"%s\"", p.get_op().c_str(), p.get_arg().c_str(), p.get_value().c_str());
    op->initialize(p);
    if (!op->set_hook(_hook)) {
      delete op;
      Dbg(pi_dbg_ctl, "in %s:%d: can't use this operator in hook=%s:  %s(%s)", filename, lineno, TSHttpHookNameLookup(_hook),
          p.get_op().c_str(), p.get_arg().c_str());
      TSError("[%s] in %s:%d: can't use this operator in hook=%s:  %s(%s)", PLUGIN_NAME, filename, lineno,
              TSHttpHookNameLookup(_hook), p.get_op().c_str(), p.get_arg().c_str());
      return false;
    }

    // Work on the appropriate operator list
    OperatorPair &ops = _is_else ? _operators[1] : _operators[0];

    if (nullptr == ops.oper) {
      ops.oper = op;
    } else {
      ops.oper->append(op);
    }

    // Update some ruleset state based on this new operator
    ops.oper_mods = static_cast<OperModifiers>(ops.oper_mods | ops.oper->get_oper_modifiers());
    _ids          = static_cast<ResourceIDs>(_ids | ops.oper->get_resource_ids());

    return true;
  }

  return false;
}

ResourceIDs
RuleSet::get_all_resource_ids() const
{
  ResourceIDs ids = _ids;
  RuleSet    *tmp = this->next;

  while (tmp) {
    ids = static_cast<ResourceIDs>(ids | tmp->get_resource_ids());
    tmp = tmp->next;
  }

  return ids;
}
