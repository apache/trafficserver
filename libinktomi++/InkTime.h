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

  Time.h

  Implements general time functions.

  
 ****************************************************************************/

#ifndef _Time_h_
#define _Time_h_

enum
{
  UNDEFINED_TIME = 0
};

#include "ink_unused.h"
#include "ink_platform.h"
#include "inktomi++.h"
#include "ink_hrtime.h"
#include "ink_time.h"

inkcoreapi int ink_gmtime_r(const ink_time_t * clock, struct tm *res);
ink_time_t convert_tm(const struct tm *tp);

#if (HOST_OS == freebsd)

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

#else  // non-freebsd for the else

inline int
ink_timezone()
{
  return timezone;
}

/* vl: not used - inline int ink_daylight() { return daylight; } */
#endif

inkcoreapi char *ink_ctime_r(const ink_time_t * clock, char *buf);
inkcoreapi struct tm *ink_localtime_r(const ink_time_t * clock, struct tm *res);

#endif // _Time_h_
