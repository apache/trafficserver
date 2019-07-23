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

  // noncopyable
  ConditionTrue(const ConditionTrue &) = delete;
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
    TSDebug(PLUGIN_NAME, "Evaluating TRUE()");
    return true;
  }
};

// Always false
class ConditionFalse : public Condition
{
public:
  ConditionFalse() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionFalse"); }

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
    TSDebug(PLUGIN_NAME, "Evaluating FALSE()");
    return false;
  }
};

// Check the HTTP return status
class ConditionStatus : public Condition
{
  typedef Matchers<TSHttpStatus> MatcherType;

public:
  ConditionStatus() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionStatus"); }

  // noncopyable
  ConditionStatus(const ConditionStatus &) = delete;
  void operator=(const ConditionStatus &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
  void initialize_hooks() override; // Return status only valid in certain hooks
};

// Check the HTTP method
class ConditionMethod : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionMethod() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionMethod"); }

  // noncopyable
  ConditionMethod(const ConditionMethod &) = delete;
  void operator=(const ConditionMethod &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// Random 0 to (N-1)
class ConditionRandom : public Condition
{
  typedef Matchers<unsigned int> MatcherType;

public:
  ConditionRandom() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionRandom"); }

  // noncopyable
  ConditionRandom(const ConditionRandom &) = delete;
  void operator=(const ConditionRandom &) = delete;

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
  ConditionAccess() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionAccess"); }

  // noncopyable
  ConditionAccess(const ConditionAccess &) = delete;
  void operator=(const ConditionAccess &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  time_t _next = 0;
  bool _last   = false;
};

// cookie(name)
class ConditionCookie : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionCookie"); }

  // noncopyable
  ConditionCookie(const ConditionCookie &) = delete;
  void operator=(const ConditionCookie &) = delete;

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

  // noncopyable
  ConditionHeader(const ConditionHeader &) = delete;
  void operator=(const ConditionHeader &) = delete;

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
  typedef Matchers<std::string> MatcherType;

public:
  enum UrlType { CLIENT, URL, FROM, TO };

  explicit ConditionUrl(const UrlType type) : _type(type) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionUrl"); }

  // noncopyable
  ConditionUrl(const ConditionUrl &) = delete;
  void operator=(const ConditionUrl &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  UrlQualifiers _url_qual = URL_QUAL_NONE;
  UrlType _type;
};

// DBM lookups
class ConditionDBM : public Condition
{
  typedef Matchers<std::string> MatcherType;

public:
  ConditionDBM()
    : //_dbm(NULL),
      _file(""),
      _mutex(TSMutexCreate())
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionDBM");
  }

  ~ConditionDBM() override
  {
    // if (_dbm) {
    //   mdbm_close(_dbm);
    //   _dbm = NULL;
    // }
  }

  // noncopyable
  ConditionDBM(const ConditionDBM &) = delete;
  void operator=(const ConditionDBM &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
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
  explicit ConditionIp() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionIp"); };

  // noncopyable
  ConditionIp(const ConditionIp &) = delete;
  void operator=(const ConditionIp &) = delete;

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
  typedef Matchers<int> MatcherType;

public:
  ConditionTransactCount() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTransactCount"); }

  // noncopyable
  ConditionTransactCount(const ConditionTransactCount &) = delete;
  void operator=(const ConditionTransactCount &) = delete;

  void initialize(Parser &p) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;
};

// now: Keeping track of current time / day / hour etc.
class ConditionNow : public Condition
{
  typedef Matchers<int64_t> MatcherType;

public:
  explicit ConditionNow() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionNow"); }

  // noncopyable
  ConditionNow(const ConditionNow &) = delete;
  void operator=(const ConditionNow &) = delete;

  void initialize(Parser &p) override;
  void set_qualifier(const std::string &q) override;
  void append_value(std::string &s, const Resources &res) override;

protected:
  bool eval(const Resources &res) override;

private:
  int64_t get_now_qualified(NowQualifiers qual) const;
  NowQualifiers _now_qual = NOW_QUAL_EPOCH;
};

// GeoIP class for the "integer" based Geo information pieces
class ConditionGeo : public Condition
{
public:
  explicit ConditionGeo() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionGeo"); }

  // noncopyable
  ConditionGeo(const ConditionGeo &) = delete;
  void operator=(const ConditionGeo &) = delete;

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
  int64_t get_geo_int(const sockaddr *addr) const;
  const char *get_geo_string(const sockaddr *addr) const;
  GeoQualifiers _geo_qual = GEO_QUAL_COUNTRY;
  bool _int_type          = false;
};

// id: Various identifiers for the requests, server process etc.
class ConditionId : public Condition
{
public:
  explicit ConditionId() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionId"); };

  // noncopyable
  ConditionId(const ConditionId &) = delete;
  void operator=(const ConditionId &) = delete;

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
  explicit ConditionStringLiteral(const std::string &v);

  // noncopyable
  ConditionStringLiteral(const ConditionStringLiteral &) = delete;
  void operator=(const ConditionStringLiteral &) = delete;

  void append_value(std::string &s, const Resources & /* res ATS_UNUSED */) override;

protected:
  bool eval(const Resources & /* res ATS_UNUSED */) override;

private:
  std::string _literal;
};
