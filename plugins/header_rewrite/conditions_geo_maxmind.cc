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
    gMaxMindDB = nullptr;
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

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (!result.found_entry) {
    Dbg(pi_dbg_ctl, "No entry for this IP was found");
    return ret;
  }

  int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up entry data: %s", MMDB_strerror(status));
    return ret;
  }

  if (entry_data_list == nullptr) {
    Dbg(pi_dbg_ctl, "No data found");
    return ret;
  }

  MMDB_entry_data_s entry_data;

  switch (_geo_qual) {
  case GEO_QUAL_COUNTRY:
    status = MMDB_get_value(&result.entry, &entry_data, "country", "names", "en", NULL);
    break;
  case GEO_QUAL_COUNTRY_ISO:
    status = MMDB_get_value(&result.entry, &entry_data, "country", "iso_code", NULL);
    break;
  case GEO_QUAL_ASN_NAME:
    status = MMDB_get_value(&result.entry, &entry_data, "autonomous_system_organization", NULL);
    break;
  default:
    Dbg(pi_dbg_ctl, "Unsupported field %d", _geo_qual);
    MMDB_free_entry_data_list(entry_data_list);
    return ret;
  }

  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up geo string field: %s", MMDB_strerror(status));
    MMDB_free_entry_data_list(entry_data_list);
    return ret;
  }

  if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    ret = std::string(entry_data.utf8_string, entry_data.data_size);
  }

  MMDB_free_entry_data_list(entry_data_list);

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

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (!result.found_entry) {
    Dbg(pi_dbg_ctl, "No entry for this IP was found");
    return ret;
  }

  int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up entry data: %s", MMDB_strerror(status));
    return ret;
  }

  if (entry_data_list == nullptr) {
    Dbg(pi_dbg_ctl, "No data found");
    return ret;
  }

  MMDB_entry_data_s entry_data;

  switch (_geo_qual) {
  case GEO_QUAL_ASN:
    status = MMDB_get_value(&result.entry, &entry_data, "autonomous_system_number", NULL);
    break;
  default:
    Dbg(pi_dbg_ctl, "Unsupported field %d", _geo_qual);
    MMDB_free_entry_data_list(entry_data_list);
    return ret;
  }

  if (MMDB_SUCCESS != status) {
    Dbg(pi_dbg_ctl, "Error looking up geo int field: %s", MMDB_strerror(status));
    MMDB_free_entry_data_list(entry_data_list);
    return ret;
  }

  if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UINT32) {
    ret = entry_data.uint32;
  }

  MMDB_free_entry_data_list(entry_data_list);

  return ret;
}
