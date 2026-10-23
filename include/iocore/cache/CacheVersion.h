/** @file

  Cache format versions shared by the server and offline inspection tools.

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

#include <cstdint>
#include "tscore/Version.h"

// Bumping the minor version does not clear anyone's cache: stripe validation looks only at the
// major version, and this build still reads every object written at an older minor version. What
// it does mean is that an ATS older than this treats the objects this build writes as corrupt and
// refetches them, so bump it whenever an object gains a shape an older ATS would misread.
//
// 24.2 marshalled the fragment offset table in full; see CACHE_DB_FRAG_OFFSET_TABLE_VERSION below.
// 24.3 added a WKS identity trailer to marshalled alternates. Missing or mismatched identities
// rebuild indexes from the stored strings in HTTPHdrImpl::recompute_wks_indices().
// That is what frees the well-known string table in proxy/hdrs/HdrToken.cc to change: any ATS at
// 24.3 or newer reads objects written against any table, and anything older refuses them outright
// rather than resolving their indexes against the wrong table.
static const uint8_t CACHE_DB_MAJOR_VERSION = 24;
static const uint8_t CACHE_DB_MINOR_VERSION = 3;
// This is used in various comparisons because otherwise if the minor version is 0,
// the compile fails because the condition is always true or false. Running it through
// VersionNumber prevents that.
inline constexpr ts::VersionNumber CACHE_DB_VERSION{CACHE_DB_MAJOR_VERSION, CACHE_DB_MINOR_VERSION};

// The first version whose objects carry a complete fragment offset table. Before it, an object
// with more than HTTPCacheAlt::N_INTEGRAL_FRAG_OFFSETS fragments marshalled only the offsets past
// the integral ones, and the reader rebuilt the combined table; see HTTPInfo::unmarshal_v24_1().
// Objects older than this need that reader, and objects from this version on need
// HTTPInfo::unmarshal(). This is a fixed point in the format's history, not the current version:
// comparing against CACHE_DB_VERSION instead would send every current object through the old
// reader the moment the cache version is bumped for any other reason.
static const uint8_t               CACHE_DB_FRAG_OFFSET_TABLE_MAJOR_VERSION = 24;
static const uint8_t               CACHE_DB_FRAG_OFFSET_TABLE_MINOR_VERSION = 2;
inline constexpr ts::VersionNumber CACHE_DB_FRAG_OFFSET_TABLE_VERSION{CACHE_DB_FRAG_OFFSET_TABLE_MAJOR_VERSION,
                                                                      CACHE_DB_FRAG_OFFSET_TABLE_MINOR_VERSION};
static_assert(CACHE_DB_VERSION >= CACHE_DB_FRAG_OFFSET_TABLE_VERSION,
              "CACHE_DB_VERSION must not be older than the HTTPInfo layout boundary");
