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
#include <stdlib.h>
typedef int64_t ink_hrtime;

int squid_timestamp_to_buf(char *buf, unsigned int buf_size, long timestamp_sec, long timestamp_usec);
char *int64_to_str(char *buf, unsigned int buf_size, int64_t val, unsigned int *total_chars, unsigned int req_width = 0,
                   char pad_char = '0');

//////////////////////////////////////////////////////////////////////////////
//
//      Factors to multiply units by to obtain coresponding ink_hrtime values.
//
//////////////////////////////////////////////////////////////////////////////

#define HRTIME_FOREVER (10 * HRTIME_DECADE)
#define HRTIME_DECADE (10 * HRTIME_YEAR)
#define HRTIME_YEAR (365 * HRTIME_DAY + HRTIME_DAY / 4)
#define HRTIME_WEEK (7 * HRTIME_DAY)
#define HRTIME_DAY (24 * HRTIME_HOUR)
#define HRTIME_HOUR (60 * HRTIME_MINUTE)
#define HRTIME_MINUTE (60 * HRTIME_SECOND)
#define HRTIME_SECOND (1000 * HRTIME_MSECOND)
#define HRTIME_MSECOND (1000 * HRTIME_USECOND)
#define HRTIME_USECOND (1000 * HRTIME_NSECOND)
#define HRTIME_NSECOND (static_cast<ink_hrtime>(1))

#define HRTIME_APPROX_SECONDS(_x) ((_x) >> 30) // off by 7.3%
#define HRTIME_APPROX_FACTOR (((float)(1 << 30)) / (((float)HRTIME_SECOND)))

//////////////////////////////////////////////////////////////////////////////
//
//      Map from units to ink_hrtime values
//
//////////////////////////////////////////////////////////////////////////////

// simple macros

#define HRTIME_YEARS(_x) ((_x)*HRTIME_YEAR)
#define HRTIME_WEEKS(_x) ((_x)*HRTIME_WEEK)
#define HRTIME_DAYS(_x) ((_x)*HRTIME_DAY)
#define HRTIME_HOURS(_x) ((_x)*HRTIME_HOUR)
#define HRTIME_MINUTES(_x) ((_x)*HRTIME_MINUTE)
#define HRTIME_SECONDS(_x) ((_x)*HRTIME_SECOND)
#define HRTIME_MSECONDS(_x) ((_x)*HRTIME_MSECOND)
#define HRTIME_USECONDS(_x) ((_x)*HRTIME_USECOND)
#define HRTIME_NSECONDS(_x) ((_x)*HRTIME_NSECOND)

// gratuituous wrappers

static inline ink_hrtime
ink_hrtime_from_years(unsigned int years)
{
  return (HRTIME_YEARS(years));
}

static inline ink_hrtime
ink_hrtime_from_weeks(unsigned int weeks)
{
  return (HRTIME_WEEKS(weeks));
}

static inline ink_hrtime
ink_hrtime_from_days(unsigned int days)
{
  return (HRTIME_DAYS(days));
}

static inline ink_hrtime
ink_hrtime_from_mins(unsigned int mins)
{
  return (HRTIME_MINUTES(mins));
}

static inline ink_hrtime
ink_hrtime_from_sec(unsigned int sec)
{
  return (HRTIME_SECONDS(sec));
}

static inline ink_hrtime
ink_hrtime_from_msec(unsigned int msec)
{
  return (HRTIME_MSECONDS(msec));
}

static inline ink_hrtime
ink_hrtime_from_usec(unsigned int usec)
{
  return (HRTIME_USECONDS(usec));
}

static inline ink_hrtime
ink_hrtime_from_nsec(unsigned int nsec)
{
  return (HRTIME_NSECONDS(nsec));
}

static inline ink_hrtime
ink_hrtime_from_timespec(const struct timespec *ts)
{
  return ink_hrtime_from_sec(ts->tv_sec) + ink_hrtime_from_nsec(ts->tv_nsec);
}

static inline ink_hrtime
ink_hrtime_from_timeval(const struct timeval *tv)
{
  return ink_hrtime_from_sec(tv->tv_sec) + ink_hrtime_from_usec(tv->tv_usec);
}

//////////////////////////////////////////////////////////////////////////////
//
//      Map from ink_hrtime values to other units
//
//////////////////////////////////////////////////////////////////////////////

static inline ink_hrtime
ink_hrtime_to_years(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_YEAR));
}

static inline ink_hrtime
ink_hrtime_to_weeks(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_WEEK));
}

static inline ink_hrtime
ink_hrtime_to_days(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_DAY));
}

static inline ink_hrtime
ink_hrtime_to_mins(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_MINUTE));
}

static inline ink_hrtime
ink_hrtime_to_sec(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_SECOND));
}

static inline ink_hrtime
ink_hrtime_to_msec(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_MSECOND));
}

static inline ink_hrtime
ink_hrtime_to_usec(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_USECOND));
}

static inline ink_hrtime
ink_hrtime_to_nsec(ink_hrtime t)
{
  return ((ink_hrtime)(t / HRTIME_NSECOND));
}

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

/*
   using Jan 1 1970 as the base year, instead of Jan 1 1601,
   which translates to (365 + 0.25)369*24*60*60 seconds   */
#define NT_TIMEBASE_DIFFERENCE_100NSECS 116444736000000000i64

static inline ink_hrtime
ink_get_hrtime_internal()
{
#if defined(freebsd) || HAVE_CLOCK_GETTIME
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ink_hrtime_from_timespec(&ts);
#else
  timeval tv;
  gettimeofday(&tv, nullptr);
  return ink_hrtime_from_timeval(&tv);
#endif
}

static inline struct timeval
ink_gettimeofday()
{
  return ink_hrtime_to_timeval(ink_get_hrtime_internal());
}

static inline int
ink_time()
{
  return (int)ink_hrtime_to_sec(ink_get_hrtime_internal());
}

static inline int
ink_hrtime_diff_msec(ink_hrtime t1, ink_hrtime t2)
{
  return (int)ink_hrtime_to_msec(t1 - t2);
}

static inline ink_hrtime
ink_hrtime_diff(ink_hrtime t1, ink_hrtime t2)
{
  return (t1 - t2);
}

static inline ink_hrtime
ink_hrtime_add(ink_hrtime t1, ink_hrtime t2)
{
  return (t1 + t2);
}

static inline void
ink_hrtime_sleep(ink_hrtime delay)
{
  struct timespec ts = ink_hrtime_to_timespec(delay);
  nanosleep(&ts, nullptr);
}

#endif /* _ink_hrtime_h_ */
