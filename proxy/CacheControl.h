/** @file

  A brief file description

  @section license License

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

/*****************************************************************************
 *
 *  CacheControl.h - Interface to Cache Control systtem
 *
 *
 ****************************************************************************/

#pragma once

#include "P_EventSystem.h"
#include "ControlBase.h"
#include "tscore/Result.h"

struct RequestData;

const int CC_UNSET_TIME = -1;

#define CACHE_CONTROL_TIMEOUT (HRTIME_HOUR * 1)

//   Use 10 second time for purify testing under low
//     load to verify memory allocation
//#define CACHE_CONTROL_TIMEOUT            (HRTIME_SECOND*10)

enum CacheControlType {
  CC_INVALID = 0,
  CC_REVALIDATE_AFTER,
  CC_NEVER_CACHE,
  CC_STANDARD_CACHE,
  CC_IGNORE_NO_CACHE,
  CC_IGNORE_CLIENT_NO_CACHE,
  CC_IGNORE_SERVER_NO_CACHE,
  CC_PIN_IN_CACHE,
  CC_TTL_IN_CACHE,
  CC_NUM_TYPES
};

struct matcher_line;

class CacheControlResult
{
public:
  inkcoreapi CacheControlResult();
  void Print();

  // Data for external use
  //
  //   Describes the cache-control for a specific URL
  //
  int revalidate_after;
  int pin_in_cache_for;
  int ttl_in_cache;
  bool never_cache;
  bool ignore_client_no_cache;
  bool ignore_server_no_cache;
  bool ignore_client_cc_max_age;
  int cache_responses_to_cookies; ///< Override for caching cookied responses.

  // Data for internal use only
  //
  //   Keeps track of the last line number
  //    on which a parameter was set
  //   Used to tell if a parameter needs to
  //    be overriden by something that appeared
  //    earlier in the the config file
  //
  int reval_line;
  int never_line;
  int pin_line;
  int ttl_line;
  int ignore_client_line;
  int ignore_server_line;
};

inline CacheControlResult::CacheControlResult()
  : revalidate_after(CC_UNSET_TIME),
    pin_in_cache_for(CC_UNSET_TIME),
    ttl_in_cache(CC_UNSET_TIME),
    never_cache(false),
    ignore_client_no_cache(false),
    ignore_server_no_cache(false),
    ignore_client_cc_max_age(true),
    cache_responses_to_cookies(-1), // do not change value
    reval_line(-1),
    never_line(-1),
    pin_line(-1),
    ttl_line(-1),
    ignore_client_line(-1),
    ignore_server_line(-1)
{
}

class CacheControlRecord : public ControlBase
{
public:
  CacheControlRecord();
  CacheControlType directive;
  int time_arg;
  int cache_responses_to_cookies;
  Result Init(matcher_line *line_info);
  inkcoreapi void UpdateMatch(CacheControlResult *result, RequestData *rdata);
  void Print();
};

inline CacheControlRecord::CacheControlRecord() : ControlBase(), directive(CC_INVALID), time_arg(0), cache_responses_to_cookies(-1)
{
}

//
// API to outside world
//
class URL;
struct HttpConfigParams;
struct OverridableHttpConfigParams;

inkcoreapi void getCacheControl(CacheControlResult *result, HttpRequestData *rdata, OverridableHttpConfigParams *h_txn_conf,
                                char *tag = nullptr);
inkcoreapi bool host_rule_in_CacheControlTable();
inkcoreapi bool ip_rule_in_CacheControlTable();

void initCacheControl();
void reloadCacheControl();
