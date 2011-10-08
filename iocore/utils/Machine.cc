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
#include <ifaddrs.h>

// Singleton
Machine* Machine::_instance = NULL;

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
    if (!the_hostname) {
      ink_release_assert(!gethostname(localhost, sizeof(localhost)-1));
      the_hostname = localhost;
    }
    hostname = ats_strdup(the_hostname);

    ifaddrs* ifa_addrs = 0;
    status = getifaddrs(&ifa_addrs);
    if (0 != status) {
      Warning("Unable to determine local host '%s' address information - %s"
        , hostname
        , strerror(errno)
      );
    } else {
      /* Loop through the interface addresses. We have to prioritize
         the values a little bit. The worst is the loopback address,
         we accept that only if we can't find anything else. Next best
         are non-routables and the best are "global" addresses.
      */
      enum { NA, LO, NR, MC, GA } spot_type = NA, ip4_type = NA, ip6_type = NA;
      for (ifaddrs* spot = ifa_addrs ; spot ; spot = spot->ifa_next ) {
        
        if (!ink_inet_is_ip(spot->ifa_addr)) spot_type = NA;
        else if (ink_inet_is_loopback(spot->ifa_addr)) spot_type = LO;
        else if (ink_inet_is_nonroutable(spot->ifa_addr)) spot_type = NR;
        else if (ink_inet_is_multicast(spot->ifa_addr)) spot_type = MC;

        if (spot_type == NA) continue; // Next!

        if (ink_inet_is_ip4(spot->ifa_addr)) {
          if (spot_type > ip4_type) {
            ink_inet_copy(&ip4, spot->ifa_addr);
            ip4_type = spot_type;
          }
        } else if (ink_inet_is_ip6(spot->ifa_addr)) {
          if (spot_type > ip6_type) {
            ink_inet_copy(&ip6, spot->ifa_addr);
            ip6_type = spot_type;
          }
        }
      }
      freeifaddrs(ifa_addrs);
      // What about the general address? Prefer IPv4?
      if (ip4_type >= ip6_type)
        ink_inet_copy(&ip.sa, &ip4.sa);
      else
        ink_inet_copy(&ip.sa, &ip6.sa);
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
  ip_hex_string_len = ink_inet_to_hex(&ip.sa, ip_hex_string, sizeof(ip_hex_string));
}

Machine::~Machine()
{
  ats_free(hostname);
}
