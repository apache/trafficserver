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

  InkTime.cc

  Implements general time functions.


 ****************************************************************************/

#include "inktomi++.h"
#include "ink_platform.h"
#include "ink_unused.h"   /* MAGIC_EDITING_TAG */
#include "InkTime.h"
#include "ink_hrtime.h"

#define DAYS_OFFSET  25508

int
ink_gmtime_r(const ink_time_t * clock, struct tm *res)
{
  static const char months[] = {
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
    10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11,
    11, 11, 11, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
  };
  static const int days[12] = {
    305, 336, -1, 30, 60, 91, 121, 152, 183, 213, 244, 274
  };

  size_t t = *clock;

  size_t d, dp;

  size_t sec = t % 60;
  t /= 60;
  size_t min = t % 60;
  t /= 60;
  size_t hour = t % 24;
  t /= 24;

  /* Jan 1, 1970 was a Thursday */
  size_t wday = (4 + t) % 7;

  /* guess the year and refine the guess */
  size_t yday = t;
  size_t year = yday / 365 + 69;

  d = dp = (year * 365) + (year / 4) - (year / 100) + (year / 100 + 3) / 4 - DAYS_OFFSET - 1;

  while (dp < yday) {
    d = dp;
    year += 1;
    dp = (year * 365) + (year / 4) - (year / 100) + (year / 100 + 3) / 4 - DAYS_OFFSET - 1;
  }

  /* convert the days */
  d = yday - d;
  if ((d<0) || (d> 366))
    return -1;

  size_t month = months[d];
  if (month > 1)
    year -= 1;

  size_t mday = d - days[month] - 1;
  // year += 1900; real year

  res->tm_sec = (int) sec;
  res->tm_min = (int) min;
  res->tm_hour = (int) hour;
  res->tm_mday = (int) mday;
  res->tm_mon = (int) month;
  res->tm_year = (int) year;
  res->tm_wday = (int) wday;
  res->tm_yday = (int) yday;
  res->tm_isdst = 0;

  return 0;
}

ink_time_t
convert_tm(const struct tm * tp)
{
  static const int days[12] = {
    305, 336, -1, 30, 60, 91, 121, 152, 183, 213, 244, 274
  };

  ink_time_t t;
  int year;
  int month;
  int mday;

  year = tp->tm_year;
  month = tp->tm_mon;
  mday = tp->tm_mday;

  /* what should we do? */
  if ((year<70) || (year> 137))
    return (ink_time_t) UNDEFINED_TIME;

  mday += days[month];
  /* month base == march */
  if (month < 2)
    year -= 1;
  mday += (year * 365) + (year / 4) - (year / 100) + (year / 100 + 3) / 4;
  mday -= DAYS_OFFSET;

  t = ((mday * 24 + tp->tm_hour) * 60 + tp->tm_min) * 60 + tp->tm_sec;

  return t;
}

char *
ink_ctime_r(const ink_time_t * clock, char *buf)
{
  return ctime_r(clock, buf);
}

struct tm *
ink_localtime_r(const ink_time_t * clock, struct tm *res)
{
  return localtime_r(clock, res);
}
