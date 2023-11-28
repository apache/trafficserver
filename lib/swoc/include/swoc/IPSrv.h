// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   IP address and network related classes.
 */

#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include "swoc/swoc_version.h"
#include "swoc/IPAddr.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

class IPSrv;

/// An IPv4 address and host_order_port, modeled on an SRV type for DNS.
class IP4Srv {
private:
  using self_type = IP4Srv;

public:
  constexpr IP4Srv() = default; ///< Default constructor.

  /** Construct from address and host_order_port.
   *
   * @param addr The address.
   * @param port The port in host order, defaults to 0.
   */
  explicit IP4Srv(IP4Addr addr, in_port_t port = 0);

  /** Construct from generic.
   *
   * @param that The generic SRV.
   *
   * If @a that is not IPv4 the result is a default constructed instance.
   */
  explicit IP4Srv(IPSrv const &that);

  /** Construct from socket address.
   *
   * @param sa Socket address.
   */
  IP4Srv(sockaddr_in const *s) : _addr(s), _port(ntohs(s->sin_port)) {}

  /** Construct from a string.
   *
   * @param text Input text.
   *
   * If the port is not present it is set to zero.
   */
  explicit IP4Srv(swoc::TextView text);

  /// Implicit conversion to an address.
  constexpr operator IP4Addr const &() const;

  /// @return The address.
  constexpr IP4Addr const &addr() const;

  /// @return The host_order_port in host order.
  in_port_t host_order_port() const;

  /// @return The host_order_port in network order.
  in_port_t network_order_port() const;

  /// The protocol family.
  /// @return @c AF_INET
  /// @note Useful primarily for template classes.
  static constexpr sa_family_t family();

  bool operator==(self_type that) const;
  bool operator!=(self_type that) const;

  bool operator<(self_type that) const;
  bool operator<=(self_type const &that) const;
  bool operator>(self_type const &that) const;
  bool operator>=(self_type const &that) const;

  /** Load from a string.
   *
   * @param text Input string.
   * @return @c true if @a text in a valid format, @c false if not.
   */
  bool load(swoc::TextView text);

  /** Change the address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &assign(IP4Addr const &addr);

  /** Change the host_order_port.
   *
   * @param port Port to assign.
   * @return @a this.
   */
  self_type &assign(in_port_t port);

  /** Change the address and port.
   *
   * @param addr Address to assign.
   * @param port Port to assign.
   * @return @a this
   */
  self_type &assign(IP4Addr const &addr, in_port_t port);

  /** Change the address and port.
   *
   * @param s A socket address.
   * @return @a this
   */
  self_type &assign(sockaddr_in const *s);

protected:
  IP4Addr _addr;       ///< Address.
  in_port_t _port = 0; ///< Port.
};

/// An IPv6 address and host_order_port, modeled on an SRV type for DNS.
class IP6Srv {
private:
  using self_type = IP6Srv;

public:
  IP6Srv() = default; ///< Default constructor.

  /** Construct from address and host_order_port.
   *
   * @param addr The address.
   * @param port The port in host order, defaults to 0.
   */
  explicit IP6Srv(IP6Addr addr, in_port_t port = 0);

  /** Construct from generic.
   *
   * @param that The generic SRV.
   *
   * If @a that is not IPv6 the result is a default constructed instance.
   */
  explicit IP6Srv(IPSrv const &that);

  /** Construct from a socket address.
   *
   * @param s Socket address.
   */
  explicit IP6Srv(sockaddr_in6 const *s);

  /** Construct from a string.
   *
   * @param text Input text.
   *
   * If the port is not present it is set to zero.
   */
  explicit IP6Srv(swoc::TextView text);

  /** Load from a string.
   *
   * @param text Input string.
   * @return @c true if @a text in a valid format, @c false if not.
   */
  bool load(swoc::TextView text);

  /// Implicit conversion to address.
  constexpr operator IP6Addr const &() const;

  /// @return The address.
  constexpr IP6Addr const &addr() const;
  /// @return The port in host order.
  in_port_t host_order_port() const;
  /// @return The port in network order.
  in_port_t network_order_port() const;

  /// The protocol family.
  /// @return @c AF_INET6
  /// @note Useful primarily for template classes.
  static constexpr sa_family_t family();

  bool operator==(self_type that) const;
  bool operator!=(self_type that) const;

  bool operator<(self_type that) const;
  bool operator<=(self_type const &that) const;
  bool operator>(self_type const &that) const;
  bool operator>=(self_type const &that) const;

  /** Change the address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &assign(IP6Addr const &addr);

  /** Change the host_order_port.
   *
   * @param port Port to assign.
   * @return @a this.
   */
  self_type &assign(in_port_t port);

  /** Change the address and host_order_port.
   *
   * @param addr Address to assign.
   * @param port Port to assign.
   * @return @a this
   */
  self_type &assign(IP6Addr const &addr, in_port_t port);

  /** Change the address and port.
   *
   * @param s A socket address.
   * @return @a this
   */
  self_type &assign(sockaddr_in6 const *s);

protected:
  IP6Addr _addr;       ///< Address.
  in_port_t _port = 0; ///< Port.
};

/// An IP address and host_order_port, modeled on an SRV type for DNS.
class IPSrv {
private:
  using self_type = IPSrv;

public:
  IPSrv() = default; ///< Default constructor.
  explicit IPSrv(IP4Addr addr, in_port_t port = 0) : _srv(IP4Srv{addr, port}), _family(addr.family()) {}
  explicit IPSrv(IP6Addr addr, in_port_t port = 0) : _srv(IP6Srv{addr, port}), _family(addr.family()) {}
  explicit IPSrv(IPAddr addr, in_port_t port = 0);
  explicit IPSrv(sockaddr const *sa);
  explicit IPSrv(sockaddr_in const *s);
  explicit IPSrv(sockaddr_in6 const *s);
  explicit IPSrv(IPEndpoint const &ep);

  /** Construct from a string.
   *
   * @param text Input text.
   *
   * If the port is not present it is set to zero.
   */
  explicit IPSrv(swoc::TextView text);

  /** Load from a string.
   *
   * @param text Input string.
   * @return @c true if @a text in a valid format, @c false if not.
   */
  bool load(swoc::TextView text);

  /// @return The address.
  IPAddr addr() const;
  /// @return The host_order_port in host order..
  constexpr in_port_t host_order_port() const;
  /// @return The host_order_port in network order.
  in_port_t network_order_port() const;
  /// @return The protocol of the current value.
  constexpr sa_family_t family() const;

  /// @return @c true if the data is IPv4, @c false if not.
  bool is_ip4() const;
  /// @return @c true if hte data is IPv6, @c false if not.
  bool is_ip6() const;

  /// @return The IPv4 data.
  IP4Srv const &
  ip4() const {
    return _srv._ip4;
  }
  /// @return The IPv6 data.
  IP6Srv const &
  ip6() const {
    return _srv._ip6;
  }

  /** Change the address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &assign(IP4Addr const &addr);

  /** Change the address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &assign(IP6Addr const &addr);

  /** Change the address.
   *
   * @param addr Address to assign.
   * @return @a this
   *
   * If @a addr isn't valid then no assignment is made.
   */
  self_type &assign(IPAddr const &addr);

  /** Change the host_order_port.
   *
   * @param port Port in host order.
   * @return @a this.
   */
  self_type &assign(in_port_t port);

  /** Change the address and port.
   *
   * @param addr Address to assign.
   * @param port Port to assign.
   * @return @a this
   */
  self_type &assign(IP4Addr const &addr, in_port_t port);

  /** Change the address and port.
   *
   * @param addr Address to assign.
   * @param port Port to assign.
   * @return @a this
   */
  self_type &assign(IP6Addr const &addr, in_port_t port);

  /** Change the address and port.
   *
   * @param sa Socket address.
   * @return @a this
   */
  self_type &assign(sockaddr const *sa);

  /** Change the address and port.
   *
   * @param s Socket address.
   * @return @a this
   */
  self_type &assign(sockaddr_in const *s);

  /** Change the address and port.
   *
   * @param s Socket address.
   * @return @a this
   */
  self_type &assign(sockaddr_in6 const *s);

  /** Change the address and host_order_port.
   *
   * @param addr Address to assign.
   * @param port Port to assign.
   * @return @a this
   *
   * If @a addr isn't valid then no assignment is made.
   */
  self_type &assign(IPAddr const &addr, in_port_t port);

  self_type &operator=(self_type const &that) = default;
  self_type &operator=(IP4Srv const &that);
  self_type &operator=(IP6Srv const &that);
  self_type &
  operator=(sockaddr const *sa) {
    return this->assign(sa);
  }
  self_type &
  operator=(sockaddr_in const *s) {
    return this->assign(s);
  }
  self_type &
  operator=(sockaddr_in6 const *s) {
    return this->assign(s);
  }

protected:
  /// Family specialized data.
  union data {
    IP4Srv _ip4; ///< IPv4 address (host)
    IP6Srv _ip6; ///< IPv6 address (host)

    data(){}; // enable default construction.

    /// Construct from IPv4 data.
    explicit data(IP4Srv const &srv) : _ip4(srv) {}
    explicit data(sockaddr_in const *s) : _ip4(s) {}

    /// Construct from IPv6 data.
    explicit data(IP6Srv const &srv) : _ip6(srv) {}
    explicit data(sockaddr_in6 const *s) : _ip6(s) {}

    /// @return A generic address.
    IPAddr addr(sa_family_t f) const;

    /// @return The port in host order.
    constexpr in_port_t port(sa_family_t f) const;
  } _srv;

  sa_family_t _family = AF_UNSPEC; ///< Protocol family.
};

// --- Implementation

inline IP4Srv::IP4Srv(IP4Addr addr, in_port_t port) : _addr(addr), _port(port) {}
inline IP4Srv::IP4Srv(IPSrv const &that) : IP4Srv(that.is_ip4() ? that.ip4() : self_type{}) {}

inline auto
IP4Srv::assign(IP4Addr const &addr) -> self_type & {
  _addr = addr;
  return *this;
}
inline auto
IP4Srv::assign(in_port_t port) -> self_type & {
  _port = port;
  return *this;
}
inline auto
IP4Srv::assign(IP4Addr const &addr, in_port_t port) -> self_type & {
  _addr = addr;
  _port = port;
  return *this;
}
inline auto
IP4Srv::assign(sockaddr_in const *s) -> self_type & {
  _addr = s;
  _port = ntohs(s->sin_port);
  return *this;
}
inline constexpr IP4Srv::operator IP4Addr const &() const {
  return _addr;
}
inline constexpr IP4Addr const &
IP4Srv::addr() const {
  return _addr;
}
inline in_port_t
IP4Srv::host_order_port() const {
  return _port;
}
inline in_port_t
IP4Srv::network_order_port() const {
  return htons(_port);
}
inline constexpr sa_family_t
IP4Srv::family() {
  return AF_INET;
}

inline bool
IP4Srv::operator==(IP4Srv::self_type that) const {
  return _addr == that._addr && _port == that._port;
}
inline bool
IP4Srv::operator!=(IP4Srv::self_type that) const {
  return _addr != that._addr || _port != that._port;
}
inline bool
IP4Srv::operator<(IP4Srv::self_type that) const {
  return _addr < that._addr || (_addr == that._addr && _port < that._port);
}
inline bool
IP4Srv::operator<=(IP4Srv::self_type const &that) const {
  return _addr < that._addr || (_addr == that._addr && _port <= that._port);
}
inline bool
IP4Srv::operator>(IP4Srv::self_type const &that) const {
  return that < *this;
}
inline bool
IP4Srv::operator>=(IP4Srv::self_type const &that) const {
  return that <= *this;
}

/// --- IPv6

inline IP6Srv::IP6Srv(IP6Addr addr, in_port_t port) : _addr(addr), _port(port) {}
inline IP6Srv::IP6Srv(IPSrv const &that) : IP6Srv(that.is_ip6() ? that.ip6() : self_type{}) {}
inline IP6Srv::IP6Srv(const sockaddr_in6 *s) : _addr(s->sin6_addr), _port(ntohs(s->sin6_port)) {}

inline constexpr IP6Srv::operator IP6Addr const &() const {
  return _addr;
}
inline constexpr IP6Addr const &
IP6Srv::addr() const {
  return _addr;
}
inline in_port_t
IP6Srv::host_order_port() const {
  return _port;
}
inline in_port_t
IP6Srv::network_order_port() const {
  return htons(_port);
}
inline constexpr sa_family_t
IP6Srv::family() {
  return AF_INET6;
}

inline bool
IP6Srv::operator==(IP6Srv::self_type that) const {
  return _addr == that._addr && _port == that._port;
}
inline bool
IP6Srv::operator!=(IP6Srv::self_type that) const {
  return _port != that._port || _addr != that._addr;
}
inline bool
IP6Srv::operator<(IP6Srv::self_type that) const {
  return _addr < that._addr || (_addr == that._addr && _port < that._port);
}
inline bool
IP6Srv::operator<=(IP6Srv::self_type const &that) const {
  return _addr < that._addr || (_addr == that._addr && _port <= that._port);
}
inline bool
IP6Srv::operator>(IP6Srv::self_type const &that) const {
  return that < *this;
}
inline bool
IP6Srv::operator>=(IP6Srv::self_type const &that) const {
  return that <= *this;
}

inline auto
IP6Srv::assign(in_port_t port) -> self_type & {
  _port = port;
  return *this;
}

inline auto
IP6Srv::assign(IP6Addr const &addr) -> self_type & {
  _addr = addr;
  return *this;
}

inline auto
IP6Srv::assign(IP6Addr const &addr, in_port_t port) -> self_type & {
  _addr = addr;
  _port = port;
  return *this;
}

inline auto
IP6Srv::assign(sockaddr_in6 const *s) -> self_type & {
  _addr = s;
  _port = ntohs(s->sin6_port);
  return *this;
}

// --- Generic SRV

inline IPSrv::IPSrv(const sockaddr_in *s) : _srv(s), _family(AF_INET) {}
inline IPSrv::IPSrv(const sockaddr_in6 *s) : _srv(s), _family(AF_INET6) {}
inline IPSrv::IPSrv(const sockaddr *sa) {
  this->assign(sa);
}

inline IPAddr
IPSrv::addr() const {
  return _srv.addr(_family);
}
inline constexpr sa_family_t
IPSrv::family() const {
  return _family;
}
inline bool
IPSrv::is_ip4() const {
  return _family == AF_INET;
}
inline bool
IPSrv::is_ip6() const {
  return _family == AF_INET6;
}

inline auto
IPSrv::assign(IP6Addr const &addr) -> self_type & {
  _srv._ip6.assign(addr, this->host_order_port());
  _family = addr.family();
  return *this;
}

inline auto
IPSrv::assign(IP4Addr const &addr, in_port_t port) -> self_type & {
  _srv._ip4.assign(addr, port);
  _family = addr.family();
  return *this;
}

inline auto
IPSrv::assign(IP6Addr const &addr, in_port_t port) -> self_type & {
  _srv._ip6.assign(addr, port);
  _family = addr.family();
  return *this;
}

inline auto
IPSrv::assign(IP4Addr const &addr) -> self_type & {
  _srv._ip4.assign(addr, this->host_order_port());
  _family = addr.family();
  return *this;
}

inline auto
IPSrv::assign(in_port_t port) -> self_type & {
  if (this->is_ip4()) {
    _srv._ip4.assign(port);
  } else if (this->is_ip6()) {
    _srv._ip6.assign(port);
  }
  return *this;
}

inline auto
IPSrv::assign(IPAddr const &addr) -> self_type & {
  if (addr.is_ip4()) {
    this->assign(addr.ip4());
  } else if (addr.is_ip6()) {
    this->assign(addr.ip6());
  }
  return *this;
}

inline auto
IPSrv::assign(IPAddr const &addr, in_port_t port) -> self_type & {
  if (addr.is_ip4()) {
    this->assign(addr.ip4(), port);
    _family = addr.family();
  } else if (addr.is_ip6()) {
    this->assign(addr.ip6(), port);
    _family = addr.family();
  }
  return *this;
}

inline auto
IPSrv::operator=(IP4Srv const &that) -> self_type & {
  _family   = that.family();
  _srv._ip4 = that;
  return *this;
}

inline auto
IPSrv::operator=(IP6Srv const &that) -> self_type & {
  _family   = that.family();
  _srv._ip6 = that;
  return *this;
}

inline auto
IPSrv::assign(sockaddr_in const *s) -> self_type & {
  _family = _srv._ip4.family();
  _srv._ip4.assign(s);
  return *this;
}

inline auto
IPSrv::assign(sockaddr_in6 const *s) -> self_type & {
  _family = _srv._ip6.family();
  _srv._ip6.assign(s);
  return *this;
}

inline constexpr in_port_t
IPSrv::host_order_port() const {
  return _srv.port(_family);
}

inline in_port_t
IPSrv::network_order_port() const {
  return ntohs(_srv.port(_family));
}

inline IPAddr
IPSrv::data::addr(sa_family_t f) const {
  return (f == AF_INET) ? _ip4.addr() : (f == AF_INET6) ? _ip6.addr() : IPAddr::INVALID;
}

constexpr inline in_port_t
IPSrv::data::port(sa_family_t f) const {
  return (f == AF_INET) ? _ip4.host_order_port() : (f == AF_INET6) ? _ip6.host_order_port() : 0;
}
// --- Independent comparisons.

inline bool
operator==(IPSrv const &lhs, IP4Srv const &rhs) {
  return lhs.is_ip4() && lhs.ip4() == rhs;
}

inline bool
operator==(IP4Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && rhs.ip4() == lhs;
}

inline bool
operator!=(IPSrv const &lhs, IP4Srv const &rhs) {
  return !lhs.is_ip4() || lhs.ip4() != rhs;
}

inline bool
operator!=(IP4Srv const &lhs, IPSrv const &rhs) {
  return !rhs.is_ip4() || rhs.ip4() != lhs;
}

inline bool
operator<(IPSrv const &lhs, IP4Srv const &rhs) {
  return lhs.is_ip4() && lhs.ip4() < rhs;
}

inline bool
operator<(IP4Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && lhs < rhs.ip4();
}

inline bool
operator<=(IPSrv const &lhs, IP4Srv const &rhs) {
  return lhs.is_ip4() && lhs.ip4() <= rhs;
}

inline bool
operator<=(IP4Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && lhs <= rhs.ip4();
}

inline bool
operator>(IPSrv const &lhs, IP4Srv const &rhs) {
  return lhs.is_ip4() && lhs.ip4() > rhs;
}

inline bool
operator>(IP4Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && lhs > rhs.ip4();
}

inline bool
operator>=(IPSrv const &lhs, IP4Srv const &rhs) {
  return lhs.is_ip4() && lhs.ip4() >= rhs;
}

inline bool
operator>=(IP4Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && lhs >= rhs.ip4();
}

inline bool
operator==(IPSrv const &lhs, IP6Srv const &rhs) {
  return lhs.is_ip6() && lhs.ip6() == rhs;
}

inline bool
operator==(IP6Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && rhs.ip6() == lhs;
}

inline bool
operator!=(IPSrv const &lhs, IP6Srv const &rhs) {
  return !lhs.is_ip6() || lhs.ip6() != rhs;
}

inline bool
operator!=(IP6Srv const &lhs, IPSrv const &rhs) {
  return !rhs.is_ip6() || rhs.ip6() != lhs;
}

inline bool
operator<(IPSrv const &lhs, IP6Srv const &rhs) {
  return lhs.is_ip6() && lhs.ip6() < rhs;
}

inline bool
operator<(IP6Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && lhs < rhs.ip6();
}

inline bool
operator<=(IPSrv const &lhs, IP6Srv const &rhs) {
  return lhs.is_ip6() && lhs.ip6() <= rhs;
}

inline bool
operator<=(IP6Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && lhs <= rhs.ip6();
}

inline bool
operator>(IPSrv const &lhs, IP6Srv const &rhs) {
  return lhs.is_ip6() && lhs.ip6() > rhs;
}

inline bool
operator>(IP6Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && lhs > rhs.ip6();
}

inline bool
operator>=(IPSrv const &lhs, IP6Srv const &rhs) {
  return lhs.is_ip6() && lhs.ip6() >= rhs;
}

inline bool
operator>=(IP6Srv const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && lhs >= rhs.ip6();
}

// --- Cross address equality

inline bool
operator==(IPSrv const &lhs, IP4Addr const &rhs) {
  return lhs.is_ip4() && lhs.ip4() == rhs;
}

inline bool
operator==(IP4Addr const &lhs, IPSrv const &rhs) {
  return rhs.is_ip4() && lhs == rhs.ip4();
}

inline bool
operator!=(IPSrv const &lhs, IP4Addr const &rhs) {
  return !lhs.is_ip4() || lhs.ip4() != rhs;
}

inline bool
operator!=(IP4Addr const &lhs, IPSrv const &rhs) {
  return !rhs.is_ip4() || lhs != rhs.ip4();
}

inline bool
operator==(IPSrv const &lhs, IP6Addr const &rhs) {
  return lhs.is_ip6() && lhs.ip6() == rhs;
}

inline bool
operator==(IP6Addr const &lhs, IPSrv const &rhs) {
  return rhs.is_ip6() && lhs == rhs.ip6();
}

inline bool
operator!=(IPSrv const &lhs, IP6Addr const &rhs) {
  return !lhs.is_ip6() || lhs.ip6() != rhs;
}

inline bool
operator!=(IP6Addr const &lhs, IPSrv const &rhs) {
  return !rhs.is_ip6() || lhs != rhs.ip6();
}

}} // namespace swoc::SWOC_VERSION_NS
