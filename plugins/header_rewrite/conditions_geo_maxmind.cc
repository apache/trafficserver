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
#include <map>
#include <mutex>
#include <string>

#include "ts/ts.h"

#include "conditions_geo.h"

#include <maxminddb.h>

enum class MmdbSchema { NESTED, FLAT };

struct MmdbHandle {
  MMDB_s     db;
  MmdbSchema schema = MmdbSchema::NESTED;
};

static std::map<std::string, MmdbHandle *> gMmdbCache;
static std::mutex                          gMmdbCacheMutex;

// Detect whether the MMDB uses nested (GeoLite2) or flat (vendor) field layout
// by probing for the nested country path on a lookup result.
static MmdbSchema
detect_schema(MMDB_entry_s *entry)
{
  MMDB_entry_data_s probe;
  int               status = MMDB_get_value(entry, &probe, "country", "iso_code", NULL);

  if (MMDB_SUCCESS == status && probe.has_data && probe.type == MMDB_DATA_TYPE_UTF8_STRING) {
    return MmdbSchema::NESTED;
  }

  status = MMDB_get_value(entry, &probe, "country_code", NULL);
  if (MMDB_SUCCESS == status && probe.has_data && probe.type == MMDB_DATA_TYPE_UTF8_STRING) {
    return MmdbSchema::FLAT;
  }

  return MmdbSchema::NESTED;
}

static const char *probe_ips[] = {"8.8.8.8", "1.1.1.1", "128.0.0.1"};

void *
MMConditionGeo::initLibrary(const std::string &path)
{
  if (path.empty()) {
    Dbg(pi_dbg_ctl, "Empty MaxMind db path specified. Not initializing!");
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(gMmdbCacheMutex);

  auto it = gMmdbCache.find(path);
  if (it != gMmdbCache.end()) {
    Dbg(pi_dbg_ctl, "Maxmind library already initialized for %s", path.c_str());
    return it->second;
  }

  auto *handle = new MmdbHandle;
  int   status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, &handle->db);

  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Cannot open %s - %s", path.c_str(), MMDB_strerror(status));
    delete handle;
    return nullptr;
  }

  // Probe the database schema at load time so we know which field paths to
  // use for country lookups.  Try a few well-known IPs until one hits.
  for (auto *ip : probe_ips) {
    int                  gai_error, mmdb_error;
    MMDB_lookup_result_s result = MMDB_lookup_string(&handle->db, ip, &gai_error, &mmdb_error);
    if (gai_error == 0 && MMDB_SUCCESS == mmdb_error && result.found_entry) {
      handle->schema = detect_schema(&result.entry);
      Dbg(pi_dbg_ctl, "Loaded %s (schema: %s)", path.c_str(), handle->schema == MmdbSchema::FLAT ? "flat" : "nested");
      gMmdbCache[path] = handle;
      return handle;
    }
  }

  Dbg(pi_dbg_ctl, "Loaded %s (schema: defaulting to nested, no probe IPs matched)", path.c_str());
  gMmdbCache[path] = handle;
  return handle;
}

std::string
MMConditionGeo::get_geo_string(const sockaddr *addr, void *geo_handle) const
{
  std::string ret = "(unknown)";
  int         mmdb_error;

  auto *handle = static_cast<MmdbHandle *>(geo_handle);

  if (handle == nullptr) {
    Dbg(pi_dbg_ctl, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(&handle->db, addr, &mmdb_error);

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
  case GEO_QUAL_COUNTRY:
    if (handle->schema == MmdbSchema::FLAT) {
      status = MMDB_get_value(&result.entry, &entry_data, "country_code", NULL);
    } else {
      status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
    }
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

  if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    ret = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  return ret;
}

int64_t
MMConditionGeo::get_geo_int(const sockaddr *addr, void *geo_handle) const
{
  int64_t ret = -1;
  int     mmdb_error;

  auto *handle = static_cast<MmdbHandle *>(geo_handle);

  if (handle == nullptr) {
    Dbg(pi_dbg_ctl, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(&handle->db, addr, &mmdb_error);

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
