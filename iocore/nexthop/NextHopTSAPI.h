#pragma once
#include "NextHopHostLookup.h"

/// ----------------------------------------
/// TS C++ API
///

/// returns existing or newly allocated host with name @host_name
NextHop::HostNamePtr TSNextHopHostAlloc(const &ts::string_view host_name);

/// returns host with name @host_name
NextHop::HostNamePtr TSNextHopHostGet(const &ts::string_view host_name);

/// ----------------------------------------
/// TS C API
///

/// returns existing or newly allocated host with name @host_name
HostID TSNextHopHostAlloc_c(const &ts::string_view host_name);

/// returns host with name @host_name
HostID TSNextHopHostGet_c(const &ts::string_view host_name);

/// associate an address with a host
void TSNextHopHostAddrAdd_c(HostID host_id, const sockaddr *addr);

///
const sockaddr *TSNextHopHostAddrGet_c(HostID host_id, size_t &length);
