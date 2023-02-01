// SPDX-License-Identifier: Apache-2.0
// Copyright Network Geographics 2014
/** @file
   IP address and network related classes.
 */

#pragma once
#include <array>
#include <climits>
#include <netinet/in.h>
#include <sys/socket.h>
#include <string_view>
#include <variant>

#include "swoc/swoc_version.h"
#include "swoc/TextView.h"

#include "swoc/IPEndpoint.h"
#include "swoc/IPAddr.h"
#include "swoc/IPSrv.h"
#include "swoc/IPRange.h"

namespace swoc { inline namespace SWOC_VERSION_NS {

// --- Cross type address operators

inline bool
operator==(IPAddr const &lhs, IPEndpoint const &rhs) {
  return lhs == &rhs.sa;
}

/// Equality.
inline bool
operator==(IPEndpoint const &lhs, IPAddr const &rhs) {
  return &lhs.sa == rhs;
}

/// Inequality.
inline bool
operator!=(IPAddr const &lhs, IPEndpoint const &rhs) {
  return !(lhs == &rhs.sa);
}

/// Inequality.
inline bool
operator!=(IPEndpoint const &lhs, IPAddr const &rhs) {
  return !(rhs == &lhs.sa);
}

}} // namespace swoc::SWOC_VERSION_NS
