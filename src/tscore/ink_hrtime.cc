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

  ink_hrtime.cc

  This file contains code supporting the Inktomi high-resolution timer.
**************************************************************************/

module;

#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"
#include "tscore/ink_defs.h"

#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif // __FreeBSD__
#include <cstring>
#include <sys/time.h>

#include <ctime>
#include <cstdint>
#include <cstdlib>

export module tscore:hrtime;

export using ink_hrtime = int64_t;

export int gSystemClock = 0; // 0 == CLOCK_REALTIME, the default

export char *
int64_to_str(char *buf, unsigned int buf_size, int64_t val, unsigned int *total_chars, unsigned int req_width = 0,
             char pad_char = '0')
{
  const unsigned int local_buf_size = 32;
  char               local_buf[local_buf_size];
  bool               using_local_buffer = false;
  bool               negative           = false;
  char              *out_buf            = buf;
  char              *working_buf;

  if (buf_size < 22) {
    // int64_t may not fit in provided buffer, use the local one
    working_buf        = &local_buf[local_buf_size - 1];
    using_local_buffer = true;
  } else {
    working_buf = &buf[buf_size - 1];
  }

  unsigned int num_chars = 1; // includes eos
  *working_buf--         = 0;

  if (val < 0) {
    val      = -val;
    negative = true;
  }

  if (val < 10) {
    *working_buf-- = '0' + static_cast<char>(val);
    ++num_chars;
  } else {
    do {
      *working_buf--  = static_cast<char>(val % 10) + '0';
      val            /= 10;
      ++num_chars;
    } while (val);
  }

  // pad with pad_char if needed
  //
  if (req_width) {
    // add minus sign if padding character is not 0
    if (negative && pad_char != '0') {
      *working_buf = '-';
      ++num_chars;
    } else {
      working_buf++;
    }
    if (req_width > buf_size) {
      req_width = buf_size;
    }
    unsigned int num_padding = 0;
    if (req_width > num_chars) {
      num_padding = req_width - num_chars;
      switch (num_padding) {
      case 3:
        *--working_buf = pad_char;
        // fallthrough

      case 2:
        *--working_buf = pad_char;
        // fallthrough

      case 1:
        *--working_buf = pad_char;
        break;

      default:
        for (unsigned int i = 0; i < num_padding; ++i, *--working_buf = pad_char) {
          ;
        }
      }
      num_chars += num_padding;
    }
    // add minus sign if padding character is 0
    if (negative && pad_char == '0') {
      if (num_padding) {
        *working_buf = '-'; // overwrite padding
      } else {
        *--working_buf = '-';
        ++num_chars;
      }
    }
  } else if (negative) {
    *working_buf = '-';
    ++num_chars;
  } else {
    working_buf++;
  }

  if (using_local_buffer) {
    if (num_chars <= buf_size) {
      memcpy(buf, working_buf, num_chars);
      // out_buf is already pointing to buf
    } else {
      // data does not fit return nullptr
      out_buf = nullptr;
    }
  }

  if (total_chars) {
    *total_chars = num_chars;
  }

  return out_buf;
}

int
squid_timestamp_to_buf(char *buf, unsigned int buf_size, long timestamp_sec, long timestamp_usec)
{
  int                res;
  const unsigned int tmp_buf_size = 32;
  char               tmp_buf[tmp_buf_size];

  unsigned int num_chars_s;
  char        *ts_s = int64_to_str(tmp_buf, tmp_buf_size - 4, timestamp_sec, &num_chars_s, 0, '0');
  ink_assert(ts_s);

  // convert milliseconds
  //
  tmp_buf[tmp_buf_size - 5] = '.';
  int              ms       = timestamp_usec / 1000;
  unsigned int     num_chars_ms;
  char ATS_UNUSED *ts_ms = int64_to_str(&tmp_buf[tmp_buf_size - 4], 4, ms, &num_chars_ms, 4, '0');
  ink_assert(ts_ms && num_chars_ms == 4);

  unsigned int chars_to_write = num_chars_s + 3; // no eos

  if (buf_size >= chars_to_write) {
    memcpy(buf, ts_s, chars_to_write);
    res = chars_to_write;
  } else {
    res = -(static_cast<int>(chars_to_write));
  }

  return res;
}

//////////////////////////////////////////////////////////////////////////////
//
//      Factors to multiply units by to obtain corresponding ink_hrtime values.
//
//////////////////////////////////////////////////////////////////////////////

#define HRTIME_FOREVER (10 * HRTIME_DECADE)
#define HRTIME_DECADE  (10 * HRTIME_YEAR)
#define HRTIME_YEAR    (365 * HRTIME_DAY + HRTIME_DAY / 4)
#define HRTIME_WEEK    (7 * HRTIME_DAY)
#define HRTIME_DAY     (24 * HRTIME_HOUR)
#define HRTIME_HOUR    (60 * HRTIME_MINUTE)
#define HRTIME_MINUTE  (60 * HRTIME_SECOND)
#define HRTIME_SECOND  (1000 * HRTIME_MSECOND)
#define HRTIME_MSECOND (1000 * HRTIME_USECOND)
#define HRTIME_USECOND (1000 * HRTIME_NSECOND)
#define HRTIME_NSECOND (static_cast<ink_hrtime>(1))

#define HRTIME_APPROX_SECONDS(_x) ((_x) >> 30) // off by 7.3%
#define HRTIME_APPROX_FACTOR      (((float)(1 << 30)) / (((float)HRTIME_SECOND)))

//////////////////////////////////////////////////////////////////////////////
//
//      Map from units to ink_hrtime values
//
//////////////////////////////////////////////////////////////////////////////

// simple macros

export template <typename T>
constexpr T
HRTIME_FOREVERS(T x)
{
  return x * HRTIME_FOREVER;
}

export template <typename T>
constexpr T
HRTIME_YEARS(T x)
{
  return x * HRTIME_YEAR;
}

export template <typename T>
constexpr T
HRTIME_WEEKS(T x)
{
  return x * HRTIME_WEEK;
}

export template <typename T>
constexpr T
HRTIME_DAYS(T x)
{
  return x * HRTIME_DAY;
}

export template <typename T>
constexpr T
HRTIME_HOURS(T x)
{
  return x * HRTIME_HOUR;
}

export template <typename T>
constexpr T
HRTIME_MINUTES(T x)
{
  return x * HRTIME_MINUTE;
}

export template <typename T>
constexpr T
HRTIME_SECONDS(T x)
{
  return x * HRTIME_SECOND;
}

export template <typename T>
constexpr T
HRTIME_MSECONDS(T x)
{
  return x * HRTIME_MSECOND;
}

export template <typename T>
constexpr T
HRTIME_USECONDS(T x)
{
  return x * HRTIME_USECOND;
}

export template <typename T>
constexpr T
HRTIME_NSECONDS(T x)
{
  return x * HRTIME_NSECOND;
}

// gratuitous wrappers

export ink_hrtime
ink_hrtime_from_years(unsigned int years)
{
  return (HRTIME_YEARS(years));
}

export ink_hrtime
ink_hrtime_from_weeks(unsigned int weeks)
{
  return (HRTIME_WEEKS(weeks));
}

export ink_hrtime
ink_hrtime_from_days(unsigned int days)
{
  return (HRTIME_DAYS(days));
}

export ink_hrtime
ink_hrtime_from_mins(unsigned int mins)
{
  return (HRTIME_MINUTES(mins));
}

export ink_hrtime
ink_hrtime_from_sec(unsigned int sec)
{
  return (HRTIME_SECONDS(sec));
}

export ink_hrtime
ink_hrtime_from_msec(unsigned int msec)
{
  return (HRTIME_MSECONDS(msec));
}

export ink_hrtime
ink_hrtime_from_usec(unsigned int usec)
{
  return (HRTIME_USECONDS(usec));
}

export ink_hrtime
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

export ink_hrtime
ink_hrtime_to_years(ink_hrtime t)
{
  return (t / HRTIME_YEAR);
}

export ink_hrtime
ink_hrtime_to_weeks(ink_hrtime t)
{
  return (t / HRTIME_WEEK);
}

export ink_hrtime
ink_hrtime_to_days(ink_hrtime t)
{
  return (t / HRTIME_DAY);
}

export ink_hrtime
ink_hrtime_to_mins(ink_hrtime t)
{
  return (t / HRTIME_MINUTE);
}

export ink_hrtime
ink_hrtime_to_sec(ink_hrtime t)
{
  return (t / HRTIME_SECOND);
}

export ink_hrtime
ink_hrtime_to_msec(ink_hrtime t)
{
  return (t / HRTIME_MSECOND);
}

export ink_hrtime
ink_hrtime_to_usec(ink_hrtime t)
{
  return (t / HRTIME_USECOND);
}

export ink_hrtime
ink_hrtime_to_nsec(ink_hrtime t)
{
  return (t / HRTIME_NSECOND);
}

export struct timespec
ink_hrtime_to_timespec(ink_hrtime t)
{
  struct timespec ts;

  ts.tv_sec  = ink_hrtime_to_sec(t);
  ts.tv_nsec = t % HRTIME_SECOND;
  return (ts);
}

export struct timeval
ink_hrtime_to_timeval(ink_hrtime t)
{
  int64_t        usecs;
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

export ink_hrtime
ink_get_hrtime()
{
#if defined(freebsd) || HAVE_CLOCK_GETTIME
  timespec ts;
  clock_gettime(static_cast<clockid_t>(gSystemClock), &ts);
  return ink_hrtime_from_timespec(&ts);
#else
  timeval tv;
  gettimeofday(&tv, nullptr);
  return ink_hrtime_from_timeval(&tv);
#endif
}

export struct timeval
ink_gettimeofday()
{
  return ink_hrtime_to_timeval(ink_get_hrtime());
}

export int
ink_time()
{
  return static_cast<int>(ink_hrtime_to_sec(ink_get_hrtime()));
}

export int
ink_hrtime_diff_msec(ink_hrtime t1, ink_hrtime t2)
{
  return static_cast<int>(ink_hrtime_to_msec(t1 - t2));
}

export ink_hrtime
ink_hrtime_diff(ink_hrtime t1, ink_hrtime t2)
{
  return (t1 - t2);
}

export ink_hrtime
ink_hrtime_add(ink_hrtime t1, ink_hrtime t2)
{
  return (t1 + t2);
}

export void
ink_hrtime_sleep(ink_hrtime delay)
{
  struct timespec ts = ink_hrtime_to_timespec(delay);
  nanosleep(&ts, nullptr);
}
