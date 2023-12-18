// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   Shared utilities for IP address classes.
 */

#pragma once
#include <netinet/in.h>

#include "swoc/swoc_version.h"

// These have to be global namespace, unfortunately.
inline bool
operator==(in6_addr const& lhs, in6_addr const& rhs) {
  return 0 == memcmp(&lhs, &rhs, sizeof(in6_addr));
}

inline bool
operator!=(in6_addr const& lhs, in6_addr const& rhs) {
  return 0 != memcmp(&lhs, &rhs, sizeof(in6_addr));
}

namespace swoc { inline namespace SWOC_VERSION_NS {

/// Internal IP address utilities.
namespace ip {

inline bool is_loopback_host_order(in_addr_t addr) {
  return (addr & 0xFF000000) == 0x7F000000;
}

inline bool is_link_local_host_order(in_addr_t addr) {
  return (addr & 0xFFFF0000) == 0xA9FE0000; // 169.254.0.0/16
}

inline bool is_multicast_host_order(in_addr_t addr) {
  return IN_MULTICAST(addr);
}

inline bool is_private_host_order(in_addr_t addr) {
  return (((addr & 0xFF000000) == 0x0A000000) || // 10.0.0.0/8
          ((addr & 0xFFC00000) == 0x64400000) || // 100.64.0.0/10
          ((addr & 0xFFF00000) == 0xAC100000) || // 172.16.0.0/12
          ((addr & 0xFFFF0000) == 0xC0A80000)    // 192.168.0.0/16
  );
}

// There really is no "host order" for IPv6, so only the network order utilities are defined.
// @c IP6Addr uses an idiosyncratic ordering for performance, not really useful to expose to
// clients.

inline bool is_loopback_network_order(in6_addr const& addr) {
  return addr == in6addr_loopback;
}

inline bool is_multicast_network_order(in6_addr const& addr) {
  return addr.s6_addr[0] == 0xFF;
}

inline bool is_link_local_network_order(in6_addr const& addr) {
  return addr.s6_addr[0] == 0xFE && (addr.s6_addr[1] & 0xC0) == 0x80; // fe80::/10
}

inline bool is_private_network_order(in6_addr const& addr) {
  return (addr.s6_addr[0] & 0xFE) == 0xFC; // fc00::/7
}

#if BYTE_ORDER == LITTLE_ENDIAN

inline bool is_loopback_network_order(in_addr_t addr) {
  return (addr & 0xFF) == 0x7F;
}

inline bool is_private_network_order(in_addr_t addr) {
  return (((addr & 0xFF) == 0x0A) || // 10.0.0.0/8
          ((addr & 0xC0FF) == 0x4064) || // 100.64.0.0/10
          ((addr & 0xF0FF) == 0x10AC) || // 172.16.0.0/12
          ((addr & 0xFFFF) == 0xA8C0)    // 192.168.0.0/16
  );
}

inline bool is_link_local_network_order(in_addr_t addr) {
  return (addr & 0xFFFF) == 0xFEA9; // 169.254.0.0/16
}

inline bool is_multicast_network_order(in_addr_t addr) {
  return (addr & 0xF0) == 0xE0;
}

#else

inline bool is_loopback_network_order(in_addr_t addr) {
  return is_loopback_host_order(addr);
}

inline bool is_link_local_network_order(inaddr_t addr) {
  return is_link_local_host_order(addr);
}

inline bool is_private_network_order(in_addr_t addr) {
  return is_link_local_host_order(addr);
}

inline bool is_multicast_network_order(in_addr_t addr) {
  return is_multicast_host_order(addr);
}

#endif

/** Check if the address in a socket address is a loopback address..
 * @return @c true if so, @c false if not.
 */
inline bool is_loopback(sockaddr const * sa) {
  return ( sa->sa_family == AF_INET && is_loopback_network_order(reinterpret_cast<sockaddr_in const *>(sa)->sin_addr.s_addr)) ||
         ( sa->sa_family == AF_INET6 && is_loopback_network_order(reinterpret_cast<sockaddr_in6 const *>(sa)->sin6_addr))
         ;
}

/** Check if the address in a socket address is multicast.
 * @return @c true if so, @c false if not.
 */
inline bool is_multicast(sockaddr const * sa) {
  return ( sa->sa_family == AF_INET && is_multicast_network_order(reinterpret_cast<sockaddr_in const *>(sa)->sin_addr.s_addr)) ||
         ( sa->sa_family == AF_INET6 && is_multicast_network_order(reinterpret_cast<sockaddr_in6 const *>(sa)->sin6_addr))
         ;
}

/** Check if the IP address in a socket address is link local.
 * @return @c true if link local, @c false if not.
 */
inline bool is_link_local(sockaddr const* sa) {
  return ( sa->sa_family == AF_INET && is_link_local_network_order(reinterpret_cast<sockaddr_in const *>(sa)->sin_addr.s_addr)) ||
         ( sa->sa_family == AF_INET6 && is_link_local_network_order(reinterpret_cast<sockaddr_in6 const *>(sa)->sin6_addr))
         ;
}

/** Check if the IP address in a socket address is private (non-routable)
 * @return @c true if private, @c false if not.
 */
inline bool is_private(sockaddr const* sa) {
  return ( sa->sa_family == AF_INET && is_private_network_order(reinterpret_cast<sockaddr_in const *>(sa)->sin_addr.s_addr)) ||
         ( sa->sa_family == AF_INET6 && is_private_network_order(reinterpret_cast<sockaddr_in6 const *>(sa)->sin6_addr))
         ;
}

} // hnamespace ip

}} // namespace swoc::SWOC_VERSION_NS
