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
// conditions_geo_geoip.cc: Implementation of the ConditionGeo class based on the GeoIP library
//
//

#include <unistd.h>
#include <arpa/inet.h>
#include <cctype>

#include "ts/ts.h"

#include "conditions_geo.h"

#include <GeoIP.h>

GeoIP *gGeoIP[NUM_DB_TYPES];

void
GeoIPConditionGeo::initLibrary(const std::string &)
{
  GeoIPDBTypes dbs[] = {GEOIP_COUNTRY_EDITION, GEOIP_COUNTRY_EDITION_V6, GEOIP_ASNUM_EDITION, GEOIP_ASNUM_EDITION_V6};

  for (auto &db : dbs) {
    if (!gGeoIP[db] && GeoIP_db_avail(db)) {
      // GEOIP_STANDARD seems to break threaded apps...
      gGeoIP[db] = GeoIP_open_type(db, GEOIP_MMAP_CACHE);

      char *db_info = GeoIP_database_info(gGeoIP[db]);
      TSDebug(PLUGIN_NAME, "initialized GeoIP-DB[%d] %s", db, db_info);
      free(db_info);
    }
  }
}

std::string
GeoIPConditionGeo::get_geo_string(const sockaddr *addr) const
{
  std::string ret = "(unknown)";
  int v           = 4;

  if (addr) {
    switch (_geo_qual) {
    // Country database
    case GEO_QUAL_COUNTRY:
      switch (addr->sa_family) {
      case AF_INET:
        if (gGeoIP[GEOIP_COUNTRY_EDITION]) {
          uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

          ret = GeoIP_country_code_by_ipnum(gGeoIP[GEOIP_COUNTRY_EDITION], ip);
        }
        break;
      case AF_INET6: {
        if (gGeoIP[GEOIP_COUNTRY_EDITION_V6]) {
          geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

          v   = 6;
          ret = GeoIP_country_code_by_ipnum_v6(gGeoIP[GEOIP_COUNTRY_EDITION_V6], ip);
        }
      } break;
      default:
        break;
      }
      TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from Country: %s", v, ret.c_str());
      break;

    // ASN database
    case GEO_QUAL_ASN_NAME:
      switch (addr->sa_family) {
      case AF_INET:
        if (gGeoIP[GEOIP_ASNUM_EDITION]) {
          uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

          ret = GeoIP_name_by_ipnum(gGeoIP[GEOIP_ASNUM_EDITION], ip);
        }
        break;
      case AF_INET6: {
        if (gGeoIP[GEOIP_ASNUM_EDITION_V6]) {
          geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

          v   = 6;
          ret = GeoIP_name_by_ipnum_v6(gGeoIP[GEOIP_ASNUM_EDITION_V6], ip);
        }
      } break;
      default:
        break;
      }
      TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from ASN Name: %s", v, ret.c_str());
      break;
    default:
      break;
    }
  }

  return ret;
}

int64_t
GeoIPConditionGeo::get_geo_int(const sockaddr *addr) const
{
  int64_t ret = -1;
  int v       = 4;

  if (!addr) {
    return 0;
  }

  switch (_geo_qual) {
  // Country Database
  case GEO_QUAL_COUNTRY_ISO:
    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_COUNTRY_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        ret = GeoIP_id_by_ipnum(gGeoIP[GEOIP_COUNTRY_EDITION], ip);
      }
      break;
    case AF_INET6: {
      if (gGeoIP[GEOIP_COUNTRY_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v   = 6;
        ret = GeoIP_id_by_ipnum_v6(gGeoIP[GEOIP_COUNTRY_EDITION_V6], ip);
      }
    } break;
    default:
      break;
    }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from Country ISO: %" PRId64, v, ret);
    break;

  case GEO_QUAL_ASN: {
    const char *asn_name = nullptr;

    switch (addr->sa_family) {
    case AF_INET:
      if (gGeoIP[GEOIP_ASNUM_EDITION]) {
        uint32_t ip = ntohl(reinterpret_cast<const struct sockaddr_in *>(addr)->sin_addr.s_addr);

        asn_name = GeoIP_name_by_ipnum(gGeoIP[GEOIP_ASNUM_EDITION], ip);
      }
      break;
    case AF_INET6:
      if (gGeoIP[GEOIP_ASNUM_EDITION_V6]) {
        geoipv6_t ip = reinterpret_cast<const struct sockaddr_in6 *>(addr)->sin6_addr;

        v        = 6;
        asn_name = GeoIP_name_by_ipnum_v6(gGeoIP[GEOIP_ASNUM_EDITION_V6], ip);
      }
      break;
    }
    if (asn_name) {
      // This is a little odd, but the strings returned are e.g. "AS1234 Acme Inc"
      while (*asn_name && !(isdigit(*asn_name))) {
        ++asn_name;
      }
      ret = strtol(asn_name, nullptr, 10);
    }
  }
    TSDebug(PLUGIN_NAME, "eval(): Client IPv%d seems to come from ASN #: %" PRId64, v, ret);
    break;

  // Likely shouldn't trip, should we assert?
  default:
    break;
  }

  return ret;
}
