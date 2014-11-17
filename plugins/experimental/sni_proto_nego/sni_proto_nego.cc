#include <stdio.h>
#include <ts/ts.h>
#include <ts/apidefs.h>
#include <openssl/ssl.h>
#include <string>
#include <map>
#include <string.h>

using namespace std;

const char* PLUGIN_NAME = "sni_proto_nego";
const int MAX_BUFFER_SIZE = 1024;
const int MAX_FILE_PATH_SIZE = 1024;
const unsigned int MAX_PROTO_LIST_LEN = 100;
const unsigned int MAX_PROTO_NAME_LEN = 255;

typedef struct {
  bool enableNpn;
  unsigned int npn_proto_list_count;
  unsigned char npn_proto_list [MAX_PROTO_LIST_LEN] [MAX_PROTO_NAME_LEN];
} SNIProtoConfig;

typedef map<string, SNIProtoConfig> stringMap;
static  stringMap _sniProtoMap;

static
bool read_config(char* config_file) {
  char file_path[MAX_FILE_PATH_SIZE];
  TSFile file;
  if (config_file == NULL) {
    TSError("invalid config file");
    return false;
  }
  TSDebug(PLUGIN_NAME, "trying to open config file in this path: %s", file_path);
  file = TSfopen(config_file, "r");
  if (file == NULL) {
    snprintf(file_path, sizeof(file_path), "%s/%s", TSInstallDirGet(), config_file);
    file = TSfopen(file_path, "r");
    if (file == NULL) {
      TSError("Failed to open config file %s", config_file);
      return false;
    }
  }
  char buffer[MAX_BUFFER_SIZE];
  memset(buffer, 0, sizeof(buffer));
  while (TSfgets(file, buffer, sizeof(buffer) - 1) != NULL) {
    char *eol = 0;
    // make sure line was not bigger than buffer
    if ((eol = strchr(buffer, '\n')) == NULL && (eol = strstr(buffer, "\r\n")) == NULL) {
      TSError("sni_proto_nego line too long, did not get a good line in cfg, skipping, line: %s", buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    }
    // make sure line has something useful on it
    if (eol - buffer < 2 || buffer[0] == '#') {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }
    char* cfg = strtok(buffer, "\n\r\n");

    if (cfg != NULL) {
        TSDebug(PLUGIN_NAME, "setting SniProto based on string: %s", cfg);

        char* domain = strtok(buffer, " ");
        SNIProtoConfig sniProtoConfig = {1, 1};

        if (domain) {
          if ((*domain == '*') && (domain+1) && (*(domain+1)=='.')) {
            domain += 2;
            if (domain == NULL) {
              continue;
            }
          }
          char* sni_proto_config = strtok (NULL, " ");
          if (sni_proto_config) {
            sniProtoConfig.enableNpn = atoi(sni_proto_config);
            TSDebug(PLUGIN_NAME, "npn_proto_config %d", sniProtoConfig.enableNpn);
            sni_proto_config = strtok (NULL, " ");
            // now get the npn proto advertisment list
            sni_proto_config = strtok (NULL, " ");
            sniProtoConfig.npn_proto_list_count = 0;
            while (sni_proto_config != NULL) {
              char* proto = strtok(NULL, "|");
              if ((proto == NULL) ||
                  (sniProtoConfig.npn_proto_list_count >= MAX_PROTO_LIST_LEN) ||
                  (strlen(proto) >= MAX_PROTO_NAME_LEN)) {
                break;
              }
              _TSstrlcpy((char*)sniProtoConfig.npn_proto_list[sniProtoConfig.npn_proto_list_count++], proto, (strlen(proto) + 1));
            }
          }
          _sniProtoMap.insert(make_pair(domain, sniProtoConfig));
        }

        memset(buffer, 0, sizeof(buffer));
    }
  }

  TSfclose(file);

  TSDebug(PLUGIN_NAME, "Done parsing config");

  return true;
}


static void
init_sni_callback(void *sslNetVC)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(sslNetVC);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl = reinterpret_cast<SSL *>(sslobj);
  const char *serverName = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  SSL_CTX * ctx = SSL_get_SSL_CTX(ssl);

  if (serverName == NULL) {
    TSDebug(PLUGIN_NAME, "invalid ssl netVC %p, servername %s for ssl obj %p", sslNetVC, serverName, ssl);
    return;
  }

  TSDebug(PLUGIN_NAME, "ssl netVC %p, servername %s for ssl obj %p", sslNetVC, serverName, ssl);

  stringMap::iterator it; 
  it=_sniProtoMap.find(serverName);

  // check for wild-card domains
  if(it==_sniProtoMap.end()) {
    char* domain = strstr((char*)serverName, ".");
    if (domain && (domain+1)) {
      it=_sniProtoMap.find(domain+1);  
    }
  }

  if (it!=_sniProtoMap.end()) {
    SNIProtoConfig sniProtoConfig = it->second; 
    if (!sniProtoConfig.enableNpn) {
      TSDebug(PLUGIN_NAME, "disabling NPN for serverName %s", serverName);
      SSL_CTX_set_next_protos_advertised_cb(ctx, NULL, NULL);
    } else {
      TSDebug(PLUGIN_NAME, "setting NPN advertised list for %s", serverName);
      TSSslAdvertiseProtocolSet(ssl_vc, (const unsigned char **)sniProtoConfig.npn_proto_list, sniProtoConfig.npn_proto_list_count);
    }
  } else {
    TSDebug(PLUGIN_NAME, "setting NPN advertised list for %s", serverName);
    TSSslAdvertiseProtocolSet(ssl_vc, NULL, 0);
  }
}

int
SSLSniInitCallbackHandler(TSCont cont, TSEvent id, void* sslNetVC) {
  (void) cont;
  TSDebug(PLUGIN_NAME, "SSLSniInitCallbackHandler with id %d", id);
  switch (id) {
  case TS_SSL_SNI_HOOK:
      {
        init_sni_callback(sslNetVC);
      }
      break;

  default:
    TSDebug(PLUGIN_NAME, "Unexpected event %d", id);
    break;
  }

  return TS_EVENT_NONE;
}

void
TSPluginInit(int argc, const char *argv[])
{
  (void) argc;
  TSPluginRegistrationInfo info;

  info.plugin_name = (char *)("sni_proto_nego");
  info.vendor_name = (char *)("ats");

  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed.");
  }

  char* config_file = (char*)"conf/sni_proto_nego/sni_proto_nego.config";

  if (argc >= 2) {
    config_file = (char*)argv[1];
  }
  
  if (!read_config(config_file)) {
    TSDebug(PLUGIN_NAME, "nothing to do..");
    return;
  }

  TSCont cont = TSContCreate(SSLSniInitCallbackHandler, NULL);
  TSHttpHookAdd(TS_SSL_SNI_HOOK, cont);
}
