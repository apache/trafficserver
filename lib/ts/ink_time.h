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

  ink_time.h

  Timing routines for libts



 ****************************************************************************/

#ifndef _ink_time_h_
#define	_ink_time_h_

#include "ink_platform.h"
#include "ink_port.h"
#include "ink_hrtime.h"
#include "ink_unused.h"

/*===========================================================================*

                                 Data Types

 *===========================================================================*/

typedef time_t ink_time_t;

typedef int InkTimeDayID;

typedef struct
{
  InkTimeDayID base;
  unsigned int width;
} InkTimeDayRange;

/*===========================================================================*

                                 Prototypes

 *===========================================================================*/

#define MICRO_USER 1
#define MICRO_SYS 2
#define MICRO_REAL 3
#define UNDEFINED_TIME ((time_t)0)

uint64_t ink_microseconds(int which);
double ink_time_wall_seconds();

int ink_time_gmt_string_to_tm(char *string, struct tm *bdt);
int ink_time_gmt_tm_to_rfc1123_string(struct tm *t, char *string, int maxsize);

InkTimeDayID ink_time_tm_to_dayid(struct tm *t);
void ink_time_dump_dayid(FILE * fp, InkTimeDayID dayid);
void ink_time_dayid_to_tm(InkTimeDayID dayid, struct tm *t);
InkTimeDayRange ink_time_dayid_to_dayrange(InkTimeDayID dayid, unsigned int width);
InkTimeDayRange ink_time_chomp_off_mouthful_of_dayrange(InkTimeDayRange * dayrange_ptr, unsigned int biggest_width);
char *ink_time_dayrange_to_string(InkTimeDayRange * dayrange_ptr, char *buf);

void ink_time_current_mdy(int *m, int *dom, int *y);
void ink_time_tm_to_mdy(struct tm *t, int *m, int *dom, int *y);
void ink_time_mdy_to_tm(int m, int dom, int y, struct tm *t);
InkTimeDayID ink_time_mdy_to_dayid(int m, int dom, int y);
InkTimeDayID ink_time_current_dayid();
void ink_time_dayid_to_mdy(InkTimeDayID dayid, int *mp, int *dp, int *yp);
int ink_time_mdy_to_doy(int m, int dom, int y);
void ink_time_doy_to_mdy(int doy, int year, int *mon, int *dom, int *dow);
int ink_time_mdy_to_dow(int month, int dom, int year);
int ink_time_days_in_month(int month, int year);
int ink_time_days_in_year(int year);
int ink_time_first_day_of_year(int year);
void ink_time_day_to_string(int day, char *buffer);
void ink_time_month_to_string(int month, char *buffer);
int ink_time_string_to_month(char *str);
int ink_time_leap_year_correction(int year);

/*===========================================================================*

                              Inline Stuffage

 *===========================================================================*/

static inline int
ink_time_is_4th_year(int year)
{
  return ((year % 4) == 0);
}                               /* End ink_time_is_4th_year */


static inline int
ink_time_is_100th_year(int year)
{
  return ((year % 100) == 0);
}                               /* End ink_time_is_100th_year */


static inline int
ink_time_is_400th_year(int year)
{
  return ((year % 400) == 0);
}                               /* End ink_time_is_400th_year */

int cftime_replacement(char *s, int maxsize, const char *format, const time_t * clock);
#define cftime(s, format, clock) cftime_replacement(s, 8192, format, clock)

inkcoreapi int ink_gmtime_r(const ink_time_t * clock, struct tm *res);
ink_time_t convert_tm(const struct tm *tp);

#if defined(freebsd) || defined(openbsd)

inline int
ink_timezone()
{
  struct timeval tp;
  struct timezone tzp;
  ink_assert(!gettimeofday(&tp, &tzp));
  return tzp.tz_minuteswest * 60;
}

/* vl: not used
inline int ink_daylight() {
  struct tm atm;
  time_t t = time(NULL);
  ink_assert(!localtime_r(&t, &atm));
  return atm.tm_isdst;
}
*/

#else  // non-freebsd, non-openbsd for the else

inline int
ink_timezone()
{
  return timezone;
}

/* vl: not used - inline int ink_daylight() { return daylight; } */
#endif

inkcoreapi char *ink_ctime_r(const ink_time_t * clock, char *buf);
inkcoreapi struct tm *ink_localtime_r(const ink_time_t * clock, struct tm *res);

#endif /* #ifndef _ink_time_h_ */
