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

/*****************************************************************************

  ink_sprintf.cc

  This file implements some Inktomi variants of sprintf, to do bounds
  checking and length counting.


  ****************************************************************************/

#include "tscore/ink_sprintf.h"

#define NUL '\0'

//////////////////////////////////////////////////////////////////////////////
//
//      int ink_bsprintf(char *buffer, char *format, ...)
//      int ink_bvsprintf(char *buffer, char *format, va_list ap)
//
//      This is a very simplified version of sprintf that has the following
//      behavior:
//
//      (1) the length in output characters is returned, including final NUL
//      (2) buffer can be nullptr, for just counting the output chars
//      (3) only %s and %d are supported, with no field modifiers
//
//////////////////////////////////////////////////////////////////////////////

int
ink_bsprintf(char *buffer, const char *format, ...)
{
  int l;

  va_list ap;
  va_start(ap, format);
  l = ink_bvsprintf(buffer, format, ap);
  va_end(ap);

  return (l);
}

int
ink_bvsprintf(char *buffer, const char *format, va_list ap)
{
  int d_val;
  const char *s;
  char *d, *p, *s_val, d_buffer[32];
  va_list ap_local;

  va_copy(ap_local, ap);

  s = format;
  d = buffer;

  while (*s) {
    /////////////////////////////
    // handle non-% characters //
    /////////////////////////////

    if (buffer) { // if have output buffer
      while (*s && (*s != '%')) {
        *d++ = *s++;
      } //   really copy, else
    } else {
      while (*s && (*s != '%')) {
        d++;
        s++;
      } //   pass over string
    }

    ///////////////////////////
    // handle NUL characters //
    ///////////////////////////

    if (*s == NUL) {
      break; // end of string
    }

    /////////////////////////
    // handle % characters //
    /////////////////////////

    ++s; // consume % character

    switch (*s) // dispatch on flag
    {
    case 's':                           // %s pattern
      ++s;                              // consume 's'
      s_val = va_arg(ap_local, char *); // grab string argument
      p     = s_val;                    // temporary pointer
      if (buffer) {                     // if have output buffer
        while (*p) {
          *d++ = *p++;
        }      //   copy value
      } else { // else
        while (*p) {
          d++;
          p++;
        } //   pass over value
      }
      break;
    case 'd':                                            // %d pattern
      ++s;                                               // consume 'd'
      d_val = va_arg(ap_local, int);                     // grab integer argument
      snprintf(d_buffer, sizeof(d_buffer), "%d", d_val); // stringify integer
      p = d_buffer;                                      // temporary pointer
      if (buffer) {                                      // if have output buffer
        while (*p) {
          *d++ = *p++;
        }      //   copy value
      } else { // else
        while (*p) {
          d++;
          p++;
        } //   pass over value
      }
      break;
    default: // something else
      if (buffer) {
        *d = *s; // copy unknown character
      }
      ++d;
      ++s;
      break;
    }
  }

  if (buffer) {
    *d = NUL;
  }
  ++d;

  va_end(ap_local);
  return static_cast<int>(d - buffer);
}
