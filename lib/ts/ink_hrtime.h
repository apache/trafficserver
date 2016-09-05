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

/**************************************************************************

  ink_hrtime.h

  This file contains code supporting the Inktomi high-resolution timer.
**************************************************************************/

#if !defined(_ink_hrtime_h_)
#define _ink_hrtime_h_

#include "ts/ink_config.h"
#include "ts/ink_assert.h"
#include <time.h>
#include <sys/time.h>
#include <chrono>
#include <stdlib.h>

/** Instaneous time in high resolution.

    @internal @c system_clock or @c high_resolution_clock ? AFAICT the latter is just an alias for
    @c system_clock or @c monotonic_clock and I have had bad experiences with the flakiness of
    @c monotonic_clock ( or @c CLOCK_MONOTONIC for @c clock_gettime ). The key advantage of going direct
    to @c system_clock is not worrying about the epoch when converting to absolute time for system calls.
    Testing on Fedora 23 indicates that uses @c system_clock for @c high_resolution_clock and I suspect
    that will be the case for any OS recent enough to be supported for ATS 7.0.

    @internal Also this is probably the same as @c std::chrono::system_clock::time_point but better
    to be explicit about the time metric.
 **/
typedef std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> ts_hrtick;
/// Durations of specific units.
/// @internal Note durations have no association with any clock.
typedef std::chrono::nanoseconds ts_nanoseconds; ///< Duration in nanoseconds.
typedef std::chrono::microseconds ts_microseconds; ///< During in micro seconds.
typedef std::chrono::milliseconds ts_milliseconds; ///< Duration in milliseconds.
typedef std::chrono::seconds ts_seconds; ///< Duration in seconds.
typedef std::chrono::minutes ts_minutes; ///< Duration in minutes.

/// Value to use for the equivalent of '0'.
/// @internal Default constructor iniitializes to zero.
static const ts_hrtick TS_HRTICK_ZERO;

//#include <tsconfig/NumericType.h>
//typedef ts::NumericType<int64_t, struct ink_hrtime_tag> ink_hrtime;
//typedef int64_t ink_hrtime;

int squid_timestamp_to_buf(char *buf, unsigned int buf_size, long timestamp_sec, long timestamp_usec);
char *int64_to_str(char *buf, unsigned int buf_size, int64_t val, unsigned int *total_chars, unsigned int req_width = 0,
                   char pad_char = '0');

//////////////////////////////////////////////////////////////////////////////
//
//      Map from units to ink_hrtime values
//
//////////////////////////////////////////////////////////////////////////////

inline struct timespec
ts_hrtick_to_timespec(ts_hrtick t)
{
  struct timespec zret;
  auto ct_s = std::chrono::time_point_cast<ts_seconds>(t);
  
  zret.tv_sec = ct_s.time_since_epoch().count();
  zret.tv_nsec = (t - std::chrono::time_point_cast<ts_nanoseconds>(ct_s)).count();
  return zret;
}

# if 0
static inline struct timespec
ink_hrtime_to_timespec(ink_hrtime t)
{
  struct timespec ts;

  ts.tv_sec  = ink_hrtime_to_sec(t);
  ts.tv_nsec = t % HRTIME_SECOND;
  return (ts);
}

static inline struct timeval
ink_hrtime_to_timeval(ink_hrtime t)
{
  int64_t usecs;
  struct timeval tv;

  usecs      = ink_hrtime_to_usec(t);
  tv.tv_sec  = usecs / 1000000;
  tv.tv_usec = usecs % 1000000;
  return (tv);
}

# endif

# if 0
/*
   using Jan 1 1970 as the base year, instead of Jan 1 1601,
   which translates to (365 + 0.25)369*24*60*60 seconds   */
//#define NT_TIMEBASE_DIFFERENCE_100NSECS 116444736000000000i64

static inline ink_hrtime
ink_get_hrtime_internal()
{
#if defined(freebsd) || HAVE_CLOCK_GETTIME
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ink_hrtime_from_timespec(&ts);
#else
  timeval tv;
  gettimeofday(&tv, NULL);
  return ink_hrtime_from_timeval(&tv);
#endif
}

static inline struct timeval
ink_gettimeofday()
{
  return ink_hrtime_to_timeval(ink_get_hrtime_internal());
}
# endif

static inline std::time_t ts_get_current_time_t()
{
  return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
}

#endif /* _ink_hrtime_h_ */
