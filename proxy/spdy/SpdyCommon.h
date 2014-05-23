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
#include "ts/libts.h"
#include "ts/experimental.h"
#include <spdylay/spdylay.h>
using namespace std;

#define STATUS_200      "200 OK"
#define STATUS_304      "304 Not Modified"
#define STATUS_400      "400 Bad Request"
#define STATUS_404      "404 Not Found"
#define STATUS_405      "405 Method Not Allowed"
#define STATUS_500      "500 Internal Server Error"
#define DEFAULT_HTML    "index.html"
#define SPDYD_SERVER    "ATS Spdylay/" SPDYLAY_VERSION

#define atomic_fetch_and_add(a, b)  __sync_fetch_and_add(&a, b)
#define atomic_fetch_and_sub(a, b)  __sync_fetch_and_sub(&a, b)
#define atomic_inc(a)   atomic_fetch_and_add(a, 1)
#define atomic_dec(a)   atomic_fetch_and_sub(a, 1)

struct SpdyConfig {
  bool verbose;
  int32_t max_concurrent_streams;
  int32_t initial_window_size;
  spdylay_session_callbacks callbacks;
};

struct Config {
  SpdyConfig spdy;
  int32_t accept_no_activity_timeout;
  int32_t no_activity_timeout_in;

  // Statistics
  /// This is the stat slot index for each statistic.
  enum StatIndex {
    STAT_ACTIVE_SESSION_COUNT, ///< Current # of active SPDY sessions.
    STAT_ACTIVE_STREAM_COUNT, ///< Current # of active SPDY streams.
    STAT_TOTAL_STREAM_COUNT, ///< Total number of streams created.
    STAT_TOTAL_STREAM_TIME,  //< Total stream time
    STAT_TOTAL_CONNECTION_COUNT, //< Total connections running spdy

    N_STATS ///< Terminal counter, NOT A STAT INDEX.
  };
  RecRawStatBlock* rsb; ///< Container for statistics.
};

// Spdy Name/Value pairs
class SpdyNV {
public:

  SpdyNV(TSFetchSM fetch_sm);
  ~SpdyNV();

public:
  const char **nv;

private:
  SpdyNV();
  void *mime_hdr;
  char status[64];
  char version[64];
};

string http_date(time_t t);
int spdy_config_load();

extern Config SPDY_CFG;

// Stat helper functions

inline void
SpdyStatIncrCount(Config::StatIndex idx, Continuation* contp) {
  RecIncrRawStatCount(SPDY_CFG.rsb, contp->mutex->thread_holding, idx, 1);
}

inline void
SpdyStatDecrCount(Config::StatIndex idx, Continuation* contp) {
  RecIncrRawStatCount(SPDY_CFG.rsb, contp->mutex->thread_holding, idx, -1);
}

inline void
SpdyStatIncr(Config::StatIndex idx, Continuation* contp, const int64_t incr) {
  RecIncrRawStat(SPDY_CFG.rsb, contp->mutex->thread_holding, idx, incr);
}

#endif
