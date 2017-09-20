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

#include "ts/ts.h"

#include "operator.h"
#include "resources.h"
#include "value.h"

///////////////////////////////////////////////////////////////////////////////
// Operator declarations.
//
class OperatorSetConfig : public Operator
{
public:
  OperatorSetConfig() : _key(TS_CONFIG_NULL), _type(TS_RECORDDATATYPE_NULL)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetConfig");
  }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetConfig);

  TSOverridableConfigKey _key;
  TSRecordDataType _type;

  std::string _config;
  Value _value;
};

class OperatorSetStatus : public Operator
{
public:
  OperatorSetStatus() : _reason(NULL), _reason_len(0) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetStatus"); }
  void initialize(Parser &p);

protected:
  void initialize_hooks();
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetStatus);

  Value _status;
  const char *_reason;
  int _reason_len;
};

class OperatorSetStatusReason : public Operator
{
public:
  OperatorSetStatusReason() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetStatusReason"); }
  void initialize(Parser &p);

protected:
  void initialize_hooks();
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetStatusReason);

  Value _reason;
};

class OperatorSetDestination : public Operator
{
public:
  OperatorSetDestination() : _url_qual(URL_QUAL_NONE) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetDestination"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetDestination);

  UrlQualifiers _url_qual;
  Value _value;
};

class OperatorSetRedirect : public Operator
{
public:
  OperatorSetRedirect() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetRedirect"); }
  void initialize(Parser &p);
  TSHttpStatus
  get_status()
  {
    return static_cast<TSHttpStatus>(_status.get_int_value());
  }
  std::string
  get_location(int &size)
  {
    size = (int)_location.size();
    return static_cast<std::string>(_location.get_value());
  }

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetRedirect);

  Value _status;
  Value _location;
};

class OperatorNoOp : public Operator
{
public:
  OperatorNoOp() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorNoOp"); }

protected:
  void exec(const Resources & /* res ATS_UNUSED */) const {};

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorNoOp);
};

class OperatorSetTimeoutOut : public Operator
{
public:
  OperatorSetTimeoutOut() : _type(TO_OUT_UNDEFINED) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetTimeoutOut"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetTimeoutOut);

  enum TimeoutOutType {
    TO_OUT_UNDEFINED,
    TO_OUT_ACTIVE,
    TO_OUT_INACTIVE,
    TO_OUT_CONNECT,
    TO_OUT_DNS,
  };

  TimeoutOutType _type;
  Value _timeout;
};

class OperatorSkipRemap : public Operator
{
public:
  OperatorSkipRemap() : _skip_remap(false) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSkipRemap"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSkipRemap);

  bool _skip_remap;
};

// All the header operators share a base class
class OperatorRMHeader : public OperatorHeaders
{
public:
  OperatorRMHeader() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorRMHeader"); }

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorRMHeader);
};

class OperatorAddHeader : public OperatorHeaders
{
public:
  OperatorAddHeader() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorAddHeader"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorAddHeader);

  Value _value;
};

class OperatorSetHeader : public OperatorHeaders
{
public:
  OperatorSetHeader() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetHeader"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetHeader);

  Value _value;
};

class OperatorCounter : public Operator
{
public:
  OperatorCounter() : _counter_name(""), _counter(TS_ERROR) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorCounter"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorCounter);

  std::string _counter_name;
  int _counter;
};

class OperatorRMCookie : public OperatorCookies
{
public:
  OperatorRMCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorRMCookie"); }

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorRMCookie);
};

class OperatorAddCookie : public OperatorCookies
{
public:
  OperatorAddCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorAddCookie"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorAddCookie);

  Value _value;
};

class OperatorSetCookie : public OperatorCookies
{
public:
  OperatorSetCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetCookie"); }
  void initialize(Parser &p);

protected:
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetCookie);

  Value _value;
};

namespace CookieHelper
{
enum CookieOp { COOKIE_OP_DEL, COOKIE_OP_ADD, COOKIE_OP_SET };

/*
 * This function returns if cookies need to be changed or not.
 * If the return value is true, updated_cookies would be cookies after the change.
 */
bool cookieModifyHelper(const char *cookies, const size_t cookies_len, std::string &updated_cookies, const CookieOp cookie_op,
                        const std::string &cookie_key, const std::string &cookie_value = std::string());
}

class OperatorSetConnDSCP : public Operator
{
public:
  OperatorSetConnDSCP() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetConnDSCP"); }
  void initialize(Parser &p);

protected:
  void initialize_hooks();
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetConnDSCP);

  Value _ds_value;
};

class OperatorSetDebug : public Operator
{
public:
  OperatorSetDebug() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for OperatorSetDebug"); }
  void initialize(Parser &p);

protected:
  void initialize_hooks();
  void exec(const Resources &res) const;

private:
  DISALLOW_COPY_AND_ASSIGN(OperatorSetDebug);
};

#endif // __OPERATORS_H
