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

#include <netinet/in.h>
#include <netdb.h>
#include <ink_memory.h>
#include <sys/socket.h>
#include <ts/ink_apidefs.h>
#include <ts/TsBuffer.h>

#define INK_GETHOSTBYNAME_R_DATA_SIZE 1024
#define INK_GETHOSTBYADDR_R_DATA_SIZE 1024

#if ! TS_HAS_IN6_IS_ADDR_UNSPECIFIED
#if defined(IN6_IS_ADDR_UNSPECIFIED)
#undef IN6_IS_ADDR_UNSPECIFIED
#endif
static inline bool IN6_IS_ADDR_UNSPECIFIED(in6_addr const* addr) {
  uint32_t const* w = reinterpret_cast<uint32_t const*>(addr);
  return 0 == w[0] && 0 == w[1] && 0 == w[2] && 0 == w[3];
}
#endif

class InkInetAddr; // forward declare.

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
union ts_ip_endpoint {
  typedef ts_ip_endpoint self; ///< Self reference type.

  struct sockaddr         sa; ///< Generic address.
  struct sockaddr_in      sin; ///< IPv4
  struct sockaddr_in6     sin6; ///< IPv6

  self& assign(
    sockaddr const* ip ///< Source address, family, port.
  );
  /// Construct from an @a addr and @a port.
  self& assign(
    InkInetAddr const& addr, ///< Address and address family.
    uint16_t port = 0 ///< Port (network order).
  );

  /// Access to port in network order.
  uint16_t& port();

  /// Test if a valid IP address.
  bool isValid() const;
  /// Test for IPv4.
  bool isIp4() const;
  /// Test for IPv6.
  bool isIp6() const;

  /// Set to be any address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self& setToAnyAddr(
    int family ///< Address family.
  );
  /// Set to be loopback for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self& setToLoopback(
    int family ///< Address family.
  );
};

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

// --
/// Size in bytes of an IPv6 address.
static size_t const INK_IP6_SIZE = sizeof(in6_addr);

/// Reset an address to invalid.
/// @note Useful for marking a member as not yet set.
inline void ink_inet_invalidate(sockaddr* addr) {
  addr->sa_family = AF_UNSPEC;
}
inline void ink_inet_invalidate(sockaddr_in6* addr) {
  addr->sin6_family = AF_UNSPEC;
}
inline void ink_inet_invalidate(ts_ip_endpoint* ip) {
  ip->sa.sa_family = AF_UNSPEC;
}

/** Get a string name for an IP address family.
    @return The string name (never @c NULL).
*/
char const*
ink_inet_family_name(int family);

/// Test for IP protocol.
/// @return @c true if the address is IP, @c false otherwise.
inline bool ink_inet_is_ip(sockaddr const* addr) {
  return addr
    && (AF_INET == addr->sa_family || AF_INET6 == addr->sa_family);
}
/// @return @c true if the address is IP, @c false otherwise.
inline bool ink_inet_is_ip(ts_ip_endpoint const* addr) {
  return addr
    && (AF_INET == addr->sa.sa_family || AF_INET6 == addr->sa.sa_family);
}
/// Test for IP protocol.
/// @return @c true if the value is an IP address family, @c false otherwise.
inline bool ink_inet_is_ip(int family) {
  return AF_INET == family || AF_INET6 == family;
}
/// Test for IPv4 protocol.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool ink_inet_is_ip4(sockaddr const* addr) {
  return addr && AF_INET == addr->sa_family;
}
/// Test for IPv4 protocol.
/// @note Convenience overload.
/// @return @c true if the address is IPv4, @c false otherwise.
inline bool ink_inet_is_ip4(ts_ip_endpoint const* addr) {
  return addr && AF_INET == addr->sa.sa_family;
}
/// Test for IPv6 protocol.
/// @return @c true if the address is IPv6, @c false otherwise.
inline bool ink_inet_is_ip6(sockaddr const* addr) {
  return addr && AF_INET6 == addr->sa_family;
}
/// Test for IPv6 protocol.
/// @note Convenience overload.
/// @return @c true if the address is IPv6, @c false otherwise.
inline bool ink_inet_is_ip6(ts_ip_endpoint const* addr) {
  return addr && AF_INET6 == addr->sa.sa_family;
}

/// @return @c true if the address families are compatible.
inline bool ink_inet_are_compatible(
  sockaddr const* lhs, ///< Address to test.
  sockaddr const* rhs  ///< Address to test.
) {
  return lhs->sa_family == rhs->sa_family;
}
/// @return @c true if the address families are compatible.
inline bool ink_inet_are_compatible(
  ts_ip_endpoint const* lhs, ///< Address to test.
  ts_ip_endpoint const* rhs  ///< Address to test.
) {
  return ink_inet_are_compatible(&lhs->sa, &rhs->sa);
}
/// @return @c true if the address families are compatible.
inline bool ink_inet_are_compatible(
  int lhs, ///< Address family to test.
  sockaddr const* rhs  ///< Address to test.
) {
  return lhs == rhs->sa_family;
}
/// @return @c true if the address families are compatible.
inline bool ink_inet_are_compatible(
  sockaddr const* lhs, ///< Address to test.
  int rhs  ///< Family to test.
) {
  return lhs->sa_family == rhs;
}

// IP address casting.
// sa_cast to cast to sockaddr*.
// ss_cast to cast to sockaddr_storage*.
// ip4_cast converts to sockaddr_in (because that's effectively an IPv4 addr).
// ip6_cast converts to sockaddr_in6

inline sockaddr* ink_inet_sa_cast(sockaddr_storage* a) {
  return static_cast<sockaddr*>(static_cast<void*>(a));
}
inline sockaddr const* ink_inet_sa_cast(sockaddr_storage const* a) {
  return static_cast<sockaddr const*>(static_cast<void const*>(a));
}

inline sockaddr* ink_inet_sa_cast(sockaddr_in* a) {
  return static_cast<sockaddr*>(static_cast<void*>(a));
}
inline sockaddr_storage const* ink_inet_sa_cast(sockaddr_in const* a) {
  return static_cast<sockaddr_storage const*>(static_cast<void const*>(a));
}

inline sockaddr* ink_inet_sa_cast(sockaddr_in6* a) {
  return static_cast<sockaddr*>(static_cast<void*>(a));
}
inline sockaddr const* ink_inet_sa_cast(sockaddr_in6 const* a) {
  return static_cast<sockaddr const*>(static_cast<void const*>(a));
}

inline sockaddr_storage* ink_inet_ss_cast(sockaddr* a) {
  return static_cast<sockaddr_storage*>(static_cast<void*>(a));
}
inline sockaddr_storage const* ink_inet_ss_cast(sockaddr const* a) {
  return static_cast<sockaddr_storage const*>(static_cast<void const*>(a));
}

inline sockaddr_in* ink_inet_ip4_cast(sockaddr* a) {
  return static_cast<sockaddr_in*>(static_cast<void*>(a));
}
inline sockaddr_in const* ink_inet_ip4_cast(sockaddr const* a) {
  return static_cast<sockaddr_in const*>(static_cast<void const*>(a));
}

inline sockaddr_in& ink_inet_ip4_cast(sockaddr& a) {
  return *static_cast<sockaddr_in*>(static_cast<void*>(&a));
}
inline sockaddr_in const& ink_inet_ip4_cast(sockaddr const& a) {
  return *static_cast<sockaddr_in const*>(static_cast<void const*>(&a));
}

inline sockaddr_in* ink_inet_ip4_cast(sockaddr_in6* a) {
  return static_cast<sockaddr_in*>(static_cast<void*>(a));
}
inline sockaddr_in const* ink_inet_ip4_cast(sockaddr_in6 const* a) {
  return static_cast<sockaddr_in const*>(static_cast<void const*>(a));
}

inline sockaddr_in& ink_inet_ip4_cast(sockaddr_in6& a) {
  return *static_cast<sockaddr_in*>(static_cast<void*>(&a));
}
inline sockaddr_in const& ink_inet_ip4_cast(sockaddr_in6 const& a) {
  return *static_cast<sockaddr_in const*>(static_cast<void const*>(&a));
}

inline sockaddr_in6* ink_inet_ip6_cast(sockaddr* a) {
  return static_cast<sockaddr_in6*>(static_cast<void*>(a));
}
inline sockaddr_in6 const* ink_inet_ip6_cast(sockaddr const* a) {
  return static_cast<sockaddr_in6 const*>(static_cast<void const*>(a));
}
inline sockaddr_in6& ink_inet_ip6_cast(sockaddr& a) {
  return *static_cast<sockaddr_in6*>(static_cast<void*>(&a));
}
inline sockaddr_in6 const& ink_inet_ip6_cast(sockaddr const& a) {
  return *static_cast<sockaddr_in6 const*>(static_cast<void const*>(&a));
}

/// @return The @c sockaddr size for the family of @a addr.
inline size_t ink_inet_ip_size(
  sockaddr const* addr ///< Address object.
) {
  return AF_INET == addr->sa_family ? sizeof(sockaddr_in)
    : AF_INET6 == addr->sa_family ? sizeof(sockaddr_in6)
    : 0
    ;
}
inline size_t ink_inet_ip_size(
  ts_ip_endpoint const* addr ///< Address object.
) {
  return AF_INET == addr->sa.sa_family ? sizeof(sockaddr_in)
    : AF_INET6 == addr->sa.sa_family ? sizeof(sockaddr_in6)
    : 0
    ;
}
/// @return The size of the IP address only.
inline size_t ink_inet_addr_size(
  sockaddr const* addr ///< Address object.
) {
  return AF_INET == addr->sa_family ? sizeof(in_addr_t)
    : AF_INET6 == addr->sa_family ? sizeof(in6_addr)
    : 0
    ;
}
inline size_t ink_inet_addr_size(
  ts_ip_endpoint const* addr ///< Address object.
) {
  return AF_INET == addr->sa.sa_family ? sizeof(in_addr_t)
    : AF_INET6 == addr->sa.sa_family ? sizeof(in6_addr)
    : 0
    ;
}

/** Get a reference to the port in an address.
    @note Because this is direct access, the port value is in network order.
    @see ink_inet_get_port.
    @return A reference to the port value in an IPv4 or IPv6 address.
    @internal This is primarily for internal use but it might be handy for
    clients so it is exposed.
*/
inline uint16_t& ink_inet_port_cast(sockaddr* sa) {
  static uint16_t dummy = 0;
  return ink_inet_is_ip4(sa)
    ? ink_inet_ip4_cast(sa)->sin_port
    : ink_inet_is_ip6(sa)
      ? ink_inet_ip6_cast(sa)->sin6_port
      : (dummy = 0)
    ;
}
inline uint16_t const& ink_inet_port_cast(sockaddr const* sa) {
  return ink_inet_port_cast(const_cast<sockaddr*>(sa));
}
inline uint16_t const& ink_inet_port_cast(ts_ip_endpoint const* ip) {
  return ink_inet_port_cast(const_cast<sockaddr*>(&ip->sa));
}
inline uint16_t& ink_inet_port_cast(ts_ip_endpoint* ip) {
  return ink_inet_port_cast(&ip->sa);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t& ink_inet_ip4_addr_cast(sockaddr* addr) {
  static in_addr_t dummy = 0;
  return ink_inet_is_ip4(addr)
    ? ink_inet_ip4_cast(addr)->sin_addr.s_addr
    : (dummy = 0)
    ;
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const& ink_inet_ip4_addr_cast(sockaddr const* addr) {
  static in_addr_t dummy = 0;
  return ink_inet_is_ip4(addr)
    ? ink_inet_ip4_cast(addr)->sin_addr.s_addr
    : static_cast<in_addr_t const&>(dummy = 0)
    ;
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.
    @note Convenience overload.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t& ink_inet_ip4_addr_cast(ts_ip_endpoint* ip) {
  return ink_inet_ip4_addr_cast(&ip->sa);
}

/** Access the IPv4 address.

    If this is not an IPv4 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.
    @note Convenience overload.

    @return A reference to the IPv4 address in @a addr.
*/
inline in_addr_t const& ink_inet_ip4_addr_cast(ts_ip_endpoint const* ip) {
  return ink_inet_ip4_addr_cast(&ip->sa);
}

/** Access the IPv6 address.

    If this is not an IPv6 address a zero valued address is returned.
    @note This is direct access to the address so it will be in
    network order.

    @return A reference to the IPv6 address in @a addr.
*/
inline in6_addr& ink_inet_ip6_addr_cast(sockaddr* addr) {
  return ink_inet_ip6_cast(addr)->sin6_addr;
}
inline in6_addr const& ink_inet_ip6_addr_cast(sockaddr const* addr) {
  return ink_inet_ip6_cast(addr)->sin6_addr;
}
inline in6_addr& ink_inet_ip6_addr_cast(ts_ip_endpoint* ip) {
  return ip->sin6.sin6_addr;
}
inline in6_addr const& ink_inet_ip6_addr_cast(ts_ip_endpoint const* ip) {
  return ip->sin6.sin6_addr;
}

/** Cast an IP address to an array of @c uint32_t.
    @note The size of the array is dependent on the address type which
    must be checked independently of this function.
    @return A pointer to the address information in @a addr or @c NULL
    if @a addr is not an IP address.
*/
inline uint32_t* ink_inet_addr32_cast(sockaddr* addr) {
  uint32_t* zret = 0;
  switch(addr->sa_family) {
  case AF_INET: zret = reinterpret_cast<uint32_t*>(&ink_inet_ip4_addr_cast(addr)); break;
  case AF_INET6: zret = reinterpret_cast<uint32_t*>(&ink_inet_ip6_addr_cast(addr)); break;
  }
  return zret;
}
inline uint32_t const* ink_inet_addr32_cast(sockaddr const* addr) {
  return ink_inet_addr32_cast(const_cast<sockaddr*>(addr));
}

/** Cast an IP address to an array of @c uint8_t.
    @note The size of the array is dependent on the address type which
    must be checked independently of this function.
    @return A pointer to the address information in @a addr or @c NULL
    if @a addr is not an IP address.
    @see ink_inet_addr_size
*/
inline uint8_t* ink_inet_addr8_cast(sockaddr* addr) {
  uint8_t* zret = 0;
  switch(addr->sa_family) {
  case AF_INET: zret = reinterpret_cast<uint8_t*>(&ink_inet_ip4_addr_cast(addr)); break;
  case AF_INET6: zret = reinterpret_cast<uint8_t*>(&ink_inet_ip6_addr_cast(addr)); break;
  }
  return zret;
}
inline uint8_t const* ink_inet_addr8_cast(sockaddr const* addr) {
  return ink_inet_addr8_cast(const_cast<sockaddr*>(addr));
}
inline uint8_t* ink_inet_addr8_cast(ts_ip_endpoint* ip) {
  return ink_inet_addr8_cast(&ip->sa);
}
inline uint8_t const* ink_inet_addr8_cast(ts_ip_endpoint const* ip) {
  return ink_inet_addr8_cast(&ip->sa);
}

/// Check for loopback.
/// @return @c true if this is an IP loopback address, @c false otherwise.
inline bool ink_inet_is_loopback(sockaddr const* ip) {
  return ip
    && (
      (AF_INET == ip->sa_family && 0x7F == ink_inet_addr8_cast(ip)[0])
      ||
      (AF_INET6 == ip->sa_family && IN6_IS_ADDR_LOOPBACK(&ink_inet_ip6_addr_cast(ip)))
    );
}

/// Check for loopback.
/// @return @c true if this is an IP loopback address, @c false otherwise.
inline bool ink_inet_is_loopback(ts_ip_endpoint const* ip) {
  return ink_inet_is_loopback(&ip->sa);
}

/// Check for multicast.
/// @return @true if @a ip is multicast.
inline bool ink_inet_is_multicast(sockaddr const* ip) {
  return ip
    && (
      (AF_INET == ip->sa_family && 0xe == *ink_inet_addr8_cast(ip))
      ||
      (AF_INET6 == ip->sa_family && IN6_IS_ADDR_MULTICAST(&ink_inet_ip6_addr_cast(ip)))
    );
}
/// Check for multicast.
/// @return @true if @a ip is multicast.
inline bool ink_inet_is_multicast(ts_ip_endpoint const* ip) {
  return ink_inet_is_multicast(&ip->sa);
}

/// Check for non-routable address.
/// @return @c true if @a ip is a non-routable.
inline bool ink_inet_is_nonroutable(sockaddr const* ip) {
  bool zret = false;
  if (ink_inet_is_ip4(ip)) {
    in_addr_t a = ink_inet_ip4_addr_cast(ip);
    zret = ((a & htonl(0xFF000000)) == htonl(0x0A000000)) ||
      ((a & htonl(0xFFFF0000)) == htonl(0xC0A80000)) ||
      ((a & htonl(0xFFF00000)) == htonl(0xAC100000))
      ;
  } // should we do something for IPv6 addresses?
  return zret;
}

/// Check for non-routable address.
/// @return @c true if @a ip is a non-routable.
inline bool ink_inet_is_nonroutable(ts_ip_endpoint const* ip) {
  return ink_inet_is_nonroutable(&ip->sa);
}

/// Check for being "any" address.
/// @return @c true if @a ip is the any / unspecified address.
inline bool ink_inet_is_any(sockaddr const* ip) {
  return (ink_inet_is_ip4(ip) && INADDR_ANY == ink_inet_ip4_addr_cast(ip)) ||
    (ink_inet_is_ip6(ip) && IN6_IS_ADDR_UNSPECIFIED(&ink_inet_ip6_addr_cast(ip)))
    ;
}
  
/// @name Address operators
//@{

/** Copy the address from @a src to @a dst if it's IP.
    This attempts to do a minimal copy based on the type of @a src.
    If @a src is not an IP address type it is @b not copied and
    @a dst is marked as invalid.
    @return @c true if @a src was an IP address, @c false otherwise.
*/
inline bool ink_inet_copy(
  sockaddr* dst, ///< Destination object.
  sockaddr const* src ///< Source object.
) {
  size_t n = 0;
  if (src) {
    switch (src->sa_family) {
    case AF_INET: n = sizeof(sockaddr_in); break;
    case AF_INET6: n = sizeof(sockaddr_in6); break;
    }
  }
  if (n) {
    memcpy(dst, src, n);
#if HAVE_STRUCT_SOCKADDR_SA_LEN
    dst->sa_len = n;
#endif
  } else {
    ink_inet_invalidate(dst);
  }
  return n != 0;
}

inline bool ink_inet_copy(
  ts_ip_endpoint* dst, ///< Destination object.
  sockaddr const* src ///< Source object.
) {
  return ink_inet_copy(&dst->sa, src);
}
inline bool ink_inet_copy(
  ts_ip_endpoint* dst, ///< Destination object.
  ts_ip_endpoint const* src ///< Source object.
) {
  return ink_inet_copy(&dst->sa, &src->sa);
}
inline bool ink_inet_copy(
  sockaddr* dst,
  ts_ip_endpoint const* src
) {
  return ink_inet_copy(dst, &src->sa);
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
inline int ink_inet_cmp(
  sockaddr const* lhs, ///< Left hand operand.
  sockaddr const* rhs ///< Right hand operand.
) {
  int zret = 0;
  uint16_t rtype = rhs->sa_family;
  uint16_t ltype = lhs->sa_family;

  // We lump all non-IP addresses into a single equivalence class
  // that is less than an IP address. This includes AF_UNSPEC.
  if (AF_INET == ltype) {
    if (AF_INET == rtype) {
      in_addr_t la = ntohl(ink_inet_ip4_cast(lhs)->sin_addr.s_addr);
      in_addr_t ra = ntohl(ink_inet_ip4_cast(rhs)->sin_addr.s_addr);
      if (la < ra) zret = -1;
      else if (la > ra) zret = 1;
      else zret = 0;
    } else if (AF_INET6 == rtype) { // IPv4 < IPv6
      zret = -1;
    } else { // IP > not IP
      zret = 1;
    }
  } else if (AF_INET6 == ltype) {
    if (AF_INET6 == rtype) {
      sockaddr_in6 const* lhs_in6 = ink_inet_ip6_cast(lhs);
      zret = memcmp(
        &lhs_in6->sin6_addr,
        &ink_inet_ip6_cast(rhs)->sin6_addr,
        sizeof(lhs_in6->sin6_addr)
      );
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
    @see ink_inet_cmp(sockaddr const* lhs, sockaddr const* rhs)
*/
inline int ink_inet_cmp(ts_ip_endpoint const* lhs, ts_ip_endpoint const* rhs) {
  return ink_inet_cmp(&lhs->sa, &rhs->sa);
}

/** Check if two addresses are equal.
    @return @c true if @a lhs and @a rhs point to equal addresses,
    @c false otherwise.
*/
inline bool ink_inet_eq(sockaddr const* lhs, sockaddr const* rhs) {
  return 0 == ink_inet_cmp(lhs, rhs);
}
inline bool ink_inet_eq(ts_ip_endpoint const* lhs, ts_ip_endpoint const* rhs) {
  return 0 == ink_inet_cmp(&lhs->sa, &rhs->sa);
}

inline bool operator == (ts_ip_endpoint const& lhs, ts_ip_endpoint const& rhs) {
  return 0 == ink_inet_cmp(&lhs.sa, &rhs.sa);
}
inline bool operator != (ts_ip_endpoint const& lhs, ts_ip_endpoint const& rhs) {
  return 0 != ink_inet_cmp(&lhs.sa, &rhs.sa);
}

//@}

/// Get IP TCP/UDP port.
/// @return The port in host order for an IPv4 or IPv6 address,
/// or zero if neither.
inline uint16_t ink_inet_get_port(
  sockaddr const* addr ///< Address with port.
) {
  // We can discard the const because this function returns
  // by value.
  return ntohs(ink_inet_port_cast(const_cast<sockaddr*>(addr)));
}

/// Get IP TCP/UDP port.
/// @return The port in host order for an IPv4 or IPv6 address,
/// or zero if neither.
inline uint16_t ink_inet_get_port(
  ts_ip_endpoint const* ip ///< Address with port.
) {
  // We can discard the const because this function returns
  // by value.
  return ntohs(ink_inet_port_cast(const_cast<sockaddr*>(&ip->sa)));
}


/** Extract the IPv4 address.
    @return Host order IPv4 address.
*/
inline in_addr_t ink_inet_get_ip4_addr(
  sockaddr const* addr ///< Address object.
) {
  return ntohl(ink_inet_ip4_addr_cast(const_cast<sockaddr*>(addr)));
}

/// Write IPv4 data to storage @a dst.
inline sockaddr* ink_inet_ip4_set(
  sockaddr_in* dst, ///< Destination storage.
  in_addr_t addr, ///< address, IPv4 network order.
  uint16_t port = 0 ///< port, network order.
) {
  ink_zero(*dst);
#if HAVE_STRUCT_SOCKADDR_IN_SIN_LEN
  dst->sin_len = sizeof(sockaddr_in);
#endif
  dst->sin_family = AF_INET;
  dst->sin_addr.s_addr = addr;
  dst->sin_port = port;
  return ink_inet_sa_cast(dst);
}

/** Write IPv4 data to @a dst.
    @note Convenience overload.
*/
inline sockaddr* ink_inet_ip4_set(
  ts_ip_endpoint* dst, ///< Destination storage.
  in_addr_t ip4, ///< address, IPv4 network order.
  uint16_t port = 0 ///< port, network order.
) {
  return ink_inet_ip4_set(&dst->sin, ip4, port);
}

/** Write IPv4 data to storage @a dst.

    This is the generic overload. Caller must verify that @a dst is at
    least @c sizeof(sockaddr_in) bytes.
*/
inline sockaddr* ink_inet_ip4_set(
  sockaddr* dst, ///< Destination storage.
  in_addr_t ip4, ///< address, IPv4 network order.
  uint16_t port = 0 ///< port, network order.
) {
  return ink_inet_ip4_set(ink_inet_ip4_cast(dst), ip4, port);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr* ink_inet_ip6_set(
  sockaddr_in6* dst, ///< Destination storage.
  in6_addr const& addr, ///< address in network order.
  uint16_t port = 0 ///< Port, network order.
) {
  ink_zero(*dst);
#if HAVE_STRUCT_SOCKADDR_IN6_SIN6_LEN
  dst->sin6_len = sizeof(sockaddr_in6);
#endif
  dst->sin6_family = AF_INET6;
  memcpy(&dst->sin6_addr, &addr, sizeof addr);
  dst->sin6_port = port;
  return ink_inet_sa_cast(dst);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr* ink_inet_ip6_set(
  sockaddr* dst, ///< Destination storage.
  in6_addr const& addr, ///< address in network order.
  uint16_t port = 0 ///< Port, network order.
) {
  return ink_inet_ip6_set(ink_inet_ip6_cast(dst), addr, port);
}
/** Write IPv6 data to storage @a dst.
    @return @a dst cast to @c sockaddr*.
 */
inline sockaddr* ink_inet_ip6_set(
  ts_ip_endpoint* dst, ///< Destination storage.
  in6_addr const& addr, ///< address in network order.
  uint16_t port = 0 ///< Port, network order.
) {
  return ink_inet_ip6_set(&dst->sin6, addr, port);
}

/** Write a null terminated string for @a addr to @a dst.
    A buffer of size INET6_ADDRSTRLEN suffices, including a terminating nul.
 */
char const* ink_inet_ntop(
  const sockaddr *addr, ///< Address.
  char *dst, ///< Output buffer.
  size_t size ///< Length of buffer.
);

/** Write a null terminated string for @a addr to @a dst.
    A buffer of size INET6_ADDRSTRLEN suffices, including a terminating nul.
 */
inline char const* ink_inet_ntop(
  ts_ip_endpoint const* addr, ///< Address.
  char *dst, ///< Output buffer.
  size_t size ///< Length of buffer.
) {
  return ink_inet_ntop(&addr->sa, dst, size);
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
char const* ink_inet_nptop(
  const sockaddr *addr, ///< Address.
  char *dst, ///< Output buffer.
  size_t size ///< Length of buffer.
);

/** Write a null terminated string for @a addr to @a dst with port.
    A buffer of size INET6_ADDRPORTSTRLEN suffices, including a terminating nul.
 */
inline char const* ink_inet_nptop(
  ts_ip_endpoint const*addr, ///< Address.
  char *dst, ///< Output buffer.
  size_t size ///< Length of buffer.
) {
  return ink_inet_nptop(&addr->sa, dst, size);
}


/** Convert @a text to an IP address and write it to @a addr.

    @a text is expected to be an explicit address, not a hostname.  No
    hostname resolution is done.

    @note This uses @c getaddrinfo internally and so involves memory
    allocation.

    @return 0 on success, non-zero on failure.
*/
int ink_inet_pton(
  char const* text, ///< [in] text.
  sockaddr* addr ///< [out] address
);

/** Convert @a text to an IP address and write it to @a addr.

    @a text is expected to be an explicit address, not a hostname.  No
    hostname resolution is done.

    @note This uses @c getaddrinfo internally and so involves memory
    allocation.
    @note Convenience overload.

    @return 0 on success, non-zero on failure.
*/
inline int ink_inet_pton(
  char const* text, ///< [in] text.
  sockaddr_in6* addr ///< [out] address
) {
  return ink_inet_pton(text, ink_inet_sa_cast(addr));
}

inline int ink_inet_pton(
  char const* text, ///< [in] text.
  ts_ip_endpoint* addr ///< [out] address
) {
  return ink_inet_pton(text, &addr->sa);
}

/** Get the best address info for @a name.

    @name is passed to @c getaddrinfo which does a host lookup if @a
    name is not in IP address format. The results are examined for the
    "best" addresses. This is only significant for the host name case
    (for IP address data, there is at most one result). The preference is
    Global > Non-Routable > Multicast > Loopback.

    IPv4 and IPv4 results are handled independently and stored in @a
    ip4 and @a ip6 respectively. If @a name is known to be a numeric
    IP address @c ink_inet_pton is a better choice. Use this function
    if the type of @a name is not known. If you want to look at the
    addresses and not just get the "best", use @c getaddrinfo
    directly.

    @a ip4 or @a ip6 can be @c NULL and the result for that family is
    discarded. It is legal for both to be @c NULL in which case this
    is just a format check.

    @return 0 if an address was found, non-zero otherwise.

    @see ink_inet_pton
    @see getaddrinfo
 */

int
ink_inet_getbestaddrinfo(
  char const* name, ///< [in] Address name (IPv4, IPv6, or host name)
  ts_ip_endpoint* ip4, ///< [out] Storage for IPv4 address.
  ts_ip_endpoint* ip6 ///< [out] Storage for IPv6 address
);

/** Generic IP address hash function.
*/    
uint32_t ink_inet_hash(sockaddr const* addr);

/** Convert address to string as a hexidecimal value.
    The string is always nul terminated, the output string is clipped
    if @a dst is insufficient.
    @return The length of the resulting string (not including nul).
*/
int
ink_inet_to_hex(
  sockaddr const* addr, ///< Address to convert. Must be IP.
  char* dst, ///< Destination buffer.
  size_t len ///< Length of @a dst.
);

/** Storage for an IP address.
    In some cases we want to store just the address and not the
    ancillary information (such as port, or flow data).
    @note This is not easily used as an address for system calls.
*/
struct InkInetAddr {
  typedef InkInetAddr self; ///< Self reference type.

  /// Default construct (invalid address).
  InkInetAddr() : _family(AF_UNSPEC) {}
  /// Construct as IPv4 @a addr.
  explicit InkInetAddr(
    in_addr_t addr ///< Address to assign.
  ) : _family(AF_INET) {
    _addr._ip4 = addr;
  }
  /// Construct as IPv6 @a addr.
  explicit InkInetAddr(
    in6_addr const& addr ///< Address to assign.
  ) : _family(AF_INET6) {
    _addr._ip6 = addr;
  }
  /// Construct from @c sockaddr.
  explicit InkInetAddr(sockaddr const* addr) { this->assign(addr); }
  /// Construct from @c sockaddr_in6.
  explicit InkInetAddr(sockaddr_in6 const& addr) { this->assign(ink_inet_sa_cast(&addr)); }
  /// Construct from @c sockaddr_in6.
  explicit InkInetAddr(sockaddr_in6 const* addr) { this->assign(ink_inet_sa_cast(addr)); }
  /// Construct from @c ts_ip_endpoint.
  explicit InkInetAddr(ts_ip_endpoint const& addr) { this->assign(&addr.sa); }
  /// Construct from @c ts_ip_endpoint.
  explicit InkInetAddr(ts_ip_endpoint const* addr) { this->assign(&addr->sa); }

  /// Assign from sockaddr storage.
  self& assign(sockaddr const* addr) {
    _family = addr->sa_family;
    if (ink_inet_is_ip4(addr)) {
      _addr._ip4 = ink_inet_ip4_addr_cast(addr);
    } else if (ink_inet_is_ip6(addr)) {
      _addr._ip6 = ink_inet_ip6_addr_cast(addr);
    } else {
      _family = AF_UNSPEC;
    }
    return *this;
  }
  /// Assign from end point.
  self& operator = (ts_ip_endpoint const& ip) {
    return this->assign(&ip.sa);
  }
  /// Assign from IPv4 raw address.
  self& operator = (
    in_addr_t ip ///< Network order IPv4 address.
  );

  /** Load from string.
      The address is copied to this object if the conversion is successful,
      otherwise this object is invalidated.
      @return 0 on success, non-zero on failure.
  */
  int load(
    char const* str ///< Nul terminated input string.
  );
  /** Output to a string.
      @return The string @a dest.
  */
  char* toString(
    char* dest, ///< [out] Destination string buffer.
    size_t len ///< [in] Size of buffer.
  ) const;

  /// Equality.
  bool operator==(self const& that) {
    return _family == AF_INET
      ? (that._family == AF_INET && _addr._ip4 == that._addr._ip4)
      : _family == AF_INET6
        ? (that._family == AF_INET6
          && 0 == memcmp(&_addr._ip6, &that._addr._ip6, INK_IP6_SIZE)
          )
        : (_family = AF_UNSPEC && that._family == AF_UNSPEC)
    ;
  }

  /// Inequality.
  bool operator!=(self const& that) {
    return ! (*this == that);
  }

  /// Test for same address family.
  /// @c return @c true if @a that is the same address family as @a this.
  bool isCompatibleWith(self const& that);

  /// Get the address family.
  /// @return The address family.
  uint16_t family() const;
  /// Test for IPv4.
  bool isIp4() const;
  /// Test for IPv6.
  bool isIp6() const;

  /// Test for validity.
  bool isValid() const { return _family == AF_INET || _family == AF_INET6; }
  /// Make invalid.
  self& invalidate() { _family = AF_UNSPEC; return *this; }
  /// Test for multicast.
  /// @return @c true if this is an IP multicast address.
  bool isMulticast() const;

  uint16_t _family; ///< Protocol family.
  /// Address data.
  union {
    in_addr_t _ip4; ///< IPv4 address storage.
    in6_addr  _ip6; ///< IPv6 address storage.
    uint8_t   _byte[INK_IP6_SIZE]; ///< As raw bytes.
  } _addr;
};

inline InkInetAddr&
InkInetAddr::operator = (in_addr_t ip) {
  _family = AF_INET;
  _addr._ip4 = ip;
  return *this;
}

inline uint16_t InkInetAddr::family() const { return _family; }

inline bool
InkInetAddr::isCompatibleWith(self const& that) {
  return this->isValid() && _family == that._family;
}

inline bool InkInetAddr::isIp4() const { return AF_INET == _family; }
inline bool InkInetAddr::isIp6() const { return AF_INET6 == _family; }

// Associated operators.
bool operator == (InkInetAddr const& lhs, sockaddr const* rhs);
inline bool operator == (sockaddr const* lhs, InkInetAddr const& rhs) {
  return rhs == lhs;
}
inline bool operator != (InkInetAddr const& lhs, sockaddr const* rhs) {
  return ! (lhs == rhs);
}
inline bool operator != (sockaddr const* lhs, InkInetAddr const& rhs) {
  return ! (rhs == lhs);
}
inline bool operator == (InkInetAddr const& lhs, ts_ip_endpoint const& rhs) {
  return lhs == &rhs.sa;
}
inline bool operator == (ts_ip_endpoint const& lhs, InkInetAddr const& rhs) {
  return &lhs.sa == rhs;
}
inline bool operator != (InkInetAddr const& lhs, ts_ip_endpoint const& rhs) {
  return ! (lhs == &rhs.sa);
}
inline bool operator != (ts_ip_endpoint const& lhs, InkInetAddr const& rhs) {
  return ! (rhs == &lhs.sa);
}

/// Write IP @a addr to storage @a dst.
/// @return @s dst.
sockaddr* ink_inet_ip_set(
  sockaddr* dst, ///< Destination storage.
  InkInetAddr const& addr, ///< source address.
  uint16_t port = 0 ///< port, network order.
);

/** Convert @a text to an IP address and write it to @a addr.
    Convenience overload.
    @return 0 on success, non-zero on failure.
*/
inline int ink_inet_pton(
  char const* text, ///< [in] text.
  InkInetAddr& addr ///< [out] address
) {
  return addr.load(text) ? 0 : -1;
}

inline ts_ip_endpoint&
ts_ip_endpoint::assign(InkInetAddr const& addr, uint16_t port) {
  ink_inet_ip_set(&sa, addr, port); 
  return *this;
}

inline ts_ip_endpoint&
ts_ip_endpoint::assign(sockaddr const* ip) {
  ink_inet_copy(&sa, ip);
  return *this;
}

inline uint16_t&
ts_ip_endpoint::port() {
  return ink_inet_port_cast(&sa);
}

inline bool
ts_ip_endpoint::isValid() const {
  return ink_inet_is_ip(this);
}

inline bool ts_ip_endpoint::isIp4() const { return AF_INET == sa.sa_family; }
inline bool ts_ip_endpoint::isIp6() const { return AF_INET6 == sa.sa_family; }

inline ts_ip_endpoint&
ts_ip_endpoint::setToAnyAddr(int family) {
  sa.sa_family = family;
  if (AF_INET == family) ink_inet_ip4_addr_cast(this) = INADDR_ANY;
  else if (AF_INET6 == family) ink_inet_ip6_addr_cast(this) = in6addr_any;
  return *this;
}

inline ts_ip_endpoint&
ts_ip_endpoint::setToLoopback(int family) {
  sa.sa_family = family;
  if (AF_INET == family) ink_inet_ip4_addr_cast(this) = htonl(INADDR_LOOPBACK);
  else if (AF_INET6 == family) ink_inet_ip6_addr_cast(this) = in6addr_loopback;
  return *this;
}

#endif // _ink_inet.h
