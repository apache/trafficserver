#pragma once
#include "NextHopHost.h"
#include "PartitionedMap.h"

/**
 * These containers allow fast lookup of host data.
 * It does not support iteration. Maintain your own container of HostNamePtr to iterate.
 * Use HostName::PropBlockDeclare(..., init, destroy) if you need to catch when host are allocated by other systems.
 */

namespace NextHop
{
/// lookup HostID, direct string hash
HostID getHostID(ts::string_view hostname);

/// lookup HostID with map
HostID getHostID(IpEndpoint addr);

/// lookup HostNamePtr
/** uses map */
HostNamePtr getHost(HostID);
HostNamePtr getHost(IpEndpoint addr);
HostNamePtr getHost(ts::string_view hostname);

/// our internal lookup maps
// TODO move to .cc
LookupMap<HostID, HostNamePtr> HostLookupByNameHash;
LookupMap<IpEndpoint, HostNamePtr> HostLookupByAddr;

}; // namespace NextHop
