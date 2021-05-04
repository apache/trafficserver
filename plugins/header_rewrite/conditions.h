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
//#include <mdbm.h>

///////////////////////////////////////////////////////////////////////////////
// Condition declarations.
//

// Always true
class ConditionTrue : public Condition
{
public:
  ConditionTrue() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTrue"); }
  void
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override
  {
    s += "TRUE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */) override
  {
    TSDebug(PLUGIN_NAME, "Evaluating TRUE()");
    return true;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionTrue);
};

// Always false
class ConditionFalse : public Condition
{
public:
  ConditionFalse() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionFalse"); }
  void
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override
  {
    s += "FALSE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */) override
  {
    TSDebug(PLUGIN_NAME, "Evaluating FALSE()");
    return false;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionFalse);
};

// Check the HTTP return status
class ConditionStatus : public Condition
{
  typedef Matchers<TSHttpStatus> MatcherType;

public:
  ConditionStatus() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionStatus"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
  void initialize_hooks() override; // Return status only valid in certain hooks

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionStatus);
};

// Check the HTTP method
class ConditionMethod : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionMethod() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionMethod"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionMethod);
};

// Random 0 to (N-1)
class ConditionRandom : public Condition
{
  typedef Matchers<unsigned int> MatcherType;

public:
  ConditionRandom() : _seed(0), _max(0) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionRandom"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionRandom);

  unsigned int _seed;
  unsigned int _max;
};

// access(file)
class ConditionAccess : public Condition
{
public:
  ConditionAccess() : _next(0), _last(false) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionAccess"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionAccess);

  time_t _next;
  bool _last;
};

// cookie(name)
class ConditionCookie : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionCookie"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionCookie);

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

      for (start += name_len; start < end && *start == ' '; start++) {
      }

      if (start == end || *start++ != '=') {
        goto skip;
      }

      while (start < end && *start == ' ') {
        start++;
      }

      for (last = start; last < end && *last != ';'; last++) {
      }

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
  typedef Matchers<std::string> MatcherType;

public:
  explicit ConditionHeader(bool client = false) : _client(client)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionHeader, client %d", client);
  }

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionHeader);

  bool _client;
};

// path
class ConditionPath : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  explicit ConditionPath() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionPath"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionPath);
};

// query
class ConditionQuery : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  explicit ConditionQuery() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionQuery"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionQuery);
};

// url
class ConditionUrl : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  enum UrlType { CLIENT, URL, FROM, TO };

  explicit ConditionUrl(const UrlType type) : _url_qual(URL_QUAL_NONE), _type(type)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionUrl");
  }

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionUrl);

  UrlQualifiers _url_qual;
  UrlType _type;
};

// DBM lookups
class ConditionDBM : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionDBM()
    : //_dbm(NULL),
      _file("")
  {
    _mutex = TSMutexCreate();
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionDBM");
  }

  ~ConditionDBM() override
  {
    // if (_dbm) {
    //   mdbm_close(_dbm);
    //   _dbm = NULL;
    // }
  }

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionDBM);
  // MDBM* _dbm;
  std::string _file;
  Value _key;
  TSMutex _mutex;
};

class ConditionInternalTxn : public Condition
{
  typedef Matchers<std::string> MatcherType;

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
  typedef Matchers<std::string> MatcherType;

public:
  explicit ConditionIp() : _ip_qual(IP_QUAL_CLIENT) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionIp"); };
  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionIp);
  IpQualifiers _ip_qual;
};

class ConditionIncomingPort : public Condition
{
  typedef Matchers<uint16_t> MatcherType;

public:
  ConditionIncomingPort() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionIncomingPort"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionIncomingPort);
};

// Transact Count
class ConditionTransactCount : public Condition
{
  typedef Matchers<int> MatcherType;

public:
  ConditionTransactCount() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTransactCount"); }
  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionTransactCount);
};

// now: Keeping track of current time / day / hour etc.
class ConditionNow : public Condition
{
  typedef Matchers<int64_t> MatcherType;

public:
  explicit ConditionNow() : _now_qual(NOW_QUAL_EPOCH) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionNow"); }
  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionNow);

  int64_t get_now_qualified(NowQualifiers qual) const;
  NowQualifiers _now_qual;
};

// GeoIP class for the "integer" based Geo information pieces
class ConditionGeo : public Condition
{
public:
  explicit ConditionGeo() : _geo_qual(GEO_QUAL_COUNTRY), _int_type(false)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionGeo");
  }

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

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionGeo);

  int64_t get_geo_int(const sockaddr *addr) const;
  const char *get_geo_string(const sockaddr *addr) const;
  GeoQualifiers _geo_qual;
  bool _int_type;
};

// id: Various identifiers for the requests, server process etc.
class ConditionId : public Condition
{
public:
  explicit ConditionId() : _id_qual(ID_QUAL_UNIQUE) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionId"); };
  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionId);
  IdQualifiers _id_qual;
};

// cidr: A CIDR masked string representation of the Client's IP.
class ConditionCidr : public Condition
{
  using MatcherType = Matchers<std::string>;
  using self        = ConditionCidr;

public:
  explicit ConditionCidr()
  {
    _create_masks(); // This must be called here, because we might not have parameters specified
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionCidr");
  };

  ConditionCidr(self &) = delete;
  self &operator=(self &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  void _create_masks();
  int _v4_cidr = 24;
  int _v6_cidr = 48;
  struct in_addr _v4_mask; // We do a 32-bit & using this mask, for efficiency
  unsigned char _v6_mask;  // Only need one byte here, since we memset the rest (see next)
  int _v6_zero_bytes;      // How many initial bytes to memset to 0
};

/// Information about the inbound (client) session.
class ConditionInbound : public Condition
{
  using MatcherType = Matchers<std::string>;
  using self        = ConditionInbound;

public:
  explicit ConditionInbound() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionInbound"); };
  ConditionInbound(self &) = delete;
  self &operator=(self &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;
  static void append_value(std::string &s, const Resources &res, NetworkSessionQualifiers qual);

  static constexpr const char *TAG = "INBOUND";

protected:
  bool eval(const Resources &res) override;

private:
  NetworkSessionQualifiers _net_qual = NET_QUAL_STACK;
};

class ConditionStringLiteral : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionStringLiteral(const std::string &v);

  void append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override;

protected:
  bool eval(const Resources & /* res ATS_UNUSED */) override;

private:
  std::string _literal;
  DISALLOW_COPY_AND_ASSIGN(ConditionStringLiteral);
};

class ConditionExpandableString : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionExpandableString(const std::string &v);

  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  std::string _value;
  DISALLOW_COPY_AND_ASSIGN(ConditionExpandableString);
};

// Single Session Transaction Count
class ConditionSessionTransactCount : public Condition
{
  typedef Matchers<int> MatcherType;

public:
  ConditionSessionTransactCount() { TSDebug(PLUGIN_NAME_DBG, "ConditionSessionTransactCount()"); }

  // noncopyable
  ConditionSessionTransactCount(const ConditionSessionTransactCount &) = delete;
  void operator=(const ConditionSessionTransactCount &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// Tcp Info
class ConditionTcpInfo : public Condition
{
  typedef Matchers<int> MatcherType;

public:
  ConditionTcpInfo() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTcpInfo"); }

  // noncopyable
  ConditionTcpInfo(const ConditionTcpInfo &) = delete;
  void operator=(const ConditionTcpInfo &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
  void initialize_hooks() override; // Return status only valid in certain hooks
};
