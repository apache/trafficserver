/*
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

//////////////////////////////////////////////////////////////////////////////////////////////
// conditions_geo_maxmind.cc: Implementation of the ConditionGeo class based on MaxMindDB
//
//

#include <unistd.h>
#include <arpa/inet.h>

#include "ts/ts.h"

#include "conditions_geo.h"

#include <maxminddb.h>

MMDB_s *gMaxMindDB = nullptr;

void
MMConditionGeo::initLibrary(const std::string &path)
{
  if (path.empty()) {
    Dbg(pi_dbg_ctl, "Empty MaxMind db path specified. Not initializing!");
    return;
  }

  if (gMaxMindDB != nullptr) {
    Dbg(pi_dbg_ctl, "Maxmind library already initialized");
    return;
  }

  gMaxMindDB = new MMDB_s;

  int status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, gMaxMindDB);
  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Cannot open %s - %s", path.c_str(), MMDB_strerror(status));
    delete gMaxMindDB;
    gMaxMindDB = nullptr; // allow retry on next call instead of dangling pointer
    return;
  }
  Dbg(pi_dbg_ctl, "Loaded %s", path.c_str());
}

std::string
MMConditionGeo::get_geo_string(const sockaddr *addr) const
{
  std::string ret = "(unknown)";
  int         mmdb_error;

  if (gMaxMindDB == nullptr) {
    Dbg(pi_dbg_ctl, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(gMaxMindDB, addr, &mmdb_error);

  if (MMDB_SUCCESS != mmdb_error) {
    Dbg(pi_dbg_ctl, "Error during sockaddr lookup: %s", MMDB_strerror(mmdb_error));
    return ret;
  }

  if (!result.found_entry) {
    Dbg(pi_dbg_ctl, "No entry for this IP was found");
    return ret;
  }

  MMDB_entry_data_s entry_data;
  int               status;

  // GeoLite2/GeoIP2/DBIP databases use nested field paths, not flat names.
  // Use MMDB_get_value() directly on the entry -- no need for the more
  // expensive MMDB_get_entry_data_list() allocation.
  switch (_geo_qual) {
  case GEO_QUAL_COUNTRY:
    // "country" -> "iso_code" returns e.g. "US", "KR" (matches old GeoIP backend behavior)
    status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
    break;
  case GEO_QUAL_ASN_NAME:
    status = MMDB_get_value(&result.entry, &entry_data, "autonomous_system_organization", NULL);
    break;
  default:
    Dbg(pi_dbg_ctl, "Unsupported field %d", _geo_qual);
    return ret;
  }

  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up geo string field: %s", MMDB_strerror(status));
    return ret;
  }

  // Validate before access -- entry_data may be uninitialized if the field
  // exists but has an unexpected type in a third-party database.
  if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    ret = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  return ret;
}

int64_t
MMConditionGeo::get_geo_int(const sockaddr *addr) const
{
  int64_t ret = -1;
  int     mmdb_error;

  if (gMaxMindDB == nullptr) {
    Dbg(pi_dbg_ctl, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(gMaxMindDB, addr, &mmdb_error);

  if (MMDB_SUCCESS != mmdb_error) {
    Dbg(pi_dbg_ctl, "Error during sockaddr lookup: %s", MMDB_strerror(mmdb_error));
    return ret;
  }

  if (!result.found_entry) {
    Dbg(pi_dbg_ctl, "No entry for this IP was found");
    return ret;
  }

  MMDB_entry_data_s entry_data;
  int               status;

  switch (_geo_qual) {
  case GEO_QUAL_ASN:
    // GeoLite2-ASN / DBIP-ASN store this as a top-level uint32 field
    status = MMDB_get_value(&result.entry, &entry_data, "autonomous_system_number", NULL);
    break;
  default:
    Dbg(pi_dbg_ctl, "Unsupported field %d", _geo_qual);
    return ret;
  }

  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up geo int field: %s", MMDB_strerror(status));
    return ret;
  }

  if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UINT32) {
    ret = entry_data.uint32;
  }

  return ret;
}
