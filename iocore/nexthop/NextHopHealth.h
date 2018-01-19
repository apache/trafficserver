#pragma once
#include "NextHopHost.h"

/// ----------------------------------
/// Health Check plugin API
/// TODO: move to another file

/// Define the types of health checks, or reasons to be down.
enum HealthCheck_t : uint8_t {
  HC_TRAFFIC_CTL, ///< marked down by traffic control or admin
  HC_PASSIVE,     ///< marked down by passive HC code, aka failed to respond
  HC_ACTIVE,      ///< marked down by active health checks, aka failed probes
  HC_NUM_TYPES    ///< total count of HC for loops and such.
};

/// Type to define a set of health checks to perform.
using HealthCheckSet_t = std::bitset<HC_NUM_TYPES>;

/// returns the set of health checks performed for @host.
HealthCheckSet_t TSNextHopHealthCheckGet(const NextHop::HostNamePtr host);

/// Change if @hc_type is performed for @host.
/// returns 0 or error code
int TSNextHopHealthCheckPut(NextHop::HostNamePtr host, HealthCheck_t hc_type, bool check_enabled);

/// Type to define which health checks failed.
using HostReasonDown_t = std::bitset<HC_NUM_TYPES>;

/// Updates the stored result of a type of health check.
/// returns error code
int TSNextHopHostDownPut(NextHop::HostNamePtr, HealthCheck_t hc_type, bool mark_down);

/// returns true if the host is marked down for any reason
bool TSNextHopHostIsDown(const NextHop::HostNamePtr);

/// returns true if the host is marked down for any reason
const IpEndpoint *TSNextHopHostGetIP(const NextHop::HostNamePtr);

/// returns a bitset of reasons why it failed.
HostReasonDown_t TSNextHopHostDownGetReason(const NextHop::HostNamePtr);

/// returns true if host is self
bool TSNextHopISSelf(NextHop::HostNamePtr);

// For C API, just replace NextHop::HostNamePtr with HostID.