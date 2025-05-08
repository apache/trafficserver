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
// Declarations for all conditionals / conditional values we support.
//
#pragma once

#include <string>
#include <cstring>

#include "ts/ts.h"
#include "tscore/ink_string.h"

#include "condition.h"
#include "matcher.h"
#include "value.h"
#include "lulu.h"
// #include <mdbm.h>

///////////////////////////////////////////////////////////////////////////////
// Condition declarations.
//

// Always true
class ConditionTrue : public Condition
{
public:
  ConditionTrue() { Dbg(dbg_ctl, "Calling CTOR for ConditionTrue"); }

  // noncopyable
  ConditionTrue(const ConditionTrue &)  = delete;
  void operator=(const ConditionTrue &) = delete;

  void
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override
  {
    s += "TRUE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */) override
  {
    Dbg(pi_dbg_ctl, "Evaluating TRUE()");
    return true;
  }
};

// Always false
class ConditionFalse : public Condition
{
public:
  ConditionFalse() { Dbg(dbg_ctl, "Calling CTOR for ConditionFalse"); }

  // noncopyable
  ConditionFalse(const ConditionFalse &) = delete;
  void operator=(const ConditionFalse &) = delete;

  void
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override
  {
    s += "FALSE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */) override
  {
    Dbg(pi_dbg_ctl, "Evaluating FALSE()");
    return false;
  }
};

// Check the HTTP return status
class ConditionStatus : public Condition
{
  using DataType    = std::underlying_type_t<TSHttpStatus>;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionStatus;

public:
  ConditionStatus() { Dbg(dbg_ctl, "Calling CTOR for ConditionStatus"); }

  // noncopyable
  ConditionStatus(const SelfType &) = delete;
  void operator=(const SelfType &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
  void initialize_hooks() override; // Return status only valid in certain hooks
};

// Check the HTTP method
class ConditionMethod : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionMethod;

public:
  ConditionMethod() { Dbg(dbg_ctl, "Calling CTOR for ConditionMethod"); }

  // noncopyable
  ConditionMethod(const SelfType &) = delete;
  void operator=(const SelfType &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// Random 0 to (N-1)
class ConditionRandom : public Condition
{
  using DataType    = unsigned int;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionRandom;

public:
  ConditionRandom() { Dbg(dbg_ctl, "Calling CTOR for ConditionRandom"); }

  // noncopyable
  ConditionRandom(const SelfType &) = delete;
  void operator=(const SelfType &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  unsigned int _seed = 0;
  unsigned int _max  = 0;
};

// access(file)
class ConditionAccess : public Condition
{
public:
  ConditionAccess() { Dbg(dbg_ctl, "Calling CTOR for ConditionAccess"); }

  // noncopyable
  ConditionAccess(const ConditionAccess &) = delete;
  void operator=(const ConditionAccess &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  time_t _next = 0;
  bool   _last = false;
};

// cookie(name)
class ConditionCookie : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionCookie;

public:
  ConditionCookie() { Dbg(dbg_ctl, "Calling CTOR for ConditionCookie"); }

  // noncopyable
  ConditionCookie(const SelfType &) = delete;
  void operator=(const SelfType &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  // Nginx-style cookie parsing:
  //   nginx/src/http/ngx_http_parse.c:ngx_http_parse_multi_header_lines()
  inline int
  get_cookie_value(const char *buf, int buf_len, const char *name, int name_len, const char **value, int *value_len)
  {
    const char *start, *last, *end;

    // Sanity
    if (buf == nullptr || name == nullptr || value == nullptr || value_len == nullptr) {
      return TS_ERROR;
    }

    start = buf;
    end   = buf + buf_len;

    while (start < end) {
      if (strncasecmp(start, name, name_len) != 0) {
        goto skip;
      }

      for (start += name_len; start < end && *start == ' '; start++) {}

      if (start == end || *start++ != '=') {
        goto skip;
      }

      while (start < end && *start == ' ') {
        start++;
      }

      for (last = start; last < end && *last != ';'; last++) {}

      *value_len = last - start;
      *value     = start;
      return TS_SUCCESS;
    skip:
      while (start < end) {
        char ch = *start++;
        if (ch == ';' || ch == ',') {
          break;
        }
      }
      while (start < end && *start == ' ') {
        start++;
      }
    }
    return TS_ERROR;
  }
};

// header
class ConditionHeader : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionHeader;

public:
  explicit ConditionHeader(bool client = false) : _client(client)
  {
    Dbg(dbg_ctl, "Calling CTOR for ConditionHeader, client %d", client);
  }

  // noncopyable
  ConditionHeader(const SelfType &) = delete;
  void operator=(const SelfType &)  = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  bool _client;
};

// url
class ConditionUrl : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionUrl;

public:
  enum UrlType { CLIENT, URL, FROM, TO };

  explicit ConditionUrl(const UrlType type) : _type(type) { Dbg(dbg_ctl, "Calling CTOR for ConditionUrl"); }

  // noncopyable
  ConditionUrl(const SelfType &)   = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  UrlQualifiers _url_qual = URL_QUAL_NONE;
  UrlType       _type;
};

// DBM lookups
class ConditionDBM : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionDBM;

public:
  ConditionDBM()
    : //_dbm(NULL),
      _file(""),
      _mutex(TSMutexCreate())
  {
    Dbg(dbg_ctl, "Calling CTOR for ConditionDBM");
  }

  ~ConditionDBM() override
  {
    // if (_dbm) {
    //   mdbm_close(_dbm);
    //   _dbm = NULL;
    // }
  }

  // noncopyable
  ConditionDBM(const SelfType &)   = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  // MDBM* _dbm;
  std::string _file;
  Value       _key;
  TSMutex     _mutex;
};

class ConditionInternalTxn : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionInternalTxn;

public:
  void
  append_value(std::string & /* s ATS_UNUSED */, const Resources & /* res ATS_UNUSED */) override
  {
  }

protected:
  bool eval(const Resources &res) override;
};

class ConditionIp : public Condition
{
  using DataType      = std::string;
  using MatcherType   = Matchers<DataType>;
  using MatcherTypeIp = Matchers<const sockaddr *>;
  using SelfType      = ConditionIp;

public:
  explicit ConditionIp() { Dbg(dbg_ctl, "Calling CTOR for ConditionIp"); };

  // noncopyable
  ConditionIp(const SelfType &)    = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  IpQualifiers _ip_qual = IP_QUAL_CLIENT;
};

// Transact Count
class ConditionTransactCount : public Condition
{
  using DataType    = int;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionTransactCount;

public:
  ConditionTransactCount() { Dbg(dbg_ctl, "Calling CTOR for ConditionTransactCount"); }

  // noncopyable
  ConditionTransactCount(const SelfType &) = delete;
  void operator=(const SelfType &)         = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// now: Keeping track of current time / day / hour etc.
class ConditionNow : public Condition
{
  using DataType    = int64_t;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionNow;

public:
  explicit ConditionNow() { Dbg(dbg_ctl, "Calling CTOR for ConditionNow"); }

  // noncopyable
  ConditionNow(const SelfType &)   = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

  bool
  need_txn_private_slot() const override
  {
    return true;
  }

private:
  int64_t       get_now_qualified(NowQualifiers qual, const Resources &res) const;
  NowQualifiers _now_qual = NOW_QUAL_EPOCH;
};

// GeoIP class for the "integer" based Geo information pieces
class ConditionGeo : public Condition
{
  using SelfType = ConditionGeo;
  // This has multiple "data types" ...

public:
  explicit ConditionGeo() { Dbg(dbg_ctl, "Calling CTOR for ConditionGeo"); }

  // noncopyable
  ConditionGeo(const SelfType &)   = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

  // Make sure we know if the type is an int-type or a string.
  bool
  is_int_type() const
  {
    return _int_type;
  }

  void
  is_int_type(bool flag)
  {
    _int_type = flag;
  }

private:
  virtual int64_t     get_geo_int(const sockaddr *addr) const;
  virtual std::string get_geo_string(const sockaddr *addr) const;

protected:
  bool          eval(const Resources &res) override;
  GeoQualifiers _geo_qual = GEO_QUAL_COUNTRY;
  bool          _int_type = false;
};

// id: Various identifiers for the requests, server process etc.
class ConditionId : public Condition
{
  using SelfType = ConditionId;
  // This has multiple "data types" for matching

public:
  explicit ConditionId() { Dbg(dbg_ctl, "Calling CTOR for ConditionId"); };

  // noncopyable
  ConditionId(const SelfType &)    = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  IdQualifiers _id_qual = ID_QUAL_UNIQUE;
};

// cidr: A CIDR masked string representation of the Client's IP.
class ConditionCidr : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionCidr;

public:
  explicit ConditionCidr()
  {
    _create_masks(); // This must be called here, because we might not have parameters specified
    Dbg(dbg_ctl, "Calling CTOR for ConditionCidr");
  };

  ConditionCidr(SelfType &)       = delete;
  SelfType &operator=(SelfType &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  void           _create_masks();
  int            _v4_cidr = 24;
  int            _v6_cidr = 48;
  struct in_addr _v4_mask;       // We do a 32-bit & using this mask, for efficiency
  unsigned char  _v6_mask;       // Only need one byte here, since we memset the rest (see next)
  int            _v6_zero_bytes; // How many initial bytes to memset to 0
};

/// Information about the inbound (client) session.
class ConditionInbound : public Condition
{
  using DataType      = const sockaddr *;
  using MatcherType   = Matchers<std::string>;
  using MatcherTypeIp = Matchers<DataType>;
  using SelfType      = ConditionInbound;

public:
  explicit ConditionInbound() { Dbg(dbg_ctl, "Calling CTOR for ConditionInbound"); };
  ConditionInbound(SelfType &)    = delete;
  SelfType &operator=(SelfType &) = delete;

  void        initialize(Parser &p) override;
  void        set_qualifier(const std::string &q) override;
  void        append_value(std::string &s, const Resources &res) override;
  static void append_value(std::string &s, const Resources &res, NetworkSessionQualifiers qual);

  static constexpr const char *TAG = "INBOUND";

protected:
  bool eval(const Resources &res) override;

private:
  NetworkSessionQualifiers _net_qual = NET_QUAL_STACK;
};

class ConditionStringLiteral : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionStringLiteral;

public:
  explicit ConditionStringLiteral(const std::string &v);

  // noncopyable
  ConditionStringLiteral(const SelfType &) = delete;
  void operator=(const SelfType &)         = delete;

  void append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override;

protected:
  bool eval(const Resources & /* res ATS_UNUSED */) override;

private:
  std::string _literal;
};

// Single Session Transaction Count
class ConditionSessionTransactCount : public Condition
{
  using DataType    = int;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionSessionTransactCount;

public:
  ConditionSessionTransactCount() { Dbg(dbg_ctl, "ConditionSessionTransactCount()"); }

  // noncopyable
  ConditionSessionTransactCount(const SelfType &) = delete;
  void operator=(const SelfType &)                = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// Tcp Info
class ConditionTcpInfo : public Condition
{
  using DataType    = int;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionTcpInfo;

public:
  ConditionTcpInfo() { Dbg(dbg_ctl, "Calling CTOR for ConditionTcpInfo"); }

  // noncopyable
  ConditionTcpInfo(const SelfType &) = delete;
  void operator=(const SelfType &)   = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
  void initialize_hooks() override; // Return status only valid in certain hooks
};

// Cache Lookup Results
class ConditionCache : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<std::string>;
  using SelfType    = ConditionCache;

public:
  ConditionCache() { Dbg(dbg_ctl, "Calling CTOR for ConditionCache"); }

  // noncopyable
  ConditionCache(const SelfType &) = delete;
  void operator=(const SelfType &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// Next Hop
class ConditionNextHop : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionNextHop;

public:
  enum HostType { NAME, PORT };

  explicit ConditionNextHop() { Dbg(dbg_ctl, "Calling CTOR for ConditionNextHop"); }

  // noncopyable
  ConditionNextHop(const SelfType &) = delete;
  void operator=(const SelfType &)   = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  NextHopQualifiers _next_hop_qual = NEXT_HOP_NONE;
};

// HTTP CNTL
class ConditionHttpCntl : public Condition
{
  using SelfType = ConditionHttpCntl;

public:
  explicit ConditionHttpCntl() { Dbg(dbg_ctl, "Calling CTOR for ConditionHttpCntl"); }

  // noncopyable
  ConditionHttpCntl(const SelfType &) = delete;
  void operator=(const SelfType &)    = delete;

  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  TSHttpCntlType _http_cntl_qual = TS_HTTP_CNTL_LOGGING_MODE;
};

class ConditionGroup : public Condition
{
  using SelfType = ConditionGroup;

public:
  ConditionGroup() { Dbg(dbg_ctl, "Calling CTOR for ConditionGroup"); }

  ~ConditionGroup() override
  {
    Dbg(dbg_ctl, "Calling DTOR for ConditionGroup");
    delete _cond;
  }

  void
  set_qualifier(const std::string &q) override
  {
    Condition::set_qualifier(q);

    if (!q.empty()) { // Anything goes here, but prefer END
      _end = true;
    }
  }

  // noncopyable
  ConditionGroup(const SelfType &) = delete;
  void operator=(const SelfType &) = delete;

  bool
  closes() const
  {
    return _end;
  }

  void
  append_value(std::string & /* s ATS_UNUSED */, const Resources & /* res ATS_UNUSED */) override
  {
    TSAssert(!"%{GROUP} should never be used as a condition value!");
    TSError("[%s] %%{GROUP} should never be used as a condition value!", PLUGIN_NAME);
  }

  void
  add_condition(Condition *cond)
  {
    if (_cond) {
      _cond->append(cond);
    } else {
      _cond = cond;
    }
  }

  // This can't be protected, because we actually evaluate this condition directly from the Ruleset
  bool
  eval(const Resources &res) override
  {
    Dbg(pi_dbg_ctl, "Evaluating GROUP()");

    if (_cond) {
      return _cond->do_eval(res);
    } else {
      return true;
    }
  }

private:
  Condition *_cond = nullptr; // First pre-condition (linked list)
  bool       _end  = false;
};

// State Flags
class ConditionStateFlag : public Condition
{
  using SelfType = ConditionStateFlag;
  // No matcher for this, it's all easy peasy

public:
  explicit ConditionStateFlag()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for ConditionStateFlag");
  }

  // noncopyable
  ConditionStateFlag(const SelfType &) = delete;
  void operator=(const SelfType &)     = delete;

  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  int      _flag_ix = -1;
  uint64_t _mask    = 0;
};

// INT8 state variables
class ConditionStateInt8 : public Condition
{
  using DataType    = uint8_t;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionStateInt8;

public:
  explicit ConditionStateInt8()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for ConditionStateInt8");
  }

  // noncopyable
  ConditionStateInt8(const SelfType &) = delete;
  void operator=(const SelfType &)     = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  // Little helper function to extract out the data from the TXN user pointer
  uint8_t
  _get_data(const Resources &res) const
  {
    TSAssert(_byte_ix >= 0 && _byte_ix < NUM_STATE_INT8S);
    auto    ptr  = reinterpret_cast<uint64_t>(TSUserArgGet(res.txnp, _txn_slot));
    uint8_t data = (ptr & STATE_INT8_MASKS[_byte_ix]) >> (NUM_STATE_FLAGS + _byte_ix * 8);

    return data;
  }

  int _byte_ix = -1;
};

// INT16 state variables
class ConditionStateInt16 : public Condition
{
  using DataType    = uint16_t;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionStateInt16;

public:
  explicit ConditionStateInt16()
  {
    static_assert(sizeof(void *) == 8, "State Variables requires a 64-bit system.");
    Dbg(dbg_ctl, "Calling CTOR for ConditionStateInt16");
  }

  // noncopyable
  ConditionStateInt16(const SelfType &) = delete;
  void operator=(const SelfType &)      = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

  bool
  need_txn_slot() const override
  {
    return true;
  }

private:
  // Little helper function to extract out the data from the TXN user pointer
  uint16_t
  _get_data(const Resources &res) const
  {
    auto ptr = reinterpret_cast<uint64_t>(TSUserArgGet(res.txnp, _txn_slot));

    return ((ptr & STATE_INT16_MASK) >> 48);
  }
};

// Last regex capture
class ConditionLastCapture : public Condition
{
  using DataType    = std::string;
  using MatcherType = Matchers<DataType>;
  using SelfType    = ConditionLastCapture;

public:
  explicit ConditionLastCapture() { Dbg(dbg_ctl, "Calling CTOR for ConditionLastCapture"); }

  // noncopyable
  ConditionLastCapture(const SelfType &) = delete;
  void operator=(const SelfType &)       = delete;

  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  int _ix = -1;
};
