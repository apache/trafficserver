/** @file

    IP address handling support.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more
    contributor license agreements.  See the NOTICE file distributed with this
    work for additional information regarding copyright ownership.  The ASF
    licenses this file to you under the Apache License, Version 2.0 (the
    "License"); you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
    License for the specific language governing permissions and limitations under
    the License.
*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "tsutil/ts_ip.h"

namespace ts
{
IPAddrPair::self_type &
IPAddrPair::operator+=(const IPAddrPair::self_type &that)
{
  if (that.has_ip4()) {
    _ip4 = that._ip4;
  }
  if (that.has_ip6()) {
    _ip6 = that._ip6;
  }
  return *this;
}

IPAddrPair
getbestaddrinfo(swoc::TextView name)
{
  // If @a name parses as a valid address, return it as that address.

  if (swoc::IP4Addr addr; addr.load(name)) {
    return addr;
  }

  if (swoc::IP6Addr addr; addr.load(name)) {
    return addr;
  }

  // Presume it is a host name, make a copy to guarantee C string.
  char *tmp = static_cast<char *>(alloca(name.size() + 1));
  memcpy(tmp, name.data(), name.size());
  tmp[name.size()] = 0;
  name.assign(tmp, name.size());

  // List of address types, in order of worst to best.
  enum {
    NA, // Not an (IP) Address.
    LO, // Loopback.
    LL, // Link Local.
    PR, // Private.
    MC, // Multicast.
    GL  // Global.
  } spot_type = NA,
    ip4_type = NA, ip6_type = NA;
  addrinfo   ai_hints{};
  addrinfo  *ai_result;
  IPAddrPair zret;

  // Do the resolution
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_flags  = AI_ADDRCONFIG;

  if (0 == getaddrinfo(name.data(), nullptr, &ai_hints, &ai_result)) { // Walk the returned addresses and pick the "best".
    for (addrinfo *ai_spot = ai_result; ai_spot; ai_spot = ai_spot->ai_next) {
      swoc::IPAddr addr(ai_spot->ai_addr);
      if (!addr.is_valid()) {
        spot_type = NA;
      } else if (addr.is_loopback()) {
        spot_type = LO;
      } else if (addr.is_link_local()) {
        spot_type = LL;
      } else if (addr.is_private()) {
        spot_type = PR;
      } else if (addr.is_multicast()) {
        spot_type = MC;
      } else {
        spot_type = GL;
      }

      if (spot_type == NA) {
        continue; // Next!
      }

      if (addr.is_ip4()) {
        if (spot_type > ip4_type) {
          zret     = addr.ip4();
          ip4_type = spot_type;
        }
      } else if (addr.is_ip6()) {
        if (spot_type > ip6_type) {
          zret     = addr.ip6();
          ip6_type = spot_type;
        }
      }
    }

    freeaddrinfo(ai_result); // free *after* the copy.
  }

  return zret;
}

IPSrvPair
getbestsrvinfo(swoc::TextView src)
{
  swoc::TextView addr_text;
  swoc::TextView port_text;
  if (swoc::IPEndpoint::tokenize(src, &addr_text, &port_text)) {
    in_port_t port = swoc::svtoi(port_text);
    return IPSrvPair{ts::getbestaddrinfo(addr_text), port};
  }
  return {};
}

} // namespace ts
