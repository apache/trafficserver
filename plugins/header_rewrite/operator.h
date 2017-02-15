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
// Public interface for creating all operators. Don't user the operator.h interface
// directly!
//
#ifndef __OPERATOR_H__
#define __OPERATOR_H__ 1

#include <string>

#include "ts/ts.h"

#include "resources.h"
#include "statement.h"
#include "parser.h"

// Operator modifiers
enum OperModifiers {
  OPER_NONE = 0,
  OPER_LAST = 1,
  OPER_NEXT = 2,
  OPER_QSA  = 4,
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Operators (this is also the interface)
//
class Operator : public Statement
{
public:
  Operator() : _mods(OPER_NONE) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Operator"); }
  void
  do_exec(const Resources &res) const
  {
    exec(res);
    if (NULL != _next)
      static_cast<Operator *>(_next)->do_exec(res);
  }

  OperModifiers get_oper_modifiers() const;

  virtual void initialize(Parser &p);

protected:
  virtual void exec(const Resources &res) const = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(Operator);

  OperModifiers _mods;
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Header based Operators, this is obviously also an
// Operator interface.
//
class OperatorHeaders : public Operator
{
public:
  OperatorHeaders() : _header("") { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorHeaders"); }
  void initialize(Parser &p);

protected:
  std::string _header;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorHeaders);
};

#endif // __OPERATOR_H
