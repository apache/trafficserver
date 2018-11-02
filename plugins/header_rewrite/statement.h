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
//
// Base class for all Conditions and Operations. We share the "linked" list, and the
// resource management / requirements.
//
#pragma once

#include <string>
#include <ctime>
#include <vector>

#include "ts/ts.h"

#include "resources.h"
#include "parser.h"
#include "lulu.h"

// URL data (both client and server)
enum UrlQualifiers {
  URL_QUAL_NONE,
  URL_QUAL_HOST,
  URL_QUAL_PORT,
  URL_QUAL_PATH,
  URL_QUAL_QUERY,
  URL_QUAL_MATRIX,
  URL_QUAL_SCHEME,
  URL_QUAL_URL
};

// NOW data
enum NowQualifiers {
  NOW_QUAL_EPOCH,
  NOW_QUAL_YEAR,
  NOW_QUAL_MONTH,
  NOW_QUAL_DAY,
  NOW_QUAL_HOUR,
  NOW_QUAL_MINUTE,
  NOW_QUAL_WEEKDAY,
  NOW_QUAL_YEARDAY
};

// GEO data
enum GeoQualifiers {
  GEO_QUAL_COUNTRY,
  GEO_QUAL_COUNTRY_ISO,
  GEO_QUAL_ASN,
  GEO_QUAL_ASN_NAME,
};

// ID data
enum IdQualifiers {
  ID_QUAL_REQUEST,
  ID_QUAL_PROCESS,
  ID_QUAL_UNIQUE,
};

// IP
enum IpQualifiers {
  IP_QUAL_CLIENT,
  IP_QUAL_INBOUND,
  // These two might not necessarily get populated, e.g. on a cache hit.
  IP_QUAL_SERVER,
  IP_QUAL_OUTBOUND,
};

enum NetworkSessionQualifiers {
  NET_QUAL_LOCAL_ADDR,  ///< Local address.
  NET_QUAL_LOCAL_PORT,  ///< Local port.
  NET_QUAL_REMOTE_ADDR, ///< Remote address.
  NET_QUAL_REMOTE_PORT, ///< Remote port.
  NET_QUAL_TLS,         ///< TLS protocol
  NET_QUAL_H2,          ///< 'h2' or not.
  NET_QUAL_IPV4,        ///< 'ipv4' or not.
  NET_QUAL_IPV6,        ///< 'ipv6' or not.
  NET_QUAL_IP_FAMILY,   ///< IP protocol family.
  NET_QUAL_STACK,       ///< Full protocol stack.
};

class Statement
{
public:
  Statement() { TSDebug(PLUGIN_NAME_DBG, "Calling CTOR for Statement"); }

  virtual ~Statement()
  {
    TSDebug(PLUGIN_NAME_DBG, "Calling DTOR for Statement");
    delete _next;
  }

  // Which hook are we adding this statement to?
  bool set_hook(TSHttpHookID hook);
  TSHttpHookID
  get_hook() const
  {
    return _hook;
  }

  // Which hooks are this "statement" applicable for? Used during parsing only.
  void
  add_allowed_hook(const TSHttpHookID hook)
  {
    _allowed_hooks.push_back(hook);
  }

  // Linked list.
  void append(Statement *stmt);

  ResourceIDs get_resource_ids() const;

  virtual void
  initialize(Parser &)
  {
    TSReleaseAssert(_initialized == false);
    initialize_hooks();
    _initialized = true;
  }

  bool
  initialized() const
  {
    return _initialized;
  }

protected:
  virtual void initialize_hooks();

  UrlQualifiers parse_url_qualifier(const std::string &q) const;

  void
  require_resources(const ResourceIDs ids)
  {
    _rsrc = static_cast<ResourceIDs>(_rsrc | ids);
  }

  Statement *_next = nullptr; // Linked list

private:
  DISALLOW_COPY_AND_ASSIGN(Statement);

  ResourceIDs _rsrc  = RSRC_NONE;
  bool _initialized  = false;
  TSHttpHookID _hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  std::vector<TSHttpHookID> _allowed_hooks;
};
