/** @file

  SSL SNI white list plugin
  If the server name and IP address are not in the ssl_multicert.config
  go head and blind tunnel it.

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
#include "tscore/ink_config.h"
#include <tsconfig/TsValue.h>
#include <openssl/ssl.h>
#include <getopt.h>

using ts::config::Configuration;
using ts::config::Value;

#define PLUGIN_NAME "ssl_sni_whitelist"
#define PCP "[" PLUGIN_NAME "] "

namespace
{
std::string ConfigPath;

Configuration Config; // global configuration

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

  return 0;
}

int
CB_servername_whitelist(TSCont /* contp */, TSEvent /* event */, void *edata)
{
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

  bool do_blind_tunnel = true;
  if (servername != nullptr) {
    TSSslContext ctxobj = TSSslContextFindByName(servername);
    if (ctxobj != nullptr) {
      do_blind_tunnel = false;
    } else {
      // Look up by destination address
      ctxobj = TSSslContextFindByAddr(TSNetVConnRemoteAddrGet(ssl_vc));
      if (ctxobj != nullptr) {
        do_blind_tunnel = false;
      }
    }
  }
  if (do_blind_tunnel) {
    TSDebug("skh", "SNI callback: do blind tunnel for %s", servername);
    TSVConnTunnel(ssl_vc);
    return TS_SUCCESS; // Don't re-enable so we interrupt processing
  }
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

} // namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[])
{
  bool success = false;
  TSPluginRegistrationInfo info;
  TSCont cb_sni                        = nullptr; // sni callback continuation
  static const struct option longopt[] = {
    {const_cast<char *>("config"), required_argument, nullptr, 'c'},
    {nullptr, no_argument, nullptr, '\0'},
  };

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  int opt = 0;
  while (opt >= 0) {
    opt = getopt_long(argc, (char *const *)argv, "c:", longopt, nullptr);
    switch (opt) {
    case 'c':
      ConfigPath = optarg;
      ConfigPath = std::string(TSConfigDirGet()) + '/' + std::string(optarg);
      break;
    }
  }
  if (ConfigPath.length() == 0) {
    static const char *const DEFAULT_CONFIG_PATH = "ssl_sni_whitelist.config";
    ConfigPath                                   = std::string(TSConfigDirGet()) + '/' + std::string(DEFAULT_CONFIG_PATH);
    TSDebug(PLUGIN_NAME, "No config path set in arguments, using default: %s", DEFAULT_CONFIG_PATH);
  }

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later");
  } else if (0 > Load_Configuration()) {
    TSError(PCP "Failed to load config file");
  } else if (nullptr == (cb_sni = TSContCreate(&CB_servername_whitelist, TSMutexCreate()))) {
    TSError(PCP "Failed to create SNI callback");
  } else {
    TSHttpHookAdd(TS_SSL_CERT_HOOK, cb_sni);
    success = true;
  }

  if (!success) {
    TSError(PCP "not initialized");
  }
  TSDebug(PLUGIN_NAME, "Plugin %s", success ? "online" : "offline");

  return;
}
