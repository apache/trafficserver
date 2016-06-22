/** @file

  SSL Preaccept test plugin
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

#include <stdio.h>
#include <memory.h>
#include <inttypes.h>
#include <ts/ts.h>
#include <tsconfig/TsValue.h>
#include <ts/ink_inet.h>
#include <getopt.h>

using ts::config::Configuration;
using ts::config::Value;

#define PN "ssl-preaccept"
#define PCP "[" PN " Plugin] "

namespace
{
std::string ConfigPath;
typedef std::pair<IpAddr, IpAddr> IpRange;
typedef std::deque<IpRange> IpRangeQueue;
IpRangeQueue ClientBlindTunnelIp;

Configuration Config; // global configuration

void
Parse_Addr_String(ts::ConstBuffer const &text, IpRange &range)
{
  IpAddr newAddr;
  std::string textstr(text._ptr, text._size);
  // Is there a hyphen?
  size_t hyphen_pos = textstr.find("-");
  if (hyphen_pos != std::string::npos) {
    std::string addr1 = textstr.substr(0, hyphen_pos);
    std::string addr2 = textstr.substr(hyphen_pos + 1);
    range.first.load(ts::ConstBuffer(addr1.c_str(), addr1.length()));
    range.second.load(ts::ConstBuffer(addr2.c_str(), addr2.length()));
  } else { // Assume it is a single address
    newAddr.load(text);
    range.first  = newAddr;
    range.second = newAddr;
  }
}

/// Get a string value from a config node.
void
Load_Config_Value(Value const &parent, char const *name, IpRangeQueue &addrs)
{
  Value v = parent[name];
  std::string zret;
  IpRange ipRange;
  if (v.isLiteral()) {
    Parse_Addr_String(v.getText(), ipRange);
    addrs.push_back(ipRange);
  } else if (v.isContainer()) {
    size_t i;
    for (i = 0; i < v.childCount(); i++) {
      std::string val_str(v[i].getText()._ptr, v[i].getText()._size);
      Parse_Addr_String(v[i].getText(), ipRange);
      addrs.push_back(ipRange);
    }
  }
}

int
Load_Config_File()
{
  ts::Rv<Configuration> cv = Configuration::loadFromPath(ConfigPath.c_str());
  if (!cv.isOK()) {
    TSError(PCP "Failed to parse %s as TSConfig format", ConfigPath.c_str());
    return -1;
  }
  Config = cv;
  return 1;
}

int
Load_Configuration()
{
  int ret = Load_Config_File();
  if (ret != 0) {
    TSError(PCP "Failed to load the config file, check debug output for errata");
  }

  // Still need to use the file
  Value root = Config.getRoot();
  Load_Config_Value(root, "client-blind-tunnel", ClientBlindTunnelIp);

  return 0;
}

int
CB_Pre_Accept(TSCont, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  IpAddr ip(TSNetVConnLocalAddrGet(ssl_vc));
  char buff[INET6_ADDRSTRLEN];
  IpAddr ip_client(TSNetVConnRemoteAddrGet(ssl_vc));
  char buff2[INET6_ADDRSTRLEN];

  TSDebug("skh", "Pre accept callback %p - event is %s, target address %s, client address %s", ssl_vc,
          event == TS_EVENT_VCONN_PRE_ACCEPT ? "good" : "bad", ip.toString(buff, sizeof(buff)),
          ip_client.toString(buff2, sizeof(buff2)));

  // Not the worlds most efficient address comparison.  For short lists
  // shouldn't be too bad.  If the client IP is in any of the ranges,
  // flip the tunnel to be blind tunneled instead of decrypted and proxied
  bool proxy_tunnel = true;
  IpRangeQueue::iterator iter;
  for (iter = ClientBlindTunnelIp.begin(); iter != ClientBlindTunnelIp.end() && proxy_tunnel; iter++) {
    if (ip_client >= iter->first && ip_client <= iter->second) {
      proxy_tunnel = false;
    }
  }
  if (!proxy_tunnel) {
    TSDebug("skh", "Blind tunnel");
    // Push everything to blind tunnel
    TSVConnTunnel(ssl_vc);
  } else {
    TSDebug("skh", "Proxy tunnel");
  }

  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

} // Anon namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  bool success = false;
  TSPluginRegistrationInfo info;
  TSCont cb_pa                         = 0; // pre-accept callback continuation
  static const struct option longopt[] = {{const_cast<char *>("config"), required_argument, NULL, 'c'},
                                          {NULL, no_argument, NULL, '\0'}};

  info.plugin_name   = const_cast<char *>("SSL Preaccept test");
  info.vendor_name   = const_cast<char *>("Network Geographics");
  info.support_email = const_cast<char *>("shinrich@network-geographics.com");

  int opt = 0;
  while (opt >= 0) {
    opt = getopt_long(argc, (char *const *)argv, "c:", longopt, NULL);
    switch (opt) {
    case 'c':
      ConfigPath = optarg;
      ConfigPath = std::string(TSConfigDirGet()) + '/' + std::string(optarg);
      break;
    }
  }
  if (ConfigPath.length() == 0) {
    static char const *const DEFAULT_CONFIG_PATH = "ssl_preaccept.config";
    ConfigPath                                   = std::string(TSConfigDirGet()) + '/' + std::string(DEFAULT_CONFIG_PATH);
    TSDebug(PN, "No config path set in arguments, using default: %s", DEFAULT_CONFIG_PATH);
  }

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed.");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later.");
  } else if (0 > Load_Configuration()) {
    TSError(PCP "Failed to load config file.");
  } else if (0 == (cb_pa = TSContCreate(&CB_Pre_Accept, TSMutexCreate()))) {
    TSError(PCP "Failed to pre-accept callback.");
  } else {
    TSHttpHookAdd(TS_VCONN_PRE_ACCEPT_HOOK, cb_pa);
    success = true;
  }

  if (!success) {
    TSError(PCP "not initialized");
  }
  TSDebug(PN, "Plugin %s", success ? "online" : "offline");

  return;
}
