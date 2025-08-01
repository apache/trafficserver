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

#include "cripts/Preamble.hpp"

#if CRIPTS_HAS_MAXMIND
#include <maxminddb.h>

extern MMDB_s *gMaxMindDB;

enum Qualifiers {
  GEO_QUAL_COUNTRY,
  GEO_QUAL_COUNTRY_ISO,
  GEO_QUAL_ASN,
  GEO_QUAL_ASN_NAME,
};

cripts::string
get_geo_string(const sockaddr *addr, Qualifiers q)
{
  ink_release_assert(gMaxMindDB != nullptr);
  ink_release_assert(addr != nullptr);

  cripts::string ret = "(unknown)";
  int            mmdb_error;

  MMDB_lookup_result_s result = MMDB_lookup_sockaddr(gMaxMindDB, addr, &mmdb_error);

  if (MMDB_SUCCESS != mmdb_error) {
    return ret;
  }

  MMDB_entry_data_list_s *entry_data_list = nullptr;
  if (!result.found_entry) {
    return ret;
  }

  int status = MMDB_get_entry_data_list(&result.entry, &entry_data_list);
  if (MMDB_SUCCESS != status) {
    return ret;
  }

  if (entry_data_list == nullptr) {
    return ret;
  }

  const char *field_name;
  switch (q) {
  case GEO_QUAL_COUNTRY:
    field_name = "country_code";
    break;
  case GEO_QUAL_ASN_NAME:
    field_name = "autonomous_system_organization";
    break;
  default:
    Error("Cripts: Unsupported field %d", q);
    return ret;
    break;
  }

  MMDB_entry_data_s entry_data;

  status = MMDB_get_value(&result.entry, &entry_data, field_name, NULL);
  if (MMDB_SUCCESS != status) {
    return ret;
  }
  ret = cripts::string(entry_data.utf8_string, entry_data.data_size);

  if (nullptr != entry_data_list) {
    MMDB_free_entry_data_list(entry_data_list);
  }

  return ret;
}

// Helper function to convert IP to sockaddr for geo lookup
cripts::string
get_geo_string_from_ip(const cripts::IP &ip, Qualifiers q)
{
  auto addr = ip.Socket();
  return get_geo_string(&addr, q);
}

// IP class Geo methods - can be used with any IP address
cripts::string
cripts::IP::ASN() const
{
  return get_geo_string_from_ip(*this, GEO_QUAL_ASN);
}

cripts::string
cripts::IP::ASNName() const
{
  return get_geo_string_from_ip(*this, GEO_QUAL_ASN_NAME);
}

cripts::string
cripts::IP::Country() const
{
  return get_geo_string_from_ip(*this, GEO_QUAL_COUNTRY);
}

cripts::string
cripts::IP::CountryCode() const
{
  return get_geo_string_from_ip(*this, GEO_QUAL_COUNTRY_ISO);
}

#else

// IP class Geo methods - unavailable when MaxMind is not available
cripts::string
cripts::IP::ASN() const
{
  return "(unavailable)";
}

cripts::string
cripts::IP::ASNName() const
{
  return "(unavailable)";
}

cripts::string
cripts::IP::Country() const
{
  return "(unavailable)";
}

cripts::string
cripts::IP::CountryCode() const
{
  return "(unavailable)";
}

#endif // CRIPTS_HAS_MAXMIND

// ConnBase::Geo methods - delegate to IP-based implementation for backward compatibility
cripts::string
detail::ConnBase::Geo::ASN() const
{
  return this->_owner->IP().ASN();
}

cripts::string
detail::ConnBase::Geo::ASNName() const
{
  return this->_owner->IP().ASNName();
}

cripts::string
detail::ConnBase::Geo::Country() const
{
  return this->_owner->IP().Country();
}

cripts::string
detail::ConnBase::Geo::CountryCode() const
{
  return this->_owner->IP().CountryCode();
}
