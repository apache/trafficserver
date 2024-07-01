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
#pragma once

#include <string>

#include "ts/ts.h"

#include "resources.h"
#include "statement.h"
#include "parser.h"

// Operator modifiers
enum OperModifiers {
  OPER_NONE        = 0,
  OPER_LAST        = 1,
  OPER_NEXT        = 2,
  OPER_QSA         = 4,
  OPER_INV         = 8,
  OPER_NO_REENABLE = 16,
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Operators (this is also the interface)
//
class Operator : public Statement
{
public:
  Operator() { Dbg(dbg_ctl, "Calling CTOR for Operator"); }

  virtual ~Operator() = default; // Very uncommon for an Operator to have a custom DTOR, but happens.

  // noncopyable
  Operator(const Operator &)       = delete;
  void operator=(const Operator &) = delete;

  OperModifiers get_oper_modifiers() const;
  void          initialize(Parser &p) override;

  // Only call TSHttpTxnReenable() if this returns true.
  // NOTE:  This approach to controlling whether reenable is called only works because
  // there is only one operator currently that defers the reenable.
  bool
  do_exec(const Resources &res) const
  {
    bool reenable{exec(res)};
    if (nullptr != _next) {
      reenable &&static_cast<Operator *>(_next)->do_exec(res);
    }
    return reenable;
  }

protected:
  // Return false to disable call of TSHttpTxnReenable().  Operators executed in the remap pseudo-hook MUST return true,
  // as reenable is implicit in remap execution.
  virtual bool exec(const Resources &res) const = 0;

private:
  OperModifiers _mods = OPER_NONE;
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Header based Operators, this is obviously also an
// Operator interface.
//
class OperatorHeaders : public Operator
{
public:
  OperatorHeaders() { Dbg(dbg_ctl, "Calling CTOR for OperatorHeaders"); }

  // noncopyable
  OperatorHeaders(const OperatorHeaders &) = delete;
  void operator=(const OperatorHeaders &)  = delete;

  void initialize(Parser &p) override;

protected:
  std::string _header;
  const char *_header_wks = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
// Base class for all Cookie based Operators, this is obviously also an
// Operator interface.
//
class OperatorCookies : public Operator
{
public:
  OperatorCookies() { Dbg(dbg_ctl, "Calling CTOR for OperatorCookies"); }

  // noncopyable
  OperatorCookies(const OperatorCookies &) = delete;
  void operator=(const OperatorCookies &)  = delete;

  void initialize(Parser &p) override;

protected:
  std::string _cookie;
};
