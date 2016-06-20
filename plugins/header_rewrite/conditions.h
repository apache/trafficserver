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
#ifndef __CONDITIONS_H__
#define __CONDITIONS_H__ 1

#include <string>
#include <cstring>

#include "ts/ts.h"
#include "ts/ink_string.h"

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
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
  {
    s += "TRUE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */)
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
  append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
  {
    s += "FALSE";
  }

protected:
  bool
  eval(const Resources & /* res ATS_UNUSED */)
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
public:
  ConditionStatus() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionStatus"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);
  void initialize_hooks(); // Return status only valid in certain hooks

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionStatus);
};

// Check the HTTP method
class ConditionMethod : public Condition
{
public:
  ConditionMethod() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionMethod"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionMethod);
};

// Random 0 to (N-1)
class ConditionRandom : public Condition
{
public:
  ConditionRandom() : _seed(0), _max(0) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionRandom"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

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
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionAccess);

  time_t _next;
  bool _last;
};

// cookie(name)
class ConditionCookie : public Condition
{
public:
  ConditionCookie() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionCookie"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionCookie);

  // Nginx-style cookie parsing:
  //   nginx/src/http/ngx_http_parse.c:ngx_http_parse_multi_header_lines()
  inline int
  get_cookie_value(const char *buf, int buf_len, const char *name, int name_len, const char **value, int *value_len)
  {
    const char *start, *last, *end;

    // Sanity
    if (buf == NULL || name == NULL || value == NULL || value_len == NULL)
      return TS_ERROR;

    start = buf;
    end   = buf + buf_len;

    while (start < end) {
      if (strncasecmp(start, name, name_len) != 0)
        goto skip;

      for (start += name_len; start < end && *start == ' '; start++)
        ;

      if (start == end || *start++ != '=')
        goto skip;

      while (start < end && *start == ' ') {
        start++;
      }
      for (last = start; last < end && *last != ';'; last++)
        ;

      *value_len = last - start;
      *value     = start;
      return TS_SUCCESS;
    skip:
      while (start < end) {
        char ch = *start++;
        if (ch == ';' || ch == ',')
          break;
      }
      while (start < end && *start == ' ') {
        start++;
      }
    }
    return TS_ERROR;
  };
};

// header
class ConditionHeader : public Condition
{
public:
  explicit ConditionHeader(bool client = false) : _client(client)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionHeader, client %d", client);
  };

  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionHeader);

  bool _client;
};

// path
class ConditionPath : public Condition
{
public:
  explicit ConditionPath() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionPath"); };
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionPath);
};

// query
class ConditionQuery : public Condition
{
public:
  explicit ConditionQuery() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionQuery"); };
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionQuery);
};

// url
class ConditionUrl : public Condition
{
public:
  enum UrlType { CLIENT, URL, FROM, TO };

  explicit ConditionUrl(const UrlType type) : _url_qual(URL_QUAL_NONE), _type(type)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionUrl");
  };

  void initialize(Parser &p);
  void set_qualifier(const std::string &q);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionUrl);

  UrlQualifiers _url_qual;
  UrlType _type;
};

// DBM lookups
class ConditionDBM : public Condition
{
public:
  ConditionDBM()
    : //_dbm(NULL),
      _file("")
  {
    _mutex = TSMutexCreate();
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionDBM");
  }

  ~ConditionDBM()
  {
    // if (_dbm) {
    //   mdbm_close(_dbm);
    //   _dbm = NULL;
    // }
  }

  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionDBM);

  // MDBM* _dbm;
  std::string _file;
  Value _key;
  TSMutex _mutex;
};

class ConditionInternalTxn : public Condition
{
public:
  void
  append_value(std::string & /* s ATS_UNUSED */, const Resources & /* res ATS_UNUSED */)
  {
  }

protected:
  bool eval(const Resources &res);
};

class ConditionClientIp : public Condition
{
public:
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);
};

class ConditionIncomingPort : public Condition
{
public:
  ConditionIncomingPort() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionIncomingPort"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionIncomingPort);
};

// Transact Count
class ConditionTransactCount : public Condition
{
  typedef Matchers<int> MatcherType;

public:
  ConditionTransactCount() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionTransactCount"); }
  void initialize(Parser &p);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  DISALLOW_COPY_AND_ASSIGN(ConditionTransactCount);
};

// now: Keeping track of current time / day / hour etc.
class ConditionNow : public Condition
{
public:
  explicit ConditionNow() : _now_qual(NOW_QUAL_EPOCH) { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionNow"); };
  void initialize(Parser &p);
  void set_qualifier(const std::string &q);
  void append_value(std::string &s, const Resources &res);

protected:
  bool eval(const Resources &res);

private:
  int64_t get_now_qualified(NowQualifiers qual) const;

  DISALLOW_COPY_AND_ASSIGN(ConditionNow);
  NowQualifiers _now_qual;
};

// GeoIP class for the "integer" based Geo information pieces
class ConditionGeo : public Condition
{
public:
  explicit ConditionGeo() : _geo_qual(GEO_QUAL_COUNTRY), _int_type(false)
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionGeo");
  };

  void initialize(Parser &p);
  void set_qualifier(const std::string &q);
  void append_value(std::string &s, const Resources &res);

  // These are special for this sub-class
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
  bool eval(const Resources &res);

private:
  int64_t get_geo_int(const sockaddr *addr) const;
  const char *get_geo_string(const sockaddr *addr) const;

  DISALLOW_COPY_AND_ASSIGN(ConditionGeo);

  GeoQualifiers _geo_qual;
  bool _int_type;
};

#endif // __CONDITIONS_H
