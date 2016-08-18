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

#include "ts/ink_hrtime.h"
#include "ts/ink_assert.h"
#include "ts/ink_defs.h"

#if defined(freebsd)
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#include <sys/time.h>

char *
int64_to_str(char *buf, unsigned int buf_size, int64_t val, unsigned int *total_chars, unsigned int req_width, char pad_char)
{
  const unsigned int local_buf_size = 32;
  char local_buf[local_buf_size];
  bool using_local_buffer = false;
  bool negative           = false;
  char *out_buf;

  if (buf_size < 22) {
    // int64_t may not fit in provided buffer, use the local one
    out_buf            = &local_buf[local_buf_size - 1];
    using_local_buffer = true;
  } else {
    out_buf = &buf[buf_size - 1];
  }

  unsigned int num_chars = 1; // includes eos
  *out_buf--             = 0;

  if (val < 0) {
    val      = -val;
    negative = true;
  }

  if (val < 10) {
    *out_buf-- = '0' + (char)val;
    ++num_chars;
  } else {
    do {
      *out_buf-- = (char)(val % 10) + '0';
      val /= 10;
      ++num_chars;
    } while (val);
  }

  // pad with pad_char if needed
  //
  if (req_width) {
    // add minus sign if padding character is not 0
    if (negative && pad_char != '0') {
      *out_buf = '-';
      ++num_chars;
    } else {
      out_buf++;
    }
    if (req_width > buf_size)
      req_width              = buf_size;
    unsigned int num_padding = 0;
    if (req_width > num_chars) {
      num_padding = req_width - num_chars;
      switch (num_padding) {
      case 3:
        *--out_buf = pad_char;
      case 2:
        *--out_buf = pad_char;
      case 1:
        *--out_buf = pad_char;
        break;
      default:
        for (unsigned int i = 0; i < num_padding; ++i, *--out_buf = pad_char)
          ;
      }
      num_chars += num_padding;
    }
    // add minus sign if padding character is 0
    if (negative && pad_char == '0') {
      if (num_padding) {
        *out_buf = '-'; // overwrite padding
      } else {
        *--out_buf = '-';
        ++num_chars;
      }
    }
  } else if (negative) {
    *out_buf = '-';
    ++num_chars;
  } else {
    out_buf++;
  }

  if (using_local_buffer) {
    if (num_chars <= buf_size) {
      memcpy(buf, out_buf, num_chars);
      out_buf = buf;
    } else {
      // data does not fit return NULL
      out_buf = NULL;
    }
  }

  if (total_chars)
    *total_chars = num_chars;
  return out_buf;
}

int
squid_timestamp_to_buf(char *buf, unsigned int buf_size, long timestamp_sec, long timestamp_usec)
{
  int res;
  const unsigned int tmp_buf_size = 32;
  char tmp_buf[tmp_buf_size];

  unsigned int num_chars_s;
  char *ts_s = int64_to_str(tmp_buf, tmp_buf_size - 4, timestamp_sec, &num_chars_s, 0, '0');
  ink_assert(ts_s);

  // convert milliseconds
  //
  tmp_buf[tmp_buf_size - 5] = '.';
  int ms                    = timestamp_usec / 1000;
  unsigned int num_chars_ms;
  char ATS_UNUSED *ts_ms = int64_to_str(&tmp_buf[tmp_buf_size - 4], 4, ms, &num_chars_ms, 4, '0');
  ink_assert(ts_ms && num_chars_ms == 4);

  unsigned int chars_to_write = num_chars_s + 3; // no eos

  if (buf_size >= chars_to_write) {
    memcpy(buf, ts_s, chars_to_write);
    res = chars_to_write;
  } else {
    res = -((int)chars_to_write);
  }

  return res;
}

#ifdef USE_TIME_STAMP_COUNTER_HRTIME
uint32_t
init_hrtime_TCS()
{
  int freqlen = sizeof(hrtime_freq);
  if (sysctlbyname("machdep.tsc_freq", &hrtime_freq, (size_t *)&freqlen, NULL, 0) < 0) {
    perror("sysctl: machdep.tsc_freq");
    exit(1);
  }
  hrtime_freq_float = (double)1000000000 / (double)hrtime_freq;
  return hrtime_freq;
}

double hrtime_freq_float = 0.5; // 500 Mhz
uint32_t hrtime_freq     = init_hrtime_TCS();
#endif

#ifdef NEED_HRTIME_BASIS
timespec timespec_basis;
ink_hrtime hrtime_offset;
ink_hrtime hrtime_basis = init_hrtime_basis();

ink_hrtime
init_hrtime_basis()
{
  ink_hrtime t1, t2, b, now;
  timespec ts;
#ifdef USE_TIME_STAMP_COUNTER_HRTIME
  init_hrtime_TCS();
#endif
  do {
    t1 = ink_get_hrtime_internal();
#if HAVE_CLOCK_GETTIME
    ink_assert(!clock_gettime(CLOCK_REALTIME, &timespec_basis));
#else
    {
      struct timeval tnow;
      ink_assert(!gettimeofday(&tnow, NULL));
      timespec_basis.tv_sec  = tnow.tv_sec;
      timespec_basis.tv_nsec = tnow.tv_usec * 1000;
    }
#endif
    t2 = ink_get_hrtime_internal();
    // accuracy must be at least 100 microseconds
  } while (t2 - t1 > HRTIME_USECONDS(100));
  b   = (t2 + t1) / 2;
  now = ink_hrtime_from_timespec(&timespec_basis);
  ts  = ink_hrtime_to_timespec(now);
  ink_assert(ts.tv_sec == timespec_basis.tv_sec && ts.tv_nsec == timespec_basis.tv_nsec);
  hrtime_offset = now - b;
  hrtime_basis  = b;
  return b;
}
#endif
