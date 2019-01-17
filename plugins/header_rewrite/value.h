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
#pragma once

#include <string>
#include <vector>

#include "ts/ts.h"

#include "resources.h"
#include "statement.h"
#include "condition.h"

///////////////////////////////////////////////////////////////////////////////
// Base class for all Values (this is also the interface).
//
// TODO: This is very incomplete, we need to support linked lists of these,
// which evaluate each component and create a "joined" final string.
//
class Value : Statement
{
public:
  Value() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Value"); }

  virtual ~Value();

  // noncopyable
  Value(const Value &) = delete;
  void operator=(const Value &) = delete;

  void set_value(const std::string &val);

  void
  append_value(std::string &s, const Resources &res) const
  {
    if (!_cond_vals.empty()) {
      for (auto it = _cond_vals.begin(); it != _cond_vals.end(); it++) {
        (*it)->append_value(s, res);
      }
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

private:
  int _int_value      = 0;
  double _float_value = 0.0;
  std::string _value;
  std::vector<Condition *> _cond_vals;
};
