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

#pragma once

#include <chrono>

#include "tscore/ink_platform.h"
#include "tscore/ink_defs.h"
#include "tscore/ink_hrtime.h"

/*===========================================================================*

                                 Data Types

 *===========================================================================*/

using ink_time_t  = time_t;
using ts_clock    = std::chrono::system_clock;
using ts_time     = ts_clock::time_point;
using ts_hr_clock = std::chrono::high_resolution_clock;
using ts_hr_time  = ts_hr_clock::time_point;

using ts_seconds      = std::chrono::seconds;
using ts_milliseconds = std::chrono::milliseconds;

/// Equivalent of 0 for @c ts_time. This should be used as the default initializer.
static constexpr ts_time TS_TIME_ZERO;

/*===========================================================================*

                                 Prototypes

 *===========================================================================*/

#define UNDEFINED_TIME ((time_t)0)

double ink_time_wall_seconds();

int cftime_replacement(char *s, int maxsize, const char *format, const time_t *clock);
#define cftime(s, format, clock) cftime_replacement(s, 8192, format, clock)

ink_time_t convert_tm(const struct tm *tp);

char *ink_ctime_r(const ink_time_t *clock, char *buf);
struct tm *ink_localtime_r(const ink_time_t *clock, struct tm *res);

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
