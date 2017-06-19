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
  ink_time.c

  Timing routines for libts

 ****************************************************************************/

#include "ts/ink_platform.h"
#include "ts/ink_defs.h"
#include "ts/ink_time.h"
#include "ts/ink_assert.h"
#include "ts/ink_string.h"

#include <clocale>
#include <sys/resource.h>

/*===========================================================================*

                                  Timers

 *===========================================================================*/

/*---------------------------------------------------------------------------*
  uint64_t microseconds(which)

  returns microsecond-resolution clock info
 *---------------------------------------------------------------------------*/

uint64_t
ink_microseconds(int which)
{
  struct timeval tp;
  struct rusage ru;

  switch (which) {
  case MICRO_REAL:
    gettimeofday(&tp, nullptr);
    break;
  case MICRO_USER:
    getrusage(RUSAGE_SELF, &ru);
    tp = ru.ru_utime;
    break;
  case MICRO_SYS:
    getrusage(RUSAGE_SELF, &ru);
    tp = ru.ru_stime;
    break;
  default:
    return 0;
  }

  return tp.tv_sec * 1000000 + tp.tv_usec;
}

/*---------------------------------------------------------------------------*

  double ink_time_wall_seconds()

  This routine returns a double precision number of wall clock seconds
  elapsed since some fixed time in the past.

 *---------------------------------------------------------------------------*/
double
ink_time_wall_seconds()
{
  struct timeval s_val;

  gettimeofday(&s_val, nullptr);
  return ((double)s_val.tv_sec + 0.000001 * s_val.tv_usec);
} /* End ink_time_wall_seconds */

struct dtconv {
  char *abbrev_month_names[12];
  char *month_names[12];
  char *abbrev_weekday_names[7];
  char *weekday_names[7];
  char *time_format;
  char *sdate_format;
  char *dtime_format;
  char *am_string;
  char *pm_string;
  char *ldate_format;
};

/*
 * The man page for cftime lies. It claims that it is thread safe.
 * Instead, it silently trashes the heap (by freeing things more than
 * once) when used in a mulithreaded program. Gack!
 */
int
cftime_replacement(char *s, int maxsize, const char *format, const time_t *clock)
{
  struct tm tm;

  ink_assert(ink_localtime_r(clock, &tm) != nullptr);

  return strftime(s, maxsize, format, &tm);
}

#undef cftime
/* Throw an error if they ever call plain-old cftime. */
int
cftime(char *s, char *format, const time_t *clock)
{
  (void)s;
  (void)format;
  (void)clock;
  printf("ERROR cftime is not thread safe -- call cftime_replacement\n");
  ink_assert(!"cftime");
  return 0;
}

#define DAYS_OFFSET 25508

ink_time_t
convert_tm(const struct tm *tp)
{
  static const int days[12] = {305, 336, -1, 30, 60, 91, 121, 152, 183, 213, 244, 274};

  ink_time_t t;
  int year;
  int month;
  int mday;

  year  = tp->tm_year;
  month = tp->tm_mon;
  mday  = tp->tm_mday;

  /* what should we do? */
  if ((year < 70) || (year > 137)) {
    return (ink_time_t)UNDEFINED_TIME;
  }

  mday += days[month];
  /* month base == march */
  if (month < 2) {
    year -= 1;
  }
  mday += (year * 365) + (year / 4) - (year / 100) + (year / 100 + 3) / 4;
  mday -= DAYS_OFFSET;

  t = ((mday * 24 + tp->tm_hour) * 60 + tp->tm_min) * 60 + tp->tm_sec;

  return t;
}

char *
ink_ctime_r(const ink_time_t *clock, char *buf)
{
  return ctime_r(clock, buf);
}

struct tm *
ink_localtime_r(const ink_time_t *clock, struct tm *res)
{
  return localtime_r(clock, res);
}
