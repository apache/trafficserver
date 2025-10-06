/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "realip.h"
#include "simple.h"

SimpleAddressSource::SimpleAddressSource(YAML::Node config)
{
  if (auto header_node = config["header"]; header_node) {
    header_name = header_node.as<std::string>();
    Dbg(dbg_ctl, "Header name: %s", header_name.c_str());
  }

  if (auto list_node = config["trustedAddress"]; list_node) {
    for (YAML::const_iterator it = list_node.begin(); it != list_node.end(); ++it) {
      std::string item = it->as<std::string>();
      if (swoc::IPRange r; r.load(item)) {
        Dbg(dbg_ctl, "Adding %s to IP range set", item.c_str());
        ip_range_set.mark(r);
      }
    }
  }
}

bool
SimpleAddressSource::verify(TSHttpTxn txnp)
{
  return ip_range_set.contains(swoc::IPAddr(TSHttpTxnClientAddrGet(txnp)));
}

struct sockaddr *
SimpleAddressSource::get_address(TSHttpTxn txnp, struct sockaddr_storage *addr)
{
  TSMBuffer        bufp;
  TSMLoc           hdr_loc;
  TSMLoc           field_loc;
  struct sockaddr *ret = nullptr;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    Dbg(dbg_ctl, "Failed to get client request");
    return nullptr;
  }

  if (header_name.empty()) {
    return nullptr;
  }

  field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, header_name.c_str(), header_name.size());
  if (field_loc) {
    int         value_len;
    const char *value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, -1, &value_len);
    if (inet_pton46({value, static_cast<std::string_view::size_type>(value_len)}, addr) == 1) {
      ret = reinterpret_cast<struct sockaddr *>(addr);
    }
    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  } else {
    Dbg(dbg_ctl, "Failed to find %s header", header_name.c_str());
  }
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  return ret;
}
