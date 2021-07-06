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

#include "configs.h"
#include "rules.h"

///////////////////////////////////////////////////////////////////////////
// These are little helper functions for the main rules evaluator.
//
static bool
check_client_ip_configured(TSHttpTxn txnp, const char *cfg_ip)
{
  const sockaddr *client_ip = TSHttpTxnClientAddrGet(txnp);
  char ip_buf[INET6_ADDRSTRLEN];

  if (AF_INET == client_ip->sa_family) {
    inet_ntop(AF_INET, &(reinterpret_cast<const sockaddr_in *>(client_ip)->sin_addr), ip_buf, INET_ADDRSTRLEN);
  } else if (AF_INET6 == client_ip->sa_family) {
    inet_ntop(AF_INET6, &(reinterpret_cast<const sockaddr_in6 *>(client_ip)->sin6_addr), ip_buf, INET6_ADDRSTRLEN);
  } else {
    TSError("[%s] Unknown family %d", PLUGIN_NAME, client_ip->sa_family);
    return false;
  }

  TSDebug(PLUGIN_NAME, "cfg_ip %s, client_ip %s", cfg_ip, ip_buf);

  if ((strlen(cfg_ip) == strlen(ip_buf)) && !strcmp(cfg_ip, ip_buf)) {
    TSDebug(PLUGIN_NAME, "bg fetch for ip %s, configured ip %s", ip_buf, cfg_ip);
    return true;
  }

  return false;
}

static bool
check_content_length(const uint32_t len, const char *cfg_val)
{
  uint32_t cfg_cont_len = atoi(&cfg_val[1]);

  if (cfg_val[0] == '<') {
    return (len <= cfg_cont_len);
  } else if (cfg_val[0] == '>') {
    return (len >= cfg_cont_len);
  } else {
    TSError("[%s] Invalid content length condition %c", PLUGIN_NAME, cfg_val[0]);
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////
// Check if a header excludes us from running the background fetch
//
bool
BgFetchRule::check_field_configured(TSHttpTxn txnp) const
{
  // check for client-ip first
  if (!strcmp(_field, "Client-IP")) {
    if (!strcmp(_value, "*")) {
      TSDebug(PLUGIN_NAME, "Found client_ip wild card");
      return true;
    }
    if (check_client_ip_configured(txnp, _value)) {
      TSDebug(PLUGIN_NAME, "Found client_ip match");
      return true;
    }
  }

  bool hdr_found = false;
  TSMBuffer hdr_bufp;
  TSMLoc hdr_loc;

  // Check response headers. ToDo: This doesn't check e.g. Content-Type :-/.
  if (!strcmp(_field, "Content-Length")) {
    if (TS_SUCCESS == TSHttpTxnServerRespGet(txnp, &hdr_bufp, &hdr_loc)) {
      TSMLoc loc = TSMimeHdrFieldFind(hdr_bufp, hdr_loc, _field, -1);

      if (TS_NULL_MLOC != loc) {
        unsigned int content_len = TSMimeHdrFieldValueUintGet(hdr_bufp, hdr_loc, loc, 0 /* index */);

        if (check_content_length(content_len, _value)) {
          TSDebug(PLUGIN_NAME, "Found content-length match");
          hdr_found = true;
        }
        TSHandleMLocRelease(hdr_bufp, hdr_loc, loc);
      } else {
        TSDebug(PLUGIN_NAME, "No content-length field in resp");
      }
      TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, hdr_loc);
    } else {
      TSError("[%s] Failed to get resp headers", PLUGIN_NAME);
    }
    return hdr_found;
  }

  // Check request headers
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &hdr_bufp, &hdr_loc)) {
    TSMLoc loc = TSMimeHdrFieldFind(hdr_bufp, hdr_loc, _field, -1);

    if (TS_NULL_MLOC != loc) {
      if (!strcmp(_value, "*")) {
        TSDebug(PLUGIN_NAME, "Found %s wild card", _field);
        hdr_found = true;
      } else {
        int val_len         = 0;
        const char *val_str = TSMimeHdrFieldValueStringGet(hdr_bufp, hdr_loc, loc, 0, &val_len);

        if (!val_str || val_len <= 0) {
          TSDebug(PLUGIN_NAME, "invalid field");
        } else {
          TSDebug(PLUGIN_NAME, "comparing with %s", _value);
          if (std::string_view::npos != std::string_view(val_str, val_len).find(_value)) {
            hdr_found = true;
          }
        }
      }
      TSHandleMLocRelease(hdr_bufp, hdr_loc, loc);
    } else {
      TSDebug(PLUGIN_NAME, "no field %s in request header", _field);
    }
    TSHandleMLocRelease(hdr_bufp, TS_NULL_MLOC, hdr_loc);
  } else {
    TSError("[%s] Failed to get resp headers", PLUGIN_NAME);
  }

  return hdr_found;
}
