/** @file

  Support class for describing the local machine.

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

#include "libts.h"
#include "I_Machine.h"

// Singleton
Machine* Machine::_instance = NULL;

int
nstrhex(char *dst, uint8_t const* src, size_t len) {
  int zret = 0;
  for ( uint8_t const* limit = src + len ; src < limit ; ++src, zret += 2 ) {
    uint8_t n1 = (*src >> 4) & 0xF; // high nybble.
    uint8_t n0 = *src & 0xF; // low nybble.

    *dst++ = n1 > 9 ? n1 + 'A' - 10 : n1 + '0';
    *dst++ = n0 > 9 ? n0 + 'A' - 10 : n0 + '0';
  }
  *dst = 0; // terminate but don't include that in the length.
  return zret;
}

Machine*
Machine::instance() {
  ink_assert(_instance || !"Machine instance accessed before initialization");
  return Machine::_instance;
}

Machine*
Machine::init(char const* name, sockaddr const* ip) {
  ink_assert(!_instance || !"Machine instance initialized twice.");
  Machine::_instance = new Machine(name, ip);
  return Machine::_instance;
}

Machine::Machine(char const* the_hostname, sockaddr const* addr)
  : hostname(0), hostname_len(0)
  , ip_string_len(0)
  , ip_hex_string_len(0)
{
  char localhost[1024];
  int status; // return for system calls.

  ip_string[0] = 0;
  ip_hex_string[0] = 0;
  ink_zero(ip);
  ink_zero(ip4);
  ink_zero(ip6);

  localhost[sizeof(localhost)-1] = 0; // ensure termination.

  if (!ink_inet_is_ip(addr)) {
    addrinfo ai_hints;
    addrinfo* ai_ret = 0;

    if (!the_hostname) {
      ink_release_assert(!gethostname(localhost, sizeof(localhost)-1));
      the_hostname = localhost;
    }
    hostname = ats_strdup(the_hostname);

    ink_zero(ai_hints);
    ai_hints.ai_flags = AI_ADDRCONFIG; // require existence of IP family addr.
    status = getaddrinfo(hostname, 0, &ai_hints, &ai_ret);
    if (0 != status) {
      Warning("Unable to determine local host '%s' address information - %s"
        , hostname
        , gai_strerror(status)
      );
    } else {
      for (addrinfo* spot = ai_ret ; spot ; spot = spot->ai_next ) {
        if (AF_INET == spot->ai_family) {
          if (!ink_inet_is_ip4(&ip4)) {
            ink_inet_copy(&ip4, spot->ai_addr);
            if (!ink_inet_is_ip(&ip))
              ink_inet_copy(&ip, spot->ai_addr);
            if (ink_inet_is_ip6(&ip6)) break; // got both families, done.
          }
        } else if (AF_INET6 == spot->ai_family) {
          if (!ink_inet_is_ip6(&ip6)) {
            ink_inet_copy(&ip6, spot->ai_addr);
            if (!ink_inet_is_ip(&ip))
              ink_inet_copy(&ip, spot->ai_addr);
            if (ink_inet_is_ip4(&ip4)) break; // got both families, done.
          }
        }
      }
      freeaddrinfo(ai_ret);
    }
  } else { // address provided.
    ink_inet_copy(&ip, addr);
    if (ink_inet_is_ip4(addr)) ink_inet_copy(&ip4, addr);
    else if (ink_inet_is_ip6(addr)) ink_inet_copy(&ip6, addr);

    status = getnameinfo(
      addr, ink_inet_ip_size(addr),
      localhost, sizeof(localhost) - 1,
      0, 0, // do not request service info
      0 // no flags.
    );

    if (0 != status) {
      ip_text_buffer ipbuff;
      Warning("Failed to find hostname for address '%s' - %s"
        , ink_inet_ntop(addr, ipbuff, sizeof(ipbuff))
        , gai_strerror(status)
      );
    } else
      hostname = ats_strdup(localhost);
  }

  hostname_len = hostname ? strlen(hostname) : 0;

  ink_inet_ntop(&ip.sa, ip_string, sizeof(ip_string));
  ip_string_len = strlen(ip_string);
  ip_hex_string_len =  nstrhex(ip_hex_string, ink_inet_addr8_cast(&ip), ink_inet_addr_size(&ip));
}

Machine::~Machine()
{
  ats_free(hostname);
}
