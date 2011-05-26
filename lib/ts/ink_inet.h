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

#if !defined (_ink_inet_h_)
#define _ink_inet_h_

#include "ink_platform.h"
#include "ink_port.h"
#include "ink_apidefs.h"

#define INK_GETHOSTBYNAME_R_DATA_SIZE 1024
#define INK_GETHOSTBYADDR_R_DATA_SIZE 1024

struct ink_gethostbyname_r_data
{
  int herrno;
  struct hostent ent;
  char buf[INK_GETHOSTBYNAME_R_DATA_SIZE];
};

struct ink_gethostbyaddr_r_data
{
  int herrno;
  struct hostent ent;
  char buf[INK_GETHOSTBYADDR_R_DATA_SIZE];
};

/**
  returns the IP address of the hostname. If the hostname has
  multiple IP addresses, the first IP address in the list returned
  by 'gethostbyname' is returned.

  @note Not thread-safe

*/
unsigned int host_to_ip(char *hostname);

/**
  Wrapper for gethostbyname_r(). If successful, returns a pointer
  to the hostent structure. Returns NULL and sets data->herrno to
  the appropriate error code on failure.

  @param hostname null-terminated host name string
  @param data pointer to ink_gethostbyname_r_data allocated by the caller

*/
struct hostent *ink_gethostbyname_r(char *hostname, ink_gethostbyname_r_data * data);

/**
  Wrapper for gethostbyaddr_r(). If successful, returns a pointer
  to the hostent structure. Returns NULL and sets data->herrno to
  the appropriate error code on failure.

  @param ip IP address of the host
  @param len length of the buffer indicated by ip
  @param type family of the address
  @param data pointer to ink_gethostbyname_r_data allocated by the caller

*/
struct hostent *ink_gethostbyaddr_r(char *ip, int len, int type, ink_gethostbyaddr_r_data * data);

/**
  Wrapper for inet_addr().

  @param s IP address in the Internet standard dot notation.

*/
inkcoreapi uint32_t ink_inet_addr(const char *s);

const char *ink_inet_ntop(const struct sockaddr *addr, char *dst, size_t size);
uint16_t ink_inet_port(const struct sockaddr *addr);

/// Reset an address to invalid.
/// Convenience overload.
/// @note Useful for marking a member as not yet set.
inline void ink_inet_invalidate(sockaddr_storage* addr) {
  addr->ss_family = AF_UNSPEC;
}

/// Set to all zero.
inline void ink_inet_init(sockaddr_storage* addr) {
  memset(addr, 0, sizeof(addr));
  ink_inet_invalidate(addr);
}

/// @return @a a cast to @c sockaddr_storage*.
inline sockaddr_storage* ink_inet_ss_cast(sockaddr* a) {
  return static_cast<sockaddr_storage*>(static_cast<void*>(a));
}
/// @return @a a cast to @c sockaddr_storage const*.
inline sockaddr_storage const* ink_inet_ss_cast(sockaddr const* a) {
  return static_cast<sockaddr_storage const*>(static_cast<void const*>(a));
}
/// @return @a a cast to @c sockaddr_storage const*.
inline sockaddr_storage* ink_inet_ss_cast(sockaddr_in6* a) {
  return reinterpret_cast<sockaddr_storage*>(a);
}
/// @return @a a cast to @c sockaddr_storage const*.
inline sockaddr_storage const* ink_inet_ss_cast(sockaddr_in6 const* a) {
  return reinterpret_cast<sockaddr_storage const*>(a);
}
/// @return @a a cast to @c sockaddr*.
inline sockaddr* ink_inet_sa_cast(sockaddr_storage* a) {
  return reinterpret_cast<sockaddr*>(a);
}
/// @return @a a cast to sockaddr const*.
inline sockaddr const* ink_inet_sa_cast(sockaddr_storage const* a) {
  return reinterpret_cast<sockaddr const*>(a);
}
/// @return @a a cast to sockaddr const*.
inline sockaddr const* ink_inet_sa_cast(sockaddr_in6 const* a) {
  return reinterpret_cast<sockaddr const*>(a);
}

/// Test for IPv4 protocol.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool ink_inet_is_ip4(sockaddr_storage const* addr) {
  return AF_INET == addr->ss_family;
}
/// Test for IPv4 protocol.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool ink_inet_is_ip4(sockaddr const* addr) {
  return AF_INET == addr->sa_family;
}
/// Test for IPv6 protocol.
/// Convenience overload.
/// @return @c true if the address is IPv6, @c false otherwise.
inline bool ink_inet_is_ip6(sockaddr_storage const* addr) {
  return AF_INET6 == addr->ss_family;
}

/// IPv4 cast.
/// @return @a a cast to a @c sockaddr_in*
inline sockaddr_in* ink_inet_ip4_cast(
  sockaddr_storage* a ///< Address structure.
) {
  return static_cast<sockaddr_in*>(static_cast<void*>(a));
}
/// IPv4 cast.
/// @return @a a cast to a @c sockaddr_in*
inline sockaddr_in const* ink_inet_ip4_cast(
  sockaddr_storage const* a ///< Address structure.
) {
  return static_cast<sockaddr_in const*>(static_cast<void const*>(a));
}
/// IPv4 cast.
/// @return @a a cast to a @c sockaddr_in*
inline sockaddr_in const* ink_inet_ip4_cast(
  sockaddr const* a ///< Address structure.
) {
  return reinterpret_cast<sockaddr_in const*>(a);
}
/// IPv6 cast.
/// @return @a a cast to a @c sockaddr_in6*
inline sockaddr_in6* ink_inet_ip6_cast(sockaddr_storage* a) {
  return static_cast<sockaddr_in6*>(static_cast<void*>(a));
}
/// IPv6 cast.
/// @return @a a cast to a @c sockaddr_in6*
inline sockaddr_in6 const* ink_inet_ip6_cast(sockaddr_storage const* a) {
  return static_cast<sockaddr_in6 const*>(static_cast<void const*>(a));
}

/** Get a reference to the port in an address.
    @note Because this is direct access, the port value is in network order.
    @see ink_inet_get_port for host order copy.
    @return A reference to the port value in an IPv4 or IPv6 address.
    @internal This is primarily for internal use but it might be handy for
    clients so it is exposed.
*/
inline uint16_t& ink_inet_port_cast(sockaddr_storage* ss) {
  static uint16_t dummy = 0;
  return AF_INET == ss->ss_family
    ? ink_inet_ip4_cast(ss)->sin_port
    : AF_INET6 == ss->ss_family
      ? ink_inet_ip6_cast(ss)->sin6_port
      : (dummy = 0)
    ;
}
/** Get a reference to the port in an address.
    @note Because this is direct access, the port value is in network order.
    @see ink_inet_get_port for host order copy.
    @return A reference to the port value in an IPv4 or IPv6 address.
    @internal This is primarily for internal use but it might be handy for
    clients so it is exposed.
*/
inline uint16_t const& ink_inet_port_cast(sockaddr_storage const* ss) {
    return ink_inet_port_cast(const_cast<sockaddr_storage*>(ss));
}
/** Get a reference to the port in an address.
    @note Because this is direct access, the port value is in network order.
    @see ink_inet_get_port for host order copy.
    @return A reference to the port value in an IPv4 or IPv6 address.
    @internal This is primarily for internal use but it might be handy for
    clients so it is exposed.
*/
inline uint16_t const& ink_inet_port_cast(sockaddr const* sa) {
    return ink_inet_port_cast(ink_inet_ss_cast(sa));
}

/** Access the IPv4 address.

    If @a addr is not IPv4 the results are indeterminate.
    
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t& ink_inet_ip4_addr_cast(sockaddr_storage* addr) {
    return ink_inet_ip4_cast(addr)->sin_addr.s_addr;
}
/** Access the IPv4 address.

    If @a addr is not IPv4 the results are indeterminate.
    
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const& ink_inet_ip4_addr_cast(sockaddr_storage const* addr) {
    return ink_inet_ip4_cast(addr)->sin_addr.s_addr;
}
/** Access the IPv4 address.

    If @a addr is not IPv4 the results are indeterminate.
    
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const& ink_inet_ip4_addr_cast(sockaddr const* addr) {
    return ink_inet_ip4_cast(addr)->sin_addr.s_addr;
}

/// Write IPv4 data to a @c sockaddr_storage.
inline void ink_inet_ip4_set(
  sockaddr_storage* ss, ///< Destination storage.
  in_addr_t ip4, ///< address, IPv4 network order.
  uint16_t port = 0 ///< port, network order.
) {
  sockaddr_in* sin = ink_inet_ip4_cast(ss);
  memset(sin, 0, sizeof(*sin));
  sin->sin_family = AF_INET;
  memcpy(&(sin->sin_addr), &ip4, sizeof(ip4));
  sin->sin_port = port;
}

#endif // _ink_inet.h
