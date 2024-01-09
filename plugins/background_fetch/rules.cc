/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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

#include <cstdlib>
#include <string_view>
#include <cstring>

#include <swoc/IPEndpoint.h>
#include <swoc/swoc_meta.h>

#include "configs.h"
#include "rules.h"

#include "tsutil/ts_bw_format.h"
#include "tsutil/ts_ip.h"

///////////////////////////////////////////////////////////////////////////
// These are little helper functions for the main rules evaluator.
//
static bool
check_value(TSHttpTxn txnp, swoc::IPRange const &range)
{
  const sockaddr *client_ip = TSHttpTxnClientAddrGet(txnp);
  if (!client_ip) {
    return false;
  }

  if (range.empty()) { // this means "match any address".
    return true;
  }

  swoc::IPEndpoint client_addr{client_ip};

  swoc::bwprint(ts::bw_dbg, "cfg_ip {::c}, client_ip {}", range, client_addr);
  Dbg(Bg_dbg_ctl, "%s", ts::bw_dbg.c_str());

  if (client_addr.family() == range.family()) {
    return (range.is_ip4() && range.ip4().contains(swoc::IP4Addr(client_addr.ip4()))) ||
           (range.is_ip6() && range.ip6().contains(swoc::IP6Addr(client_addr.ip6())));
  }

  return false; // Different family, no match.
}

static bool
check_value(TSHttpTxn txnp, BgFetchRule::size_cmp_type const &cmp)
{
  TSMBuffer hdr_bufp;
  TSMLoc hdr_loc;

  if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &hdr_bufp, &hdr_loc)) {
    TSError("[%s] Failed to get resp headers", PLUGIN_NAME);
    return false;
  }

  TSMLoc loc = TSMimeHdrFieldFind(hdr_bufp, hdr_loc, TS_MIME_FIELD_CONTENT_LENGTH, TS_MIME_LEN_CONTENT_LENGTH);
  if (TS_NULL_MLOC == loc) {
    Dbg(Bg_dbg_ctl, "No content-length field in resp");
    return false; // Field not found.
  }

  auto content_len = TSMimeHdrFieldValueUintGet(hdr_bufp, hdr_loc, loc, 0 /* index */);
  TSHandleMLocRelease(hdr_bufp, hdr_loc, loc);

  if (cmp._op == BgFetchRule::size_cmp_type::OP::GREATER_THAN_OR_EQUAL) {
    return content_len >= cmp._size;
  } else if (cmp._op == BgFetchRule::size_cmp_type::OP::LESS_THAN_OR_EQUAL) {
    return content_len <= cmp._size;
  }

  return false;
}

static bool
check_value(TSHttpTxn txnp, BgFetchRule::field_cmp_type const &cmp)
{
  TSMBuffer hdr_bufp;
  TSMLoc hdr_loc;

  if (TS_SUCCESS != TSHttpTxnClientReqGet(txnp, &hdr_bufp, &hdr_loc)) {
    TSError("[%s] Failed to get resp headers", PLUGIN_NAME);
    return false;
  }

  TSMLoc loc = TSMimeHdrFieldFind(hdr_bufp, hdr_loc, cmp._name.data(), cmp._name.size());

  if (TS_NULL_MLOC == loc) {
    Dbg(Bg_dbg_ctl, "no field %s in request header", cmp._name.c_str());
    return false;
  }

  if (cmp._name.size() == 1 && cmp._name.front() == '*') {
    Dbg(Bg_dbg_ctl, "Found %s wild card", cmp._name.c_str());
    return true;
  }

  int val_len         = 0;
  char const *val_str = TSMimeHdrFieldValueStringGet(hdr_bufp, hdr_loc, loc, 0, &val_len);
  bool zret           = false;

  if (!val_str || val_len <= 0) {
    Dbg(Bg_dbg_ctl, "invalid field");
  } else {
    Dbg(Bg_dbg_ctl, "comparing with %s", cmp._value.c_str());
    zret = std::string_view::npos != std::string_view(val_str, val_len).find(cmp._value);
  }
  TSHandleMLocRelease(hdr_bufp, hdr_loc, loc);
  return zret;
}

///////////////////////////////////////////////////////////////////////////
// Check if a header excludes us from running the background fetch
//
bool
BgFetchRule::check_field_configured(TSHttpTxn txnp) const
{
  return std::visit(swoc::meta::vary{[=](std::monostate) { return false; },
                                     [=](swoc::IPRange const &range) { return check_value(txnp, range); },
                                     [=](size_cmp_type const &cmp) { return check_value(txnp, cmp); },
                                     [=](field_cmp_type const &cmp) { return check_value(txnp, cmp); }},
                    _value);
}
