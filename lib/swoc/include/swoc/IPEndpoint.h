// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   IPEndpoint - wrapper for raw socket address objects.
 */

#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <stdexcept>

#include "swoc/swoc_version.h"
#include "swoc/TextView.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

using ::std::string_view;

class IPAddr;
class IP4Addr;
class IP6Addr;

namespace detail {
extern void *const pseudo_nullptr;
}

/** A union to hold @c sockaddr compliant IP address structures.

    This class contains a number of static methods to perform operations on external @c sockaddr
    instances. These are all duplicates of methods that operate on the internal @c sockaddr and
    are provided primarily for backwards compatibility during the shift to using this class.

    We use the term "endpoint" because these contain more than just the raw address, all of the data
    for an IP endpoint is present.
 */
union IPEndpoint {
  using self_type = IPEndpoint; ///< Self reference type.

  struct sockaddr sa;      ///< Generic address.
  struct sockaddr_in sa4;  ///< IPv4
  struct sockaddr_in6 sa6; ///< IPv6

  /// Default construct invalid instance.
  IPEndpoint();
  IPEndpoint(self_type const &that); ///< Copy constructor.
  ~IPEndpoint() = default;

  /// Construct from the @a text representation of an address.
  explicit IPEndpoint(string_view const &text);

  // Construct from @c IPAddr
  explicit IPEndpoint(IPAddr const &addr);

  // Construct from @c sockaddr
  IPEndpoint(sockaddr const *sa);

  /// Copy assignment.
  self_type &operator=(self_type const &that);

  /** Break a string in to IP address relevant tokens.
   *
   * @param src Source text. [in]
   * @param host The host / address. [out]
   * @param port The host_order_port. [out]
   * @param rest Any text past the end of the IP address. [out]
   * @return @c true if an IP address was found, @c false otherwise.
   *
   * Any of the out parameters can be @c nullptr in which case they are not updated.
   * This parses and discards the IPv6 brackets.
   *
   * @note This is intended for internal use to do address parsing, but it can be useful in other contexts.
   */
  static bool tokenize(string_view src, string_view *host = nullptr, string_view *port = nullptr, string_view *rest = nullptr);

  /** Parse a string for an IP address.

      The address resulting from the parse is copied to this object if the conversion is successful,
      otherwise this object is invalidated.

      @return @c true on success, @c false otherwise.
  */
  bool parse(string_view const &str);

  /// Invalidate a @c sockaddr.
  static void invalidate(sockaddr *addr);

  /// Invalidate this endpoint.
  self_type &invalidate();

  /** Copy (assign) the contents of @a src to @a dst.
   *
   * The caller must ensure @a dst is large enough to hold the contents of @a src, the size of which
   * can vary depending on the type of address in @a dst.
   *
   * @param dst Destination.
   * @param src Source.
   * @return @c true if @a dst is a valid IP address, @c false otherwise.
   */
  static bool assign(sockaddr *dst, sockaddr const *src);

  /** Assign from a socket address.
      The entire address (all parts) are copied if the @a ip is valid.
  */
  self_type &assign(sockaddr const *addr);

  /// Assign from an @a addr and @a host_order_port.
  self_type &assign(IPAddr const &addr, in_port_t port = 0);

  /// Copy to @a sa.
  const self_type &copy_to(sockaddr *addr) const;

  /// Test for valid IP address.
  bool is_valid() const;

  /// Test for IPv4.
  bool is_ip4() const;

  /// Test for IPv6.
  bool is_ip6() const;

  /** Effectively size of the address.
   *
   * @return The size of the structure appropriate for the address family of the stored address.
   */
  socklen_t size() const;

  /// @return The IP address family.
  sa_family_t family() const;

  /// Set to be the ANY address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self_type &set_to_any(int family);

  /// @return @c true if this is the ANY address, @c false if not.
  bool is_any() const;

  /// Set to be loopback address for family @a family.
  /// @a family must be @c AF_INET or @c AF_INET6.
  /// @return This object.
  self_type &set_to_loopback(int family);

  /// @return @c true if this is a loopback address, @c false if not.
  bool is_loopback() const;

  /// Port in network order.
  in_port_t &network_order_port();

  /// Port in network order.
  in_port_t network_order_port() const;

  /// Port in host horder.
  in_port_t host_order_port() const;

  /// Port in network order from @a sockaddr.
  static in_port_t &port(sockaddr *sa);

  /// Port in network order from @a sockaddr.
  static in_port_t port(sockaddr const *sa);

  /// Port in host order directly from a @c sockaddr
  static in_port_t host_order_port(sockaddr const *sa);

  /// Automatic conversion to @c sockaddr.
  operator sockaddr *() { return &sa; }

  /// Automatic conversion to @c sockaddr.
  operator sockaddr const *() const { return &sa; }

  /// The string name of the address family.
  static string_view family_name(sa_family_t family);
};

inline IPEndpoint::IPEndpoint() {
  sa.sa_family = AF_UNSPEC;
}

inline IPEndpoint::IPEndpoint(IPAddr const &addr) {
  this->assign(addr);
}

inline IPEndpoint::IPEndpoint(sockaddr const *sa) {
  this->assign(sa);
}

inline IPEndpoint::IPEndpoint(IPEndpoint::self_type const &that) : self_type(&that.sa) {}

inline IPEndpoint &
IPEndpoint::invalidate() {
  sa.sa_family = AF_UNSPEC;
  return *this;
}

inline void
IPEndpoint::invalidate(sockaddr *addr) {
  addr->sa_family = AF_UNSPEC;
}

inline bool
IPEndpoint::is_valid() const {
  return sa.sa_family == AF_INET || sa.sa_family == AF_INET6;
}

inline IPEndpoint &
IPEndpoint::operator=(self_type const &that) {
  self_type::assign(&sa, &that.sa);
  return *this;
}

inline IPEndpoint &
IPEndpoint::assign(sockaddr const *src) {
  self_type::assign(&sa, src);
  return *this;
}

inline IPEndpoint const &
IPEndpoint::copy_to(sockaddr *addr) const {
  self_type::assign(addr, &sa);
  return *this;
}

inline bool
IPEndpoint::is_ip4() const {
  return AF_INET == sa.sa_family;
}

inline bool
IPEndpoint::is_ip6() const {
  return AF_INET6 == sa.sa_family;
}

inline sa_family_t
IPEndpoint::family() const {
  return sa.sa_family;
}

inline in_port_t &
IPEndpoint::network_order_port() {
  return self_type::port(&sa);
}

inline in_port_t
IPEndpoint::network_order_port() const {
  return self_type::port(&sa);
}

inline in_port_t
IPEndpoint::host_order_port() const {
  return ntohs(this->network_order_port());
}

inline in_port_t &
IPEndpoint::port(sockaddr *sa) {
  switch (sa->sa_family) {
  case AF_INET:
    return reinterpret_cast<sockaddr_in *>(sa)->sin_port;
    case AF_INET6:
      return reinterpret_cast<sockaddr_in6 *>(sa)->sin6_port;
  }
  // Force a failure upstream by returning a null reference.
  throw std::domain_error("sockaddr is not a valid IP address");
}

inline in_port_t
IPEndpoint::port(sockaddr const *sa) {
  return self_type::port(const_cast<sockaddr *>(sa));
}

inline in_port_t
IPEndpoint::host_order_port(sockaddr const *sa) {
  return ntohs(self_type::port(sa));
}

}} // namespace swoc::SWOC_VERSION_NS
