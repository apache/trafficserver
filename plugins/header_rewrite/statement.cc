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
// statement.cc: Implementation of the statement base class.
//
//
#include "statement.h"

void
Statement::append(Statement *stmt)
{
  Statement *tmp = this;

  TSReleaseAssert(stmt->_next == NULL);
  while (tmp->_next)
    tmp = tmp->_next;
  tmp->_next = stmt;
}


const ResourceIDs
Statement::get_resource_ids() const
{
  const Statement *stmt = this;
  ResourceIDs ids = RSRC_NONE;

  while (stmt) {
    ids = static_cast<ResourceIDs>(ids | stmt->_rsrc);
    stmt = stmt->_next;
  }

  return ids;
}


bool
Statement::set_hook(TSHttpHookID hook)
{
  bool ret = std::find(_allowed_hooks.begin(), _allowed_hooks.end(), hook) != _allowed_hooks.end();

  if (ret)
    _hook = hook;

  return ret;
}


// This should be overridden for any Statement which only supports some hooks
void
Statement::initialize_hooks()
{
  add_allowed_hook(TS_HTTP_READ_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_PRE_REMAP_HOOK);
  add_allowed_hook(TS_HTTP_READ_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_REQUEST_HDR_HOOK);
  add_allowed_hook(TS_HTTP_SEND_RESPONSE_HDR_HOOK);
  add_allowed_hook(TS_REMAP_PSEUDO_HOOK);
}

// Time related functionality for statements. We return an int64_t here, to assure that
// gettimeofday() / Epoch does not lose bits.
int64_t
Statement::get_now_qualified(NowQualifiers qual) const
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
    case NOW_QUAL_EPOCH:
      TSReleaseAssert(!"EPOCH should have been handled before");
      break;
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
    }
  }
  return 0;
}


// Parse URL qualifiers
UrlQualifiers
Statement::parse_url_qualifier(const std::string &q) const
{
  UrlQualifiers qual = URL_QUAL_NONE;

  if (q == "HOST")
    qual = URL_QUAL_HOST;
  else if (q == "PORT")
    qual = URL_QUAL_PORT;
  else if (q == "PATH")
    qual = URL_QUAL_PATH;
  else if (q == "QUERY")
    qual = URL_QUAL_QUERY;
  else if (q == "MATRIX")
    qual = URL_QUAL_MATRIX;
  else if (q == "SCHEME")
    qual = URL_QUAL_SCHEME;
  else if (q == "URL")
    qual = URL_QUAL_URL;

  return qual;
}


// Parse NOW qualifiers
NowQualifiers
Statement::parse_now_qualifier(const std::string &q) const
{
  NowQualifiers qual = NOW_QUAL_EPOCH; // Default is seconds since epoch

  if (q == "EPOCH") {
    qual = NOW_QUAL_EPOCH;
  } else if (q == "YEAR") {
    qual = NOW_QUAL_YEAR;
  } else if (q == "MONTH") {
    qual = NOW_QUAL_MONTH;
  } else if (q == "DAY") {
    qual = NOW_QUAL_DAY;
  } else if (q == "HOUR") {
    qual = NOW_QUAL_HOUR;
  } else if (q == "MINUTE") {
    qual = NOW_QUAL_MINUTE;
  } else if (q == "WEEKDAY") {
    qual = NOW_QUAL_WEEKDAY;
  } else if (q == "YEARDAY") {
    qual = NOW_QUAL_YEARDAY;
  }

  return qual;
}
