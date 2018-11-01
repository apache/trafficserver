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
// conditions.cc: Implementation of the condition classes
//
//
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <cctype>
#include <sstream>
#include <array>
#include <atomic>

#include "ts/ts.h"

#include "conditions.h"
#include "expander.h"
#include "lulu.h"

// ConditionStatus
void
ConditionStatus::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  match->set(static_cast<TSHttpStatus>(strtol(p.get_arg().c_str(), nullptr, 10)));
  _matcher = match;

  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_RESPONSE_STATUS);
}

void
ConditionStatus::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
}

bool
ConditionStatus::eval(const Resources &res)
{
  TSDebug(PLUGIN_NAME, "Evaluating STATUS()");
  return static_cast<MatcherType *>(_matcher)->test(res.resp_status);
}

void
ConditionStatus::append_value(std::string &s, const Resources &res)
{
  s += std::to_string(res.resp_status);
  TSDebug(PLUGIN_NAME, "Appending STATUS(%d) to evaluation value -> %s", res.resp_status, s.c_str());
}

// ConditionMethod
void
ConditionMethod::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

bool
ConditionMethod::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating METHOD()");

  return static_cast<const MatcherType *>(_matcher)->test(s);
}

void
ConditionMethod::append_value(std::string &s, const Resources &res)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int len;

  bufp    = res.client_bufp;
  hdr_loc = res.client_hdr_loc;

  if (bufp && hdr_loc) {
    const char *value = TSHttpHdrMethodGet(bufp, hdr_loc, &len);
    TSDebug(PLUGIN_NAME, "Appending METHOD(%s) to evaluation value -> %.*s", _qualifier.c_str(), len, value);
    s.append(value, len);
  }
}

// ConditionRandom: random 0 to (N-1)
void
ConditionRandom::initialize(Parser &p)
{
  struct timeval tv;
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  gettimeofday(&tv, nullptr);
  _seed = getpid() * tv.tv_usec;
  _max  = strtol(_qualifier.c_str(), nullptr, 10);

  match->set(static_cast<unsigned int>(strtol(p.get_arg().c_str(), nullptr, 10)));
  _matcher = match;
}

bool
ConditionRandom::eval(const Resources & /* res ATS_UNUSED */)
{
  TSDebug(PLUGIN_NAME, "Evaluating RANDOM()");
  return static_cast<const MatcherType *>(_matcher)->test(rand_r(&_seed) % _max);
}

void
ConditionRandom::append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
{
  std::ostringstream oss;

  oss << rand_r(&_seed) % _max;
  s += oss.str();
  TSDebug(PLUGIN_NAME, "Appending RANDOM(%d) to evaluation value -> %s", _max, s.c_str());
}

// ConditionAccess: access(file)
void
ConditionAccess::initialize(Parser &p)
{
  struct timeval tv;
  Condition::initialize(p);

  gettimeofday(&tv, nullptr);

  _next = tv.tv_sec + 2;
  _last = !access(_qualifier.c_str(), R_OK);
}

void
ConditionAccess::append_value(std::string &s, const Resources &res)
{
  if (eval(res)) {
    s += "OK";
  } else {
    s += "NOT OK";
  }
}

bool
ConditionAccess::eval(const Resources & /* res ATS_UNUSED */)
{
  struct timeval tv;

  gettimeofday(&tv, nullptr);
  if (tv.tv_sec > _next) {
    // There is a small "race" here, where we could end up calling access() a few times extra. I think
    // that is OK, and not worth protecting with a lock.
    bool check = !access(_qualifier.c_str(), R_OK);

    tv.tv_sec += 2;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    _next = tv.tv_sec; // I hope this is an atomic "set"...
    _last = check;     // This sure ought to be
  }
  TSDebug(PLUGIN_NAME, "Evaluating ACCESS(%s) -> %d", _qualifier.c_str(), _last);

  return _last;
}

// ConditionHeader: request or response header
void
ConditionHeader::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
}

void
ConditionHeader::append_value(std::string &s, const Resources &res)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  int len;

  if (_client) {
    bufp    = res.client_bufp;
    hdr_loc = res.client_hdr_loc;
  } else {
    bufp    = res.bufp;
    hdr_loc = res.hdr_loc;
  }

  if (bufp && hdr_loc) {
    TSMLoc field_loc;

    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, _qualifier.c_str(), _qualifier.size());
    TSDebug(PLUGIN_NAME, "Getting Header: %s, field_loc: %p", _qualifier.c_str(), field_loc);

    while (field_loc) {
      const char *value     = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);
      TSMLoc next_field_loc = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);

      TSDebug(PLUGIN_NAME, "Appending HEADER(%s) to evaluation value -> %.*s", _qualifier.c_str(), len, value);
      s.append(value, len);
      // multiple headers with the same name must be semantically the same as one value which is comma separated
      if (next_field_loc) {
        s += ',';
      }
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
      field_loc = next_field_loc;
    }
  }
}

bool
ConditionHeader::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating HEADER()");

  return static_cast<const MatcherType *>(_matcher)->test(s);
}

// ConditionPath
void
ConditionPath::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionPath::append_value(std::string &s, const Resources &res)
{
  TSMBuffer bufp;
  TSMLoc url_loc;

  if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url_loc) == TS_SUCCESS) {
    int path_length;
    const char *path = TSUrlPathGet(bufp, url_loc, &path_length);

    if (path && path_length) {
      s.append(path, path_length);
    }

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
  }
}

bool
ConditionPath::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating PATH()");

  return static_cast<MatcherType *>(_matcher)->test(s);
}

// ConditionQuery
void
ConditionQuery::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionQuery::append_value(std::string &s, const Resources &res)
{
  int query_len     = 0;
  const char *query = TSUrlHttpQueryGet(res._rri->requestBufp, res._rri->requestUrl, &query_len);

  TSDebug(PLUGIN_NAME, "Appending QUERY to evaluation value: %.*s", query_len, query);
  s.append(query, query_len);
}

bool
ConditionQuery::eval(const Resources &res)
{
  if (nullptr != res._rri) {
    std::string s;

    append_value(s, res);
    TSDebug(PLUGIN_NAME, "Evaluating QUERY()");

    return static_cast<const MatcherType *>(_matcher)->test(s);
  }

  TSDebug(PLUGIN_NAME, "\tQUERY requires remap initialization! Evaluating to false!");
  return false;
}

// ConditionUrl: request or response header. TODO: This is not finished, at all!!!
void
ConditionUrl::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);
  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionUrl::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{URL:%s}", q.c_str());
  _url_qual = parse_url_qualifier(q);
}

void
ConditionUrl::append_value(std::string &s, const Resources &res)
{
  TSMLoc url     = nullptr;
  TSMBuffer bufp = nullptr;

  if (res._rri != nullptr) {
    // called at the remap hook
    bufp = res._rri->requestBufp;
    if (_type == URL || _type == CLIENT) {
      // res._rri->requestBufp and res.client_bufp are the same if it is at the remap hook
      TSDebug(PLUGIN_NAME, "   Using the request url");
      url = res._rri->requestUrl;
    } else if (_type == FROM) {
      TSDebug(PLUGIN_NAME, "   Using the from url");
      url = res._rri->mapFromUrl;
    } else if (_type == TO) {
      TSDebug(PLUGIN_NAME, "   Using the to url");
      url = res._rri->mapToUrl;
    } else {
      TSError("[header_rewrite] Invalid option value");
      return;
    }
  } else {
    TSMLoc hdr_loc = nullptr;
    if (_type == CLIENT) {
      bufp    = res.client_bufp;
      hdr_loc = res.client_hdr_loc;
    } else if (_type == URL) {
      bufp    = res.bufp;
      hdr_loc = res.hdr_loc;
    } else {
      TSError("[header_rewrite] Rule not supported at this hook");
      return;
    }
    if (TSHttpHdrUrlGet(bufp, hdr_loc, &url) != TS_SUCCESS) {
      TSError("[header_rewrite] Error getting the URL");
      return;
    }
  }

  int i;
  const char *q_str;

  switch (_url_qual) {
  case URL_QUAL_HOST:
    q_str = TSUrlHostGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   Host to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_PORT:
    i = TSUrlPortGet(bufp, url);
    s.append(std::to_string(i));
    TSDebug(PLUGIN_NAME, "   Port to match is: %d", i);
    break;
  case URL_QUAL_PATH:
    q_str = TSUrlPathGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   Path to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_QUERY:
    q_str = TSUrlHttpQueryGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   Query parameters to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_MATRIX:
    q_str = TSUrlHttpParamsGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   Matrix parameters to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_SCHEME:
    q_str = TSUrlSchemeGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   Scheme to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_URL:
  case URL_QUAL_NONE:
    q_str = TSUrlStringGet(bufp, url, &i);
    s.append(q_str, i);
    TSDebug(PLUGIN_NAME, "   URL to match is: %.*s", i, q_str);
    break;
  }
}

bool
ConditionUrl::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  return static_cast<const Matchers<std::string> *>(_matcher)->test(s);
}

// ConditionDBM: do a lookup against a DBM
void
ConditionDBM::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);
  match->set(p.get_arg());
  _matcher = match;

  std::string::size_type pos = _qualifier.find_first_of(',');

  if (pos != std::string::npos) {
    _file = _qualifier.substr(0, pos);
    //_dbm = mdbm_open(_file.c_str(), O_RDONLY, 0, 0, 0);
    // if (NULL != _dbm) {
    //   TSDebug(PLUGIN_NAME, "Opened DBM file %s", _file.c_str());
    //   _key.set_value(_qualifier.substr(pos + 1));
    // } else {
    //   TSError("Failed to open DBM file: %s", _file.c_str());
    // }
  } else {
    TSError("[%s] Malformed DBM condition", PLUGIN_NAME);
  }
}

void
ConditionDBM::append_value(std::string & /* s ATS_UNUSED */, const Resources & /* res ATS_UNUSED */)
{
  // std::string key;

  // if (!_dbm) {
  //   return;
  // }

  // _key.append_value(key, res);
  // if (key.size() > 0) {
  //   datum k, v;

  //   TSDebug(PLUGIN_NAME, "Looking up DBM(\"%s\")", key.c_str());
  //   k.dptr = const_cast<char*>(key.c_str());
  //   k.dsize = key.size();

  //   TSMutexLock(_mutex);
  //   //v = mdbm_fetch(_dbm, k);
  //   TSMutexUnlock(_mutex);
  //   if (v.dsize > 0) {
  //     TSDebug(PLUGIN_NAME, "Appending DBM(%.*s) to evaluation value -> %.*s", k.dsize, k.dptr, v.dsize, v.dptr);
  //     s.append(v.dptr, v.dsize);
  //   }
  // }
}

bool
ConditionDBM::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating DBM()");

  return static_cast<const MatcherType *>(_matcher)->test(s);
}

// ConditionCookie: request or response header
void
ConditionCookie::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
}

void
ConditionCookie::append_value(std::string &s, const Resources &res)
{
  TSMBuffer bufp = res.client_bufp;
  TSMLoc hdr_loc = res.client_hdr_loc;
  TSMLoc field_loc;
  int error;
  int cookies_len;
  int cookie_value_len;
  const char *cookies;
  const char *cookie_value;
  const char *const cookie_name = _qualifier.c_str();
  const int cookie_name_len     = _qualifier.length();

  // Sanity
  if (bufp == nullptr || hdr_loc == nullptr) {
    return;
  }

  // Find Cookie
  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE);
  if (field_loc == nullptr) {
    return;
  }

  // Get all cookies
  cookies = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &cookies_len);
  if (cookies == nullptr || cookies_len <= 0) {
    goto out_release_field;
  }

  // Find particular cookie's value
  error = get_cookie_value(cookies, cookies_len, cookie_name, cookie_name_len, &cookie_value, &cookie_value_len);
  if (error == TS_ERROR) {
    goto out_release_field;
  }

  TSDebug(PLUGIN_NAME, "Appending COOKIE(%s) to evaluation value -> %.*s", cookie_name, cookie_value_len, cookie_value);
  s.append(cookie_value, cookie_value_len);

// Unwind
out_release_field:
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
}

bool
ConditionCookie::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating COOKIE()");

  return static_cast<const MatcherType *>(_matcher)->test(s);
}

// ConditionInternalTxn: Is the txn internal?
bool
ConditionInternalTxn::eval(const Resources &res)
{
  bool ret = (0 != TSHttpTxnIsInternal(res.txnp));

  TSDebug(PLUGIN_NAME, "Evaluating INTERNAL-TRANSACTION() -> %d", ret);
  return ret;
}

void
ConditionIp::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionIp::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{IP:%s} qualifier", q.c_str());

  if (q == "CLIENT") {
    _ip_qual = IP_QUAL_CLIENT;
  } else if (q == "INBOUND") {
    _ip_qual = IP_QUAL_INBOUND;
  } else if (q == "SERVER") {
    _ip_qual = IP_QUAL_SERVER;
  } else if (q == "OUTBOUND") {
    _ip_qual = IP_QUAL_OUTBOUND;
  } else {
    TSError("[%s] Unknown IP() qualifier: %s", PLUGIN_NAME, q.c_str());
  }
}

bool
ConditionIp::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s);

  TSDebug(PLUGIN_NAME, "Evaluating IP(): %s - rval: %d", s.c_str(), rval);

  return rval;
}

void
ConditionIp::append_value(std::string &s, const Resources &res)
{
  bool ip_set = false;
  char ip[INET6_ADDRSTRLEN];

  switch (_ip_qual) {
  case IP_QUAL_CLIENT:
    ip_set = (nullptr != getIP(TSHttpTxnClientAddrGet(res.txnp), ip));
    break;
  case IP_QUAL_INBOUND:
    ip_set = (nullptr != getIP(TSHttpTxnIncomingAddrGet(res.txnp), ip));
    break;
  case IP_QUAL_SERVER:
    ip_set = (nullptr != getIP(TSHttpTxnServerAddrGet(res.txnp), ip));
    break;
  case IP_QUAL_OUTBOUND:
    ip_set = (nullptr != getIP(TSHttpTxnOutgoingAddrGet(res.txnp), ip));
    break;
  }

  if (ip_set) {
    s += ip;
  }
}

void
ConditionIncomingPort::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(static_cast<uint16_t>(strtoul(p.get_arg().c_str(), nullptr, 10)));
  _matcher = match;
}

bool
ConditionIncomingPort::eval(const Resources &res)
{
  uint16_t port = getPort(TSHttpTxnIncomingAddrGet(res.txnp));

  TSDebug(PLUGIN_NAME, "Evaluating INCOMING-PORT()");
  return static_cast<MatcherType *>(_matcher)->test(port);
}

void
ConditionIncomingPort::append_value(std::string &s, const Resources &res)
{
  std::ostringstream oss;
  uint16_t port = getPort(TSHttpTxnIncomingAddrGet(res.txnp));

  oss << port;
  s += oss.str();
  TSDebug(PLUGIN_NAME, "Appending %d to evaluation value -> %s", port, s.c_str());
}

// ConditionTransactCount
void
ConditionTransactCount::initialize(Parser &p)
{
  Condition::initialize(p);
  MatcherType *match     = new MatcherType(_cond_op);
  std::string const &arg = p.get_arg();

  match->set(strtol(arg.c_str(), nullptr, 10));
  _matcher = match;
}

bool
ConditionTransactCount::eval(const Resources &res)
{
  TSHttpSsn ssn = TSHttpTxnSsnGet(res.txnp);

  if (ssn) {
    int n = TSHttpSsnTransactionCount(ssn);

    TSDebug(PLUGIN_NAME, "Evaluating TXN-COUNT()");
    return static_cast<MatcherType *>(_matcher)->test(n);
  }

  TSDebug(PLUGIN_NAME, "\tNo session found, returning false");
  return false;
}

void
ConditionTransactCount::append_value(std::string &s, Resources const &res)
{
  TSHttpSsn ssn = TSHttpTxnSsnGet(res.txnp);

  if (ssn) {
    char value[32]; // enough for UINT64_MAX
    int count  = TSHttpSsnTransactionCount(ssn);
    int length = ink_fast_itoa(count, value, sizeof(value));

    if (length > 0) {
      TSDebug(PLUGIN_NAME, "Appending TXN-COUNT %s to evaluation value %.*s", _qualifier.c_str(), length, value);
      s.append(value, length);
    }
  }
}

// ConditionNow: time related conditions, such as time since epoch (default), hour, day etc.
// Time related functionality for statements. We return an int64_t here, to assure that
// gettimeofday() / Epoch does not lose bits.
int64_t
ConditionNow::get_now_qualified(NowQualifiers qual) const
{
  time_t now;

  // First short circuit for the Epoch qualifier, since it needs less data
  time(&now);
  if (NOW_QUAL_EPOCH == qual) {
    return static_cast<int64_t>(now);
  } else {
    struct tm res;

    localtime_r(&now, &res);
    switch (qual) {
    case NOW_QUAL_YEAR:
      return static_cast<int64_t>(res.tm_year + 1900); // This makes more sense
      break;
    case NOW_QUAL_MONTH:
      return static_cast<int64_t>(res.tm_mon);
      break;
    case NOW_QUAL_DAY:
      return static_cast<int64_t>(res.tm_mday);
      break;
    case NOW_QUAL_HOUR:
      return static_cast<int64_t>(res.tm_hour);
      break;
    case NOW_QUAL_MINUTE:
      return static_cast<int64_t>(res.tm_min);
      break;
    case NOW_QUAL_WEEKDAY:
      return static_cast<int64_t>(res.tm_wday);
      break;
    case NOW_QUAL_YEARDAY:
      return static_cast<int64_t>(res.tm_yday);
      break;
    default:
      TSReleaseAssert(!"All cases should have been handled");
      break;
    }
  }
  return 0;
}

void
ConditionNow::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(static_cast<int64_t>(strtol(p.get_arg().c_str(), nullptr, 10)));
  _matcher = match;
}

void
ConditionNow::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{NOW:%s} qualifier", q.c_str());

  if (q == "EPOCH") {
    _now_qual = NOW_QUAL_EPOCH;
  } else if (q == "YEAR") {
    _now_qual = NOW_QUAL_YEAR;
  } else if (q == "MONTH") {
    _now_qual = NOW_QUAL_MONTH;
  } else if (q == "DAY") {
    _now_qual = NOW_QUAL_DAY;
  } else if (q == "HOUR") {
    _now_qual = NOW_QUAL_HOUR;
  } else if (q == "MINUTE") {
    _now_qual = NOW_QUAL_MINUTE;
  } else if (q == "WEEKDAY") {
    _now_qual = NOW_QUAL_WEEKDAY;
  } else if (q == "YEARDAY") {
    _now_qual = NOW_QUAL_YEARDAY;
  } else {
    TSError("[%s] Unknown NOW() qualifier: %s", PLUGIN_NAME, q.c_str());
  }
}

void
ConditionNow::append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
{
  std::ostringstream oss;

  oss << get_now_qualified(_now_qual);
  s += oss.str();
  TSDebug(PLUGIN_NAME, "Appending NOW() to evaluation value -> %s", s.c_str());
}

bool
ConditionNow::eval(const Resources &res)
{
  int64_t now = get_now_qualified(_now_qual);

  TSDebug(PLUGIN_NAME, "Evaluating NOW()");
  return static_cast<const MatcherType *>(_matcher)->test(now);
}

// ConditionGeo: Geo-based information (integer). See ConditionGeoCountry for the string version.
#if HAVE_GEOIP_H
const char *
ConditionGeo::get_geo_string(const sockaddr *addr) const
{
  const char *ret = nullptr;
  int v           = 4;

  switch (_geo_qual) {
  // Country database
  case GEO_QUAL_COUNTRY:
    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_COUNTRY_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        ret = GeoIP_country_code_by_ipnum(gGeoIP[GEOIP_COUNTRY_EDITION], ip);
      }
      break;
    case AF_INET6: {
      if (gGeoIP[GEOIP_COUNTRY_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v   = 6;
        ret = GeoIP_country_code_by_ipnum_v6(gGeoIP[GEOIP_COUNTRY_EDITION_V6], ip);
      }
    } break;
    default:
      break;
    }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from Country: %s", v, ret);
    break;

  // ASN database
  case GEO_QUAL_ASN_NAME:
    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_ASNUM_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        ret = GeoIP_name_by_ipnum(gGeoIP[GEOIP_ASNUM_EDITION], ip);
      }
      break;
    case AF_INET6: {
      if (gGeoIP[GEOIP_ASNUM_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v   = 6;
        ret = GeoIP_name_by_ipnum_v6(gGeoIP[GEOIP_ASNUM_EDITION_V6], ip);
      }
    } break;
    default:
      break;
    }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from ASN Name: %s", v, ret);
    break;

  default:
    break;
  }

  return ret ? ret : "(unknown)";
}

int64_t
ConditionGeo::get_geo_int(const sockaddr *addr) const
{
  int64_t ret = -1;
  int v       = 4;

  switch (_geo_qual) {
  // Country Databse
  case GEO_QUAL_COUNTRY_ISO:
    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_COUNTRY_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        ret = GeoIP_id_by_ipnum(gGeoIP[GEOIP_COUNTRY_EDITION], ip);
      }
      break;
    case AF_INET6: {
      if (gGeoIP[GEOIP_COUNTRY_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v   = 6;
        ret = GeoIP_id_by_ipnum_v6(gGeoIP[GEOIP_COUNTRY_EDITION_V6], ip);
      }
    } break;
    default:
      break;
    }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from Country ISO: %" PRId64, v, ret);
    break;

  case GEO_QUAL_ASN: {
    const char *asn_name = nullptr;

    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_ASNUM_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        asn_name = GeoIP_name_by_ipnum(gGeoIP[GEOIP_ASNUM_EDITION], ip);
      }
      break;
    case AF_INET6:
      if (gGeoIP[GEOIP_ASNUM_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v        = 6;
        asn_name = GeoIP_name_by_ipnum_v6(gGeoIP[GEOIP_ASNUM_EDITION_V6], ip);
      }
      break;
    }
    if (asn_name) {
      // This is a little odd, but the strings returned are e.g. "AS1234 Acme Inc"
      while (*asn_name && !(isdigit(*asn_name))) {
        ++asn_name;
      }
      ret = strtol(asn_name, nullptr, 10);
    }
  }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from ASN #: %" PRId64, v, ret);
    break;

  // Likely shouldn't trip, should we assert?
  default:
    break;
  }

  return ret;
}

#else

// No Geo library avaiable, these are just stubs.

const char *
ConditionGeo::get_geo_string(const sockaddr *addr) const
{
  TSError("[%s] No Geo library available!", PLUGIN_NAME);
  return nullptr;
}

int64_t
ConditionGeo::get_geo_int(const sockaddr *addr) const
{
  TSError("[%s] No Geo library available!", PLUGIN_NAME);
  return 0;
}

#endif

void
ConditionGeo::initialize(Parser &p)
{
  Condition::initialize(p);

  if (is_int_type()) {
    Matchers<int64_t> *match = new Matchers<int64_t>(_cond_op);

    match->set(static_cast<int64_t>(strtol(p.get_arg().c_str(), nullptr, 10)));
    _matcher = match;
  } else {
    // The default is to have a string matcher
    Matchers<std::string> *match = new Matchers<std::string>(_cond_op);

    match->set(p.get_arg());
    _matcher = match;
  }
}

void
ConditionGeo::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{GEO:%s} qualifier", q.c_str());

  if (q == "COUNTRY") {
    _geo_qual = GEO_QUAL_COUNTRY;
    is_int_type(false);
  } else if (q == "COUNTRY-ISO") {
    _geo_qual = GEO_QUAL_COUNTRY_ISO;
    is_int_type(true);
  } else if (q == "ASN") {
    _geo_qual = GEO_QUAL_ASN;
    is_int_type(true);
  } else if (q == "ASN-NAME") {
    _geo_qual = GEO_QUAL_ASN_NAME;
    is_int_type(false);
  } else {
    TSError("[%s] Unknown Geo() qualifier: %s", PLUGIN_NAME, q.c_str());
  }
}

void
ConditionGeo::append_value(std::string &s, const Resources &res)
{
  std::ostringstream oss;

  if (is_int_type()) {
    oss << get_geo_int(TSHttpTxnClientAddrGet(res.txnp));
  } else {
    oss << get_geo_string(TSHttpTxnClientAddrGet(res.txnp));
  }
  s += oss.str();
  TSDebug(PLUGIN_NAME, "Appending GEO() to evaluation value -> %s", s.c_str());
}

bool
ConditionGeo::eval(const Resources &res)
{
  bool ret = false;

  TSDebug(PLUGIN_NAME, "Evaluating GEO()");
  if (is_int_type()) {
    int64_t geo = get_geo_int(TSHttpTxnClientAddrGet(res.txnp));

    ret = static_cast<const Matchers<int64_t> *>(_matcher)->test(geo);
  } else {
    std::string s;

    append_value(s, res);
    ret = static_cast<const Matchers<std::string> *>(_matcher)->test(s);
  }

  return ret;
}

// ConditionId: Some identifier strings, currently:
//      PROCESS: The process UUID string
//      REQUEST: The request (HttpSM::sm_id) counter
//      UNIQUE:  The combination of UUID-sm_id
void
ConditionId::initialize(Parser &p)
{
  Condition::initialize(p);

  if (_id_qual == ID_QUAL_REQUEST) {
    Matchers<uint64_t> *match = new Matchers<uint64_t>(_cond_op);

    match->set(static_cast<uint64_t>(strtol(p.get_arg().c_str(), nullptr, 10)));
    _matcher = match;
  } else {
    // The default is to have a string matcher
    Matchers<std::string> *match = new Matchers<std::string>(_cond_op);

    match->set(p.get_arg());
    _matcher = match;
  }
}

void
ConditionId::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{ID:%s} qualifier", q.c_str());

  if (q == "UNIQUE") {
    _id_qual = ID_QUAL_UNIQUE;
  } else if (q == "PROCESS") {
    _id_qual = ID_QUAL_PROCESS;
  } else if (q == "REQUEST") {
    _id_qual = ID_QUAL_REQUEST;
  } else {
    TSError("[%s] Unknown ID() qualifier: %s", PLUGIN_NAME, q.c_str());
  }
}

void
ConditionId::append_value(std::string &s, const Resources &res ATS_UNUSED)
{
  switch (_id_qual) {
  case ID_QUAL_REQUEST: {
    std::ostringstream oss;

    oss << TSHttpTxnIdGet(res.txnp);
    s += oss.str();
  } break;
  case ID_QUAL_PROCESS: {
    TSUuid process = TSProcessUuidGet();

    if (process) {
      s += TSUuidStringGet(process);
    }
  } break;
  case ID_QUAL_UNIQUE: {
    char uuid[TS_CRUUID_STRING_LEN + 1];

    if (TS_SUCCESS == TSClientRequestUuidGet(res.txnp, uuid)) {
      s += uuid;
    }
  } break;
  }
  TSDebug(PLUGIN_NAME, "Appending ID() to evaluation value -> %s", s.c_str());
}

bool
ConditionId::eval(const Resources &res)
{
  if (_id_qual == ID_QUAL_REQUEST) {
    uint64_t id = TSHttpTxnIdGet(res.txnp);

    TSDebug(PLUGIN_NAME, "Evaluating GEO() -> %" PRIu64, id);
    return static_cast<const Matchers<uint64_t> *>(_matcher)->test(id);
  } else {
    std::string s;

    append_value(s, res);
    bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s);

    TSDebug(PLUGIN_NAME, "Evaluating ID(): %s - rval: %d", s.c_str(), rval);
    return rval;
  }
}

void
ConditionCidr::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionCidr::set_qualifier(const std::string &q)
{
  bool ok = true;
  int cidr;
  char *endp;

  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{CIDR:%s} qualifier", q.c_str());
  cidr = strtol(q.c_str(), &endp, 10);
  if (cidr >= 0 && cidr <= 32) {
    _v4_mask.s_addr = UINT32_MAX >> (32 - cidr);
    _v4_cidr        = cidr;
    if (endp && (*endp == ',' || *endp == '/' || *endp == ':')) {
      cidr = strtol(endp + 1, nullptr, 10);
      if (cidr >= 0 && cidr <= 128) {
        _v6_cidr = cidr;
      } else {
        TSError("[%s] Bad CIDR mask for IPv6: %s", PLUGIN_NAME, q.c_str());
        ok = false;
      }
    }
  } else {
    TSError("[%s] Bad CIDR mask for IPv4: %s", PLUGIN_NAME, q.c_str());
    ok = false;
  }

  // Update the bit-masks
  if (ok) {
    _create_masks();
  }
}

bool
ConditionCidr::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating CIDR()");

  return static_cast<MatcherType *>(_matcher)->test(s);
}

void
ConditionCidr::append_value(std::string &s, const Resources &res)
{
  struct sockaddr const *addr = TSHttpTxnClientAddrGet(res.txnp);

  switch (addr->sa_family) {
  case AF_INET: {
    char res[INET_ADDRSTRLEN];
    struct in_addr ipv4 = reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr;

    ipv4.s_addr &= _v4_mask.s_addr;
    inet_ntop(AF_INET, &ipv4, res, INET_ADDRSTRLEN);
    if (res[0]) {
      s += res;
    }
  } break;
  case AF_INET6: {
    char res[INET6_ADDRSTRLEN];
    struct in6_addr ipv6 = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

    if (_v6_zero_bytes > 0) {
      memset(&ipv6.s6_addr[16 - _v6_zero_bytes], 0, _v6_zero_bytes);
    }
    if (_v6_mask != 0xff) {
      ipv6.s6_addr[16 - _v6_zero_bytes] &= _v6_mask;
    }
    inet_ntop(AF_INET6, &ipv6, res, INET6_ADDRSTRLEN);
    if (res[0]) {
      s += res;
    }
  } break;
  }
}

// Little helper function, to create the masks
void
ConditionCidr::_create_masks()
{
  _v4_mask.s_addr = htonl(UINT32_MAX << (32 - _v4_cidr));
  _v6_zero_bytes  = (128 - _v6_cidr) / 8;
  _v6_mask        = 0xff >> ((128 - _v6_cidr) % 8);
}

void
ConditionInbound::initialize(Parser &p)
{
  Condition::initialize(p);

  MatcherType *match = new MatcherType(_cond_op);

  match->set(p.get_arg());
  _matcher = match;
}

void
ConditionInbound::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  TSDebug(PLUGIN_NAME, "\tParsing %%{%s:%s} qualifier", TAG, q.c_str());

  if (q == "LOCAL-ADDR") {
    _net_qual = NET_QUAL_LOCAL_ADDR;
  } else if (q == "LOCAL-PORT") {
    _net_qual = NET_QUAL_LOCAL_PORT;
  } else if (q == "REMOTE-ADDR") {
    _net_qual = NET_QUAL_REMOTE_ADDR;
  } else if (q == "REMOTE-PORT") {
    _net_qual = NET_QUAL_REMOTE_PORT;
  } else if (q == "TLS") {
    _net_qual = NET_QUAL_TLS;
  } else if (q == "H2") {
    _net_qual = NET_QUAL_H2;
  } else if (q == "IPV4") {
    _net_qual = NET_QUAL_IPV4;
  } else if (q == "IPV6") {
    _net_qual = NET_QUAL_IPV6;
  } else if (q == "IP-FAMILY") {
    _net_qual = NET_QUAL_IP_FAMILY;
  } else if (q == "STACK") {
    _net_qual = NET_QUAL_STACK;
  } else {
    TSError("[%s] Unknown %s() qualifier: %s", PLUGIN_NAME, TAG, q.c_str());
  }
}

bool
ConditionInbound::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s);

  TSDebug(PLUGIN_NAME, "Evaluating %s(): %s - rval: %d", TAG, s.c_str(), rval);

  return rval;
}

void
ConditionInbound::append_value(std::string &s, const Resources &res)
{
  this->append_value(s, res, _net_qual);
}

void
ConditionInbound::append_value(std::string &s, const Resources &res, NetworkSessionQualifiers qual)
{
  const char *zret = nullptr;
  char text[INET6_ADDRSTRLEN];

  switch (qual) {
  case NET_QUAL_LOCAL_ADDR: {
    zret = getIP(TSHttpTxnIncomingAddrGet(res.txnp), text);
  } break;
  case NET_QUAL_LOCAL_PORT: {
    uint16_t port = getPort(TSHttpTxnIncomingAddrGet(res.txnp));
    snprintf(text, sizeof(text), "%d", port);
    zret = text;
  } break;
  case NET_QUAL_REMOTE_ADDR: {
    zret = getIP(TSHttpTxnClientAddrGet(res.txnp), text);
  } break;
  case NET_QUAL_REMOTE_PORT: {
    uint16_t port = getPort(TSHttpTxnClientAddrGet(res.txnp));
    snprintf(text, sizeof(text), "%d", port);
    zret = text;
  } break;
  case NET_QUAL_TLS:
    zret = TSHttpTxnClientProtocolStackContains(res.txnp, "tls/");
    break;
  case NET_QUAL_H2:
    zret = TSHttpTxnClientProtocolStackContains(res.txnp, "h2");
    break;
  case NET_QUAL_IPV4:
    zret = TSHttpTxnClientProtocolStackContains(res.txnp, "ipv4");
    break;
  case NET_QUAL_IPV6:
    zret = TSHttpTxnClientProtocolStackContains(res.txnp, "ipv6");
    break;
  case NET_QUAL_IP_FAMILY:
    zret = TSHttpTxnClientProtocolStackContains(res.txnp, "ip");
    break;
  case NET_QUAL_STACK: {
    std::array<char const *, 8> tags;
    int count  = 0;
    size_t len = 0;
    TSHttpTxnClientProtocolStackGet(res.txnp, tags.size(), tags.data(), &count);
    for (int i = 0; i < count; ++i) {
      len += 1 + strlen(tags[i]);
    }
    s.reserve(len);
    for (int i = 0; i < count; ++i) {
      if (i) {
        s += ',';
      }
      s += tags[i];
    }
  } break;
  }

  if (zret) {
    s += zret;
  }
}

ConditionStringLiteral::ConditionStringLiteral(const std::string &v)
{
  TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionStringLiteral");
  _literal = v;
}

void
ConditionStringLiteral::append_value(std::string &s, const Resources &res)
{
  s += _literal;
  TSDebug(PLUGIN_NAME, "Appending '%s' to evaluation value", _literal.c_str());
}

bool
ConditionStringLiteral::eval(const Resources &res)
{
  TSDebug(PLUGIN_NAME, "Evaluating StringLiteral");

  return static_cast<const MatcherType *>(_matcher)->test(_literal);
}

ConditionExpandableString::ConditionExpandableString(const std::string &v)
{
  TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for ConditionExpandableString");
  _value = v;
}

bool
ConditionExpandableString::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);

  return static_cast<const MatcherType *>(_matcher)->test(s);
}

void
ConditionExpandableString::append_value(std::string &s, const Resources &res)
{
  VariableExpander ve(_value);
  s += ve.expand(res);
  TSDebug(PLUGIN_NAME, "Appending to evaluation value -> %s", s.c_str());
}
