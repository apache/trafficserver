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
#ifndef __OPERATORS_H__
#define __OPERATORS_H__ 1

#include <string>
#include <ts/ts.h>

#include "operator.h"
#include "resources.h"
#include "value.h"


///////////////////////////////////////////////////////////////////////////////
// Operator declarations.
//
class OperatorRMHeader : public Operator
{
public:
  OperatorRMHeader()
    : _header("")
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorRMHeader");
  }
  void initialize(Parser& p);

protected:
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorRMHeader);

  std::string _header;
};


class OperatorSetStatus : public Operator
{
public:
  OperatorSetStatus()
    : _reason(NULL), _reason_len(0)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetStatus");
  }
  void initialize(Parser& p);

protected:
  void initialize_hooks();
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetStatus);

  Value _status;
  const char* _reason;
  int _reason_len;
};


class OperatorSetStatusReason : public Operator
{
public:
  OperatorSetStatusReason()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetStatusReason");
  }
  void initialize(Parser& p);

protected:
  void initialize_hooks();
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetStatusReason);

  Value _reason;
};


class OperatorAddHeader : public Operator
{
public:
  OperatorAddHeader()
    : _header("")
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorAddHeader");
  }
  void initialize(Parser& p);

protected:
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorAddHeader);

  std::string _header;
  Value _value;
};


class OperatorSetDestination : public Operator
{
public:
  OperatorSetDestination()
    : _url_qual(URL_QUAL_NONE)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetDestination");
  }
  void initialize(Parser& p);

protected:
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetDestination);

  UrlQualifiers _url_qual;
  Value _value;
};


class OperatorSetRedirect : public Operator
{
public:
  OperatorSetRedirect()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetRedirect");
  }
  void initialize(Parser& p);

protected:
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetRedirect);

  Value _status;
  Value _location;
};


class OperatorNoOp : public Operator
{
public:
  OperatorNoOp()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorNoOp");
  }

protected:
  void exec(const Resources&) const { }; // UNUSED Resources &res

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorNoOp);
};


class OperatorSetTimeoutOut : public Operator
{
public:
  OperatorSetTimeoutOut()
    : _type(TO_OUT_UNDEFINED)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetTimeoutOut");
  }
  void initialize(Parser& p);

protected:
  void exec(const Resources& res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetTimeoutOut);

  enum TimeoutOutType {
    TO_OUT_UNDEFINED,
    TO_OUT_ACTIVE,
    TO_OUT_INACTIVE,
    TO_OUT_CONNECT,
    TO_OUT_DNS
  };

  TimeoutOutType _type;
  Value _timeout;
};


#endif // __OPERATORS_H
