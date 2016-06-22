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
// Public interface for creating all values.
//
//
#ifndef __VALUE_H__
#define __VALUE_H__ 1

#include <string>

#include "ts/ts.h"

#include "resources.h"
#include "statement.h"
#include "condition.h"
#include "factory.h"
#include "parser.h"

///////////////////////////////////////////////////////////////////////////////
// Base class for all Values (this is also the interface).
//
// TODO: This is very incomplete, we need to support linked lists of these,
// which evaluate each component and create a "joined" final string.
//
class Value : Statement
{
public:
  Value() : _need_expander(false), _value(""), _int_value(0), _float_value(0.0), _cond_val(NULL)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Value");
  }

  void
  set_value(const std::string &val)
  {
    _value = val;
    if (_value.substr(0, 2) == "%{") {
      Parser parser(_value);

      _cond_val = condition_factory(parser.get_op());
      if (_cond_val) {
        _cond_val->initialize(parser);
      }
    } else if (_value.find("%<") != std::string::npos) { // It has a Variable to expand
      _need_expander = true;                             // And this is clearly not an integer or float ...
      // TODO: This is still not optimal, we should pre-parse the _value string here,
      // and perhaps populate a per-Value VariableExpander that holds state.
    } else {
      _int_value   = strtol(_value.c_str(), NULL, 10);
      _float_value = strtod(_value.c_str(), NULL);
    }
  }

  void
  append_value(std::string &s, const Resources &res) const
  {
    if (_cond_val) {
      _cond_val->append_value(s, res);
    } else {
      s += _value;
    }
  }

  const std::string &
  get_value() const
  {
    return _value;
  }
  size_t
  size() const
  {
    return _value.size();
  }
  int
  get_int_value() const
  {
    return _int_value;
  }
  double
  get_float_value() const
  {
    return _float_value;
  }

  bool
  empty() const
  {
    return _value.empty();
  }
  bool
  need_expansion() const
  {
    return _need_expander;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(Value);

  bool _need_expander;
  std::string _value;
  int _int_value;
  double _float_value;
  Condition *_cond_val;
};

#endif // __VALUE_H
