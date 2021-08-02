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
    TSDebug(PLUGIN_NAME, "Empty MaxMind db path specified. Not initializing!");
    return;
  }

  if (gMaxMindDB != nullptr) {
    TSDebug(PLUGIN_NAME, "Maxmind library already initialized");
    return;
  }

  gMaxMindDB = new MMDB_s;

  int status = MMDB_open(path.c_str(), MMDB_MODE_MMAP, gMaxMindDB);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "Cannot open %s - %s", path.c_str(), MMDB_strerror(status));
    delete gMaxMindDB;
    return;
  }
  TSDebug(PLUGIN_NAME, "Loaded %s", path.c_str());
}

std::string
MMConditionGeo::get_geo_string(const sockaddr *addr) const
{
  std::string ret = "(unknown)";
  int mmdb_error;

  if (gMaxMindDB == nullptr) {
    TSDebug(PLUGIN_NAME, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(gMaxMindDB, addr, &mmdb_error);

  if (MMDB_SUCCESS != mmdb_error) {
    TSDebug(PLUGIN_NAME, "Error during sockaddr lookup: %s", MMDB_strerror(mmdb_error));
    return ret;
  }

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (!result.found_entry) {
    TSDebug(PLUGIN_NAME, "No entry for this IP was found");
    return ret;
  }

  int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "Error looking up entry data: %s", MMDB_strerror(status));
    return ret;
  }

  if (entry_data_list == nullptr) {
    TSDebug(PLUGIN_NAME, "No data found");
    return ret;
  }

  const char *field_name;
  switch (_geo_qual) {
  case GEO_QUAL_COUNTRY:
    field_name = "country_code";
    break;
  case GEO_QUAL_ASN_NAME:
    field_name = "autonomous_system_organization";
    break;
  default:
    TSDebug(PLUGIN_NAME, "Unsupported field %d", _geo_qual);
    return ret;
    break;
  }

  MMDB_entry_data_s entry_data;

  status = MMDB_get_value(&result.entry, &entry_data, field_name, NULL);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "ERROR on get value asn value: %s", MMDB_strerror(status));
    return ret;
  }
  ret = std::string(entry_data.utf8_string, entry_data.data_size);

  if (nullptr != entry_data_list) {
    MMDB_free_entry_data_list(entry_data_list);
  }

  return ret;
}

int64_t
MMConditionGeo::get_geo_int(const sockaddr *addr) const
{
  int64_t ret = -1;
  int mmdb_error;

  if (gMaxMindDB == nullptr) {
    TSDebug(PLUGIN_NAME, "MaxMind not initialized; using default value");
    return ret;
  }

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(gMaxMindDB, addr, &mmdb_error);

  if (MMDB_SUCCESS != mmdb_error) {
    TSDebug(PLUGIN_NAME, "Error during sockaddr lookup: %s", MMDB_strerror(mmdb_error));
    return ret;
  }

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (!result.found_entry) {
    TSDebug(PLUGIN_NAME, "No entry for this IP was found");
    return ret;
  }

  int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "Error looking up entry data: %s", MMDB_strerror(status));
    return ret;
  }

  if (entry_data_list == nullptr) {
    TSDebug(PLUGIN_NAME, "No data found");
    return ret;
  }

  const char *field_name;
  switch (_geo_qual) {
  case GEO_QUAL_ASN:
    field_name = "autonomous_system_number";
    break;
  default:
    TSDebug(PLUGIN_NAME, "Unsupported field %d", _geo_qual);
    return ret;
    break;
  }

  MMDB_entry_data_s entry_data;

  status = MMDB_get_value(&result.entry, &entry_data, field_name, NULL);
  if (MMDB_SUCCESS != status) {
    TSDebug(PLUGIN_NAME, "ERROR on get value asn value: %s", MMDB_strerror(status));
    return ret;
  }
  ret = entry_data.uint32;

  if (nullptr != entry_data_list) {
    MMDB_free_entry_data_list(entry_data_list);
  }

  return ret;
}
