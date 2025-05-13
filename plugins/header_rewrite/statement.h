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

namespace header_rewrite_ns
{
constexpr int NUM_STATE_FLAGS = 16;
constexpr int NUM_STATE_INT8S = 4;

constexpr uint64_t STATE_INT8_MASKS[NUM_STATE_INT8S] = {
  // These would change if the number of flag bits changes
  0x0000000000FF0000ULL, // Bits 16-23
  0x00000000FF000000ULL, // Bits 24-31
  0x000000FF00000000ULL, // Bits 32-39
  0x0000FF0000000000ULL, // Bits 40-47
};

constexpr uint64_t STATE_INT16_MASK = 0xFFFF000000000000ULL; // Bits 48-63
} // namespace header_rewrite_ns

// URL data (both client and server)
enum UrlQualifiers {
  URL_QUAL_NONE,
  URL_QUAL_HOST,
  URL_QUAL_PORT,
  URL_QUAL_PATH,
  URL_QUAL_QUERY,
  URL_QUAL_SCHEME,
  URL_QUAL_URL,
};

enum NextHopQualifiers {
  NEXT_HOP_NONE,
  NEXT_HOP_HOST,
  NEXT_HOP_PORT,
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
  NOW_QUAL_YEARDAY,
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
  Statement() { Dbg(dbg_ctl, "Calling CTOR for Statement"); }

  virtual ~Statement()
  {
    Dbg(dbg_ctl, "Calling DTOR for Statement");
    delete _next;
  }

  // noncopyable
  Statement(const Statement &)      = delete;
  void operator=(const Statement &) = delete;

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
    acquire_txn_slot();
    acquire_txn_private_slot();

    _initialized = true;
  }

  bool
  initialized() const
  {
    return _initialized;
  }

protected:
  virtual void initialize_hooks();

  UrlQualifiers     parse_url_qualifier(const std::string &q) const;
  NextHopQualifiers parse_next_hop_qualifier(const std::string &q) const;
  TSHttpCntlType    parse_http_cntl_qualifier(const std::string &q) const;

  void
  require_resources(const ResourceIDs ids)
  {
    _rsrc = static_cast<ResourceIDs>(_rsrc | ids);
  }

  virtual bool
  need_txn_slot() const
  {
    return false;
  }

  virtual bool
  need_txn_private_slot() const
  {
    return false;
  }

  Statement *_next             = nullptr; // Linked list
  int        _txn_slot         = -1;
  int        _txn_private_slot = -1;

private:
  void acquire_txn_slot();
  void acquire_txn_private_slot();

  ResourceIDs               _rsrc = RSRC_NONE;
  TSHttpHookID              _hook = TS_HTTP_READ_RESPONSE_HDR_HOOK;
  std::vector<TSHttpHookID> _allowed_hooks;
  bool                      _initialized = false;
};

union PrivateSlotData {
  uint64_t raw;
  struct {
    uint64_t timezone : 1; // TIMEZONE_LOCAL, or TIMEZONE_GMT
  };
};

enum { TIMEZONE_LOCAL, TIMEZONE_GMT };
