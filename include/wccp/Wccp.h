/** @file
    WCCP (v2) support API.

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

#include "tscore/TsBuffer.h"
#include <tsconfig/Errata.h>
#include <memory.h>
#include "tscore/ink_defs.h"
#include "tscore/ink_memory.h"
// Nasty, defining this with no prefix. The value is still available
// in TS_VERSION_STRING.
#undef VERSION

// INADDR_ANY
#include <netinet/in.h>

/// WCCP Support.
namespace wccp
{
/// Forward declare implementation classes.
class Impl;
class CacheImpl;
class RouterImpl;

/// Namespace for implementation details.
namespace detail
{
  /// Cache implementation details.
  namespace cache
  {
    struct GroupData;
  }
  namespace router
  {
    struct GroupData;
  }
} // namespace detail

/// Basic time unit for WCCP in seconds
/// @note Sec 4.14: HERE_I_AM_T.
static time_t const TIME_UNIT   = 10;
static time_t const ASSIGN_WAIT = (3 * TIME_UNIT) / 2;
static time_t const RAPID_TIME  = TIME_UNIT / 10;

/// Service group related constants.
/// @internal In a struct so enum values can be imported to more than
/// one class.
struct ServiceConstants {
  /// Methods for forwarding intercepted packets to cache.
  /// @internal Enumerations values match protocol values.
  enum PacketStyle {
    NO_PACKET_STYLE = 0, ///< Undefined or invalid.
    GRE             = 1, ///< GRE tunnel only. [default]
    L2              = 2, ///< L2 rewrite only.
    GRE_OR_L2       = 3  ///< L2 rewrite or GRE tunnel.
  };

  /// Cache assignment supported methods.
  /// @internal Enumerations values match protocol values.
  enum CacheAssignmentStyle {
    NO_CACHE_ASSIGN_STYLE = 0, ///< Undefined or invalid.
    HASH_ONLY             = 1, ///< Use only hash assignment. [default]
    MASK_ONLY             = 2, ///< Use only mask assignment.
    HASH_OR_MASK          = 3  ///< Use hash or mask assignment.
  };
};

/** Service group definition.

    Also used as serialized layout internally by ServiceGroupElt. This is kept
    in serialized form because it is copied to and from message data far more
    often then it is accessed directly.
 */
class ServiceGroup : public ServiceConstants
{
public:
  typedef ServiceGroup self; ///< Self reference type.

  /// Type of service.
  enum Type : uint8_t {
    STANDARD = 0, ///< Well known service.
    DYNAMIC  = 1  ///< Dynamic (locally defined) service.
  };

  /// Result codes for service definition.
  enum Result {
    DEFINED  = 0, ///< Service group was created by the call.
    EXISTS   = 1, ///< Service group already existed.
    CONFLICT = 2  ///< Service group existed but didn't match new definition.
  };

  /// @name Well known (standard) services.
  //@{
  /// HTTP
  static uint8_t const HTTP = 0;
  //@}
  /// Service IDs of this value or less are reserved.
  static uint8_t const RESERVED = 50;

  /// Number of ports in component (defined by protocol).
  static size_t const N_PORTS = 8;

  /// @name Flag mask values.
  //@{
  /// Source IP address hash
  static uint32_t const SRC_IP_HASH = 1 << 0;
  /// Destination IP address hash
  static uint32_t const DST_IP_HASH = 1 << 1;
  /// Source port hash.
  static uint32_t const SRC_PORT_HASH = 1 << 2;
  /// Destination port hash
  static uint32_t const DST_PORT_HASH = 1 << 3;
  /// @a m_ports has port information.
  static uint32_t const PORTS_DEFINED = 1 << 4;
  /// @a m_ports has source ports (otherwise destination ports).
  static uint32_t const PORTS_SOURCE = 1 << 5;
  /// Alternate source IP address hash
  static uint32_t const SRC_IP_ALT_HASH = 1 << 8;
  /// Alternate destination IP address hash
  static uint32_t const DST_IP_ALT_HASH = 1 << 9;
  /// Alternate source port hash
  static uint32_t const SRC_PORT_ALT_HASH = 1 << 10;
  /// Alternate destination port hash
  static uint32_t const DST_PORT_ALT_HASH = 1 << 11;
  /// All hash related flags.
  static uint32_t const HASH_FLAGS = SRC_IP_HASH | DST_IP_HASH | SRC_PORT_HASH | DST_PORT_HASH | SRC_IP_ALT_HASH | DST_IP_ALT_HASH |
                                     SRC_PORT_ALT_HASH | DST_PORT_ALT_HASH;
  //@}

  /// Default constructor - no member initialization.
  ServiceGroup();
  /// Test for equivalent.
  bool operator==(self const &that) const;
  /// Test for not equivalent.
  bool operator!=(self const &that) const;

  /// @name Accessors
  //@{
  ServiceGroup::Type getSvcType() const; ///< Get service type field.
                                         /** Set the service type.
                                             If @a svc is @c SERVICE_STANDARD then all fields except the
                                             component header and service id are set to zero as required
                                             by the protocol.
                                         */
  self &setSvcType(ServiceGroup::Type svc);

  uint8_t getSvcId() const;   ///< Get service ID field.
  self &setSvcId(uint8_t id); ///< Set service ID field to @a id.

  uint8_t getPriority() const;    ///< Get priority field.
  self &setPriority(uint8_t pri); ///< Set priority field to @a p.

  uint8_t getProtocol() const;  ///< Get protocol field.
  self &setProtocol(uint8_t p); ///< Set protocol field to @a p.

  uint32_t getFlags() const;  ///< Get flags field.
  self &setFlags(uint32_t f); ///< Set the flags flags in field to @a f.
  /// Set the flags in the flag field that are set in @a f.
  /// Other flags are unchanged.
  self &enableFlags(uint32_t f);
  /// Clear the flags in the flag field that are set in @a f.
  /// Other flags are unchanged.
  self &disableFlags(uint32_t f);

  /// Get a port value.
  uint16_t getPort(int idx ///< Index of target port.
                   ) const;
  /// Set a port value.
  self &setPort(int idx,      ///< Index of port.
                uint16_t port ///< Value for port.
  );
  /// Zero (clear) all ports.
  self &clearPorts();
  //@}

protected:
  Type m_svc_type;           ///< @ref Type.
  uint8_t m_svc_id;          ///< ID for service type.
  uint8_t m_priority;        ///< Redirection priority ordering.
  uint8_t m_protocol;        ///< IP protocol for service.
  uint32_t m_flags;          ///< Flags.
  uint16_t m_ports[N_PORTS]; ///< Service ports.
};

/// Security component option (sub-type)
enum SecurityOption {
  SECURITY_NONE = 0, ///< No security @c WCCP2_NO_SECURITY
  SECURITY_MD5  = 1  ///< MD5 security @c WCCP2_MD5_SECURITY
};

class EndPoint
{
public:
  typedef EndPoint self; ///< Self reference type.
  typedef Impl ImplType; ///< Implementation type.

  /** Set the identifying IP address.
      This is also used as the address for the socket.
  */
  self &setAddr(uint32_t addr ///< IP address.
  );

  /** Check if this endpoint is ready to use.
      @return @c true if the address has been set and services
      have been added.
  */
  bool isConfigured() const;

  /** Open a socket for communications.

      If @a addr is @c INADDR_ANY then the identifying address is used.
      If that is not set this method will attempt to find an arbitrary
      local address and use that as the identifying address.

      Otherwise @a addr replaces any previously set address.

      @return 0 on success, -ERRNO on failure.
      @see setAddr
  */
  int open(uint32_t addr = INADDR_ANY ///< Local IP address for socket.
  );

  /// Get the internal socket.
  /// Useful primarily for socket options and using
  /// in @c select.
  int getSocket() const;

  /// Use MD5 based security with @a key.
  void useMD5Security(char const *key ///< Shared hash key.
  );
  /// Use MD5 based security with @a key.
  void useMD5Security(ts::ConstBuffer const &key ///< Shared hash key.
  );

  /// Perform house keeping, including sending outbound messages.
  int housekeeping();

  /// Recieve and process a message on the socket.
  /// @return 0 for success, -ERRNO on system error.
  ts::Rv<int> handleMessage();

protected:
  /// Default constructor.
  EndPoint();
  /// Copy constructor.
  EndPoint(self const &that);
  /// Force virtual destructor
  virtual ~EndPoint();

  ts::IntrusivePtr<ImplType> m_ptr; ///< Implementation instance.

  /** Get a pointer to the implementation instance, creating it if needed.
      @internal This is paired with @c make so that the implementation check
      can be done non-virtually inline, while still allowing the actual
      implementation instantiation to be virtual so the correct type is
      created.
   */
  ImplType *instance();

  virtual ImplType *make() = 0; ///< Create a new implementation instance.
};

class Cache : public EndPoint
{
public:
  typedef Cache self;         ///< Self reference type.
  typedef EndPoint super;     ///< Parent type.
  typedef CacheImpl ImplType; ///< Implementation type.

  class Service;

  /// Default constructor.
  Cache();
  /// Destructor
  ~Cache() override;

  /// Define services from a configuration file.
  ts::Errata loadServicesFromFile(char const *path ///< Path to file.
  );

  /** Define a service group.

      Return a service reference object which references the group.

      If @a result is not @c NULL then its target is set to
      - @c ServiceGroup::DEFINED if the service was created.
      - @c ServiceGroup::EXISTS if the service matches the existing service.
      - @c ServiceGroup::CONFLICT if the service doesn't match the existing service.
   */
  Service defineServiceGroup(ServiceGroup const &svc, ///< Service group description.
                             ServiceGroup::Result *result = nullptr);

  /** Add a seed router to the service group.

      A seed router is one that is defined at start up and is where
      initial messages will be sent. Other routers will be added as
      discovered. The protocol cannot start successfully without at
      least one seed router.

      Seed routers are removed when a reply is received from that router.

  */
  self &addSeedRouter(uint8_t id,   ///< Service group ID.
                      uint32_t addr ///< IP address of router.
  );

  /// Number of seconds until next housekeeping activity is due.
  time_t waitTime() const;

protected:
  /// Get implementation instance, creating if needed.
  ImplType *instance();
  /// Get the current implementation instance cast to correct type.
  ImplType *impl();
  /// Get the current implementation instance cast to correct type.
  ImplType const *impl() const;
  /// Create a new implementation instance.
  super::ImplType *make() override;
};

/** Hold a reference to a service group in this end point.
    This is useful when multiple operations are to be done on the
    same group, rather than doing a lookup by id every time.
*/
class Cache::Service : public ServiceConstants
{
public:
  typedef Service self; ///< Self reference type.

  /// Default constructor (invalid reference).
  Service();

  /// Add an address for a seed router.
  self &addSeedRouter(uint32_t addr ///< Router IP address.
  );
  /// Set the security key.
  self &setKey(char const *key /// Shared key.
  );
  /// Set the service local security option.
  self &setSecurity(SecurityOption opt ///< Security style to use.
  );
  /// Set intercepted packet forwarding style.
  self &setForwarding(PacketStyle style ///< Type of forwarding supported.
  );
  /// Enable or disable packet return by layer 2 rewrite.
  self &setReturn(PacketStyle style ///< Type of return supported.
  );

  /// Set cache assignment style.
  self &setCacheAssignment(CacheAssignmentStyle style ///< Style to use.
  );

private:
  Service(Cache const &cache, detail::cache::GroupData &group);
  Cache m_cache;                     ///< Parent cache.
  detail::cache::GroupData *m_group; ///< Service Group data.
  friend class Cache;
};

class Router : public EndPoint
{
public:
  typedef Router self;         ///< Self reference type.
  typedef EndPoint super;      ///< Parent type.
  typedef RouterImpl ImplType; ///< Implementation type.

  /// Default constructor
  Router();
  /// Destructor.
  ~Router() override;

  /// Transmit pending messages.
  int sendPendingMessages();

protected:
  /// Get implementation instance, creating if needed.
  ImplType *instance();
  /// Get the current implementation instance cast to correct type.
  ImplType *impl();
  /// Create a new implementation instance.
  super::ImplType *make() override;
};

// ------------------------------------------------------
inline bool
ServiceGroup::operator!=(self const &that) const
{
  return !(*this == that);
}

inline ServiceGroup::Type
ServiceGroup::getSvcType() const
{
  return static_cast<ServiceGroup::Type>(m_svc_type);
}
inline uint8_t
ServiceGroup::getSvcId() const
{
  return m_svc_id;
}

inline ServiceGroup &
ServiceGroup::setSvcId(uint8_t id)
{
  m_svc_id = id;
  return *this;
}

inline uint8_t
ServiceGroup::getPriority() const
{
  return m_priority;
}

inline ServiceGroup &
ServiceGroup::setPriority(uint8_t pri)
{
  m_priority = pri;
  return *this;
}

inline uint8_t
ServiceGroup::getProtocol() const
{
  return m_protocol;
}

inline ServiceGroup &
ServiceGroup::setProtocol(uint8_t proto)
{
  m_protocol = proto;
  return *this;
}

inline uint32_t
ServiceGroup::getFlags() const
{
  return ntohl(m_flags);
}

inline ServiceGroup &
ServiceGroup::setFlags(uint32_t flags)
{
  m_flags = htonl(flags);
  return *this;
}

inline ServiceGroup &
ServiceGroup::enableFlags(uint32_t flags)
{
  m_flags |= htonl(flags);
  return *this;
}

inline ServiceGroup &
ServiceGroup::disableFlags(uint32_t flags)
{
  m_flags &= ~htonl(flags);
  return *this;
}

inline uint16_t
ServiceGroup::getPort(int idx) const
{
  return ntohs(m_ports[idx]);
}

inline ServiceGroup &
ServiceGroup::setPort(int idx, uint16_t port)
{
  m_ports[idx] = htons(port);
  return *this;
}

inline ServiceGroup &
ServiceGroup::clearPorts()
{
  memset(m_ports, 0, sizeof(m_ports));
  return *this;
}

inline Cache::Service::Service() : m_group(nullptr) {}

inline Cache::Service::Service(Cache const &cache, detail::cache::GroupData &group) : m_cache(cache), m_group(&group) {}

inline void
EndPoint::useMD5Security(char const *key)
{
  this->useMD5Security(ts::ConstBuffer(key, strlen(key)));
}
// ------------------------------------------------------

} // namespace wccp
