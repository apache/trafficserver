/** @file

  SSL Preaccept test plugin.

  Implements blind tunneling based on the client IP address
  The client ip addresses are specified in the plugin's
  config file as an array of IP addresses or IP address ranges under the
  key "client-blind-tunnel"

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

#include <cstdio>
#include <memory.h>
#include <cinttypes>
#include <ts/ts.h>
#include "tscore/ink_inet.h"
#include <algorithm>
#include <deque>

#define PLUGIN_NAME "ssl-preaccept"
#define PCP "[" PLUGIN_NAME "] "

namespace
{
using IpRange      = std::pair<IpAddr, IpAddr>;
using IpRangeQueue = std::deque<IpRange>;
IpRangeQueue ClientBlindTunnelIp;

void
Parse_Addr_String(std::string_view const &text, IpRange &range)
{
  IpAddr newAddr;
  // Is there a hyphen?
  size_t hyphen_pos = text.find('-');
  if (hyphen_pos != std::string_view::npos) {
    std::string_view addr1 = text.substr(0, hyphen_pos);
    std::string_view addr2 = text.substr(hyphen_pos + 1);
    range.first.load(addr1);
    range.second.load(addr2);
  } else { // Assume it is a single address
    newAddr.load(text);
    range.first  = newAddr;
    range.second = newAddr;
  }
}

int
CB_Pre_Accept(TSCont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  IpAddr ip(TSNetVConnLocalAddrGet(ssl_vc));
  char buff[INET6_ADDRSTRLEN];
  IpAddr ip_client(TSNetVConnRemoteAddrGet(ssl_vc));
  char buff2[INET6_ADDRSTRLEN];

  // Not the worlds most efficient address comparison.  For short lists
  // shouldn't be too bad.  If the client IP is in any of the ranges,
  // flip the tunnel to be blind tunneled instead of decrypted and proxied
  bool proxy_tunnel = true;

  for (auto const &r : ClientBlindTunnelIp) {
    if (r.first <= ip_client && ip_client <= r.second) {
      proxy_tunnel = false;
      break;
    }
  }

  if (!proxy_tunnel) {
    // Push everything to blind tunnel
    TSVConnTunnel(ssl_vc);
  }

  TSDebug(PLUGIN_NAME, "Pre accept callback %p - event is %s, target address %s, client address %s%s", ssl_vc,
          event == TS_EVENT_VCONN_START ? "good" : "bad", ip.toString(buff, sizeof(buff)), ip_client.toString(buff2, sizeof(buff2)),
          proxy_tunnel ? "" : " blind tunneled");

  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

} // namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  bool success = false;
  TSCont cb_pa = nullptr; // pre-accept callback continuation

  TSPluginRegistrationInfo info;
  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (argc < 2) {
    TSError(PCP "Usage: ssl_preaccept.so <ip or network>");
    return;
  }

  IpRange ipRange;
  Parse_Addr_String(std::string_view(argv[1]), ipRange);
  ClientBlindTunnelIp.push_back(ipRange);

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later");
  } else if (nullptr == (cb_pa = TSContCreate(&CB_Pre_Accept, TSMutexCreate()))) {
    TSError(PCP "Failed to pre-accept callback");
  } else {
    TSHttpHookAdd(TS_VCONN_START_HOOK, cb_pa);
    success = true;
  }

  if (!success) {
    TSError(PCP "not initialized");
  }
  TSDebug(PLUGIN_NAME, "Plugin %s", success ? "online" : "offline");

  return;
}
