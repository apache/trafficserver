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

static_assert(sizeof(AssignInfoComp::Bucket) == sizeof(uint8_t), "Assignment bucket size must be exactly 1 byte");

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
// ------------------------------------------------------
// ------------------------------------------------------
} // namespace wccp

namespace ts
{
BufferWriter &
bwformat(BufferWriter &w, const BWFSpec &spec, const wccp::ServiceConstants::PacketStyle &style)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<uintmax_t>(style));
  } else {
    switch (style) {
    case wccp::ServiceConstants::PacketStyle::NO_PACKET_STYLE:
      w.write("NO_PACKET_STYLE");
      break;
    case wccp::ServiceConstants::PacketStyle::GRE:
      w.write("GRE");
      break;
    case wccp::ServiceConstants::PacketStyle::L2:
      w.write("L2");
      break;
    case wccp::ServiceConstants::PacketStyle::GRE_OR_L2:
      w.write("GRE_OR_L2");
      break;
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, const BWFSpec &spec, const wccp::ServiceConstants::CacheAssignmentStyle &style)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<uintmax_t>(style));
  } else {
    switch (style) {
    case wccp::ServiceConstants::CacheAssignmentStyle::NO_CACHE_ASSIGN_STYLE:
      w.write("NO_CACHE_ASSIGN_STYLE");
      break;
    case wccp::ServiceConstants::CacheAssignmentStyle::HASH_ONLY:
      w.write("HASH_ONLY");
      break;
    case wccp::ServiceConstants::CacheAssignmentStyle::MASK_ONLY:
      w.write("MASK_ONLY");
      break;
    case wccp::ServiceConstants::CacheAssignmentStyle::HASH_OR_MASK:
      w.write("HASH_OR_MASK");
      break;
    }
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, const BWFSpec &spec, const wccp::CapabilityElt::Type &cap)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<uintmax_t>(cap));
  } else {
    switch (cap) {
    case wccp::CapabilityElt::Type::NO_METHOD:
      w.write("NO_METHOD");
      break;
    case wccp::CapabilityElt::Type::PACKET_FORWARD_METHOD:
      w.write("PACKET_FORWARD_METHOD");
      break;
    case wccp::CapabilityElt::Type::CACHE_ASSIGNMENT_METHOD:
      w.write("CACHE_ASSIGNMENT_METHOD");
      break;
    case wccp::CapabilityElt::Type::PACKET_RETURN_METHOD:
      w.write("PACKET_RETURN_METHOD");
      break;
    }
  }
  return w;
}

} // namespace ts
