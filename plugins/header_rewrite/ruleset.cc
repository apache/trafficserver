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
#include "operators.h"

RuleSet::RuleSet()
{
  Dbg(dbg_ctl, "RuleSet CTOR");
}

RuleSet::~RuleSet()
{
  Dbg(dbg_ctl, "RulesSet DTOR");
}

OperModifiers
RuleSet::exec(const Resources &res) const
{
  return _op_if.exec_and_return_mods(res);
}

void
RuleSet::append(std::unique_ptr<RuleSet> rule)
{
  TSReleaseAssert(rule->next == nullptr);
  std::unique_ptr<RuleSet> *cur = &next;

  while (*cur) {
    cur = &(*cur)->next;
  }

  *cur = std::move(rule);
}

// This stays here, since the condition, albeit owned by a group, is tightly couple to the ruleset.
Condition *
RuleSet::make_condition(Parser &p, const char *filename, int lineno)
{
  Condition *c = condition_factory(p.get_op());

  if (nullptr == c) {
    return nullptr;
  }

  Dbg(pi_dbg_ctl, "    Creating condition: %%{%s} with arg: %s", p.get_op().c_str(), p.get_arg().c_str());
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

    auto *cur_sec = _op_if.cur_section();

    if (!cur_sec->ops.oper) {
      cur_sec->ops.oper.reset(op);
    } else {
      cur_sec->ops.oper->append(op);
    }

    cur_sec->ops.oper_mods = static_cast<OperModifiers>(cur_sec->ops.oper_mods | cur_sec->ops.oper->get_oper_modifiers());
    _ids                   = static_cast<ResourceIDs>(_ids | cur_sec->ops.oper->get_resource_ids());

    return true;
  }

  return false;
}

ResourceIDs
RuleSet::get_all_resource_ids() const
{
  ResourceIDs                     ids = _ids;
  const std::unique_ptr<RuleSet> *cur = &next;

  while (*cur) {
    ids = static_cast<ResourceIDs>(ids | (*cur)->get_resource_ids());
    cur = &(*cur)->next;
  }

  return ids;
}

bool
RuleSet::add_operator(Operator *op)
{
  // OperatorIf is a pseudo-operator container - it doesn't need hook validation itself.
  if (op->type_name() != "OperatorIf") {
    if (!op->set_hook(_hook)) {
      Dbg(pi_dbg_ctl, "can't use this operator in hook=%s", TSHttpHookNameLookup(_hook));
      TSError("[%s] can't use this operator in hook=%s", PLUGIN_NAME, TSHttpHookNameLookup(_hook));
      return false;
    }
  }

  auto *cur_sec = _op_if.cur_section();

  if (!cur_sec->ops.oper) {
    cur_sec->ops.oper.reset(op);
  } else {
    cur_sec->ops.oper->append(op);
  }

  // Update some ruleset state based on this new operator
  cur_sec->ops.oper_mods = static_cast<OperModifiers>(cur_sec->ops.oper_mods | cur_sec->ops.oper->get_oper_modifiers());
  _ids                   = static_cast<ResourceIDs>(_ids | cur_sec->ops.oper->get_resource_ids());

  return true;
}
