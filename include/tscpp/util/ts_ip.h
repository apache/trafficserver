/** @file

  IP address handling support.

  Built on top of libswoc IP networking support to provide utilities specialized for ATS.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
*/

#pragma once

#include <limits>
#include <optional>

#include "swoc/DiscreteRange.h"
#include "swoc/swoc_ip.h"
#include "swoc/bwf_ip.h"

namespace ts
{
inline constexpr in_port_t MAX_PORT_VALUE{std::numeric_limits<in_port_t>::max()};
using port_range_t = swoc::DiscreteRange<in_port_t>;

/// Pair of addresses, each optional.
/// Used in situations where both an IPv4 and IPv6 may be needed.
class IPAddrPair
{
public:
  using self_type = IPAddrPair;

  IPAddrPair() = default; ///< Default construct empty pair.

  /** Construct with IPv4 address.
   *
   * @param a4 Address.
   */
  IPAddrPair(swoc::IP4Addr const &a4);

  /** Construct with IPv6 address.
   *
   * @param a6 Address.
   */
  IPAddrPair(swoc::IP6Addr const &a6);

  /** Construct from two addresses.
   *
   * @param addr4 IPv4 address.
   * @param addr6 IPv6 address.
   */
  IPAddrPair(swoc::IP4Addr const &addr4, swoc::IP6Addr const &addr6);

  /// @return @c true if either address is present.
  bool has_value() const;

  /// @return @c true if an IPv4 address is present.
  bool has_ip4() const;

  /// @return @c true if an IPv6 address is present.
  bool has_ip6() const;

  /// @return The IPv4 address
  /// @note Does not check if the address is present.
  swoc::IP4Addr const &ip4() const;

  /// @return The IPv6 address
  /// @note Does not check if the address is present.
  swoc::IP6Addr const &ip6() const;

  /** Assign the IPv4 address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &operator=(swoc::IP4Addr const &addr);

  /** Assign the IPv6 address.
   *
   * @param addr Address to assign.
   * @return @a this
   */
  self_type &operator=(swoc::IP6Addr const &addr);

  /** Assign an address.
   *
   * @param addr Address to assign.
   * @return @a this
   *
   * The appropriate internal address is assigned based on the address in @a addr.
   */
  self_type &operator=(swoc::IPAddr const &addr);

  /** Additive / union.
   *
   * @param that Source value.
   * @return @a this
   *
   * Missing values in @a that are not copied, the original value remains.
   */
  self_type &operator+=(self_type const &that);

protected:
  std::optional<swoc::IP4Addr> _ip4;
  std::optional<swoc::IP6Addr> _ip6;
};

inline IPAddrPair::IPAddrPair(swoc::IP4Addr const &a4) : _ip4(a4) {}
inline IPAddrPair::IPAddrPair(swoc::IP6Addr const &a6) : _ip6(a6) {}
inline IPAddrPair::IPAddrPair(const swoc::IP4Addr &addr4, const swoc::IP6Addr &addr6) : _ip4(addr4), _ip6(addr6) {}

inline bool
IPAddrPair::has_value() const
{
  return _ip4.has_value() || _ip6.has_value();
}

inline bool
IPAddrPair::has_ip4() const
{
  return _ip4.has_value();
}

inline bool
IPAddrPair::has_ip6() const
{
  return _ip6.has_value();
}

inline swoc::IP4Addr const &
IPAddrPair::ip4() const
{
  return _ip4.value();
}

inline swoc::IP6Addr const &
IPAddrPair::ip6() const
{
  return _ip6.value();
}

inline auto
IPAddrPair::operator=(swoc::IP4Addr const &addr) -> self_type &
{
  _ip4 = addr;
  return *this;
}

inline auto
IPAddrPair::operator=(swoc::IP6Addr const &addr) -> self_type &
{
  _ip6 = addr;
  return *this;
}

inline auto
IPAddrPair::operator=(swoc::IPAddr const &addr) -> self_type &
{
  if (addr.is_ip4()) {
    _ip4 = addr.ip4();
  } else if (addr.is_ip6()) {
    _ip6 = addr.ip6();
  }

  return *this;
}

/// Pair of services, each optional.
/// Used in situations where both IPv4 and IPv6 may be needed.
class IPSrvPair
{
  using self_type = IPSrvPair;

public:
  IPSrvPair() = default; ///< Default construct empty pair.

  /** Construct from address(es) and port.
   *
   * @param a4 IPv4 address.
   * @param a6 IPv6 address.
   * @param port Port
   *
   * @a port is used for both service instances.
   */
  IPSrvPair(swoc::IP4Addr const &a4, swoc::IP6Addr const &a6, in_port_t port = 0);

  /** Construct from IPv4 address and optional port.
   *
   * @param a4 IPv4 address
   * @param port Port.
   */
  IPSrvPair(swoc::IP4Addr const &a4, in_port_t port = 0);

  /** Construct from IPv6 address and optional port.
   *
   * @param a6 IPv6 address
   * @param port Port.
   */
  IPSrvPair(swoc::IP6Addr const &a6, in_port_t port = 0);

  /** Construct from an address pair and optional port.
   *
   * @param a Address pair.
   * @param port port.
   *
   * For each family the service is instantatied only if the address is present in @a a.
   * @a port is used for all service instances.
   */
  explicit IPSrvPair(IPAddrPair const &a, in_port_t port = 0);

  /// @return @c true if any service is present.
  bool has_value() const;

  /// @return @c true if the IPv4 service is present.
  bool has_ip4() const;

  /// @return @c true if the the IPv6 service is present.
  bool has_ip6() const;

  /// @return The IPv4 service.
  /// @note Does not check if the service is present.
  swoc::IP4Srv const &ip4() const;

  /// @return The IPv6 service.
  /// @note Does not check if the service is present.
  swoc::IP6Srv const &ip6() const;

  /** Assign the IPv4 service.
   *
   * @param srv Service to assign.
   * @return @a this
   */
  self_type &operator=(swoc::IP4Srv const &srv);

  /** Assign the IPv6 service.
   *
   * @param srv Service to assign.
   * @return @a this
   */
  self_type &operator=(swoc::IP6Srv const &srv);

  /** Assign a service.
   *
   * @param srv Service to assign.
   * @return @a this
   *
   * The assigned service is the same family as @a srv.
   */
  self_type &operator=(swoc::IPSrv const &srv);

protected:
  std::optional<swoc::IP4Srv> _ip4;
  std::optional<swoc::IP6Srv> _ip6;
};

inline IPSrvPair::IPSrvPair(swoc::IP4Addr const &a4, swoc::IP6Addr const &a6, in_port_t port)
  : _ip4(swoc::IP4Srv(a4, port)), _ip6(swoc::IP6Srv(a6, port))
{
}

inline IPSrvPair::IPSrvPair(swoc::IP4Addr const &a4, in_port_t port) : _ip4(swoc::IP4Srv(a4, port)) {}

inline IPSrvPair::IPSrvPair(swoc::IP6Addr const &a6, in_port_t port) : _ip6(swoc::IP6Srv(a6, port)) {}

inline IPSrvPair::IPSrvPair(IPAddrPair const &a, in_port_t port)
{
  if (a.has_ip4()) {
    _ip4 = swoc::IP4Srv(a.ip4(), port);
  }

  if (a.has_ip6()) {
    _ip6 = swoc::IP6Srv(a.ip6(), port);
  }
}

inline bool
IPSrvPair::has_value() const
{
  return _ip4.has_value() || _ip6.has_value();
}

inline bool
IPSrvPair::has_ip4() const
{
  return _ip4.has_value();
}

inline bool
IPSrvPair::has_ip6() const
{
  return _ip6.has_value();
}

inline swoc::IP4Srv const &
IPSrvPair::ip4() const
{
  return _ip4.value();
}

inline swoc::IP6Srv const &
IPSrvPair::ip6() const
{
  return _ip6.value();
}

inline auto
IPSrvPair::operator=(swoc::IP4Srv const &srv) -> self_type &
{
  _ip4 = srv;
  return *this;
}

inline auto
IPSrvPair::operator=(swoc::IP6Srv const &srv) -> self_type &
{
  _ip6 = srv;
  return *this;
}

inline auto
IPSrvPair::operator=(swoc::IPSrv const &srv) -> self_type &
{
  if (srv.is_ip4()) {
    _ip4 = srv.ip4();
  } else if (srv.is_ip6()) {
    _ip6 = srv.ip6();
  }
  return *this;
}

/** Get the best address info for @a name.

 * @param name Address / host.
 * @return An address pair.
 *
 * If @a name is a valid IP address it is interpreted as such. Otherwise it is presumed
 * to be a host name suitable for resolution using @c getaddrinfo. The "best" address is
 * selected by ranking the types of addresses in the order
 *
 * - Global, multi-cast, non-routable (private), link local, loopback
 *
 * For a host name, an IPv4 and IPv6 address may be returned. The "best" is computed independently
 * for each family.
 *
 * @see getaddrinfo
 * @see ts::getbestsrvinfo
 */
IPAddrPair getbestaddrinfo(swoc::TextView name);

/** Get the best address and port info for @a name.

 * @param name Address / host.
 * @return An address pair.
 *
 * If @a name is a valid IP address (with optional port) it is interpreted as such. Otherwise it is
 * presumed to be a host name (with optional port) suitable for resolution using @c getaddrinfo. The "best" address is
 * selected by ranking the types of addresses in the order
 *
 * - Global, multi-cast, non-routable (private), link local, loopback
 *
 * For a host name, an IPv4 and IPv6 service may be returned. The "best" is computed independently
 * for each family. The port, if present, is the same for all returned services.
 *
 * @see getaddrinfo
 * @see ts::getbestaddrinfo
 */
IPSrvPair getbestsrvinfo(swoc::TextView name);

} // namespace ts
