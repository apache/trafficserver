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
    // There should be a slash at the end
    if (arg.length() >= 1 && arg[arg.length() - 1] == '/') {
      arg.erase(arg.length() - 1, arg.length());
      return MATCH_REGULAR_EXPRESSION;
    } else {
      return MATCH_ERROR;
    }
    break;
  case '{':
    arg.erase(0, 1);
    // There should be a right brace at the end
    if (arg.length() >= 1 && arg[arg.length() - 1] == '}') {
      arg.erase(arg.length() - 1, arg.length());
      return MATCH_IP_RANGES;
    } else {
      return MATCH_ERROR;
    }
  case '(':
    arg.erase(0, 1);
    // There should be a right paren at the end
    if (arg.length() >= 1 && arg[arg.length() - 1] == ')') {
      arg.erase(arg.length() - 1, arg.length());
      return MATCH_SET;
    } else {
      return MATCH_ERROR;
    }
  default:
    return MATCH_EQUAL;
    break;
  }
}

void
Condition::initialize(Parser &p)
{
  Statement::initialize(p);

  if (p.consume_mod("OR")) {
    if (p.consume_mod("AND")) {
      TSError("[%s] Can't have both AND and OR in mods", PLUGIN_NAME);
    } else {
      _mods |= CondModifiers::OR;
    }
  } else if (p.consume_mod("AND")) {
    _mods |= CondModifiers::AND;
  }

  if (p.consume_mod("NOT")) {
    _mods |= CondModifiers::NOT;
  }

  // The NOCASE / CASE modifier is a bit special, since it ripples down into the Matchers for
  // strings and regexes.
  int _substr_seen = 0;

  if (p.consume_mod("NOCASE")) {
    _mods |= CondModifiers::MOD_NOCASE;
  } else if (p.consume_mod("CASE")) {
    // Nothing to do — default is case-sensitive, but still allow this string for clearness.
  }

  if (p.consume_mod("EXT")) {
    _mods |= CondModifiers::MOD_EXT;
    _substr_seen++;
  }
  if (p.consume_mod("SUF")) {
    _mods |= CondModifiers::MOD_SUF;
    _substr_seen++;
  }
  if (p.consume_mod("PRE")) {
    _mods |= CondModifiers::MOD_PRE;
    _substr_seen++;
  }
  if (p.consume_mod("MID")) {
    _mods |= CondModifiers::MOD_MID;
    _substr_seen++;
  }

  if (_substr_seen > 1) {
    throw std::runtime_error("Only one substring modifier (EXT, SUF, PRE, MID) may be used.");
  }

  if (p.consume_mod("L")) {
    _mods |= CondModifiers::MOD_L;
  }

  _cond_op = parse_matcher_op(p.get_arg());
  p.validate_mods();
}

bool
Condition::equals(const Statement *other) const
{
  if (!Statement::equals(other)) {
    return false;
  }

  auto *cond = static_cast<const Condition *>(other);

  // Compare base Condition state
  if (_qualifier != cond->_qualifier || _cond_op != cond->_cond_op || _mods != cond->_mods) {
    return false;
  }

  // Compare matcher state if both have matchers
  if (_matcher && cond->_matcher) {
    // For now, we'll compare matcher operator type
    // Full matcher comparison requires template specializations
    return _matcher->op() == cond->_matcher->op();
  }

  // Both should have or not have matchers
  return (_matcher == nullptr) == (cond->_matcher == nullptr);
}

void
Condition::initialize(const hrw::ConditionSpec &spec)
{
  initialize_hooks();

  if (need_txn_slot()) {
    _txn_slot = acquire_txn_slot();
  }
  if (need_txn_private_slot()) {
    _txn_private_slot = acquire_txn_private_slot();
  }

  if (spec.mod_or) {
    _mods |= CondModifiers::OR;
  } else if (spec.mod_and) {
    _mods |= CondModifiers::AND;
  }

  if (spec.mod_not) {
    _mods |= CondModifiers::NOT;
  }

  if (spec.mod_nocase) {
    _mods |= CondModifiers::MOD_NOCASE;
  }

  if (spec.mod_ext) {
    _mods |= CondModifiers::MOD_EXT;
  }

  if (spec.mod_pre) {
    _mods |= CondModifiers::MOD_PRE;
  }

  if (spec.mod_last) {
    _mods |= CondModifiers::MOD_L;
  }

  // Parse matcher operation from match_arg
  std::string arg = spec.match_arg;
  _cond_op        = parse_matcher_op(arg);
}
