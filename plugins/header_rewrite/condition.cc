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
// condition.cc: Implementation of the condition base class
//
//
#include <string>

#include "ts/ts.h"

#include "condition.h"

static MatcherOps
parse_matcher_op(std::string &arg)
{
  switch (arg[0]) {
  case '=':
    arg.erase(0, 1);
    return MATCH_EQUAL;
    break;
  case '<':
    arg.erase(0, 1);
    return MATCH_LESS_THEN;
    break;
  case '>':
    arg.erase(0, 1);
    return MATCH_GREATER_THEN;
    break;
  case '/':
    arg.erase(0, 1);
    arg.erase(arg.length() - 1, arg.length());
    return MATCH_REGULAR_EXPRESSION;
    break;
  default:
    return MATCH_EQUAL;
    break;
  }
}

void
Condition::initialize(Parser &p)
{
  Statement::initialize(p);

  if (p.mod_exist("OR")) {
    if (p.mod_exist("AND")) {
      TSError("[%s] Can't have both AND and OR in mods", PLUGIN_NAME);
    } else {
      _mods = static_cast<CondModifiers>(_mods | COND_OR);
    }
  } else if (p.mod_exist("AND")) {
    _mods = static_cast<CondModifiers>(_mods | COND_AND);
  }

  if (p.mod_exist("NOT")) {
    _mods = static_cast<CondModifiers>(_mods | COND_NOT);
  }

  if (p.mod_exist("L")) {
    _mods = static_cast<CondModifiers>(_mods | COND_LAST);
  }

  _cond_op = parse_matcher_op(p.get_arg());
}
