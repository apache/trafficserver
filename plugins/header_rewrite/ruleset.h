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
#ifndef __RULESET_H__
#define __RULESET_H__ 1

#include <string>

#include "matcher.h"
#include "factory.h"
#include "resources.h"
#include "parser.h"

///////////////////////////////////////////////////////////////////////////////
// Class holding one ruleset. A ruleset is one (or more) pre-conditions, and
// one (or more) operators.
//
class RuleSet
{
public:
  RuleSet()
    : next(NULL),
      _cond(NULL),
      _oper(NULL),
      _hook(TS_HTTP_READ_RESPONSE_HDR_HOOK),
      _ids(RSRC_NONE),
      _opermods(OPER_NONE),
      _last(false){};

  // No reason to inline these
  void append(RuleSet *rule);

  void add_condition(Parser &p);
  void add_operator(Parser &p);
  bool
  has_operator() const
  {
    return NULL != _oper;
  }
  bool
  has_condition() const
  {
    return NULL != _cond;
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
  get_all_resource_ids() const
  {
    return _ids;
  }

  bool
  eval(const Resources &res) const
  {
    if (NULL == _cond) {
      return true;
    } else {
      return _cond->do_eval(res);
    }
  }

  bool
  last() const
  {
    return _last;
  }

  OperModifiers
  exec(const Resources &res) const
  {
    _oper->do_exec(res);
    return _opermods;
  }

  RuleSet *next; // Linked list

private:
  DISALLOW_COPY_AND_ASSIGN(RuleSet);

  Condition *_cond;   // First pre-condition (linked list)
  Operator *_oper;    // First operator (linked list)
  TSHttpHookID _hook; // Which hook is this rule for

  // State values (updated when conds / operators are added)
  ResourceIDs _ids;
  OperModifiers _opermods;
  bool _last;
};

#endif // __RULESET_H
