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
#include "lulu.h"

// ConditionStatus
void
ConditionStatus::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType {
    auto status = Parser::parseNumeric<DataType>(s);
    if (status > 999) {
      throw std::runtime_error("Invalid status code: " + s);
    }
    return status;
  });
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
  Dbg(pi_dbg_ctl, "Evaluating STATUS()");

  return static_cast<MatcherType *>(_matcher)->test(res.resp_status, res);
}

void
ConditionStatus::append_value(std::string &s, const Resources &res)
{
  s += std::to_string(res.resp_status);
  Dbg(pi_dbg_ctl, "Appending STATUS(%d) to evaluation value -> %s", res.resp_status, s.c_str());
}

// ConditionMethod
void
ConditionMethod::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods());
  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
}

bool
ConditionMethod::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  Dbg(pi_dbg_ctl, "Evaluating METHOD()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}

void
ConditionMethod::append_value(std::string &s, const Resources &res)
{
  TSMBuffer bufp;
  TSMLoc    hdr_loc;
  int       len;

  bufp    = res.client_bufp;
  hdr_loc = res.client_hdr_loc;

  if (bufp && hdr_loc) {
    const char *value = TSHttpHdrMethodGet(bufp, hdr_loc, &len);
    Dbg(pi_dbg_ctl, "Appending METHOD(%s) to evaluation value -> %.*s", _qualifier.c_str(), len, value);
    s.append(value, len);
  }
}

// ConditionRandom: random 0 to (N-1)
void
ConditionRandom::initialize(Parser &p)
{
  struct timeval tv;
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  gettimeofday(&tv, nullptr);
  _seed = getpid() * tv.tv_usec;
  _max  = strtol(_qualifier.c_str(), nullptr, 10);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

bool
ConditionRandom::eval(const Resources &res)
{
  Dbg(pi_dbg_ctl, "Evaluating RANDOM()");
  return static_cast<const MatcherType *>(_matcher)->test(rand_r(&_seed) % _max, res);
}

void
ConditionRandom::append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
{
  s += std::to_string(rand_r(&_seed) % _max);
  Dbg(pi_dbg_ctl, "Appending RANDOM(%d) to evaluation value -> %s", _max, s.c_str());
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
  Dbg(pi_dbg_ctl, "Evaluating ACCESS(%s) -> %d", _qualifier.c_str(), _last);

  return _last;
}

// ConditionHeader: request or response header
void
ConditionHeader::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods());
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
  TSMLoc    hdr_loc;
  int       len;

  if (_client) {
    bufp    = res.client_bufp;
    hdr_loc = res.client_hdr_loc;
  } else {
    bufp    = res.bufp;
    hdr_loc = res.hdr_loc;
  }

  if (bufp && hdr_loc) {
    TSMLoc field_loc;

    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, _qualifier_wks ? _qualifier_wks : _qualifier.c_str(), _qualifier.size());
    Dbg(pi_dbg_ctl, "Getting Header: %s, field_loc: %p", _qualifier.c_str(), field_loc);

    while (field_loc) {
      const char *value          = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);
      TSMLoc      next_field_loc = TSMimeHdrFieldNextDup(bufp, hdr_loc, field_loc);

      Dbg(pi_dbg_ctl, "Appending HEADER(%s) to evaluation value -> %.*s", _qualifier.c_str(), len, value);
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
  Dbg(pi_dbg_ctl, "Evaluating HEADER()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}

// ConditionUrl: request or response header. TODO: This is not finished, at all!!!
void
ConditionUrl::initialize(Parser &p)
{
  Condition::initialize(p);

  auto *match = new MatcherType(_cond_op);
  match->set(p.get_arg(), mods());
  _matcher = match;
}

void
ConditionUrl::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{URL:%s}", q.c_str());
  _url_qual = parse_url_qualifier(q);
}

void
ConditionUrl::append_value(std::string &s, const Resources &res)
{
  TSMLoc    url  = nullptr;
  TSMBuffer bufp = nullptr;

  if (_type == CLIENT) {
    // CLIENT always uses the pristine URL
    Dbg(pi_dbg_ctl, "   Using the pristine url");
    if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url) != TS_SUCCESS) {
      TSError("[%s] Error getting the pristine URL", PLUGIN_NAME);
      return;
    }
  } else if (res._rri != nullptr) {
    // called at the remap hook
    bufp = res._rri->requestBufp;
    if (_type == URL) {
      Dbg(pi_dbg_ctl, "   Using the request url");
      url = res._rri->requestUrl;
    } else if (_type == FROM) {
      Dbg(pi_dbg_ctl, "   Using the from url");
      url = res._rri->mapFromUrl;
    } else if (_type == TO) {
      Dbg(pi_dbg_ctl, "   Using the to url");
      url = res._rri->mapToUrl;
    } else {
      TSError("[%s] Invalid option value", PLUGIN_NAME);
      return;
    }
  } else {
    if (_type == URL) {
      bufp           = res.bufp;
      TSMLoc hdr_loc = res.hdr_loc;
      if (TSHttpHdrUrlGet(bufp, hdr_loc, &url) != TS_SUCCESS) {
        TSError("[%s] Error getting the URL", PLUGIN_NAME);
        return;
      }
    } else {
      TSError("[%s] Rule not supported at this hook", PLUGIN_NAME);
      return;
    }
  }

  int         i;
  const char *q_str;

  switch (_url_qual) {
  case URL_QUAL_HOST:
    q_str = TSUrlHostGet(bufp, url, &i);
    s.append(q_str, i);
    Dbg(pi_dbg_ctl, "   Host to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_PORT:
    i = TSUrlPortGet(bufp, url);
    s.append(std::to_string(i));
    Dbg(pi_dbg_ctl, "   Port to match is: %d", i);
    break;
  case URL_QUAL_PATH:
    q_str = TSUrlPathGet(bufp, url, &i);
    s.append(q_str, i);
    Dbg(pi_dbg_ctl, "   Path to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_QUERY:
    q_str = TSUrlHttpQueryGet(bufp, url, &i);
    s.append(q_str, i);
    Dbg(pi_dbg_ctl, "   Query parameters to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_SCHEME:
    q_str = TSUrlSchemeGet(bufp, url, &i);
    s.append(q_str, i);
    Dbg(pi_dbg_ctl, "   Scheme to match is: %.*s", i, q_str);
    break;
  case URL_QUAL_URL:
  case URL_QUAL_NONE: {
    // TSUrlStringGet returns an allocated char * we must free
    char *non_const_q_str = TSUrlStringGet(bufp, url, &i);
    s.append(non_const_q_str, i);
    Dbg(pi_dbg_ctl, "   URL to match is: %.*s", i, non_const_q_str);
    TSfree(non_const_q_str);
    break;
  }
  }
}

bool
ConditionUrl::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);

  return static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);
}

// ConditionDBM: do a lookup against a DBM
void
ConditionDBM::initialize(Parser &p)
{
  Condition::initialize(p);

  auto *match = new MatcherType(_cond_op);
  match->set(p.get_arg(), mods());
  _matcher = match;

  std::string::size_type pos = _qualifier.find_first_of(',');

  if (pos != std::string::npos) {
    _file = _qualifier.substr(0, pos);
    //_dbm = mdbm_open(_file.c_str(), O_RDONLY, 0, 0, 0);
    // if (NULL != _dbm) {
    //   Dbg(pi_dbg_ctl, "Opened DBM file %s", _file.c_str());
    //   _key.set_value(_qualifier.substr(pos + 1));
    // } else {
    //   TSError("[%s] Failed to open DBM file: %s", PLUGIN_NAME, _file.c_str());
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

  //   Dbg(pi_dbg_ctl, "Looking up DBM(\"%s\")", key.c_str());
  //   k.dptr = const_cast<char*>(key.c_str());
  //   k.dsize = key.size();

  //   TSMutexLock(_mutex);
  //   //v = mdbm_fetch(_dbm, k);
  //   TSMutexUnlock(_mutex);
  //   if (v.dsize > 0) {
  //     Dbg(pi_dbg_ctl, "Appending DBM(%.*s) to evaluation value -> %.*s", k.dsize, k.dptr, v.dsize, v.dptr);
  //     s.append(v.dptr, v.dsize);
  //   }
  // }
}

bool
ConditionDBM::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  Dbg(pi_dbg_ctl, "Evaluating DBM()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}

// ConditionCookie: request or response header
void
ConditionCookie::initialize(Parser &p)
{
  Condition::initialize(p);

  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods());
  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
}

void
ConditionCookie::append_value(std::string &s, const Resources &res)
{
  TSMBuffer         bufp    = res.client_bufp;
  TSMLoc            hdr_loc = res.client_hdr_loc;
  TSMLoc            field_loc;
  int               error;
  int               cookies_len;
  int               cookie_value_len;
  const char       *cookies;
  const char       *cookie_value;
  const char *const cookie_name     = _qualifier.c_str();
  const int         cookie_name_len = _qualifier.length();

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

  Dbg(pi_dbg_ctl, "Appending COOKIE(%s) to evaluation value -> %.*s", cookie_name, cookie_value_len, cookie_value);
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
  Dbg(pi_dbg_ctl, "Evaluating COOKIE()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}

// ConditionInternalTxn: Is the txn internal?
bool
ConditionInternalTxn::eval(const Resources &res)
{
  bool ret = (0 != TSHttpTxnIsInternal(res.txnp));

  Dbg(pi_dbg_ctl, "Evaluating INTERNAL-TRANSACTION() -> %d", ret);
  return ret;
}

void
ConditionIp::initialize(Parser &p)
{
  Condition::initialize(p);

  if (_cond_op == MATCH_IP_RANGES) { // Special hack for IP ranges
    MatcherTypeIp *match = new MatcherTypeIp(_cond_op);

    match->set(p.get_arg(), mods(), [](const std::string & /*s*/) { return static_cast<const sockaddr *>(nullptr); });
    _matcher = match;
  } else {
    auto *match = new MatcherType(_cond_op);

    match->set(p.get_arg(), mods());
    _matcher = match;
  }
}

void
ConditionIp::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{IP:%s} qualifier", q.c_str());

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
  if (_matcher->op() == MATCH_IP_RANGES) {
    const sockaddr *addr = nullptr;

    switch (_ip_qual) {
    case IP_QUAL_CLIENT:
      addr = TSHttpTxnClientAddrGet(res.txnp);
      break;
    case IP_QUAL_INBOUND:
      addr = TSHttpTxnIncomingAddrGet(res.txnp);
      break;
    case IP_QUAL_SERVER:
      addr = TSHttpTxnServerAddrGet(res.txnp);
      break;
    case IP_QUAL_OUTBOUND:
      addr = TSHttpTxnOutgoingAddrGet(res.txnp);
      break;
    }

    if (addr) {
      return static_cast<const Matchers<const sockaddr *> *>(_matcher)->test(addr, res);
    } else {
      return false;
    }
  } else {
    std::string s;

    append_value(s, res);
    bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);

    Dbg(pi_dbg_ctl, "Evaluating IP(): %s - rval: %d", s.c_str(), rval);

    return rval;
  }
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
    Dbg(pi_dbg_ctl, "Requesting output ip");
    ip_set = (nullptr != getIP(TSHttpTxnOutgoingAddrGet(res.txnp), ip));
    break;
  }

  if (ip_set) {
    s += ip;
  }
}

// ConditionTransactCount
void
ConditionTransactCount::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

bool
ConditionTransactCount::eval(const Resources &res)
{
  TSHttpSsn ssn = TSHttpTxnSsnGet(res.txnp);

  if (ssn) {
    int n = TSHttpSsnTransactionCount(ssn);

    Dbg(pi_dbg_ctl, "Evaluating TXN-COUNT()");
    return static_cast<MatcherType *>(_matcher)->test(n, res);
  }

  Dbg(pi_dbg_ctl, "\tNo session found, returning false");
  return false;
}

void
ConditionTransactCount::append_value(std::string &s, Resources const &res)
{
  TSHttpSsn ssn = TSHttpTxnSsnGet(res.txnp);

  if (ssn) {
    char value[32]; // enough for UINT64_MAX
    int  count  = TSHttpSsnTransactionCount(ssn);
    int  length = ink_fast_itoa(count, value, sizeof(value));

    if (length > 0) {
      Dbg(pi_dbg_ctl, "Appending TXN-COUNT %s to evaluation value %.*s", _qualifier.c_str(), length, value);
      s.append(value, length);
    }
  }
}

// ConditionNow: time related conditions, such as time since epoch (default), hour, day etc.
// Time related functionality for statements. We return an int64_t here, to assure that
// gettimeofday() / Epoch does not lose bits.
int64_t
ConditionNow::get_now_qualified(NowQualifiers qual, const Resources &resources) const
{
  time_t now;

  // First short circuit for the Epoch qualifier, since it needs less data
  time(&now);
  if (NOW_QUAL_EPOCH == qual) {
    return static_cast<int64_t>(now);
  } else {
    struct tm res;

    PrivateSlotData private_data;
    private_data.raw = reinterpret_cast<uint64_t>(TSUserArgGet(resources.txnp, _txn_private_slot));
    if (private_data.timezone == 1) {
      gmtime_r(&now, &res);
    } else {
      localtime_r(&now, &res);
    }

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

  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

void
ConditionNow::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{NOW:%s} qualifier", q.c_str());

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
ConditionNow::append_value(std::string &s, const Resources &res)
{
  s += std::to_string(get_now_qualified(_now_qual, res));
  Dbg(pi_dbg_ctl, "Appending NOW() to evaluation value -> %s", s.c_str());
}

bool
ConditionNow::eval(const Resources &res)
{
  int64_t now = get_now_qualified(_now_qual, res);

  Dbg(pi_dbg_ctl, "Evaluating NOW()");
  return static_cast<const MatcherType *>(_matcher)->test(now, res);
}

std::string
ConditionGeo::get_geo_string(const sockaddr * /* addr ATS_UNUSED */) const
{
  TSError("[%s] No Geo library available!", PLUGIN_NAME);
  return "";
}

int64_t
ConditionGeo::get_geo_int(const sockaddr * /* addr ATS_UNUSED */) const
{
  TSError("[%s] No Geo library available!", PLUGIN_NAME);
  return 0;
}

void
ConditionGeo::initialize(Parser &p)
{
  Condition::initialize(p);

  if (is_int_type()) {
    auto *match = new Matchers<int64_t>(_cond_op);

    match->set(p.get_arg(), mods(), [](const std::string &s) -> int64_t { return Parser::parseNumeric<int64_t>(s); });
    _matcher = match;
  } else {
    // The default is to have a string matcher
    Matchers<std::string> *match = new Matchers<std::string>(_cond_op);

    match->set(p.get_arg(), mods());
    _matcher = match;
  }
}

void
ConditionGeo::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{GEO:%s} qualifier", q.c_str());

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
  if (is_int_type()) {
    s += std::to_string(get_geo_int(TSHttpTxnClientAddrGet(res.txnp)));
  } else {
    s += get_geo_string(TSHttpTxnClientAddrGet(res.txnp));
  }
  Dbg(pi_dbg_ctl, "Appending GEO() to evaluation value -> %s", s.c_str());
}

bool
ConditionGeo::eval(const Resources &res)
{
  bool ret = false;

  Dbg(pi_dbg_ctl, "Evaluating GEO()");
  if (is_int_type()) {
    int64_t geo = get_geo_int(TSHttpTxnClientAddrGet(res.txnp));

    ret = static_cast<const Matchers<int64_t> *>(_matcher)->test(geo, res);
  } else {
    std::string s;

    append_value(s, res);
    ret = static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);
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
    auto *match = new Matchers<uint64_t>(_cond_op);

    match->set(p.get_arg(), mods(), [](const std::string &s) -> uint64_t { return Parser::parseNumeric<uint64_t>(s); });
    _matcher = match;
  } else {
    // The default is to have a string matcher
    Matchers<std::string> *match = new Matchers<std::string>(_cond_op);

    match->set(p.get_arg(), mods());
    _matcher = match;
  }
}

void
ConditionId::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{ID:%s} qualifier", q.c_str());

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
    s += std::to_string(TSHttpTxnIdGet(res.txnp));
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
  Dbg(pi_dbg_ctl, "Appending ID() to evaluation value -> %s", s.c_str());
}

bool
ConditionId::eval(const Resources &res)
{
  if (_id_qual == ID_QUAL_REQUEST) {
    uint64_t id = TSHttpTxnIdGet(res.txnp);

    Dbg(pi_dbg_ctl, "Evaluating GEO() -> %" PRIu64, id);
    return static_cast<const Matchers<uint64_t> *>(_matcher)->test(id, res);
  } else {
    std::string s;

    append_value(s, res);
    bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);

    Dbg(pi_dbg_ctl, "Evaluating ID(): %s - rval: %d", s.c_str(), rval);
    return rval;
  }
}

void
ConditionCidr::initialize(Parser &p)
{
  Condition::initialize(p);

  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods());
  _matcher = match;
}

void
ConditionCidr::set_qualifier(const std::string &q)
{
  bool  ok = true;
  int   cidr;
  char *endp;

  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{CIDR:%s} qualifier", q.c_str());
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
  Dbg(pi_dbg_ctl, "Evaluating CIDR()");

  return static_cast<MatcherType *>(_matcher)->test(s, res);
}

void
ConditionCidr::append_value(std::string &s, const Resources &res)
{
  struct sockaddr const *addr = TSHttpTxnClientAddrGet(res.txnp);

  if (addr) {
    switch (addr->sa_family) {
    case AF_INET: {
      char           resource[INET_ADDRSTRLEN];
      struct in_addr ipv4 = reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr;

      ipv4.s_addr &= _v4_mask.s_addr;
      inet_ntop(AF_INET, &ipv4, resource, INET_ADDRSTRLEN);
      if (resource[0]) {
        s += resource;
      }
    } break;
    case AF_INET6: {
      char            resource[INET6_ADDRSTRLEN];
      struct in6_addr ipv6 = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

      if (_v6_zero_bytes > 0) {
        memset(&ipv6.s6_addr[16 - _v6_zero_bytes], 0, _v6_zero_bytes);
      }
      if (_v6_mask != 0xff) {
        ipv6.s6_addr[16 - _v6_zero_bytes] &= _v6_mask;
      }
      inet_ntop(AF_INET6, &ipv6, resource, INET6_ADDRSTRLEN);
      if (resource[0]) {
        s += resource;
      }
    } break;
    }
  } else {
    s += "0.0.0.0"; // No client addr for some reason ...
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

  if (_cond_op == MATCH_IP_RANGES) { // Special hack for IP ranges for now ...
    MatcherTypeIp *match = new MatcherTypeIp(_cond_op);

    match->set(p.get_arg(), mods(), [](const std::string & /* s */) { return static_cast<const sockaddr *>(nullptr); });
    _matcher = match;
  } else {
    auto *match = new MatcherType(_cond_op);

    match->set(p.get_arg(), mods());
    _matcher = match;
  }
}

void
ConditionInbound::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{%s:%s} qualifier", TAG, q.c_str());

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
  // Special hack for IP-Ranges since we really don't need to do a string conversion for the comparison.
  if (_matcher->op() == MATCH_IP_RANGES) {
    const sockaddr *addr = nullptr;

    switch (_net_qual) {
    case NET_QUAL_LOCAL_ADDR:
      addr = TSHttpTxnIncomingAddrGet(res.txnp);
      break;
    case NET_QUAL_REMOTE_ADDR:
      addr = TSHttpTxnClientAddrGet(res.txnp);
      break;
    default:
      // Only support actual IP addresses of course...
      TSError("[%s] %%{%s:%s} is not supported, only IP-Addresses allowed", PLUGIN_NAME, TAG, get_qualifier().c_str());
      break;
    }

    if (addr) {
      return static_cast<const Matchers<const sockaddr *> *>(_matcher)->test(addr, res);
    } else {
      return false;
    }
  } else {
    std::string s;

    append_value(s, res);
    bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);

    Dbg(pi_dbg_ctl, "Evaluating %s(): %s - rval: %d", TAG, s.c_str(), rval);

    return rval;
  }
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
  char        text[INET6_ADDRSTRLEN];

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
    std::array<char const *, 8> tags  = {};
    int                         count = 0;
    size_t                      len   = 0;
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
  Dbg(dbg_ctl, "Calling CTOR for ConditionStringLiteral");
  _literal = v;
}

void
ConditionStringLiteral::append_value(std::string &s, const Resources & /* res ATS_UNUSED */)
{
  s += _literal;
  Dbg(pi_dbg_ctl, "Appending '%s' to evaluation value", _literal.c_str());
}

bool
ConditionStringLiteral::eval(const Resources &res)
{
  Dbg(pi_dbg_ctl, "Evaluating StringLiteral");

  return static_cast<const MatcherType *>(_matcher)->test(_literal, res);
}

// ConditionSessionTransactCount
void
ConditionSessionTransactCount::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

bool
ConditionSessionTransactCount::eval(const Resources &res)
{
  int const val = TSHttpTxnServerSsnTransactionCount(res.txnp);

  Dbg(pi_dbg_ctl, "Evaluating SSN-TXN-COUNT()");
  return static_cast<MatcherType *>(_matcher)->test(val, res);
}

void
ConditionSessionTransactCount::append_value(std::string &s, Resources const &res)
{
  char      value[32]; // enough for UINT64_MAX
  int const count  = TSHttpTxnServerSsnTransactionCount(res.txnp);
  int const length = ink_fast_itoa(count, value, sizeof(value));

  if (length > 0) {
    Dbg(pi_dbg_ctl, "Appending SSN-TXN-COUNT %s to evaluation value %.*s", _qualifier.c_str(), length, value);
    s.append(value, length);
  }
}

void
ConditionTcpInfo::initialize(Parser &p)
{
  Condition::initialize(p);
  Dbg(pi_dbg_ctl, "Initializing TCP Info");
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

void
ConditionTcpInfo::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_TXN_START_HOOK);
  add_allowed_hook(TS_HTTP_TXN_CLOSE_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
}

bool
ConditionTcpInfo::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);

  Dbg(pi_dbg_ctl, "Evaluating TCP-Info: %s - rval: %d", s.c_str(), rval);

  return rval;
}

void
ConditionTcpInfo::append_value(std::string &s, [[maybe_unused]] Resources const &res)
{
#if defined(TCP_INFO) && defined(HAVE_STRUCT_TCP_INFO)
  if (TSHttpTxnIsInternal(res.txnp)) {
    Dbg(pi_dbg_ctl, "No TCP-INFO available for internal transactions");
    return;
  }
  TSReturnCode    tsSsn;
  int             fd;
  struct tcp_info info;
  socklen_t       tcp_info_len = sizeof(info);
  tsSsn                        = TSHttpTxnClientFdGet(res.txnp, &fd);
  if (tsSsn != TS_SUCCESS || fd <= 0) {
    Dbg(pi_dbg_ctl, "error getting the client socket fd from ssn");
  }
  if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &tcp_info_len) != 0) {
    Dbg(pi_dbg_ctl, "getsockopt(%d, TCP_INFO) failed: %s", fd, strerror(errno));
  }

  if (tsSsn == TS_SUCCESS) {
    if (tcp_info_len > 0) {
      char buf[12 * 4 + 9]; // 4x uint32's + 4x "; " + '\0'
#if defined(HAVE_STRUCT_TCP_INFO_TCPI_TOTAL_RETRANS)
      // Linux 2.6.12+
      snprintf(buf, sizeof(buf), "%" PRIu32 ";%" PRIu32 ";%" PRIu32 ";%" PRIu32 "", info.tcpi_rtt, info.tcpi_rto,
               info.tcpi_snd_cwnd, info.tcpi_retrans);
#elif defined(HAVE_STRUCT_TCP_INFO___TCPI_RETRANS)
      // FreeBSD 6.0+
      snprintf(buf, sizeof(buf), "%" PRIu32 ";%" PRIu32 ";%" PRIu32 ";%" PRIu32 "", info.tcpi_rtt, info.tcpi_rto,
               info.tcpi_snd_cwnd, info.__tcpi_retrans);
#endif
      s += buf;
    }
  }
#else
  s += "-";
#endif
}

void
ConditionCache::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods());
  _matcher = match;
}

bool
ConditionCache::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  Dbg(pi_dbg_ctl, "Evaluating CACHE()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}

void
ConditionCache::append_value(std::string &s, const Resources &res)
{
  TSHttpTxn txn = res.txnp;
  int       status;

  static const char *names[] = {
    "miss",      // TS_CACHE_LOOKUP_MISS,
    "hit-stale", // TS_CACHE_LOOKUP_HIT_STALE,
    "hit-fresh", // TS_CACHE_LOOKUP_HIT_FRESH,
    "skipped"    // TS_CACHE_LOOKUP_SKIPPED
  };

  Dbg(pi_dbg_ctl, "Appending CACHE() to evaluation value -> %s", s.c_str());

  if (TSHttpTxnCacheLookupStatusGet(txn, &status) == TS_ERROR || status < 0 || status >= 4) {
    Dbg(pi_dbg_ctl, "Cache Status Invalid: %d", status);

    s += "none";
  } else {
    Dbg(pi_dbg_ctl, "Cache Status Valid: %d", status);

    s += names[status];
  }
}

// ConditionNextHop: request header.
void
ConditionNextHop::initialize(Parser &p)
{
  Condition::initialize(p);

  auto *match = new MatcherType(_cond_op);
  match->set(p.get_arg(), mods());
  _matcher = match;
}

void
ConditionNextHop::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);
  Dbg(pi_dbg_ctl, "\tParsing %%{NEXT-HOP:%s}", q.c_str());
  _next_hop_qual = parse_next_hop_qualifier(q);
}

void
ConditionNextHop::append_value(std::string &s, const Resources &res)
{
  switch (_next_hop_qual) {
  case NEXT_HOP_HOST: {
    char const *const name = TSHttpTxnNextHopNameGet(res.txnp);
    if (nullptr != name) {
      Dbg(pi_dbg_ctl, "Appending '%s' to evaluation value", name);
      s.append(name);
    } else {
      Dbg(pi_dbg_ctl, "NextHopName is empty");
    }
  } break;
  case NEXT_HOP_PORT: {
    int const port = TSHttpTxnNextHopPortGet(res.txnp);
    Dbg(pi_dbg_ctl, "Appending '%d' to evaluation value", port);
    s.append(std::to_string(port));
  } break;
  default:
    TSReleaseAssert(!"All cases should have been handled");
    break;
  }
}

bool
ConditionNextHop::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);

  return static_cast<const Matchers<std::string> *>(_matcher)->test(s, res);
}

// ConditionHttpCntl: request header.
void
ConditionHttpCntl::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  Dbg(pi_dbg_ctl, "\tParsing %%{HTTP-CNTL:%s}", q.c_str());
  _http_cntl_qual = parse_http_cntl_qualifier(q);
}

void
ConditionHttpCntl::append_value(std::string &s, const Resources &res)
{
  s += TSHttpTxnCntlGet(res.txnp, _http_cntl_qual) ? "TRUE" : "FALSE";
  Dbg(pi_dbg_ctl, "Evaluating HTTP-CNTL(%s)", _qualifier.c_str());
}

bool
ConditionHttpCntl::eval(const Resources &res)
{
  Dbg(pi_dbg_ctl, "Evaluating HTTP-CNTL()");
  return TSHttpTxnCntlGet(res.txnp, _http_cntl_qual);
}

// ConditionStateFlag
void
ConditionStateFlag::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  _flag_ix = strtol(q.c_str(), nullptr, 10);
  if (_flag_ix < 0 || _flag_ix >= NUM_STATE_FLAGS) {
    TSError("[%s] STATE-FLAG index out of range: %s", PLUGIN_NAME, q.c_str());
  } else {
    Dbg(pi_dbg_ctl, "\tParsing %%{STATE-FLAG:%s}", q.c_str());
    _mask = 1ULL << _flag_ix;
  }
}

void
ConditionStateFlag::append_value(std::string &s, const Resources &res)
{
  s += eval(res) ? "TRUE" : "FALSE";
  Dbg(pi_dbg_ctl, "Evaluating STATE-FLAG(%d)", _flag_ix);
}

bool
ConditionStateFlag::eval(const Resources &res)
{
  auto data = reinterpret_cast<uint64_t>(TSUserArgGet(res.txnp, _txn_slot));

  Dbg(pi_dbg_ctl, "Evaluating STATE-FLAG()");

  return (data & _mask) == _mask;
}

// ConditionStateInt8
void
ConditionStateInt8::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

void
ConditionStateInt8::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  _byte_ix = strtol(q.c_str(), nullptr, 10);
  if (_byte_ix < 0 || _byte_ix >= NUM_STATE_INT8S) {
    TSError("[%s] STATE-INT8 index out of range: %s", PLUGIN_NAME, q.c_str());
  } else {
    Dbg(pi_dbg_ctl, "\tParsing %%{STATE-INT8:%s}", q.c_str());
  }
}

void
ConditionStateInt8::append_value(std::string &s, const Resources &res)
{
  uint8_t data = _get_data(res);

  s += std::to_string(data);

  Dbg(pi_dbg_ctl, "Appending STATE-INT8(%d) to evaluation value -> %s", data, s.c_str());
}

bool
ConditionStateInt8::eval(const Resources &res)
{
  uint8_t data = _get_data(res);

  Dbg(pi_dbg_ctl, "Evaluating STATE-INT8()");

  return static_cast<const MatcherType *>(_matcher)->test(data, res);
}

// ConditionStateInt16
void
ConditionStateInt16::initialize(Parser &p)
{
  Condition::initialize(p);
  auto *match = new MatcherType(_cond_op);

  match->set(p.get_arg(), mods(), [](const std::string &s) -> DataType { return Parser::parseNumeric<DataType>(s); });
  _matcher = match;
}

void
ConditionStateInt16::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  if (!q.empty()) { // This qualifier is optional, but must be 0 if there
    long ix = strtol(q.c_str(), nullptr, 10);

    if (ix != 0) {
      TSError("[%s] STATE-INT16 index out of range: %s", PLUGIN_NAME, q.c_str());
    } else {
      Dbg(pi_dbg_ctl, "\tParsing %%{STATE-INT16:%s}", q.c_str());
    }
  }
}

void
ConditionStateInt16::append_value(std::string &s, const Resources &res)
{
  uint16_t data = _get_data(res);

  s += std::to_string(data);
  Dbg(pi_dbg_ctl, "Appending STATE-INT16(%d) to evaluation value -> %s", data, s.c_str());
}

bool
ConditionStateInt16::eval(const Resources &res)
{
  uint16_t data = _get_data(res);

  Dbg(pi_dbg_ctl, "Evaluating STATE-INT8()");

  return static_cast<const MatcherType *>(_matcher)->test(data, res);
}

// ConditionLastCapture
void
ConditionLastCapture::set_qualifier(const std::string &q)
{
  Condition::set_qualifier(q);

  if (q.empty()) {
    _ix = 0;
  } else {
    _ix = strtol(q.c_str(), nullptr, 10);
  }

  if (_ix < 0 || _ix > 9) { // Only $0 - $9
    TSError("[%s] LAST-CAPTURE index out of range: %s", PLUGIN_NAME, q.c_str());
  } else {
    Dbg(pi_dbg_ctl, "\tParsing %%{LAST-CAPTURE:%s}", q.c_str());
  }
}

void
ConditionLastCapture::append_value(std::string &s, const Resources &res)
{
  if (res.ovector_ptr && res.ovector_count > _ix) {
    int start = res.ovector[_ix * 2];
    int end   = res.ovector[_ix * 2 + 1];

    s.append(std::string_view(res.ovector_ptr).substr(start, (end - start)));
    Dbg(pi_dbg_ctl, "Evaluating LAST-CAPTURE(%d)", _ix);
  }
}

bool
ConditionLastCapture::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  Dbg(pi_dbg_ctl, "Evaluating LAST-CAPTURE()");

  return static_cast<const MatcherType *>(_matcher)->test(s, res);
}
