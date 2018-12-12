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

#include "ts/ts.h"

#include "resources.h"
#include "statement.h"
#include "matcher.h"
#include "parser.h"

// Condition modifiers
enum CondModifiers {
  COND_NONE   = 0,
  COND_OR     = 1,
  COND_AND    = 2,
  COND_NOT    = 4,
  COND_NOCASE = 8, // Not implemented
  COND_LAST   = 16,
  COND_CHAIN  = 32 // Not implemented
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Conditions (this is also the interface)
//
class Condition : public Statement
{
public:
  Condition() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Condition"); }

  virtual ~Condition()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Condition");
    delete _matcher;
  }

  // noncopyable
  Condition(const Condition &) = delete;
  void operator=(const Condition &) = delete;

  // Inline this, it's critical for speed (and only used twice)
  bool
  do_eval(const Resources &res)
  {
    bool rt = eval(res);

    if (_mods & COND_NOT) {
      rt = !rt;
    }

    if (_next) {
      if (_mods & COND_OR) {
        return rt || (static_cast<Condition *>(_next)->do_eval(res));
      } else { // AND is the default
        // Short circuit if we're an AND and the first condition is FALSE.
        if (rt) {
          return static_cast<Condition *>(_next)->do_eval(res);
        } else {
          return false;
        }
      }
    } else {
      return rt;
    }

    return false; // Shouldn't happen.
  }

  bool
  last() const
  {
    return _mods & COND_LAST;
  }

  // Setters
  virtual void
  set_qualifier(const std::string &q)
  {
    _qualifier = q;
  }

  // Some getters
  const Matcher *
  get_matcher() const
  {
    return _matcher;
  }

  MatcherOps
  get_cond_op() const
  {
    return _cond_op;
  }

  const std::string
  get_qualifier() const
  {
    return _qualifier;
  }

  // Virtual methods, has to be implemented by each conditional;
  void initialize(Parser &p) override;
  virtual void append_value(std::string &s, const Resources &res) = 0;

protected:
  // Evaluate the condition
  virtual bool eval(const Resources &res) = 0;

  std::string _qualifier;
  MatcherOps _cond_op = MATCH_EQUAL;
  Matcher *_matcher   = nullptr;

private:
  CondModifiers _mods = COND_NONE;
};
