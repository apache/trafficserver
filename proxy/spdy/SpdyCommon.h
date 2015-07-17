/** @file

  SpdyCommon.h

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

#ifndef __P_SPDY_COMMON_H__
#define __P_SPDY_COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>

#include "P_Net.h"
#include "ts/ts.h"
#include "ts/ink_platform.h"
#include "ts/experimental.h"
#include <spdylay/spdylay.h>
using namespace std;

#define STATUS_200 "200 OK"
#define STATUS_304 "304 Not Modified"
#define STATUS_400 "400 Bad Request"
#define STATUS_404 "404 Not Found"
#define STATUS_405 "405 Method Not Allowed"
#define STATUS_500 "500 Internal Server Error"
#define DEFAULT_HTML "index.html"
#define SPDYD_SERVER "ATS Spdylay/" SPDYLAY_VERSION

#define atomic_fetch_and_add(a, b) __sync_fetch_and_add(&a, b)
#define atomic_fetch_and_sub(a, b) __sync_fetch_and_sub(&a, b)
#define atomic_inc(a) atomic_fetch_and_add(a, 1)
#define atomic_dec(a) atomic_fetch_and_sub(a, 1)

// SPDYlay callbacks
extern spdylay_session_callbacks spdy_callbacks;

// Configurations
extern uint32_t spdy_max_concurrent_streams;
extern uint32_t spdy_initial_window_size;
extern int32_t spdy_accept_no_activity_timeout;
extern int32_t spdy_no_activity_timeout_in;

// Statistics
extern RecRawStatBlock *spdy_rsb;

enum {
  SPDY_STAT_CURRENT_CLIENT_SESSION_COUNT,  ///< Current # of active SPDY sessions.
  SPDY_STAT_CURRENT_CLIENT_STREAM_COUNT,   ///< Current # of active SPDY streams.
  SPDY_STAT_TOTAL_TRANSACTIONS_TIME,       //< Total stream time and streams
  SPDY_STAT_TOTAL_CLIENT_CONNECTION_COUNT, //< Total connections running spdy

  SPDY_N_STATS ///< Terminal counter, NOT A STAT INDEX.
};

// Spdy Name/Value pairs
class SpdyNV
{
public:
  SpdyNV(TSFetchSM fetch_sm);
  ~SpdyNV();

  bool
  is_valid_response()
  {
    return valid_response;
  }

public:
  const char **nv;

private:
  SpdyNV();
  bool valid_response;
  void *mime_hdr;
  char status[64];
  char version[64];
};

string http_date(time_t t);
int spdy_config_load();

// Stat helper functions. ToDo: These probably should be turned into #define's as we do elsewhere
#define SPDY_INCREMENT_THREAD_DYN_STAT(_s, _t) RecIncrRawStat(spdy_rsb, _t, (int)_s, 1);

#define SPDY_DECREMENT_THREAD_DYN_STAT(_s, _t) RecIncrRawStat(spdy_rsb, _t, (int)_s, -1);

#define SPDY_SUM_THREAD_DYN_STAT(_s, _t, _v) RecIncrRawStat(spdy_rsb, _t, (int)_s, _v);

#endif
