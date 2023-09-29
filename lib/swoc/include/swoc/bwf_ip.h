// SPDX-License-Identifier: Apache-2.0
// Copyright Apache Software Foundation 2019
/** @file

    BufferWriter formatting for IP addresses.
 */

#pragma once

#include <iosfwd>
#include <netinet/in.h>

#include "swoc/swoc_version.h"
#include "swoc/bwf_base.h"
#include "swoc/swoc_ip.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

// All of these expect the address to be in network order.
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, in6_addr const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, sockaddr const *addr);

/// @{
/// @internal The various @c sockaddr types are handled almost identically due to how formatting
/// codes are handled so it's ugly to split the code base on family. Instead we depend on
/// @a sa_family being set correctly.

inline BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, sockaddr_in const *addr) {
  return bwformat(w,spec,reinterpret_cast<sockaddr const*>(addr));
}
inline BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, sockaddr_in6 const *addr) {
  return bwformat(w,spec,reinterpret_cast<sockaddr const*>(addr));
}

/// @}

// Use class information for ordering.
BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP4Addr const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP6Addr const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPAddr const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP4Srv const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP6Srv const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPSrv const &addr);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP4Range const &range);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP6Range const &range);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPRange const &range);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPRangeView const &range);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPNet const &net);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP4Net const &net);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IP6Net const &net);

BufferWriter &bwformat(BufferWriter &w, bwf::Spec const &spec, IPMask const &mask);

inline BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, IPEndpoint const &addr) {
  return bwformat(w, spec, &addr.sa);
}

/// Buffer space sufficient for printing any basic IP address type.
static const size_t IP_STREAM_SIZE = 80;

}} // namespace swoc::SWOC_VERSION_NS

namespace std {
inline ostream &
operator<<(ostream &s, swoc::IP4Addr const &addr) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, addr);
}

inline ostream &
operator<<(ostream &s, swoc::IP6Addr const &addr) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, addr);
}

inline ostream &
operator<<(ostream &s, swoc::IPAddr const &addr) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, addr);
}

inline ostream &
operator<<(ostream &s, swoc::IP4Range const &Range) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, Range);
}

inline ostream &
operator<<(ostream &s, swoc::IP6Range const &Range) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, Range);
}

inline ostream &
operator<<(ostream &s, swoc::IPRange const &Range) {
  swoc::LocalBufferWriter<swoc::IP_STREAM_SIZE> w;
  return s << bwformat(w, swoc::bwf::Spec::DEFAULT, Range);
}

} // namespace std
