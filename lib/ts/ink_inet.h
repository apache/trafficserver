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

#pragma once

#include <netinet/in.h>
#include <netdb.h>
#include <sys/socket.h>
#include <string_view>

#include <ts/ink_memory.h>
#include <ts/ink_apidefs.h>
#include <ts/BufferWriterForward.h>

#if !TS_HAS_IN6_IS_ADDR_UNSPECIFIED
#if defined(IN6_IS_ADDR_UNSPECIFIED)
#undef IN6_IS_ADDR_UNSPECIFIED
#endif
static inline bool
IN6_IS_ADDR_UNSPECIFIED(in6_addr const *addr)
{
  uint64_t const *w = reinterpret_cast<uint64_t const *>(addr);
  return 0 == w[0] && 0 == w[1];
}
#endif

// IP protocol stack tags.
extern const std::string_view IP_PROTO_TAG_IPV4;
extern const std::string_view IP_PROTO_TAG_IPV6;
extern const std::string_view IP_PROTO_TAG_UDP;
extern const std::string_view IP_PROTO_TAG_TCP;
extern const std::string_view IP_PROTO_TAG_TLS_1_0;
extern const std::string_view IP_PROTO_TAG_TLS_1_1;
extern const std::string_view IP_PROTO_TAG_TLS_1_2;
extern const std::string_view IP_PROTO_TAG_TLS_1_3;
extern const std::string_view IP_PROTO_TAG_HTTP_0_9;
extern const std::string_view IP_PROTO_TAG_HTTP_1_0;
extern const std::string_view IP_PROTO_TAG_HTTP_1_1;
extern const std::string_view IP_PROTO_TAG_HTTP_2_0;

struct IpAddr; // forward declare.

/** A union to hold the standard IP address structures.
    By standard we mean @c sockaddr compliant.

    We use the term "endpoint" because these contain more than just the
    raw address, all of the data for an IP endpoint is present.

    @internal This might be useful to promote to avoid strict aliasing
    problems.  Experiment with it here to see how it works in the
    field.

    @internal @c sockaddr_storage is not present because it is so
    large and the benefits of including it are small. Use of this
    structure will make it easy to add if that becomes necessary.

 */
union IpEndpoint {
  typedef IpEndpoint self; ///< Self reference type.

  struct sockaddr sa;       ///< Generic address.
  struct sockaddr_in sin;   ///< IPv4
  struct sockaddr_in6 sin6; ///< IPv6

  /** Assign from a socket address.
      The entire address (all parts) are copied if the @a ip is valid.
  */
  self &assign(sockaddr const *ip ///< Source address, family, port.
  );
  /// Assign from an @a addr and @a port.
  self &assign(IpAddr const &addr, ///< Address and address family.
               in_port_t port = 0  ///< Port (network order).
  );

  /// Test for valid IP address.
  bool isValid() const;
  /// Test for IPv4.
  bool isIp4() const;
  /// Test for IPv6.
  bool isIp6() const;

  uint16_t family() const;

  /// Set to be any address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self &setToAnyAddr(int family ///< Address family.
  );
  /// Set to be loopback for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self &setToLoopback(int family ///< Address family.
  );

  /// Port in network order.
  in_port_t &port();
  /// Port in network order.
  in_port_t port() const;
  /// Port in host horder.
  in_port_t host_order_port() const;

  operator sockaddr *() { return &sa; }
  operator sockaddr const *() const { return &sa; }
};

/** Return the detected maximum listen(2) backlog for TCP. */
int ats_tcp_somaxconn();

/** Parse a string for pieces of an IP address.

    This doesn't parse the actual IP address, but picks it out from @a
    src. It is intended to deal with the brackets that can optionally
    surround an IP address (usually IPv6) which in turn are used to
    differentiate between an address and an attached port. E.g.
    @code
      [FE80:9312::192:168:1:1]:80
    @endcode
    @a addr or @a port can be @c nullptr in which case that value isn't returned.

    @return 0 if an address was found, non-zero otherwise.
*/
int ats_ip_parse(std::string_view src,            ///< [in] String to search.
                 std::string_view *addr,          ///< [out] Range containing IP address.
                 std::string_view *port,          ///< [out] Range containing port.
                 std::string_view *rest = nullptr ///< [out] Remnant past the addr/port if any.
);

/**  Check to see if a buffer contains only IP address characters.
     @return
    - AF_UNSPEC - not a numeric address.
    - AF_INET - only digits and dots.
    - AF_INET6 - colons found.
*/
int ats_ip_check_characters(std::string_view text);

/**
  Wrapper for inet_addr().

  @param s IP address in the Internet standard dot notation.

*/
inkcoreapi uint32_t ats_inet_addr(const char *s);

const char *ats_ip_ntop(const struct sockaddr *addr, char *dst, size_t size);

// --
/// Size in bytes of an IPv6 address.
static size_t const TS_IP6_SIZE = sizeof(in6_addr);

/// Reset an address to invalid.
/// @note Useful for marking a member as not yet set.
inline void
ats_ip_invalidate(sockaddr *addr)
{
  addr->sa_family = AF_UNSPEC;
}
inline void
ats_ip_invalidate(sockaddr_in6 *addr)
{
  addr->sin6_family = AF_UNSPEC;
}
inline void
ats_ip_invalidate(IpEndpoint *ip)
{
  ip->sa.sa_family = AF_UNSPEC;
}

/** Get a string name for an IP address family.
    @return The string name (never @c nullptr).
*/
std::string_view ats_ip_family_name(int family);

/// Test for IP protocol.
/// @return @c true if the address is IP, @c false otherwise.
inline bool
ats_is_ip(sockaddr const *addr)
{
  return addr && (AF_INET == addr->sa_family || AF_INET6 == addr->sa_family);
}
/// @return @c true if the address is IP, @c false otherwise.
inline bool
ats_is_ip(IpEndpoint const *addr)
{
  return addr && (AF_INET == addr->sa.sa_family || AF_INET6 == addr->sa.sa_family);
}
/// Test for IP protocol.
/// @return @c true if the value is an IP address family, @c false otherwise.
inline bool
ats_is_ip(int family)
{
  return AF_INET == family || AF_INET6 == family;
}
/// Test for IPv4 protocol.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool
ats_is_ip4(sockaddr const *addr)
{
  return addr && AF_INET == addr->sa_family;
}
/// Test for IPv4 protocol.
/// @note Convenience overload.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool
ats_is_ip4(IpEndpoint const *addr)
{
  return addr && AF_INET == addr->sa.sa_family;
}
/// Test for IPv6 protocol.
/// @return @c true if the address is IPv6, @c false otherwise.
inline bool
ats_is_ip6(sockaddr const *addr)
{
  return addr && AF_INET6 == addr->sa_family;
}
/// Test for IPv6 protocol.
/// @note Convenience overload.
/// @return @c true if the address is IPv6, @c false otherwise.
inline bool
ats_is_ip6(IpEndpoint const *addr)
{
  return addr && AF_INET6 == addr->sa.sa_family;
}

/// @return @c true if the address families are compatible.
inline bool
ats_ip_are_compatible(sockaddr const *lhs, ///< Address to test.
                      sockaddr const *rhs  ///< Address to test.
)
{
  return lhs->sa_family == rhs->sa_family;
}
/// @return @c true if the address families are compatible.
inline bool
ats_ip_are_compatible(IpEndpoint const *lhs, ///< Address to test.
                      IpEndpoint const *rhs  ///< Address to test.
)
{
  return ats_ip_are_compatible(&lhs->sa, &rhs->sa);
}
/// @return @c true if the address families are compatible.
inline bool
ats_ip_are_compatible(int lhs,            ///< Address family to test.
                      sockaddr const *rhs ///< Address to test.
)
{
  return lhs == rhs->sa_family;
}
/// @return @c true if the address families are compatible.
inline bool
ats_ip_are_compatible(sockaddr const *lhs, ///< Address to test.
                      int rhs              ///< Family to test.
)
{
  return lhs->sa_family == rhs;
}

// IP address casting.
// sa_cast to cast to sockaddr*.
// ss_cast to cast to sockaddr_storage*.
// ip4_cast converts to sockaddr_in (because that's effectively an IPv4 addr).
// ip6_cast converts to sockaddr_in6

inline sockaddr *
ats_ip_sa_cast(sockaddr_storage *a)
{
  return static_cast<sockaddr *>(static_cast<void *>(a));
}
inline sockaddr const *
ats_ip_sa_cast(sockaddr_storage const *a)
{
  return static_cast<sockaddr const *>(static_cast<void const *>(a));
}

inline sockaddr *
ats_ip_sa_cast(sockaddr_in *a)
{
  return static_cast<sockaddr *>(static_cast<void *>(a));
}
inline sockaddr const *
ats_ip_sa_cast(sockaddr_in const *a)
{
  return static_cast<sockaddr const *>(static_cast<void const *>(a));
}

inline sockaddr *
ats_ip_sa_cast(sockaddr_in6 *a)
{
  return static_cast<sockaddr *>(static_cast<void *>(a));
}
inline sockaddr const *
ats_ip_sa_cast(sockaddr_in6 const *a)
{
  return static_cast<sockaddr const *>(static_cast<void const *>(a));
}

inline sockaddr_storage *
ats_ip_ss_cast(sockaddr *a)
{
  return static_cast<sockaddr_storage *>(static_cast<void *>(a));
}
inline sockaddr_storage const *
ats_ip_ss_cast(sockaddr const *a)
{
  return static_cast<sockaddr_storage const *>(static_cast<void const *>(a));
}

inline sockaddr_in *
ats_ip4_cast(sockaddr *a)
{
  return static_cast<sockaddr_in *>(static_cast<void *>(a));
}
inline sockaddr_in const *
ats_ip4_cast(sockaddr const *a)
{
  return static_cast<sockaddr_in const *>(static_cast<void const *>(a));
}

inline sockaddr_in &
ats_ip4_cast(sockaddr &a)
{
  return *static_cast<sockaddr_in *>(static_cast<void *>(&a));
}
inline sockaddr_in const &
ats_ip4_cast(sockaddr const &a)
{
  return *static_cast<sockaddr_in const *>(static_cast<void const *>(&a));
}

inline sockaddr_in *
ats_ip4_cast(sockaddr_in6 *a)
{
  return static_cast<sockaddr_in *>(static_cast<void *>(a));
}
inline sockaddr_in const *
ats_ip4_cast(sockaddr_in6 const *a)
{
  return static_cast<sockaddr_in const *>(static_cast<void const *>(a));
}

inline sockaddr_in &
ats_ip4_cast(sockaddr_in6 &a)
{
  return *static_cast<sockaddr_in *>(static_cast<void *>(&a));
}
inline sockaddr_in const &
ats_ip4_cast(sockaddr_in6 const &a)
{
  return *static_cast<sockaddr_in const *>(static_cast<void const *>(&a));
}

inline sockaddr_in6 *
ats_ip6_cast(sockaddr *a)
{
  return static_cast<sockaddr_in6 *>(static_cast<void *>(a));
}
inline sockaddr_in6 const *
ats_ip6_cast(sockaddr const *a)
{
  return static_cast<sockaddr_in6 const *>(static_cast<void const *>(a));
}
inline sockaddr_in6 &
ats_ip6_cast(sockaddr &a)
{
  return *static_cast<sockaddr_in6 *>(static_cast<void *>(&a));
}
inline sockaddr_in6 const &
ats_ip6_cast(sockaddr const &a)
{
  return *static_cast<sockaddr_in6 const *>(static_cast<void const *>(&a));
}

/// @return The @c sockaddr size for the family of @a addr.
inline size_t
ats_ip_size(sockaddr const *addr ///< Address object.
)
{
  return AF_INET == addr->sa_family ? sizeof(sockaddr_in) : AF_INET6 == addr->sa_family ? sizeof(sockaddr_in6) : 0;
}
inline size_t
ats_ip_size(IpEndpoint const *addr ///< Address object.
)
{
  return AF_INET == addr->sa.sa_family ? sizeof(sockaddr_in) : AF_INET6 == addr->sa.sa_family ? sizeof(sockaddr_in6) : 0;
}
/// @return The size of the IP address only.
inline size_t
ats_ip_addr_size(sockaddr const *addr ///< Address object.
)
{
  return AF_INET == addr->sa_family ? sizeof(in_addr_t) : AF_INET6 == addr->sa_family ? sizeof(in6_addr) : 0;
}
inline size_t
ats_ip_addr_size(IpEndpoint const *addr ///< Address object.
)
{
  return AF_INET == addr->sa.sa_family ? sizeof(in_addr_t) : AF_INET6 == addr->sa.sa_family ? sizeof(in6_addr) : 0;
}

/** Get a reference to the port in an address.
    @note Because this is direct access, the port value is in network order.
    @see ats_ip_port_host_order.
    @return A reference to the port value in an IPv4 or IPv6 address.
    @internal This is primarily for internal use but it might be handy for
    clients so it is exposed.
*/
inline in_port_t &
ats_ip_port_cast(sockaddr *sa)
{
  static in_port_t dummy = 0;
  return ats_is_ip4(sa) ? ats_ip4_cast(sa)->sin_port : ats_is_ip6(sa) ? ats_ip6_cast(sa)->sin6_port : (dummy = 0);
}
inline in_port_t const &
ats_ip_port_cast(sockaddr const *sa)
{
  return ats_ip_port_cast(const_cast<sockaddr *>(sa));
}
inline in_port_t const &
ats_ip_port_cast(IpEndpoint const *ip)
{
  return ats_ip_port_cast(const_cast<sockaddr *>(&ip->sa));
}
inline in_port_t &
ats_ip_port_cast(IpEndpoint *ip)
{
  return ats_ip_port_cast(&ip->sa);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t &
ats_ip4_addr_cast(sockaddr *addr)
{
  static in_addr_t dummy = 0;
  return ats_is_ip4(addr) ? ats_ip4_cast(addr)->sin_addr.s_addr : (dummy = 0);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const &
ats_ip4_addr_cast(sockaddr const *addr)
{
  static in_addr_t dummy = 0;
  return ats_is_ip4(addr) ? ats_ip4_cast(addr)->sin_addr.s_addr : static_cast<in_addr_t const &>(dummy = 0);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.
    @note Convenience overload.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t &
ats_ip4_addr_cast(IpEndpoint *ip)
{
  return ats_ip4_addr_cast(&ip->sa);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.
    @note Convenience overload.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const &
ats_ip4_addr_cast(IpEndpoint const *ip)
{
  return ats_ip4_addr_cast(&ip->sa);
}

/** Access the IPv6 address.

    If this is not an IPv6 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv6 address in @a addr.
*/
inline in6_addr &
ats_ip6_addr_cast(sockaddr *addr)
{
  return ats_ip6_cast(addr)->sin6_addr;
}
inline in6_addr const &
ats_ip6_addr_cast(sockaddr const *addr)
{
  return ats_ip6_cast(addr)->sin6_addr;
}
inline in6_addr &
ats_ip6_addr_cast(IpEndpoint *ip)
{
  return ip->sin6.sin6_addr;
}
inline in6_addr const &
ats_ip6_addr_cast(IpEndpoint const *ip)
{
  return ip->sin6.sin6_addr;
}

/** Cast an IP address to an array of @c uint32_t.
    @note The size of the array is dependent on the address type which
    must be checked independently of this function.
    @return A pointer to the address information in @a addr or @c nullptr
    if @a addr is not an IP address.
*/
inline uint32_t *
ats_ip_addr32_cast(sockaddr *addr)
{
  uint32_t *zret = nullptr;
  switch (addr->sa_family) {
  case AF_INET:
    zret = reinterpret_cast<uint32_t *>(&ats_ip4_addr_cast(addr));
    break;
  case AF_INET6:
    zret = reinterpret_cast<uint32_t *>(&ats_ip6_addr_cast(addr));
    break;
  }
  return zret;
}
inline uint32_t const *
ats_ip_addr32_cast(sockaddr const *addr)
{
  return ats_ip_addr32_cast(const_cast<sockaddr *>(addr));
}

/** Cast an IP address to an array of @c uint8_t.
    @note The size of the array is dependent on the address type which
    must be checked independently of this function.
    @return A pointer to the address information in @a addr or @c nullptr
    if @a addr is not an IP address.
    @see ats_ip_addr_size
*/
inline uint8_t *
ats_ip_addr8_cast(sockaddr *addr)
{
  uint8_t *zret = nullptr;
  switch (addr->sa_family) {
  case AF_INET:
    zret = reinterpret_cast<uint8_t *>(&ats_ip4_addr_cast(addr));
    break;
  case AF_INET6:
    zret = reinterpret_cast<uint8_t *>(&ats_ip6_addr_cast(addr));
    break;
  }
  return zret;
}
inline uint8_t const *
ats_ip_addr8_cast(sockaddr const *addr)
{
  return ats_ip_addr8_cast(const_cast<sockaddr *>(addr));
}
inline uint8_t *
ats_ip_addr8_cast(IpEndpoint *ip)
{
  return ats_ip_addr8_cast(&ip->sa);
}
inline uint8_t const *
ats_ip_addr8_cast(IpEndpoint const *ip)
{
  return ats_ip_addr8_cast(&ip->sa);
}

/// Check for loopback.
/// @return @c true if this is an IP loopback address, @c false otherwise.
inline bool
ats_is_ip_loopback(sockaddr const *ip)
{
  return ip && ((AF_INET == ip->sa_family && 0x7F == ats_ip_addr8_cast(ip)[0]) ||
                (AF_INET6 == ip->sa_family && IN6_IS_ADDR_LOOPBACK(&ats_ip6_addr_cast(ip))));
}

/// Check for loopback.
/// @return @c true if this is an IP loopback address, @c false otherwise.
inline bool
ats_is_ip_loopback(IpEndpoint const *ip)
{
  return ats_is_ip_loopback(&ip->sa);
}

/// Check for multicast.
/// @return @true if @a ip is multicast.
inline bool
ats_is_ip_multicast(sockaddr const *ip)
{
  return ip && ((AF_INET == ip->sa_family && 0xe == (ats_ip_addr8_cast(ip)[0] >> 4)) ||
                (AF_INET6 == ip->sa_family && IN6_IS_ADDR_MULTICAST(&ats_ip6_addr_cast(ip))));
}
/// Check for multicast.
/// @return @true if @a ip is multicast.
inline bool
ats_is_ip_multicast(IpEndpoint const *ip)
{
  return ats_is_ip_multicast(&ip->sa);
}

/// Check for Private.
/// @return @true if @a ip is private.
inline bool
ats_is_ip_private(sockaddr const *ip)
{
  bool zret = false;
  if (ats_is_ip4(ip)) {
    in_addr_t a = ats_ip4_addr_cast(ip);
    zret        = ((a & htonl(0xFF000000)) == htonl(0x0A000000)) || // 10.0.0.0/8
           ((a & htonl(0xFFC00000)) == htonl(0x64400000)) ||        // 100.64.0.0/10
           ((a & htonl(0xFFF00000)) == htonl(0xAC100000)) ||        // 172.16.0.0/12
           ((a & htonl(0xFFFF0000)) == htonl(0xC0A80000))           // 192.168.0.0/16
      ;
  } else if (ats_is_ip6(ip)) {
    in6_addr a = ats_ip6_addr_cast(ip);
    zret       = ((a.s6_addr[0] & 0xFE) == 0xFC) // fc00::/7
      ;
  }
  return zret;
}

/// Check for Private.
/// @return @true if @a ip is private.
inline bool
ats_is_ip_private(IpEndpoint const *ip)
{
  return ats_is_ip_private(&ip->sa);
}

/// Check for Link Local.
/// @return @true if @a ip is link local.
inline bool
ats_is_ip_linklocal(sockaddr const *ip)
{
  bool zret = false;
  if (ats_is_ip4(ip)) {
    in_addr_t a = ats_ip4_addr_cast(ip);
    zret        = ((a & htonl(0xFFFF0000)) == htonl(0xA9FE0000)) // 169.254.0.0/16
      ;
  } else if (ats_is_ip6(ip)) {
    in6_addr a = ats_ip6_addr_cast(ip);
    zret       = ((a.s6_addr[0] == 0xFE) && ((a.s6_addr[1] & 0xC0) == 0x80)) // fe80::/10
      ;
  }
  return zret;
}

/// Check for Link Local.
/// @return @true if @a ip is link local.
inline bool
ats_is_ip_linklocal(IpEndpoint const *ip)
{
  return ats_is_ip_linklocal(&ip->sa);
}

/// Check for being "any" address.
/// @return @c true if @a ip is the any / unspecified address.
inline bool
ats_is_ip_any(sockaddr const *ip)
{
  return (ats_is_ip4(ip) && INADDR_ANY == ats_ip4_addr_cast(ip)) ||
         (ats_is_ip6(ip) && IN6_IS_ADDR_UNSPECIFIED(&ats_ip6_addr_cast(ip)));
}

/// @name Address operators
//@{

/** Copy the address from @a src to @a dst if it's IP.
    This attempts to do a minimal copy based on the type of @a src.
    If @a src is not an IP address type it is @b not copied and
    @a dst is marked as invalid.
    @return @c true if @a src was an IP address, @c false otherwise.
*/
inline bool
ats_ip_copy(sockaddr *dst,      ///< Destination object.
            sockaddr const *src ///< Source object.
)
{
  size_t n = 0;
  if (src) {
    switch (src->sa_family) {
    case AF_INET:
      n = sizeof(sockaddr_in);
      break;
    case AF_INET6:
      n = sizeof(sockaddr_in6);
      break;
    }
  }
  if (n) {
    if (src != dst) {
      memcpy(dst, src, n);
#if HAVE_STRUCT_SOCKADDR_SA_LEN
      dst->sa_len = n;
#endif
    }
  } else {
    ats_ip_invalidate(dst);
  }
  return n != 0;
}

inline bool
ats_ip_copy(IpEndpoint *dst,    ///< Destination object.
            sockaddr const *src ///< Source object.
)
{
  return ats_ip_copy(&dst->sa, src);
}
inline bool
ats_ip_copy(IpEndpoint *dst,      ///< Destination object.
            IpEndpoint const *src ///< Source object.
)
{
  return ats_ip_copy(&dst->sa, &src->sa);
}
inline bool
ats_ip_copy(sockaddr *dst, IpEndpoint const *src)
{
  return ats_ip_copy(dst, &src->sa);
}

/** Compare two addresses.
    This is useful for IPv4, IPv6, and the unspecified address type.
    If the addresses are of different types they are ordered

    Non-IP < IPv4 < IPv6

     - all non-IP addresses are the same ( including @c AF_UNSPEC )
     - IPv4 addresses are compared numerically (host order)
     - IPv6 addresses are compared byte wise in network order (MSB to LSB)

    @return
      - -1 if @a lhs is less than @a rhs.
      - 0 if @a lhs is identical to @a rhs.
      - 1 if @a lhs is greater than @a rhs.

    @internal This looks like a lot of code for an inline but I think it
    should compile down to something reasonable.
*/
inline int
ats_ip_addr_cmp(sockaddr const *lhs, ///< Left hand operand.
                sockaddr const *rhs  ///< Right hand operand.
)
{
  int zret       = 0;
  uint16_t rtype = rhs->sa_family;
  uint16_t ltype = lhs->sa_family;

  // We lump all non-IP addresses into a single equivalence class
  // that is less than an IP address. This includes AF_UNSPEC.
  if (AF_INET == ltype) {
    if (AF_INET == rtype) {
      in_addr_t la = ntohl(ats_ip4_cast(lhs)->sin_addr.s_addr);
      in_addr_t ra = ntohl(ats_ip4_cast(rhs)->sin_addr.s_addr);
      if (la < ra)
        zret = -1;
      else if (la > ra)
        zret = 1;
      else
        zret = 0;
    } else if (AF_INET6 == rtype) { // IPv4 < IPv6
      zret = -1;
    } else { // IP > not IP
      zret = 1;
    }
  } else if (AF_INET6 == ltype) {
    if (AF_INET6 == rtype) {
      sockaddr_in6 const *lhs_in6 = ats_ip6_cast(lhs);
      zret                        = memcmp(&lhs_in6->sin6_addr, &ats_ip6_cast(rhs)->sin6_addr, sizeof(lhs_in6->sin6_addr));
    } else {
      zret = 1; // IPv6 greater than any other type.
    }
  } else if (AF_INET == rtype || AF_INET6 == rtype) {
    // ltype is non-IP so it's less than either IP type.
    zret = -1;
  } else {
    // Both types are non-IP so they're equal.
    zret = 0;
  }

  return zret;
}

/** Compare two addresses.
    @note Convenience overload.
    @see ats_ip_addr_cmp(sockaddr const* lhs, sockaddr const* rhs)
*/
inline int
ats_ip_addr_cmp(IpEndpoint const *lhs, IpEndpoint const *rhs)
{
  return ats_ip_addr_cmp(&lhs->sa, &rhs->sa);
}

/** Check if two addresses are equal.
    @return @c true if @a lhs and @a rhs point to equal addresses,
    @c false otherwise.
*/
inline bool
ats_ip_addr_eq(sockaddr const *lhs, sockaddr const *rhs)
{
  return 0 == ats_ip_addr_cmp(lhs, rhs);
}
inline bool
ats_ip_addr_eq(IpEndpoint const *lhs, IpEndpoint const *rhs)
{
  return 0 == ats_ip_addr_cmp(&lhs->sa, &rhs->sa);
}

inline bool
operator==(IpEndpoint const &lhs, IpEndpoint const &rhs)
{
  return 0 == ats_ip_addr_cmp(&lhs.sa, &rhs.sa);
}
inline bool
operator!=(IpEndpoint const &lhs, IpEndpoint const &rhs)
{
  return 0 != ats_ip_addr_cmp(&lhs.sa, &rhs.sa);
}

/// Compare address and port for equality.
inline bool
ats_ip_addr_port_eq(sockaddr const *lhs, sockaddr const *rhs)
{
  bool zret = false;
  if (lhs->sa_family == rhs->sa_family && ats_ip_port_cast(lhs) == ats_ip_port_cast(rhs)) {
    if (AF_INET == lhs->sa_family)
      zret = ats_ip4_cast(lhs)->sin_addr.s_addr == ats_ip4_cast(rhs)->sin_addr.s_addr;
    else if (AF_INET6 == lhs->sa_family)
      zret = 0 == memcmp(&ats_ip6_cast(lhs)->sin6_addr, &ats_ip6_cast(rhs)->sin6_addr, sizeof(in6_addr));
  }
  return zret;
}

//@}

/// Get IP TCP/UDP port.
/// @return The port in host order for an IPv4 or IPv6 address,
/// or zero if neither.
inline in_port_t
ats_ip_port_host_order(sockaddr const *addr ///< Address with port.
)
{
  // We can discard the const because this function returns
  // by value.
  return ntohs(ats_ip_port_cast(const_cast<sockaddr *>(addr)));
}

/// Get IP TCP/UDP port.
/// @return The port in host order for an IPv4 or IPv6 address,
/// or zero if neither.
inline in_port_t
ats_ip_port_host_order(IpEndpoint const *ip ///< Address with port.
)
{
  // We can discard the const because this function returns
  // by value.
  return ntohs(ats_ip_port_cast(const_cast<sockaddr *>(&ip->sa)));
}

/** Extract the IPv4 address.
    @return Host order IPv4 address.
*/
inline in_addr_t
ats_ip4_addr_host_order(sockaddr const *addr ///< Address object.
)
{
  return ntohl(ats_ip4_addr_cast(const_cast<sockaddr *>(addr)));
}

/// Write IPv4 data to storage @a dst.
inline sockaddr *
ats_ip4_set(sockaddr_in *dst,  ///< Destination storage.
            in_addr_t addr,    ///< address, IPv4 network order.
            in_port_t port = 0 ///< port, network order.
)
{
  ink_zero(*dst);
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  dst->sin_len = sizeof(sockaddr_in);
#endif
  dst->sin_family      = AF_INET;
  dst->sin_addr.s_addr = addr;
  dst->sin_port        = port;
  return ats_ip_sa_cast(dst);
}

/** Write IPv4 data to @a dst.
    @note Convenience overload.
*/
inline sockaddr *
ats_ip4_set(IpEndpoint *dst,   ///< Destination storage.
            in_addr_t ip4,     ///< address, IPv4 network order.
            in_port_t port = 0 ///< port, network order.
)
{
  return ats_ip4_set(&dst->sin, ip4, port);
}

/** Write IPv4 data to storage @a dst.

    This is the generic overload. Caller must verify that @a dst is at
    least @c sizeof(sockaddr_in) bytes.
*/
inline sockaddr *
ats_ip4_set(sockaddr *dst,     ///< Destination storage.
            in_addr_t ip4,     ///< address, IPv4 network order.
            in_port_t port = 0 ///< port, network order.
)
{
  return ats_ip4_set(ats_ip4_cast(dst), ip4, port);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr *
ats_ip6_set(sockaddr_in6 *dst,    ///< Destination storage.
            in6_addr const &addr, ///< address in network order.
            in_port_t port = 0    ///< Port, network order.
)
{
  ink_zero(*dst);
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  dst->sin6_len = sizeof(sockaddr_in6);
#endif
  dst->sin6_family = AF_INET6;
  memcpy(&dst->sin6_addr, &addr, sizeof addr);
  dst->sin6_port = port;
  return ats_ip_sa_cast(dst);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr *
ats_ip6_set(sockaddr *dst,        ///< Destination storage.
            in6_addr const &addr, ///< address in network order.
            in_port_t port = 0    ///< Port, network order.
)
{
  return ats_ip6_set(ats_ip6_cast(dst), addr, port);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr *
ats_ip6_set(IpEndpoint *dst,      ///< Destination storage.
            in6_addr const &addr, ///< address in network order.
            in_port_t port = 0    ///< Port, network order.
)
{
  return ats_ip6_set(&dst->sin6, addr, port);
}

/** Write a null terminated string for @a addr to @a dst.
    A buffer of size INET6_ADDRSTRLEN suffices, including a terminating nul.
 */
const char *ats_ip_ntop(const sockaddr *addr, ///< Address.
                        char *dst,            ///< Output buffer.
                        size_t size           ///< Length of buffer.
);

/** Write a null terminated string for @a addr to @a dst.
    A buffer of size INET6_ADDRSTRLEN suffices, including a terminating nul.
 */
inline const char *
ats_ip_ntop(IpEndpoint const *addr, ///< Address.
            char *dst,              ///< Output buffer.
            size_t size             ///< Length of buffer.
)
{
  return ats_ip_ntop(&addr->sa, dst, size);
}

/// Buffer size sufficient for IPv6 address and port.
static size_t const INET6_ADDRPORTSTRLEN = INET6_ADDRSTRLEN + 6;
/// Convenience type for address formatting.
typedef char ip_text_buffer[INET6_ADDRSTRLEN];
/// Convenience type for address formatting.
typedef char ip_port_text_buffer[INET6_ADDRPORTSTRLEN];

/** Write a null terminated string for @a addr to @a dst with port.
    A buffer of size INET6_ADDRPORTSTRLEN suffices, including a terminating nul.
 */
const char *ats_ip_nptop(const sockaddr *addr, ///< Address.
                         char *dst,            ///< Output buffer.
                         size_t size           ///< Length of buffer.
);

/** Write a null terminated string for @a addr to @a dst with port.
    A buffer of size INET6_ADDRPORTSTRLEN suffices, including a terminating nul.
 */
inline const char *
ats_ip_nptop(IpEndpoint const *addr, ///< Address.
             char *dst,              ///< Output buffer.
             size_t size             ///< Length of buffer.
)
{
  return ats_ip_nptop(&addr->sa, dst, size);
}

/** Convert @a text to an IP address and write it to @a addr.

    @a text is expected to be an explicit address, not a hostname.  No
    hostname resolution is done. The call must provide an @a ip large
    enough to hold the address value.

    This attempts to recognize and process a port value if
    present. The port in @a ip is set appropriately, or to zero if no
    port was found or it was malformed.

    @note The return values are logically reversed from @c inet_pton.
    @note This uses @c getaddrinfo internally and so involves memory
    allocation.

    @return 0 on success, non-zero on failure.
*/
int ats_ip_pton(const std::string_view &text, ///< [in] text.
                sockaddr *addr                ///< [out] address
);

/** Convert @a text to an IP address and write it to @a addr.

    @a text is expected to be an explicit address, not a hostname.  No
    hostname resolution is done.

    @note This uses @c getaddrinfo internally and so involves memory
    allocation.
    @note Convenience overload.

    @return 0 on success, non-zero on failure.
*/
inline int
ats_ip_pton(const char *text,  ///< [in] text.
            sockaddr_in6 *addr ///< [out] address
)
{
  return ats_ip_pton(std::string_view(text, strlen(text)), ats_ip_sa_cast(addr));
}

inline int
ats_ip_pton(const std::string_view &text, ///< [in] text.
            IpEndpoint *addr              ///< [out] address
)
{
  return ats_ip_pton(text, &addr->sa);
}

inline int
ats_ip_pton(const char *text, ///< [in] text.
            IpEndpoint *addr  ///< [out] address
)
{
  return ats_ip_pton(std::string_view(text, strlen(text)), &addr->sa);
}

inline int
ats_ip_pton(const char *text, ///< [in] text.
            sockaddr *addr    ///< [out] address
)
{
  return ats_ip_pton(std::string_view(text, strlen(text)), addr);
}

/** Get the best address info for @a name.

    @name is passed to @c getaddrinfo which does a host lookup if @a
    name is not in IP address format. The results are examined for the
    "best" addresses. This is only significant for the host name case
    (for IP address data, there is at most one result). The preference is
    Global > Non-Routable > Multicast > Loopback.

    IPv4 and IPv4 results are handled independently and stored in @a
    ip4 and @a ip6 respectively. If @a name is known to be a numeric
    IP address @c ats_ip_pton is a better choice. Use this function
    if the type of @a name is not known. If you want to look at the
    addresses and not just get the "best", use @c getaddrinfo
    directly.

    @a ip4 or @a ip6 can be @c nullptr and the result for that family is
    discarded. It is legal for both to be @c nullptr in which case this
    is just a format check.

    @return 0 if an address was found, non-zero otherwise.

    @see ats_ip_pton
    @see getaddrinfo
 */

int ats_ip_getbestaddrinfo(const char *name, ///< [in] Address name (IPv4, IPv6, or host name)
                           IpEndpoint *ip4,  ///< [out] Storage for IPv4 address.
                           IpEndpoint *ip6   ///< [out] Storage for IPv6 address
);

/** Generic IP address hash function.
 */
uint32_t ats_ip_hash(sockaddr const *addr);

uint64_t ats_ip_port_hash(sockaddr const *addr);

/** Convert address to string as a hexidecimal value.
    The string is always nul terminated, the output string is clipped
    if @a dst is insufficient.
    @return The length of the resulting string (not including nul).
*/
int ats_ip_to_hex(sockaddr const *addr, ///< Address to convert. Must be IP.
                  char *dst,            ///< Destination buffer.
                  size_t len            ///< Length of @a dst.
);

/** Storage for an IP address.
    In some cases we want to store just the address and not the
    ancillary information (such as port, or flow data).
    @note This is not easily used as an address for system calls.
*/
struct IpAddr {
  typedef IpAddr self; ///< Self reference type.

  /// Default construct (invalid address).
  IpAddr() : _family(AF_UNSPEC) {}
  /// Construct as IPv4 @a addr.
  explicit IpAddr(in_addr_t addr ///< Address to assign.
                  )
    : _family(AF_INET)
  {
    _addr._ip4 = addr;
  }
  /// Construct as IPv6 @a addr.
  explicit IpAddr(in6_addr const &addr ///< Address to assign.
                  )
    : _family(AF_INET6)
  {
    _addr._ip6 = addr;
  }
  /// Construct from @c sockaddr.
  explicit IpAddr(sockaddr const *addr) { this->assign(addr); }
  /// Construct from @c sockaddr_in6.
  explicit IpAddr(sockaddr_in6 const &addr) { this->assign(ats_ip_sa_cast(&addr)); }
  /// Construct from @c sockaddr_in6.
  explicit IpAddr(sockaddr_in6 const *addr) { this->assign(ats_ip_sa_cast(addr)); }
  /// Construct from @c IpEndpoint.
  explicit IpAddr(IpEndpoint const &addr) { this->assign(&addr.sa); }
  /// Construct from @c IpEndpoint.
  explicit IpAddr(IpEndpoint const *addr) { this->assign(&addr->sa); }
  /// Assign sockaddr storage.
  self &assign(sockaddr const *addr ///< May be @c nullptr
  );

  /// Assign from end point.
  self &
  operator=(IpEndpoint const &ip)
  {
    return this->assign(&ip.sa);
  }
  /// Assign from IPv4 raw address.
  /// @param ip Network order IPv4 address.
  self &operator=(in_addr_t ip);

  /// Assign from IPv6 raw address.
  self &operator=(in6_addr const &ip);

  /** Load from string.
      The address is copied to this object if the conversion is successful,
      otherwise this object is invalidated.
      @return 0 on success, non-zero on failure.
  */
  int load(const char *str ///< Nul terminated input string.
  );

  /** Load from string.
      The address is copied to this object if the conversion is successful,
      otherwise this object is invalidated.
      @return 0 on success, non-zero on failure.
  */
  int load(std::string_view const &str ///< Text of IP address.
  );

  /** Output to a string.
      @return The string @a dest.
  */
  char *toString(char *dest, ///< [out] Destination string buffer.
                 size_t len  ///< [in] Size of buffer.
                 ) const;

  /// Equality.
  bool
  operator==(self const &that) const
  {
    return _family == AF_INET ?
             (that._family == AF_INET && _addr._ip4 == that._addr._ip4) :
             _family == AF_INET6 ? (that._family == AF_INET6 && 0 == memcmp(&_addr._ip6, &that._addr._ip6, TS_IP6_SIZE)) :
                                   (_family == AF_UNSPEC && that._family == AF_UNSPEC);
  }

  /// Inequality.
  bool
  operator!=(self const &that) const
  {
    return !(*this == that);
  }

  /// Generic compare.
  int cmp(self const &that) const;

  /** Return a normalized hash value.
      - Ipv4: the address in host order.
      - Ipv6: folded 32 bit of the address.
      - Else: 0.
  */
  uint32_t hash() const;

  /** The hashing function embedded in a functor.
      @see hash
  */
  struct Hasher {
    uint32_t
    operator()(self const &ip) const
    {
      return ip.hash();
    }
  };

  /// Test for same address family.
  /// @c return @c true if @a that is the same address family as @a this.
  bool isCompatibleWith(self const &that);

  /// Get the address family.
  /// @return The address family.
  uint16_t family() const;
  /// Test for IPv4.
  bool isIp4() const;
  /// Test for IPv6.
  bool isIp6() const;

  /// Test for validity.
  bool
  isValid() const
  {
    return _family == AF_INET || _family == AF_INET6;
  }
  /// Make invalid.
  self &
  invalidate()
  {
    _family = AF_UNSPEC;
    return *this;
  }
  /// Test for multicast
  bool isMulticast() const;
  /// Test for loopback
  bool isLoopback() const;

  uint16_t _family; ///< Protocol family.
  /// Address data.
  union {
    in_addr_t _ip4;                                                    ///< IPv4 address storage.
    in6_addr _ip6;                                                     ///< IPv6 address storage.
    uint8_t _byte[TS_IP6_SIZE];                                        ///< As raw bytes.
    uint32_t _u32[TS_IP6_SIZE / (sizeof(uint32_t) / sizeof(uint8_t))]; ///< As 32 bit chunks.
    uint64_t _u64[TS_IP6_SIZE / (sizeof(uint64_t) / sizeof(uint8_t))]; ///< As 64 bit chunks.
  } _addr;

  ///< Pre-constructed invalid instance.
  static self const INVALID;
};

inline IpAddr &
IpAddr::operator=(in_addr_t ip)
{
  _family    = AF_INET;
  _addr._ip4 = ip;
  return *this;
}

inline IpAddr &
IpAddr::operator=(in6_addr const &ip)
{
  _family    = AF_INET6;
  _addr._ip6 = ip;
  return *this;
}

inline uint16_t
IpAddr::family() const
{
  return _family;
}

inline bool
IpAddr::isCompatibleWith(self const &that)
{
  return this->isValid() && _family == that._family;
}

inline bool
IpAddr::isIp4() const
{
  return AF_INET == _family;
}
inline bool
IpAddr::isIp6() const
{
  return AF_INET6 == _family;
}

inline bool
IpAddr::isLoopback() const
{
  return (AF_INET == _family && 0x7F == _addr._byte[0]) || (AF_INET6 == _family && IN6_IS_ADDR_LOOPBACK(&_addr._ip6));
}

/// Assign sockaddr storage.
inline IpAddr &
IpAddr::assign(sockaddr const *addr)
{
  if (addr) {
    _family = addr->sa_family;
    if (ats_is_ip4(addr)) {
      _addr._ip4 = ats_ip4_addr_cast(addr);
    } else if (ats_is_ip6(addr)) {
      _addr._ip6 = ats_ip6_addr_cast(addr);
    } else {
      _family = AF_UNSPEC;
    }
  } else {
    _family = AF_UNSPEC;
  }
  return *this;
}

// Associated operators.
bool operator==(IpAddr const &lhs, sockaddr const *rhs);
inline bool
operator==(sockaddr const *lhs, IpAddr const &rhs)
{
  return rhs == lhs;
}
inline bool
operator!=(IpAddr const &lhs, sockaddr const *rhs)
{
  return !(lhs == rhs);
}
inline bool
operator!=(sockaddr const *lhs, IpAddr const &rhs)
{
  return !(rhs == lhs);
}
inline bool
operator==(IpAddr const &lhs, IpEndpoint const &rhs)
{
  return lhs == &rhs.sa;
}
inline bool
operator==(IpEndpoint const &lhs, IpAddr const &rhs)
{
  return &lhs.sa == rhs;
}
inline bool
operator!=(IpAddr const &lhs, IpEndpoint const &rhs)
{
  return !(lhs == &rhs.sa);
}
inline bool
operator!=(IpEndpoint const &lhs, IpAddr const &rhs)
{
  return !(rhs == &lhs.sa);
}

inline bool
operator<(IpAddr const &lhs, IpAddr const &rhs)
{
  return -1 == lhs.cmp(rhs);
}

inline bool
operator>=(IpAddr const &lhs, IpAddr const &rhs)
{
  return lhs.cmp(rhs) >= 0;
}

inline bool
operator>(IpAddr const &lhs, IpAddr const &rhs)
{
  return 1 == lhs.cmp(rhs);
}

inline bool
operator<=(IpAddr const &lhs, IpAddr const &rhs)
{
  return lhs.cmp(rhs) <= 0;
}

inline uint32_t
IpAddr::hash() const
{
  uint32_t zret = 0;
  if (this->isIp4()) {
    zret = ntohl(_addr._ip4);
  } else if (this->isIp6()) {
    zret = _addr._u32[0] ^ _addr._u32[1] ^ _addr._u32[2] ^ _addr._u32[3];
  }
  return zret;
}

/// Write IP @a addr to storage @a dst.
/// @return @s dst.
sockaddr *ats_ip_set(sockaddr *dst,      ///< Destination storage.
                     IpAddr const &addr, ///< source address.
                     in_port_t port = 0  ///< port, network order.
);

/** Convert @a text to an IP address and write it to @a addr.
    Convenience overload.
    @return 0 on success, non-zero on failure.
*/
inline int
ats_ip_pton(const char *text, ///< [in] text.
            IpAddr &addr      ///< [out] address
)
{
  return addr.load(text) ? 0 : -1;
}

int ats_ip_range_parse(std::string_view src, IpAddr &lower, IpAddr &upper);

inline IpEndpoint &
IpEndpoint::assign(IpAddr const &addr, in_port_t port)
{
  ats_ip_set(&sa, addr, port);
  return *this;
}

inline IpEndpoint &
IpEndpoint::assign(sockaddr const *ip)
{
  ats_ip_copy(&sa, ip);
  return *this;
}

inline in_port_t &
IpEndpoint::port()
{
  return ats_ip_port_cast(&sa);
}

inline in_port_t
IpEndpoint::port() const
{
  return ats_ip_port_cast(&sa);
}

inline in_port_t
IpEndpoint::host_order_port() const
{
  return ntohs(this->port());
}

inline bool
IpEndpoint::isValid() const
{
  return ats_is_ip(this);
}

inline bool
IpEndpoint::isIp4() const
{
  return AF_INET == sa.sa_family;
}
inline bool
IpEndpoint::isIp6() const
{
  return AF_INET6 == sa.sa_family;
}
inline uint16_t
IpEndpoint::family() const
{
  return sa.sa_family;
}

inline IpEndpoint &
IpEndpoint::setToAnyAddr(int family)
{
  ink_zero(*this);
  sa.sa_family = family;
  if (AF_INET == family) {
    sin.sin_addr.s_addr = INADDR_ANY;
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
    sin.sin_len = sizeof(sockaddr_in);
#endif
  } else if (AF_INET6 == family) {
    sin6.sin6_addr = in6addr_any;
#if HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN
    sin6.sin6_len = sizeof(sockaddr_in6);
#endif
  }
  return *this;
}

inline IpEndpoint &
IpEndpoint::setToLoopback(int family)
{
  ink_zero(*this);
  sa.sa_family = family;
  if (AF_INET == family) {
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
    sin.sin_len = sizeof(sockaddr_in);
#endif
  } else if (AF_INET6 == family) {
    static const struct in6_addr init = IN6ADDR_LOOPBACK_INIT;
    sin6.sin6_addr                    = init;
#if HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN
    sin6.sin6_len = sizeof(sockaddr_in6);
#endif
  }
  return *this;
}

// BufferWriter formatting support.
namespace ts
{
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, IpAddr const &addr);
BufferWriter &bwformat(BufferWriter &w, BWFSpec const &spec, sockaddr const *addr);
inline BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, IpEndpoint const &addr)
{
  return bwformat(w, spec, &addr.sa);
}
} // namespace ts
