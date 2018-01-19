#pragma once
#include <ts/ts.h>
#include "PartitionedMap.h"
#include "../PropertyBlock.h"

/**
 * @file
 *
 * @breif this file defines structures to store data about each host and ip.
 *
 * HostAddr - stores a IpEndpoint, expire time, and up status. Additional properties can be defined at system start.
 *
 * HostName - stores a FQDN and a list of HostAddr. Additional properties can be defined at system start.
 *  All references to HostNames will be shared pointers. They are only deleted when all references are deleted.
 *
 * Look ups are performed with one global mutex lock to protect integrity of the map.
 * @see HostLookupByName, HostLookupByAddr
 *
 * When you are reading/writing a HostNamePtr, ensure that you find the assigned mutex from the LockPool. This ensures a single
 * reader/writer to the HostName, associated HostAddrs and property blocks.
 * @see HostName::getMutex()
 *
 */

std_hasher_macro(IdEndpoint, ip, ip.hash());

namespace NextHop
{
/// uid for a host that should persist between updates and reloads.
/// It is prefered for other systems to cache HostID instead of HostNamePtr.
using HostID = size_t;

struct HostName;
using HostNamePtr = std::shared_ptr<HostName>;

/// Defines a host address, status, and a block of associated properties.
/** Only variables need by TSCore are in this structure.
 * Plugins should define property blocks.
 * @see PropBlockDeclare
 */
struct HostAddr : public PropertyBlock<HostAddr> {
  const IpEndpoint m_addr;      ///< ip and port
  std::atomic_uint m_eol;       ///< "end of life" time, set by DNS time to live
  std::atomic_bool m_available; ///< true when this IP is available

  HostAddr(const IpEndpoint &ip_addr, HostNamePtr host) : m_addr(ip_addr) { HostLookupByAddr.put(ip_addr, host); }

  // Note, this uses PropertyBlock::new and delete to manage.

private:
  HostAddr() = 0;
};

//////////////////////////////////////////////

/// Defines a host (name), group of IPs, and block of associated properties.
/** Only variables need by TSCore are in the structure.
 * Plugins should use property blocks for associated data.
 * @see PropBlockDeclare
 */
class HostName : public PropertyBlock<HostNamePtr>
{
public:
  // to enforce all references be shared_ptr
  static HostNamePtr
  alloc(string_view hostname)
  {
    HostNamePtr existing_host = HostLookupByName.get(hostname);
    if (existing_host) {
      return existing_host;
    }

    HostNamePtr host = std::make_shared<HostName>(hostname); ///< call PropertyBlock::operator new

    PropBlockInit(host);
    HostLookupByName.put(hostname, host);
    return host;
  }

  void
  free()
  {
    TSAssert(canAccess(this));
    propBlockDestroy();
    HostLookupByName.put(hostname, {}); // put empty
    getMutex()->unlock();
    m_lock_idx = -1;
  }

  static bool
  canAccess(const HostName *host)
  {
    return host->getMutex().hasLock();
  }
  static bool
  hasLock(const ts::string_view *hostname)
  {
    return s_lock_pool.getMutex(std::hash<ts::string_view>()(hostname)).hasLock();
  }

  /// ----------------------------
  /// Thread Safe Methods
  ///

  /// thread management
  Mutex *
  getMutex() const
  {
    return s_lock_pool.getMutex(m_lock_idx);
  }

  /// get the FQDN that defines this host.
  const ts::string_view
  getName() const
  {
    return ts::string_view(m_name);
  }

  /// ----------------------------
  /// Requires Lock Methods
  ///

  /// Address editing and processing
  HostAddr *addAddr(IpEndpoint &addr);
  HostAddr *getAddr(IpEndpoint &addr);
  const std::vector<HostAddr *> &getAddrList(IpEndpoint &addr);

  /// remove all data associated with this host
  void
  reset()
  {
    TSAssert(hasLock(this));
    propBlockDestroy();
    m_addrs.clear();
    propBlockInit();
  }

private:
  HostName() = 0 // call HostName::alloc() instead

    HostName(string_view name)
    : m_name(name.data(), name.size()),
  m_lock_idx(std::hash<string_view>()(name))
  {
  }
  ~HostName() {}

  // member variables

  const std::string m_name;           ///< the FQDN that defines this host
  const LockPool::Index_t m_lock_idx; ///< reference to lock.
  std::vector<HostAddr *> m_addrs;    ///< hosts can have multiple IPs.

  /// a shared lock pool for all HostNames
  static LockPool s_lock_pool;
};

}; // namespace NextHop
