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
#define _ink_time_h_

#include "ts/ink_platform.h"
#include "ts/ink_defs.h"
#include "ts/ink_hrtime.h"

/*===========================================================================*

                                 Data Types

 *===========================================================================*/

typedef time_t ink_time_t;

/*===========================================================================*

                                 Prototypes

 *===========================================================================*/

#define MICRO_USER 1
#define MICRO_SYS 2
#define MICRO_REAL 3
#define UNDEFINED_TIME ((time_t)0)

uint64_t ink_microseconds(int which);
double ink_time_wall_seconds();

int cftime_replacement(char *s, int maxsize, const char *format, const time_t *clock);
#define cftime(s, format, clock) cftime_replacement(s, 8192, format, clock)

ink_time_t convert_tm(const struct tm *tp);

inkcoreapi char *ink_ctime_r(const ink_time_t *clock, char *buf);
inkcoreapi struct tm *ink_localtime_r(const ink_time_t *clock, struct tm *res);

/*===========================================================================*
                              Inline Stuffage
 *===========================================================================*/
#if defined(freebsd) || defined(openbsd)

inline int
ink_timezone()
{
  struct timeval tp;
  struct timezone tzp;
  ink_assert(!gettimeofday(&tp, &tzp));
  return tzp.tz_minuteswest * 60;
}

#else // non-freebsd, non-openbsd for the else

inline int
ink_timezone()
{
  return timezone;
}

#endif /* #if defined(freebsd) || defined(openbsd) */

#endif /* #ifndef _ink_time_h_ */
