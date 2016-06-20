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

#include "ts/ink_platform.h"
#include "ts/ink_inet.h"
#include "ts/ink_assert.h"
#include "ts/Diags.h"
#include "I_Machine.h"

#if HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif

// Singleton
Machine *Machine::_instance = NULL;

Machine *
Machine::instance()
{
  ink_assert(_instance || !"Machine instance accessed before initialization");
  return Machine::_instance;
}

Machine *
Machine::init(char const *name, sockaddr const *ip)
{
  ink_assert(!_instance || !"Machine instance initialized twice.");
  Machine::_instance = new Machine(name, ip);
  return Machine::_instance;
}

Machine::Machine(char const *the_hostname, sockaddr const *addr)
  : hostname(0), hostname_len(0), ip_string_len(0), ip_hex_string_len(0)
{
  char localhost[1024];
  int status; // return for system calls.

  ip_string[0]     = 0;
  ip_hex_string[0] = 0;
  ink_zero(ip);
  ink_zero(ip4);
  ink_zero(ip6);

  localhost[sizeof(localhost) - 1] = 0; // ensure termination.

  if (!ats_is_ip(addr)) {
    if (!the_hostname) {
      ink_release_assert(!gethostname(localhost, sizeof(localhost) - 1));
      the_hostname = localhost;
    }
    hostname = ats_strdup(the_hostname);

#if HAVE_IFADDRS_H
    ifaddrs *ifa_addrs = 0;
    status             = getifaddrs(&ifa_addrs);
#else
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    // This number is hard to determine, but needs to be much larger than
    // you would expect. On a normal system with just two interfaces and
    // one address / interface the return count is 120. Stack space is
    // cheap so it's best to go big.
    static const int N_REQ = 1024;
    ifconf conf;
    ifreq req[N_REQ];
    if (0 <= s) {
      conf.ifc_len = sizeof(req);
      conf.ifc_req = req;
      status       = ioctl(s, SIOCGIFCONF, &conf);
    } else {
      status = -1;
    }
#endif

    if (0 != status) {
      Warning("Unable to determine local host '%s' address information - %s", hostname, strerror(errno));
    } else {
      // Loop through the interface addresses and prefer by type.
      enum {
        NA, // Not an (IP) Address.
        LO, // Loopback.
        LL, // Link Local
        PR, // Private.
        MC, // Multicast.
        GL  // Global.
      } spot_type = NA,
        ip4_type = NA, ip6_type = NA;
      sockaddr const *ifip;
      unsigned int ifflags;
      for (
#if HAVE_IFADDRS_H
        ifaddrs *spot = ifa_addrs; spot; spot = spot->ifa_next
#else
        ifreq *spot = req, *req_limit = req + (conf.ifc_len / sizeof(*req)); spot < req_limit; ++spot
#endif
        ) {
#if HAVE_IFADDRS_H
        ifip    = spot->ifa_addr;
        ifflags = spot->ifa_flags;
#else
        ifip = &spot->ifr_addr;

        // get the interface's flags
        struct ifreq ifr;
        ink_strlcpy(ifr.ifr_name, spot->ifr_name, IFNAMSIZ);
        if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0)
          ifflags = ifr.ifr_flags;
        else
          ifflags = 0; // flags not available, default to just looking at IP
#endif
        if (!ats_is_ip(ifip))
          spot_type = NA;
        else if (ats_is_ip_loopback(ifip) || (IFF_LOOPBACK & ifflags))
          spot_type = LO;
        else if (ats_is_ip_linklocal(ifip))
          spot_type = LL;
        else if (ats_is_ip_private(ifip))
          spot_type = PR;
        else if (ats_is_ip_multicast(ifip))
          spot_type = MC;
        else
          spot_type = GL;
        if (spot_type == NA)
          continue; // Next!

        if (ats_is_ip4(ifip)) {
          if (spot_type > ip4_type) {
            ats_ip_copy(&ip4, ifip);
            ip4_type = spot_type;
          }
        } else if (ats_is_ip6(ifip)) {
          if (spot_type > ip6_type) {
            ats_ip_copy(&ip6, ifip);
            ip6_type = spot_type;
          }
        }
      }

#if HAVE_IFADDRS_H
      freeifaddrs(ifa_addrs);
#endif

      // What about the general address? Prefer IPv4?
      if (ip4_type >= ip6_type)
        ats_ip_copy(&ip.sa, &ip4.sa);
      else
        ats_ip_copy(&ip.sa, &ip6.sa);
    }
#if !HAVE_IFADDRS_H
    close(s);
#endif
  } else { // address provided.
    ats_ip_copy(&ip, addr);
    if (ats_is_ip4(addr))
      ats_ip_copy(&ip4, addr);
    else if (ats_is_ip6(addr))
      ats_ip_copy(&ip6, addr);

    status = getnameinfo(addr, ats_ip_size(addr), localhost, sizeof(localhost) - 1, 0, 0, // do not request service info
                         0                                                                // no flags.
                         );

    if (0 != status) {
      ip_text_buffer ipbuff;
      Warning("Failed to find hostname for address '%s' - %s", ats_ip_ntop(addr, ipbuff, sizeof(ipbuff)), gai_strerror(status));
    } else
      hostname = ats_strdup(localhost);
  }

  hostname_len = hostname ? strlen(hostname) : 0;

  ats_ip_ntop(&ip.sa, ip_string, sizeof(ip_string));
  ip_string_len     = strlen(ip_string);
  ip_hex_string_len = ats_ip_to_hex(&ip.sa, ip_hex_string, sizeof(ip_hex_string));
}

Machine::~Machine()
{
  ats_free(hostname);
}
