/** @file
    WCCP static data and compile time checks.

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

#include "WccpLocal.h"
#include "WccpMeta.h"
#include "ts/ink_error.h"
#include "ts/ink_defs.h"

/* Solaris considers SIOCGIFCONF obsolete and only defines it if
 * BSD compatibility activated. */
#if defined(solaris)
#define BSD_COMP
#endif
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

namespace wccp
{
// ------------------------------------------------------
// Compile time checks for internal consistency.

struct CompileTimeChecks {
  static unsigned int const BUCKET_SIZE = sizeof(AssignInfoComp::Bucket);
  static unsigned int const UINT8_SIZE  = sizeof(uint8_t);
  // A compiler error for the following line means that the size of
  // an assignment bucket is incorrect. It must be exactly 1 byte.
  ts::TEST_IF_TRUE<BUCKET_SIZE == UINT8_SIZE> m_check_bucket_size;
};

ts::Errata::Code LVL_TMP   = 1; ///< Temporary message.
ts::Errata::Code LVL_FATAL = 3; ///< Fatal, cannot continue.
ts::Errata::Code LVL_WARN  = 2; ///< Significant, should be fixed.
ts::Errata::Code LVL_INFO  = 1; /// Interesting, not necessarily a problem.
ts::Errata::Code LVL_DEBUG = 0; /// Debugging information.

// Find a valid local IP address given an open socket.
uint32_t
Get_Local_Address(int s)
{
  // If we can't find a good address in the first 255, just give up
  // and make the user specify an address.
  static int const N_REQ = 255;
  ifconf conf;
  ifreq req[N_REQ];
  uint32_t zret;

  conf.ifc_len = sizeof(req);
  conf.ifc_req = req;
  if (0 == ioctl(s, SIOCGIFCONF, &conf)) {
    int idx      = 0;
    ifreq *ptr   = req;
    ifreq *limit = req + (conf.ifc_len / sizeof(*req));
    for (; idx < N_REQ && ptr < limit; ++idx, ++ptr) {
      zret = reinterpret_cast<sockaddr_in &>(ptr->ifr_addr).sin_addr.s_addr;
      if ((zret & 0xFF) != 0x7F)
        return zret;
    }
  }
  return INADDR_ANY; // fail
}

// Cheap, can't even be used twice in the same argument list.
char const *
ip_addr_to_str(uint32_t addr)
{
  static char buff[4 * 3 + 3 + 1];
  unsigned char *octet = reinterpret_cast<unsigned char *>(&addr);
  sprintf(buff, "%d.%d.%d.%d", octet[0], octet[1], octet[2], octet[3]);
  return buff;
}

// ------------------------------------------------------
ts::Errata &
log(ts::Errata &err, ts::Errata::Id id, ts::Errata::Code code, char const *text)
{
  err.push(id, code, text);
  return err;
}

ts::Errata &
log(ts::Errata &err, ts::Errata::Code code, char const *text)
{
  err.push(0, code, text);
  return err;
}

ts::Errata
log(ts::Errata::Code code, char const *text)
{
  ts::Errata err;
  err.push(0, code, text);
  return err;
}

ts::Errata &
vlogf(ts::Errata &err, ts::Errata::Id id, ts::Errata::Code code, char const *format, va_list &rest)
{
  static size_t const SIZE = 8192;
  char buffer[SIZE];

  vsnprintf(buffer, SIZE, format, rest);
  err.push(id, code, buffer);
  return err;
}

ts::Errata &
logf(ts::Errata &err, ts::Errata::Id id, ts::Errata::Code code, char const *format, ...)
{
  va_list rest;
  va_start(rest, format);
  va_end(rest);
  return vlogf(err, id, code, format, rest);
}

ts::Errata
logf(ts::Errata::Code code, char const *format, ...)
{
  ts::Errata err;
  va_list rest;
  va_start(rest, format);
  va_end(rest);
  return vlogf(err, ts::Errata::Id(0), code, format, rest);
}

ts::Errata &
logf(ts::Errata &err, ts::Errata::Code code, char const *format, ...)
{
  va_list rest;
  va_start(rest, format);
  va_end(rest);
  return vlogf(err, ts::Errata::Id(0), code, format, rest);
}

ts::Errata
log_errno(ts::Errata::Code code, char const *text)
{
  static size_t const SIZE = 1024;
  char buffer[SIZE];
  ATS_UNUSED_RETURN(strerror_r(errno, buffer, SIZE));
  return logf(code, "%s [%d] %s", text, errno, buffer);
}

ts::Errata
vlogf_errno(ts::Errata::Code code, char const *format, va_list &rest)
{
  int e = errno; // Preserve value before making system calls.
  ts::Errata err;
  static int const E_SIZE = 1024;
  char e_buffer[E_SIZE];
  static int const T_SIZE = 8192;
  char t_buffer[T_SIZE];

  int n = vsnprintf(t_buffer, T_SIZE, format, rest);
  if (0 <= n && n < T_SIZE) { // still have room.
    ATS_UNUSED_RETURN(strerror_r(e, e_buffer, E_SIZE));
    snprintf(t_buffer + n, T_SIZE - n, "[%d] %s", e, e_buffer);
  }
  err.push(ts::Errata::Id(0), code, t_buffer);
  return err;
}

ts::Errata
logf_errno(ts::Errata::Code code, char const *format, ...)
{
  va_list rest;
  va_start(rest, format);
  va_end(rest);
  return vlogf_errno(code, format, rest);
}
// ------------------------------------------------------
} // namespace wccp
