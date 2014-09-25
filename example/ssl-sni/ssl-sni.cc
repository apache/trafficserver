/** @file 
    SSL Preaccept test plugin
    Implements blind tunneling based on the client IP address
    The client ip addresses are specified in the plugin's  
    config file as an array of IP addresses or IP address ranges under the
    key "client-blind-tunnel"
*/

# include <stdio.h>
# include <memory.h>
# include <inttypes.h>
# include <ts/ts.h>
# include <ink_config.h>
# include <tsconfig/TsValue.h>
# include <openssl/ssl.h>

using ts::config::Configuration;
using ts::config::Value;

# define PN "ssl-sni-test"
# define PCP "[" PN " Plugin] "

# if TS_USE_TLS_SNI

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
    static char const * const DEFAULT_CONFIG_PATH = "ssl_sni.config";
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

/**
   Somewhat nonscensically exercise some scenarios of proxying
   and blind tunneling from the SNI callback plugin

   Case 1: If the servername ends in facebook.com, blind tunnel
   Case 2: If the servername is www.yahoo.com and there is a context
   entry for "safelyfiled.com", use the "safelyfiled.com" context for
   this connection.
 */
int
CB_servername(TSCont /* contp */, TSEvent /* event */, void *edata) {
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (servername != NULL) {
    int servername_len = strlen(servername);
    int facebook_name_len = strlen("facebook.com");
    if (servername_len >= facebook_name_len) {
      const char *server_ptr = servername + (servername_len - facebook_name_len);
      if (strcmp(server_ptr, "facebook.com") == 0) {
        TSDebug("skh", "Blind tunnel from SNI callback");
        TSVConnTunnel(ssl_vc);
        // Don't reenable to ensure that we break out of the
        // SSL handshake processing
        return TS_SUCCESS; // Don't re-enable so we interrupt processing
      }
    }
    // If the name is yahoo, look for a context for safelyfiled and use that here
    if (strcmp("www.yahoo.com", servername) == 0) {
      TSDebug("skh", "SNI name is yahoo ssl obj is %p", sslobj);
      if (sslobj) {
        TSSslContext ctxobj = TSSslContextFindByName("safelyfiled.com");
        if (ctxobj != NULL) {
          TSDebug("skh", "Found cert for safelyfiled");
          SSL_CTX *ctx = reinterpret_cast<SSL_CTX *>(ctxobj);
          SSL_set_SSL_CTX(ssl, ctx); 
          TSDebug("skh", "SNI plugin cb: replace SSL CTX");
        }
      }
    }
  } 


  // All done, reactivate things
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

  info.plugin_name = const_cast<char*>("SSL SNI callback test");
  info.vendor_name = const_cast<char*>("Network Geographics");
  info.support_email = const_cast<char*>("shinrich@network-geographics.com");

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError(PCP "registration failed.");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later.");
  } else if (0 > Load_Configuration(argc, argv)) {
    TSError(PCP "Failed to load config file.");
  } else if (0 == (cb_sni = TSContCreate(&CB_servername, TSMutexCreate()))) {
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

# else // ! TS_USE_TLS_SNI

void
TSPluginInit(int, const char *[]) {
    TSError(PCP "requires TLS SNI which is not available.");
}

# endif // TS_USE_TLS_SNI
