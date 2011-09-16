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

#include "ink_platform.h"
#include "ink_port.h"
#include "ink_time.h"
#include "ink_assert.h"
#include "ink_string.h"
#include "ink_unused.h"

#include <locale.h>
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
    gettimeofday(&tp, NULL);
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

  gettimeofday(&s_val, 0);
  return ((double) s_val.tv_sec + 0.000001 * s_val.tv_usec);
}                               /* End ink_time_wall_seconds */

/*===========================================================================*

                          High-Level Date Processing

 *===========================================================================*/

/*---------------------------------------------------------------------------*

  int ink_time_gmt_string_to_tm(char *string, struct tm *broken_down_time)

  This routine takes an ASCII string representation of a date <string> in one
  of several formats, converts the string to a calendar time, and stores the
  calendar time through the pointer <broken_down_time>.

  This routine currently attempts to support RFC 1123 format, RFC 850
  format, and asctime() format.  The times are expected to be in GMT time.

  If the string is successfully parsed and converted to a date, the number
  of characters processed from the string is returned (useful for scanning
  over a string), otherwise 0 is returned.

 *---------------------------------------------------------------------------*/
int
ink_time_gmt_string_to_tm(char *string, struct tm *bdt)
{
  char *result;

  /* This declaration isn't being found in time.h for some reason */

  result = NULL;

  /* Try RFC 1123 Format */

  if (!result)
    result = strptime(string, (char *) "%a, %d %b %Y %T GMT", bdt);
  if (!result)
    result = strptime(string, (char *) "%a, %d %b %Y %T UTC", bdt);

  /* Try RFC 850 Format */

  if (!result)
    result = strptime(string, (char *) "%A, %d-%b-%y %T GMT", bdt);
  if (!result)
    result = strptime(string, (char *) "%A, %d-%b-%y %T UTC", bdt);

  /* Try asctime() Format */

  if (!result)
    result = strptime(string, (char *) "%a %b %d %T %Y", bdt);

  bdt->tm_isdst = -1;

  /* If No Success, You Lose, Pal */

  if (!result)
    return (0);

  return (result - string);
}                               /* End ink_time_gmt_string_to_tm */


/*---------------------------------------------------------------------------*

  int ink_time_gmt_tm_to_rfc1123_string(struct tm *t, char *string, int maxsize)

  This routine takes a calendar time <tm> in universal time, and converts
  the time to an RFC 1123 formatted string as in "Sun, 06 Nov 1994 08:49:37 GMT",
  which is placed in the string <string> up to <maxsize> bytes.  1 is
  returned on success, else 0.

 *---------------------------------------------------------------------------*/
int
ink_time_gmt_tm_to_rfc1123_string(struct tm *t, char *string, int maxsize)
{
  size_t size;

  size = strftime(string, maxsize, "%a, %d %b %Y %T GMT", t);
  if (size == 0) {
    if (maxsize > 0)
      string[0] = '\0';
    return (0);
  }
  return (1);
}                               /* End ink_time_gmt_tm_to_rfc1123_string */


/*---------------------------------------------------------------------------*

  InkTimeDayID ink_time_tm_to_dayid(struct tm *t)

  This routine takes a broken-down time <t>, and converts it to an
  InkTimeDayID <dayid>, representing an integer number of days since a base.

 *---------------------------------------------------------------------------*/
InkTimeDayID
ink_time_tm_to_dayid(struct tm * t)
{
  int m, dom, y;
  InkTimeDayID dayid;

  ink_time_tm_to_mdy(t, &m, &dom, &y);
  dayid = ink_time_mdy_to_dayid(m, dom, y);
  return (dayid);
}                               /* End ink_time_tm_to_dayid */


/*---------------------------------------------------------------------------*

  void ink_time_dump_dayid(FILE *fp, InkTimeDayID dayid)

  This routine prints an ASCII representation of <dayid> onto the file
  pointer <fp>.

 *---------------------------------------------------------------------------*/
void
ink_time_dump_dayid(FILE * fp, InkTimeDayID dayid)
{
  int m, d, y;

  ink_time_dayid_to_mdy(dayid, &m, &d, &y);
  fprintf(fp, "dayid %d (%d/%d/%d)\n", dayid, m, d, y);
}                               /* End ink_time_dump_dayid */


/*---------------------------------------------------------------------------*

  void ink_time_dayid_to_tm(InkTimeDayID dayid, struct tm *t)

  This routine takes an InkTimeDayID <dayid>, representing an integer number
  of days since a base, and computes a broken-down time <t>.

 *---------------------------------------------------------------------------*/
void
ink_time_dayid_to_tm(InkTimeDayID dayid, struct tm *t)
{
  int m, dom, y;

  ink_time_dayid_to_mdy(dayid, &m, &dom, &y);
  ink_time_mdy_to_tm(m, dom, y, t);
}                               /* End ink_time_dayid_to_tm */


/*---------------------------------------------------------------------------*

  InkTimeDayRange ink_time_dayid_to_dayrange(InkTimeDayID dayid, unsigned int width)

  This routine takes a <dayid> representing a particular day, and returns
  an InkTimeDayRange object of width <width> that spans the day in question.

 *---------------------------------------------------------------------------*/
InkTimeDayRange
ink_time_dayid_to_dayrange(InkTimeDayID dayid, unsigned int width)
{
  InkTimeDayRange range;

  range.base = dayid - (dayid % width);
  range.width = width;
  return (range);
}                               /* End ink_time_dayid_to_dayrange */


/*---------------------------------------------------------------------------*

  InkTimeDayRange ink_time_chomp_off_mouthful_of_dayrange
	(InkTimeDayRange *dayrange_ptr, unsigned int biggest_width)

  This routine takes a dayrange pointer <dayrange_ptr>, and bites off the
  biggest possible chunk of the dayrange pointed to by <dayrange_ptr>
  which is less than or equal to <biggest_width> and whose chunk is
  "chomp aligned", meaning that the start of the dayrange chunk starts on
  a multiple of the width.

  The value <biggest_width> must be a positive power of two.

  On exit, the chunk chomped off will be returned, and the original
  dayrange pointed to by <dayrange_ptr> will be modified to consist of
  the data after the chunk that was chopped off.

  If the dayrange pointed to by <dayrange_ptr> has no size, a dayrange
  of size 0 is returned, and the original data is unmodified.

  The purpose of this routine is to decompose a range of consecutive days
  into a collection of variable-sized, disjoint day ranges which cover the
  original space of days.

 *---------------------------------------------------------------------------*/
InkTimeDayRange
ink_time_chomp_off_mouthful_of_dayrange(InkTimeDayRange * dayrange_ptr, unsigned int biggest_width)
{
  unsigned int width;
  InkTimeDayRange chomped_chunk;

  chomped_chunk.base = dayrange_ptr->base;

  for (width = biggest_width; width >= 1; width = width / 2) {
    if ((width <= dayrange_ptr->width) && ((dayrange_ptr->base % width) == 0)) {
      chomped_chunk.width = width;

      dayrange_ptr->base += width;
      dayrange_ptr->width -= width;

      return (chomped_chunk);
    }
  }

  chomped_chunk.width = 0;

  return (chomped_chunk);
}                               /* End ink_time_chomp_off_mouthful_of_dayrange */


/*---------------------------------------------------------------------------*

  char *ink_time_dayrange_to_string(InkTimeDayRange *dayrange_ptr, char *buf)

  This routine take a day range pointer <dayrange_ptr>, and places a string
  representation of the day range in the buffer <buf>.  The buffer must be
  big enough to hold the representation of the dayrange.

  Of course, you shouldn't have any idea what the representation is, so I
  guess you're hosed.  Something like 64 characters is probably reasonable.

  The pointer <buf> is returned.

 *---------------------------------------------------------------------------*/
char *
ink_time_dayrange_to_string(InkTimeDayRange * dayrange_ptr, char *buf, const size_t bufSize)
{
  if (bufSize > 0) {
    buf[0] = '\0';
  }

  snprintf(buf, bufSize, "range_start_%d_width_%u", dayrange_ptr->base, dayrange_ptr->width);
  return (buf);
}                               /* End ink_time_dayrange_to_string */


/*===========================================================================*

                          Date Conversion Routines

  Note that both the day of month and the month number start at 1 not zero,
  so January 1 is <m=1,d=1> not <m=0,d=0>.

 *===========================================================================*/

static int _base_day = 4;       /* 1/1/1970 is Thursday */
static int _base_year = 1970;

static int _base_daysinmonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

static const char *_day_names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *_month_names[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


/*---------------------------------------------------------------------------*

  void ink_time_current_mdy(int *m, int *dom, int *y)

  Gets the current local date in GMT, and returns the month, day of month,
  and year in GMT.

 *---------------------------------------------------------------------------*/
void
ink_time_current_mdy(int *m, int *dom, int *y)
{
  time_t c;
  struct tm t;
  c = time(0);
  ink_gmtime_r(&c, &t);
  ink_time_tm_to_mdy(&t, m, dom, y);
}                               /* End ink_time_current_mdy */


/*---------------------------------------------------------------------------*

  void ink_time_tm_to_mdy(struct tm *t, int *m, int *dom, int *y)

  Takes a broken down time pointer <t>, and returns the month, day of month,
  and year.

 *---------------------------------------------------------------------------*/
void
ink_time_tm_to_mdy(struct tm *t, int *m, int *dom, int *y)
{
  *m = t->tm_mon + 1;
  *dom = t->tm_mday;
  *y = t->tm_year + 1900;
}                               /* End ink_time_tm_to_mdy */


/*---------------------------------------------------------------------------*

  void ink_time_mdy_to_tm(int m, int dom, int y, struct tm *t)

  Takes a month <m>, day of month <dom>, and year <y>, and places a
  broken-down time in the structure pointed to by <t>.

 *---------------------------------------------------------------------------*/
void
ink_time_mdy_to_tm(int m, int dom, int y, struct tm *t)
{
  bzero((char *) t, sizeof(*t));
  t->tm_mon = m - 1;
  t->tm_mday = dom;
  t->tm_year = y - 1900;
  t->tm_wday = ink_time_mdy_to_dow(m, dom, y);
  t->tm_yday = ink_time_mdy_to_doy(m, dom, y);
}                               /* End ink_time_mdy_to_tm */


/*---------------------------------------------------------------------------*

  InkTimeDayID ink_time_mdy_to_dayid(int m, int dom, int y)

  Return a single integer representing encoding of the day <m> <dom>, <y>.
  The encoding is performed with respect to the base day.

 *---------------------------------------------------------------------------*/
InkTimeDayID
ink_time_mdy_to_dayid(int m, int dom, int y)
{
  int year, month;
  InkTimeDayID dayid;

  dayid = 0;
  for (year = _base_year; year < y; year++)
    dayid = dayid + ink_time_days_in_year(year);
  for (month = 1; month < m; month++)
    dayid = dayid + ink_time_days_in_month(month, year);
  dayid = dayid + dom - 1;
  return (dayid);
}                               /* End ink_time_mdy_to_dayid */


/*---------------------------------------------------------------------------*

  InkTimeDayID ink_time_current_dayid()

  Return a single integer representing encoding of the today's date.
  The encoding is performed with respect to the base day.

 *---------------------------------------------------------------------------*/
InkTimeDayID
ink_time_current_dayid()
{
  InkTimeDayID today;
  int today_m, today_d, today_y;

  ink_time_current_mdy(&today_m, &today_d, &today_y);
  today = ink_time_mdy_to_dayid(today_m, today_d, today_y);

  return (today);
}                               /* End ink_time_current_dayid */


/*---------------------------------------------------------------------------*

  void ink_time_dayid_to_mdy(InkTimeDayID dayid, int *mp, int *dp, int *yp)

  Takes a single integer representation of the date, and convert to the
  month <m>, day of month <dom>, and year <y>.

 *---------------------------------------------------------------------------*/
void
ink_time_dayid_to_mdy(InkTimeDayID dayid, int *mp, int *dp, int *yp)
{
  dayid = dayid + 1;
  for (*yp = _base_year; ink_time_days_in_year(*yp) < dayid; (*yp)++)
    dayid = dayid - ink_time_days_in_year(*yp);
  for (*mp = 1; ink_time_days_in_month(*mp, *yp) < dayid; (*mp)++)
    dayid = dayid - ink_time_days_in_month(*mp, *yp);
  *dp = dayid;
}                               /* End ink_time_dayid_to_mdy */


/*---------------------------------------------------------------------------*

  int ink_time_mdy_to_doy(int m, int dom, int y)

  Takes a date <m> <dom> <y>, and returns the number of days into year <y>.

 *---------------------------------------------------------------------------*/
int
ink_time_mdy_to_doy(int m, int dom, int y)
{
  InkTimeDayID first, current;

  first = ink_time_mdy_to_dayid(1, 1, y);
  current = ink_time_mdy_to_dayid(m, dom, y);
  return (current - first);
}                               /* End ink_time_mdy_to_doy */


/*---------------------------------------------------------------------------*

  void ink_time_doy_to_mdy(int doy, int year, int *mon, int *dom, int *dow)

  This routine take a <year>, and a zero-based <doy> within the year,
  and determines the corresponding month, day of month, and day of week.

 *---------------------------------------------------------------------------*/
void
ink_time_doy_to_mdy(int doy, int year, int *mon, int *dom, int *dow)
{
  int month, daysinmonth, days_so_far, next_days_so_far;

  days_so_far = 1;
  for (month = 1; month <= 12; month++) {
    daysinmonth = ink_time_days_in_month(month, year);
    next_days_so_far = days_so_far + daysinmonth;
    if (doy >= days_so_far && doy < next_days_so_far) {
      *mon = month;
      *dom = doy - days_so_far + 1;
      *dow = ink_time_mdy_to_dow(month, *dom, year);
      return;
    }
    days_so_far = next_days_so_far;
  }
}                               /* End ink_time_doy_to_mdy */


/*---------------------------------------------------------------------------*

  int ink_time_mdy_to_dow(int month, int dom, int year)

  What day of the week does <month> <dom>, <year> fall on?

 *---------------------------------------------------------------------------*/
int
ink_time_mdy_to_dow(int month, int dom, int year)
{
  int i, base;

  base = ink_time_first_day_of_year(year);
  for (i = 0; i < month - 1; i++) {
    base = (base + ink_time_days_in_month(i + 1, year)) % 7;
  }
  return ((base + dom - 1) % 7);
}                               /* End ink_time_mdy_to_dow */


/*---------------------------------------------------------------------------*

  int ink_time_days_in_month(int month, int year)

  This routine returns the number of days in a particular <month> and <year>.

 *---------------------------------------------------------------------------*/
int
ink_time_days_in_month(int month, int year)
{
  return (_base_daysinmonth[month - 1] + (month == 2 ? ink_time_leap_year_correction(year) : 0));
}                               /* End ink_time_days_in_month */


/*---------------------------------------------------------------------------*

  int ink_time_days_in_year(int year)

  This routine returns the number of days in the year <year>, compensating
  for leap years.

 *---------------------------------------------------------------------------*/
int
ink_time_days_in_year(int year)
{
  return (365 + ink_time_leap_year_correction(year));
}                               /* End ink_time_days_in_year */


/*---------------------------------------------------------------------------*

  int ink_time_first_day_of_year(int year)

  What day is January 1 on in this year?

 *---------------------------------------------------------------------------*/
int
ink_time_first_day_of_year(int year)
{
  int i, base;

  base = _base_day;
  if (year > _base_year) {
    for (i = _base_year; i < year; i++)
      base = (base + ink_time_days_in_year(i)) % 7;
  } else if (year < _base_year) {
    for (i = _base_year - 1; i >= year; i--)
      base = ((base - ink_time_days_in_year(i)) % 7 + 7) % 7;
  }
  return (base);
}                               /* End ink_time_first_day_of_year */


/*---------------------------------------------------------------------------*

  void ink_time_day_to_string(int day, char *buffer)

  This routine takes a day number and places a 3 character, NUL terminated
  string representing this day in the buffer pointed to by <buffer>.

 *---------------------------------------------------------------------------*/
void
ink_time_day_to_string(int day, char *buffer, const size_t bufferSize)
{
  ink_strlcpy(buffer, _day_names[day], bufferSize);
}                               /* End ink_time_day_to_string */


/*---------------------------------------------------------------------------*

  void ink_time_month_to_string(int month, char *buffer)

  This routine takes a month number and places a 3 character, NUL terminated
  string representing this day in the buffer pointed to by <buffer>.

 *---------------------------------------------------------------------------*/
void
ink_time_month_to_string(int month, char *buffer, const size_t bufferSize)
{
  ink_strlcpy(buffer, _month_names[month - 1], bufferSize);
}                               /* End ink_time_month_to_string */


/*---------------------------------------------------------------------------*

  int ink_time_string_to_month(char *str)

  This routine takes a name of a month <str>, and returns the corresponding
  month number, else -1.

 *---------------------------------------------------------------------------*/
int
ink_time_string_to_month(char *str)
{
  int i;

  for (i = 0; i < 12; i++) {
    if (strcasecmp(str, _month_names[i]) == 0)
      return (i + 1);
  }
  return (-1);
}                               /* End ink_time_string_to_month */


/*---------------------------------------------------------------------------*

  int ink_time_leap_year_correction(int year)

  Return 1 if <year> is a leap year, 0 if not, and -1 if negative leap year.

 *---------------------------------------------------------------------------*/
int
ink_time_leap_year_correction(int year)
{
  return (ink_time_is_4th_year(year) - ink_time_is_100th_year(year) + ink_time_is_400th_year(year));
}                               /* End ink_time_leap_year_correction */

/* asah #ifndef sun asah */
struct dtconv
{
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
/* asah #endif asah */



/*
 * The man page for cftime lies. It claims that it is thread safe.
 * Instead, it silently trashes the heap (by freeing things more than
 * once) when used in a mulithreaded program. Gack!
 */
int
cftime_replacement(char *s, int maxsize, const char *format, const time_t * clock)
{
  struct tm tm;

  ink_assert(localtime_r(clock, &tm) != (int) NULL);    /* ADK_122100 */

  return strftime(s, maxsize, format, &tm);
}

#undef cftime
/* Throw an error if they ever call plain-old cftime. */
int
cftime(char *s, char *format, const time_t * clock)
{
  (void) s;
  (void) format;
  (void) clock;
  printf("ERROR cftime is not thread safe -- call cftime_replacement\n");
  ink_assert(!"cftime");
  return 0;
}

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
  if (d> 366)
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
