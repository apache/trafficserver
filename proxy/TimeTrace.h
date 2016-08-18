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

/****************************************************************************

  TimeTrace.h


 ****************************************************************************/

#ifndef _TimeTrace_h_
#define _TimeTrace_h_

// #define ENABLE_TIME_TRACE

#define TIME_DIST_BUCKETS 500
#define TIME_DIST_BUCKETS_SIZE TIME_DIST_BUCKETS + 1

#ifdef ENABLE_TIME_TRACE
extern int cdb_callback_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int cdb_cache_callbacks;

extern int callback_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int cache_callbacks;

extern int rmt_callback_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int rmt_cache_callbacks;

extern int lkrmt_callback_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int lkrmt_cache_callbacks;

extern int cntlck_acquire_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int cntlck_acquire_events;

extern int immediate_events_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int cnt_immediate_events;

extern int inmsg_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int inmsg_events;

extern int open_delay_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int open_delay_events;

extern int cluster_send_time_dist[TIME_DIST_BUCKETS_SIZE];
extern int cluster_send_events;
#endif // ENABLE_TIME_TRACE

#ifdef ENABLE_TIME_TRACE
#define LOG_EVENT_TIME(_start_time, _time_dist, _time_cnt)           \
  do {                                                               \
    ink_hrtime now      = ink_get_hrtime();                          \
    unsigned int bucket = (now - _start_time) / HRTIME_MSECONDS(10); \
    if (bucket > TIME_DIST_BUCKETS)                                  \
      bucket = TIME_DIST_BUCKETS;                                    \
    ink_atomic_increment(&_time_dist[bucket], 1);                    \
    ink_atomic_increment(&_time_cnt, 1);                             \
  } while (0)

#else // !ENABLE_TIME_TRACE
#define LOG_EVENT_TIME(_start_time, _time_dist, _time_cnt)
#endif // !ENABLE_TIME_TRACE

#endif // _TimeTrace_h_

// End of TimeTrace.h
