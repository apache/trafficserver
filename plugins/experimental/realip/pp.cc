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
#include "pp.h"

ProxyProtocolAddressSource::ProxyProtocolAddressSource(YAML::Node /* config ATS_UNUSED */)
{
  // There is no settings for this address source.
}

bool
ProxyProtocolAddressSource::verify(TSHttpTxn /* txnp ATS_UNUSED */)
{
  // This address source expects that proxy.config.http.proxy_protocol_allowlist is configured appropriately.
  return true;
}

struct sockaddr *
ProxyProtocolAddressSource::get_address(TSHttpTxn txnp, struct sockaddr_storage *addr)
{
  struct sockaddr       *ret = nullptr;
  const struct sockaddr *pp_addr;
  int                    pp_addr_len;

  TSVConn vconn = TSHttpSsnClientVConnGet(TSHttpTxnSsnGet(txnp));
  if (TSVConnPPInfoGet(vconn, TS_PP_INFO_SRC_ADDR, reinterpret_cast<const char **>(&pp_addr), &pp_addr_len) == TS_SUCCESS) {
    if (pp_addr->sa_family == AF_INET) {
      memcpy(addr, pp_addr, sizeof(struct sockaddr_in));
      ret = reinterpret_cast<struct sockaddr *>(addr);
    } else {
      memcpy(addr, pp_addr, sizeof(struct sockaddr_in6));
      ret = reinterpret_cast<struct sockaddr *>(addr);
    }
  }

  return ret;
}
