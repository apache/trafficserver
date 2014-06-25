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

#include "ts/ts.h"

#include "conditions.h"
#include "lulu.h"

// ConditionStatus
void
ConditionStatus::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<TSHttpStatus>* match = new Matchers<TSHttpStatus>(_cond_op);

  match->set(static_cast<TSHttpStatus>(strtol(p.get_arg().c_str(), NULL, 10)));
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
ConditionStatus::eval(const Resources& res)
{
  TSDebug(PLUGIN_NAME, "Evaluating STATUS()"); // TODO: It'd be nice to get the args here ...
  return static_cast<const Matchers<TSHttpStatus>*>(_matcher)->test(res.resp_status);
}


void
ConditionStatus::append_value(std::string& s, const Resources& res)
{
  s += boost::lexical_cast<std::string>(res.resp_status);
  TSDebug(PLUGIN_NAME, "Appending STATUS(%d) to evaluation value -> %s", res.resp_status, s.c_str());
}


// ConditionRandom: random 0 to (N-1)
void
ConditionRandom::initialize(Parser& p)
{
  struct timeval tv;

  Condition::initialize(p);

  gettimeofday(&tv, NULL);

  Matchers<unsigned int>* match = new Matchers<unsigned int>(_cond_op);
  _seed = getpid()* tv.tv_usec;
  _max = strtol(_qualifier.c_str(), NULL, 10);

  match->set(static_cast<unsigned int>(strtol(p.get_arg().c_str(), NULL, 10)));
  _matcher = match;
}


bool
ConditionRandom::eval(const Resources& /* res ATS_UNUSED */)
{
  TSDebug(PLUGIN_NAME, "Evaluating RANDOM(%d)", _max);
  return static_cast<const Matchers<unsigned int>*>(_matcher)->test(rand_r(&_seed) % _max);
}


void
ConditionRandom::append_value(std::string& s, const Resources& /* res ATS_UNUSED */)
{
  s += boost::lexical_cast<std::string>(rand_r(&_seed) % _max);
  TSDebug(PLUGIN_NAME, "Appending RANDOM(%d) to evaluation value -> %s", _max, s.c_str());
}


// ConditionAccess: access(file)
void
ConditionAccess::initialize(Parser& p)
{
  struct timeval tv;

  Condition::initialize(p);

  gettimeofday(&tv, NULL);

  _next = tv.tv_sec + 2;
  _last = !access(_qualifier.c_str(), R_OK);
}


void
ConditionAccess::append_value(std::string& s, const Resources& res)
{
  if (eval(res)) {
    s += "OK";
  } else {
    s += "NOT OK";
  }
}


bool
ConditionAccess::eval(const Resources& /* res ATS_UNUSED */)
{
  struct timeval tv;

  gettimeofday(&tv, NULL);

  TSDebug(PLUGIN_NAME, "Evaluating ACCESS(%s)", _qualifier.c_str());
  if (tv.tv_sec > _next) {
    // There is a small "race" here, where we could end up calling access() a few times extra. I think
    // that is OK, and not worth protecting with a lock.
    bool check = !access(_qualifier.c_str(), R_OK);

    tv.tv_sec += 2;
    mb();
    _next = tv.tv_sec; // I hope this is an atomic "set"...
    _last = check; // This sure ought to be
  }

  return _last;
}


// ConditionHeader: request or response header
void
ConditionHeader::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());

  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
  require_resources(RSRC_CLIENT_RESPONSE_HEADERS);
  require_resources(RSRC_SERVER_REQUEST_HEADERS);
  require_resources(RSRC_SERVER_RESPONSE_HEADERS);
}


void
ConditionHeader::append_value(std::string& s, const Resources& res)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc field_loc;
  const char* value;
  int len;

  if (_client) {
    bufp = res.client_bufp;
    hdr_loc = res.client_hdr_loc;
  } else {
    bufp = res.bufp;
    hdr_loc = res.hdr_loc;
  }

  if (bufp && hdr_loc) {
    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, _qualifier.c_str(), _qualifier.size());
    TSDebug(PLUGIN_NAME, "Getting Header: %s, field_loc: %p", _qualifier.c_str(), field_loc);
    if (field_loc != NULL) {
      value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &len);
      TSDebug(PLUGIN_NAME, "Appending HEADER(%s) to evaluation value -> %.*s", _qualifier.c_str(), len, value);
      s.append(value, len);
      TSHandleMLocRelease(bufp, hdr_loc, field_loc);
    }
  }
}


bool
ConditionHeader::eval(const Resources& res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string>*>(_matcher)->test(s);
  TSDebug(PLUGIN_NAME, "Evaluating HEADER(): %s - rval: %d", s.c_str(), rval);
  return rval;
}

// ConditionPath
void
ConditionPath::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());

  _matcher = match;
}

void ConditionPath::append_value(std::string& s, const Resources& res) {
  TSMBuffer bufp;
  TSMLoc url_loc;

  if (TSHttpTxnPristineUrlGet(res.txnp, &bufp, &url_loc) == TS_SUCCESS) {
    int path_length;
    const char *path = TSUrlPathGet(bufp, url_loc, &path_length);

    if (path && path_length)
      s.append(path, path_length);

    TSHandleMLocRelease(bufp, TS_NULL_MLOC, url_loc);
  }
}

bool
ConditionPath::eval(const Resources& res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating PATH");

  return static_cast<const Matchers<std::string>*>(_matcher)->test(s);
}

// ConditionQuery
void
ConditionQuery::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());
  _matcher = match;

}

void
ConditionQuery::append_value(std::string& s, const Resources& res)
{
  int query_len = 0;
  const char *query = TSUrlHttpQueryGet(res._rri->requestBufp, res._rri->requestUrl, &query_len);
  TSDebug(PLUGIN_NAME, "Appending QUERY to evaluation value: %.*s", query_len, query);
  s.append(query, query_len);
}

bool
ConditionQuery::eval(const Resources& res)
{
  std::string s;

  if (NULL == res._rri) {
    TSDebug(PLUGIN_NAME, "QUERY requires remap initialization! Evaluating to false!");
    return false;
  }
  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating QUERY - %s", s.c_str());
  return static_cast<const Matchers<std::string>*>(_matcher)->test(s);
}


// ConditionUrl: request or response header. TODO: This is not finished, at all!!!
void
ConditionUrl::initialize(Parser& /* p ATS_UNUSED */)
{
}


void
ConditionUrl::set_qualifier(const std::string& q)
{
  Condition::set_qualifier(q);

  _url_qual = parse_url_qualifier(q);
}


void
ConditionUrl::append_value(std::string& /* s ATS_UNUSED */, const Resources& /* res ATS_UNUSED */)
{
}


bool
ConditionUrl::eval(const Resources& /* res ATS_UNUSED */)
{
  bool ret = false;

  return ret;
}


// ConditionDBM: do a lookup against a DBM
void
ConditionDBM::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());
  _matcher = match;

  std::string::size_type pos = _qualifier.find_first_of(',');

  if (pos != std::string::npos) {
    _file = _qualifier.substr(0, pos);
    //_dbm = mdbm_open(_file.c_str(), O_RDONLY, 0, 0, 0);
    // if (NULL != _dbm) {
    //   TSDebug(PLUGIN_NAME, "Opened DBM file %s\n", _file.c_str());
    //   _key.set_value(_qualifier.substr(pos + 1));
    // } else {
    //   TSError("Failed to open DBM file: %s", _file.c_str());
    // }
  } else {
    TSError("%s: Malformed DBM condition", PLUGIN_NAME);
  }
}


void
ConditionDBM::append_value(std::string& /* s ATS_UNUSED */, const Resources& /* res ATS_UNUSED */)
{
  // std::string key;

  // if (!_dbm)
  //   return;

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
ConditionDBM::eval(const Resources& res)
{
  std::string s;

  append_value(s, res);
  TSDebug(PLUGIN_NAME, "Evaluating DBM(%s, \"%s\")", _file.c_str(), s.c_str());

  return static_cast<const Matchers<std::string>*>(_matcher)->test(s);
}


// ConditionCookie: request or response header
void
ConditionCookie::initialize(Parser& p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());

  _matcher = match;

  require_resources(RSRC_CLIENT_REQUEST_HEADERS);
}

void
ConditionCookie::append_value(std::string& s, const Resources& res)
{
  TSMBuffer bufp = res.client_bufp;
  TSMLoc hdr_loc = res.client_hdr_loc;
  TSMLoc field_loc;
  int error;
  int cookies_len;
  int cookie_value_len;
  const char *cookies;
  const char *cookie_value;
  const char * const cookie_name = _qualifier.c_str();
  const int cookie_name_len = _qualifier.length();

  // Sanity
  if (bufp == NULL || hdr_loc == NULL)
    return;

  // Find Cookie
  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, TS_MIME_FIELD_COOKIE, TS_MIME_LEN_COOKIE);
  if (field_loc == NULL)
    return;

  // Get all cookies
  cookies = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &cookies_len);
  if (cookies == NULL || cookies_len <= 0)
    goto out_release_field;

  // Find particular cookie's value
  error = get_cookie_value(cookies, cookies_len, cookie_name, cookie_name_len, &cookie_value, &cookie_value_len);
  if (error == TS_ERROR)
    goto out_release_field;

  TSDebug(PLUGIN_NAME, "Appending COOKIE(%s) to evaluation value -> %.*s", cookie_name, cookie_value_len, cookie_value);
  s.append(cookie_value, cookie_value_len);

  // Unwind
out_release_field:
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
}

bool
ConditionCookie::eval(const Resources& res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string>*>(_matcher)->test(s);
  TSDebug(PLUGIN_NAME, "Evaluating COOKIE(%s): %s: rval: %d", _qualifier.c_str(), s.c_str(), rval);
  return rval;
}

bool
ConditionInternalTransaction::eval(const Resources& res)
{
  return TSHttpIsInternalRequest(res.txnp) == TS_SUCCESS;
}

void
ConditionClientIp::initialize(Parser &p)
{
  Condition::initialize(p);

  Matchers<std::string>* match = new Matchers<std::string>(_cond_op);
  match->set(p.get_arg());

  _matcher = match;
}

bool
ConditionClientIp::eval(const Resources &res)
{
  std::string s;

  append_value(s, res);
  bool rval = static_cast<const Matchers<std::string>*>(_matcher)->test(s);
  TSDebug(PLUGIN_NAME, "Evaluating CLIENT-IP(): %s: rval: %d", s.c_str(), rval);
  return rval;
}

void
ConditionClientIp::append_value(std::string &s, const Resources &res)
{
  char ip[INET6_ADDRSTRLEN];

  if (getIP(TSHttpTxnClientAddrGet(res.txnp), ip)) {
    s.append(ip);
  }
}
