/** @file 
    SSL SNI white list plugin
    If the server name and IP address are not in the ssl_multicert.config
    go head and blind tunnel it.
*/

# include <stdio.h>
# include <memory.h>
# include <inttypes.h>
# include <ts/ts.h>
# include <tsconfig/TsValue.h>
# include <alloca.h>
# include <openssl/ssl.h>

using ts::config::Configuration;
using ts::config::Value;

# define PN "ssl-sni-whitelist"
# define PCP "[" PN " Plugin] "

namespace {

std::string ConfigPath;

Configuration Config;	// global configuration

int
Load_Config_File() {
  ts::Rv<Configuration> cv = Configuration::loadFromPath(ConfigPath.c_str());
  if (!cv.isOK()) {
    TSError(PCP "Failed to parse %s as TSConfig format", ConfigPath.c_str());
    return -1;
  }
  Config = cv;
  return 1;
}

int
Load_Configuration(int argc, const char *argv[]) {
ts::ConstBuffer text;
  std::string s; // temp holder.
  TSMgmtString config_path = NULL;

  // get the path to the config file if one was specified
  static char const * const CONFIG_ARG = "--config=";
  int arg_idx;
  for (arg_idx = 0; arg_idx < argc; arg_idx++) {
    if (0 == memcmp(argv[arg_idx], CONFIG_ARG, strlen(CONFIG_ARG))) {
       config_path = TSstrdup(argv[arg_idx] + strlen(CONFIG_ARG));
       TSDebug(PN, "Found config path %s", config_path);
    }
  }
  if (NULL == config_path) {
    static char const * const DEFAULT_CONFIG_PATH = "ssl_sni_whitelist.config";
    config_path = TSstrdup(DEFAULT_CONFIG_PATH);
    TSDebug(PN, "No config path set in arguments, using default: %s", DEFAULT_CONFIG_PATH);
  }

  // translate relative paths to absolute
  if (config_path[0] != '/') {
    ConfigPath = std::string(TSConfigDirGet()) + '/' + std::string(config_path);
  } else {
    ConfigPath = config_path;
  }

  // free up the path
  TSfree(config_path);

  int ret = Load_Config_File();
  if (ret != 0) {
    TSError(PCP "Failed to load the config file, check debug output for errata");
  }

  return 0;
}

int
CB_servername_whitelist(TSCont /* contp */, TSEvent /* event */, void *edata) {
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

  bool do_blind_tunnel = true;
  if (servername != NULL) {
    TSSslContext ctxobj = TSSslContextFindByName(servername);
    if (ctxobj != NULL) {
      do_blind_tunnel = false;
    }
    else {
      // Look up by destination address
      ctxobj = TSSslContextFindByAddr(TSNetVConnRemoteAddrGet(ssl_vc));
      if (ctxobj != NULL) {
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

} // Anon namespace

// Called by ATS as our initialization point
void
TSPluginInit(int argc, const char *argv[]) {
  bool success = false;
  TSPluginRegistrationInfo info;
  TSCont cb_sni = 0; // sni callback continuation

  info.plugin_name = const_cast<char*>("SSL SNI whitelist");
  info.vendor_name = const_cast<char*>("Network Geographics");
  info.support_email = const_cast<char*>("shinrich@network-geographics.com");

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError(PCP "registration failed.");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later.");
  } else if (0 > Load_Configuration(argc, argv)) {
    TSError(PCP "Failed to load config file.");
  } else if (0 == (cb_sni = TSContCreate(&CB_servername_whitelist, TSMutexCreate()))) {
    TSError(PCP "Failed to create SNI callback.");
  } else {
    TSHttpHookAdd(TS_SSL_SNI_HOOK, cb_sni);
    success = true;
  }
 
  if (!success) {
    if (cb_sni) TSContDestroy(cb_sni);
    TSError(PCP "not initialized");
  }
  TSDebug(PN, "Plugin %s", success ? "online" : "offline");

  return;
}

