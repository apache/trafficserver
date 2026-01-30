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
#include <memory>

#include "ts/ts.h"

#include "operator.h"
#include "resources.h"
#include "value.h"

// Forward declarations
class Parser;

// Full includes needed for member variables
#include "conditions.h"

///////////////////////////////////////////////////////////////////////////////
// Operator declarations.
//
class OperatorSetConfig : public Operator
{
public:
  OperatorSetConfig() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetConfig"); }

  // noncopyable
  OperatorSetConfig(const OperatorSetConfig &) = delete;
  void operator=(const OperatorSetConfig &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetConfig";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetConfig *>(other);
    return _key == op->_key && _type == op->_type && _config == op->_config && _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  TSOverridableConfigKey _key  = TS_CONFIG_NULL;
  TSRecordDataType       _type = TS_RECORDDATATYPE_NULL;

  std::string _config;
  Value       _value;
};

class OperatorSetStatus : public Operator
{
public:
  OperatorSetStatus() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetStatus"); }

  // noncopyable
  OperatorSetStatus(const OperatorSetStatus &) = delete;
  void operator=(const OperatorSetStatus &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetStatus";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetStatus *>(other);
    return _status.equals(&op->_status) && _reason_len == op->_reason_len &&
           (!_reason || !op->_reason || strncmp(_reason, op->_reason, _reason_len) == 0);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value       _status;
  const char *_reason     = nullptr;
  int         _reason_len = 0;
};

class OperatorSetStatusReason : public Operator
{
public:
  OperatorSetStatusReason() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetStatusReason"); }

  // noncopyable
  OperatorSetStatusReason(const OperatorSetStatusReason &) = delete;
  void operator=(const OperatorSetStatusReason &)          = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetStatusReason";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetStatusReason *>(other);
    return _reason.equals(&op->_reason);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _reason;
};

class OperatorSetDestination : public Operator
{
public:
  OperatorSetDestination() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetDestination"); }

  // noncopyable
  OperatorSetDestination(const OperatorSetDestination &) = delete;
  void operator=(const OperatorSetDestination &)         = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetDestination";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetDestination *>(other);
    return _url_qual == op->_url_qual && _value.equals(&op->_value);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + _value.get_value();
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  UrlQualifiers _url_qual = URL_QUAL_NONE;
  Value         _value;
};

// All the header operators share a base class
class OperatorRMDestination : public Operator
{
public:
  OperatorRMDestination() { Dbg(dbg_ctl, "Calling CTOR for OperatorRMDestination"); }

  // noncopyable
  OperatorRMDestination(const OperatorRMDestination &) = delete;
  void operator=(const OperatorRMDestination &)        = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorRMDestination";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  UrlQualifiers                 _url_qual = URL_QUAL_NONE;
  bool                          _keep     = false;
  std::string                   _stop     = "";
  std::vector<std::string_view> _stop_list;
};

class OperatorSetRedirect : public Operator
{
public:
  OperatorSetRedirect() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetRedirect"); }

  // noncopyable
  OperatorSetRedirect(const OperatorSetRedirect &) = delete;
  void operator=(const OperatorSetRedirect &)      = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetRedirect";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetRedirect *>(other);
    return _status.equals(&op->_status) && _location.equals(&op->_location);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + std::to_string(_status.get_int_value()) + " " + _location.get_value();
  }

  void initialize(Parser &p) override;

  TSHttpStatus
  get_status() const
  {
    return static_cast<TSHttpStatus>(_status.get_int_value());
  }

  const std::string &
  get_location() const
  {
    return _location.get_value();
  }

protected:
  void initialize_hooks() override;

  bool exec(const Resources &res) const override;

private:
  Value _status;
  Value _location;
};

class OperatorNoOp : public Operator
{
public:
  OperatorNoOp() { Dbg(dbg_ctl, "Calling CTOR for OperatorNoOp"); }

  // noncopyable
  OperatorNoOp(const OperatorNoOp &)   = delete;
  void operator=(const OperatorNoOp &) = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorNoOp";
  }

protected:
  bool
  exec(const Resources & /* res ATS_UNUSED */) const override
  {
    return true;
  };
};

class OperatorSetTimeoutOut : public Operator
{
public:
  OperatorSetTimeoutOut() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetTimeoutOut"); }

  // noncopyable
  OperatorSetTimeoutOut(const OperatorSetTimeoutOut &) = delete;
  void operator=(const OperatorSetTimeoutOut &)        = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetTimeoutOut";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetTimeoutOut *>(other);
    return _type == op->_type && _timeout.equals(&op->_timeout);
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  enum TimeoutOutType {
    TO_OUT_UNDEFINED,
    TO_OUT_ACTIVE,
    TO_OUT_INACTIVE,
    TO_OUT_CONNECT,
    TO_OUT_DNS,
  };

  TimeoutOutType _type = TO_OUT_UNDEFINED;
  Value          _timeout;
};

class OperatorSkipRemap : public Operator
{
public:
  OperatorSkipRemap() { Dbg(dbg_ctl, "Calling CTOR for OperatorSkipRemap"); }

  // noncopyable
  OperatorSkipRemap(const OperatorSkipRemap &) = delete;
  void operator=(const OperatorSkipRemap &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSkipRemap";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  bool _skip_remap = false;
};

// All the header operators share a base class
class OperatorRMHeader : public OperatorHeaders
{
public:
  OperatorRMHeader() { Dbg(dbg_ctl, "Calling CTOR for OperatorRMHeader"); }

  // noncopyable
  OperatorRMHeader(const OperatorRMHeader &) = delete;
  void operator=(const OperatorRMHeader &)   = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorRMHeader";
  }

protected:
  bool exec(const Resources &res) const override;
};

class OperatorAddHeader : public OperatorHeaders
{
public:
  OperatorAddHeader() { Dbg(dbg_ctl, "Calling CTOR for OperatorAddHeader"); }

  // noncopyable
  OperatorAddHeader(const OperatorAddHeader &) = delete;
  void operator=(const OperatorAddHeader &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorAddHeader";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorAddHeader *>(other);
    return _header == op->_header && _value.equals(&op->_value);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + _header + "=\"" + _value.get_value() + "\"";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

class OperatorSetHeader : public OperatorHeaders
{
public:
  OperatorSetHeader() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetHeader"); }

  // noncopyable
  OperatorSetHeader(const OperatorSetHeader &) = delete;
  void operator=(const OperatorSetHeader &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetHeader";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetHeader *>(other);
    return _header == op->_header && _value.equals(&op->_value);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + _header + "=\"" + _value.get_value() + "\"";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

class OperatorCounter : public Operator
{
public:
  OperatorCounter() { Dbg(dbg_ctl, "Calling CTOR for OperatorCounter"); }

  // noncopyable
  OperatorCounter(const OperatorCounter &) = delete;
  void operator=(const OperatorCounter &)  = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorCounter";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  std::string _counter_name;
  int         _counter = TS_ERROR;
};

class OperatorRMCookie : public OperatorCookies
{
public:
  OperatorRMCookie() { Dbg(dbg_ctl, "Calling CTOR for OperatorRMCookie"); }

  // noncopyable
  OperatorRMCookie(const OperatorRMCookie &) = delete;
  void operator=(const OperatorRMCookie &)   = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorRMCookie";
  }

protected:
  bool exec(const Resources &res) const override;
};

class OperatorAddCookie : public OperatorCookies
{
public:
  OperatorAddCookie() { Dbg(dbg_ctl, "Calling CTOR for OperatorAddCookie"); }

  // noncopyable
  OperatorAddCookie(const OperatorAddCookie &) = delete;
  void operator=(const OperatorAddCookie &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorAddCookie";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorAddCookie *>(other);
    return _cookie == op->_cookie && _value.equals(&op->_value);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + _cookie + "=\"" + _value.get_value() + "\"";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

class OperatorSetCookie : public OperatorCookies
{
public:
  OperatorSetCookie() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetCookie"); }

  // noncopyable
  OperatorSetCookie(const OperatorSetCookie &) = delete;
  void operator=(const OperatorSetCookie &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetCookie";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetCookie *>(other);
    return _cookie == op->_cookie && _value.equals(&op->_value);
  }

  std::string
  debug_string() const override
  {
    return std::string(type_name()) + " " + _cookie + "=\"" + _value.get_value() + "\"";
  }

  void initialize(Parser &p) override;

protected:
  bool exec(const Resources &res) const override;

private:
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
} // namespace CookieHelper

class OperatorSetConnDSCP : public Operator
{
public:
  OperatorSetConnDSCP() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetConnDSCP"); }

  // noncopyable
  OperatorSetConnDSCP(const OperatorSetConnDSCP &) = delete;
  void operator=(const OperatorSetConnDSCP &)      = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetConnDSCP";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetConnDSCP *>(other);
    return _ds_value.equals(&op->_ds_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _ds_value;
};

class OperatorSetConnMark : public Operator
{
public:
  OperatorSetConnMark() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetConnMark"); }

  // noncopyable
  OperatorSetConnMark(const OperatorSetConnMark &) = delete;
  void operator=(const OperatorSetConnMark &)      = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetConnMark";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetConnMark *>(other);
    return _ds_value.equals(&op->_ds_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _ds_value;
};

class OperatorSetDebug : public Operator
{
public:
  OperatorSetDebug() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetDebug"); }

  // noncopyable
  OperatorSetDebug(const OperatorSetDebug &) = delete;
  void operator=(const OperatorSetDebug &)   = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetDebug";
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;
};

class OperatorSetBody : public Operator
{
public:
  OperatorSetBody() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetBody"); }

  // noncopyable
  OperatorSetBody(const OperatorSetBody &) = delete;
  void operator=(const OperatorSetBody &)  = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetBody";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetBody *>(other);
    return _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

class OperatorSetHttpCntl : public Operator
{
public:
  OperatorSetHttpCntl() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetHttpCntl"); }

  // noncopyable
  OperatorSetHttpCntl(const OperatorSetHttpCntl &) = delete;
  void operator=(const OperatorSetHttpCntl &)      = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetHttpCntl";
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  bool           _flag = false;
  TSHttpCntlType _cntl_qual;
};

class OperatorSetPluginCntl : public Operator
{
public:
  OperatorSetPluginCntl() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetPluginCntl"); }

  // noncopyable
  OperatorSetPluginCntl(const OperatorSetPluginCntl &) = delete;
  void operator=(const OperatorSetPluginCntl &)        = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetPluginCntl";
  }

  void initialize(Parser &p) override;

  enum class PluginCtrl {
    TIMEZONE,
    INBOUND_IP_SOURCE,
  };

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

  bool
  need_txn_private_slot() const override
  {
    return true;
  }

private:
  PluginCtrl _name;
  int        _value;
};

class RemapPluginInst; // Opaque to the HRW operator, but needed in the implementation.

class OperatorRunPlugin : public Operator
{
public:
  OperatorRunPlugin() { Dbg(dbg_ctl, "Calling CTOR for OperatorRunPlugin"); }

  // This one is special, since we have to remove the old plugin from the factory.
  ~OperatorRunPlugin() override
  {
    Dbg(dbg_ctl, "Calling DTOR for OperatorRunPlugin");

    if (_plugin) {
      _plugin->done();
      _plugin = nullptr;
    }
  }

  // noncopyable
  OperatorRunPlugin(const OperatorRunPlugin &) = delete;
  void operator=(const OperatorRunPlugin &)    = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorRunPlugin";
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  RemapPluginInst *_plugin = nullptr;
};

class OperatorSetBodyFrom : public Operator
{
public:
  OperatorSetBodyFrom() { Dbg(pi_dbg_ctl, "Calling CTOR for OperatorSetBodyFrom"); }

  // noncopyable
  OperatorSetBodyFrom(const OperatorSetBodyFrom &) = delete;
  void operator=(const OperatorSetBodyFrom &)      = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetBodyFrom";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetBodyFrom *>(other);
    return _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

  enum { TS_EVENT_FETCHSM_SUCCESS = 70000, TS_EVENT_FETCHSM_FAILURE = 70001, TS_EVENT_FETCHSM_TIMEOUT = 70002 };

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

class OperatorSetStateFlag : public Operator
{
public:
  OperatorSetStateFlag()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for OperatorSetStateFlag");
  }

  // noncopyable
  OperatorSetStateFlag(const OperatorSetStateFlag &) = delete;
  void operator=(const OperatorSetStateFlag &)       = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetStateFlag";
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  int      _flag_ix = -1;
  int      _flag    = false;
  uint64_t _mask    = 0;
};

class OperatorSetStateInt8 : public Operator
{
public:
  OperatorSetStateInt8()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for OperatorSetStateInt8");
  }

  // noncopyable
  OperatorSetStateInt8(const OperatorSetStateInt8 &) = delete;
  void operator=(const OperatorSetStateInt8 &)       = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetStateInt8";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetStateInt8 *>(other);
    return _byte_ix == op->_byte_ix && _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  int   _byte_ix = -1;
  Value _value;
};

class OperatorSetStateInt16 : public Operator
{
public:
  OperatorSetStateInt16()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for OperatorSetStateInt16");
  }

  // noncopyable
  OperatorSetStateInt16(const OperatorSetStateInt16 &) = delete;
  void operator=(const OperatorSetStateInt16 &)        = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetStateInt16";
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  Value _value;
};

class OperatorSetEffectiveAddress : public Operator
{
public:
  OperatorSetEffectiveAddress() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetEffectiveAddress"); }

  // noncopyable
  OperatorSetEffectiveAddress(const OperatorSetEffectiveAddress &) = delete;
  void operator=(const OperatorSetEffectiveAddress &)              = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetEffectiveAddress";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetEffectiveAddress *>(other);
    return _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

  bool
  need_txn_private_slot() const override
  {
    return true;
  }

private:
  Value _value;
};

class OperatorSetNextHopStrategy : public Operator
{
public:
  OperatorSetNextHopStrategy() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetNextHopStrategy"); }

  // noncopyable
  OperatorSetNextHopStrategy(const OperatorSetNextHopStrategy &) = delete;
  void operator=(const OperatorSetNextHopStrategy &)             = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetNextHopStrategy";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetNextHopStrategy *>(other);
    return _value.equals(&op->_value);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _value;
};

///////////////////////////////////////////////////////////////////////////////
// OperatorIf class - implements nested if/elif/else as a pseudo-operator.
// Keep this at the end of the files, since this is not really an Operator.
//
class OperatorIf : public Operator
{
public:
  struct CondOpSection {
    CondOpSection() = default;

    ~CondOpSection() = default;

    CondOpSection(const CondOpSection &)            = delete;
    CondOpSection &operator=(const CondOpSection &) = delete;

    bool
    has_operator() const
    {
      return ops.oper != nullptr;
    }

    ConditionGroup                 group;
    OperatorAndMods                ops;
    std::unique_ptr<CondOpSection> next; // For elif/else sections
  };

  OperatorIf() { Dbg(dbg_ctl, "Calling CTOR for OperatorIf"); }

  // noncopyable
  OperatorIf(const OperatorIf &)     = delete;
  void operator=(const OperatorIf &) = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorIf";
  }

  ConditionGroup *new_section(Parser::CondClause clause);
  bool            add_operator(Parser &p, const char *filename, int lineno);
  Condition      *make_condition(Parser &p, const char *filename, int lineno);
  bool            has_operator() const;

  ConditionGroup *
  get_group()
  {
    return &_cur_section->group;
  }

  Parser::CondClause
  get_clause() const
  {
    return _clause;
  }

  CondOpSection *
  cur_section() const
  {
    return _cur_section;
  }

  // For comparison tool access
  const CondOpSection *
  get_sections() const
  {
    return &_sections;
  }

  OperModifiers exec_and_return_mods(const Resources &res) const;

protected:
  bool
  exec(const Resources &res) const override
  {
    OperModifiers mods = exec_and_return_mods(res);
    return !(mods & OPER_NO_REENABLE);
  }

private:
  OperModifiers exec_section(const CondOpSection *section, const Resources &res) const;

  CondOpSection      _sections;
  CondOpSection     *_cur_section = &_sections;
  Parser::CondClause _clause      = Parser::CondClause::COND;
};

class OperatorSetCCAlgorithm : public Operator
{
public:
  OperatorSetCCAlgorithm() { Dbg(dbg_ctl, "Calling CTOR for OperatorSetCCAlgorithm"); }

  // noncopyable
  OperatorSetCCAlgorithm(const OperatorSetCCAlgorithm &) = delete;
  void operator=(const OperatorSetCCAlgorithm &)         = delete;

  std::string_view
  type_name() const override
  {
    return "OperatorSetCCAlgorithm";
  }

  bool
  equals(const Statement *other) const override
  {
    if (!Operator::equals(other)) {
      return false;
    }
    auto *op = static_cast<const OperatorSetCCAlgorithm *>(other);
    return _cc_alg.equals(&op->_cc_alg);
  }

  void initialize(Parser &p) override;

protected:
  void initialize_hooks() override;
  bool exec(const Resources &res) const override;

private:
  Value _cc_alg;
};
