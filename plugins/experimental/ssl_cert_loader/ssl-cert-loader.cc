/** @file 
    SSL dynamic certificate loader
    Loads certificates into a hash table as they are requested
*/

# include <stdio.h>
# include <memory.h>
# include <inttypes.h>
# include <ts/ts.h>
# include <tsconfig/TsValue.h>
# include <openssl/ssl.h>
# include <openssl/x509.h>
# include <openssl/x509v3.h>
# include <ts/ink_inet.h>
# include <ts/IpMap.h>
# include "domain-tree.h"

using ts::config::Configuration;
using ts::config::Value;

# define PN "ssl-cert-loader"
# define PCP "[" PN " Plugin] "

namespace {

class CertLookup {
public:
  DomainNameTree tree;
  IpMap ipmap;
} Lookup;

class SslEntry {
public:
  SslEntry() : ctx(NULL), op(TS_SSL_HOOK_OP_DEFAULT) 
  { 
    this->mutex = TSMutexCreate();
  }
  ~SslEntry() {
  }
  SSL_CTX *ctx;
  TSSslVConnOp op;
  // If the CTX is not already created, use these
  // files to load things up
  std::string certFileName;
  std::string keyFileName; 
  TSMutex mutex;
  std::deque<TSVConn> waitingVConns;
};

std::string ConfigPath;
typedef std::pair<IpAddr, IpAddr> IpRange;
typedef std::deque<IpRange> IpRangeQueue;

Configuration Config;	// global configuration

void
Parse_Addr_String(ts::ConstBuffer const &text, IpRange &range) {
  IpAddr newAddr;
  std::string textstr(text._ptr, text._size);
  // Is there a hyphen?
  size_t hyphen_pos = textstr.find("-");
  if (hyphen_pos != std::string::npos) {
    std::string addr1 = textstr.substr(0, hyphen_pos);
    std::string addr2 = textstr.substr(hyphen_pos+1);
    range.first.load(ts::ConstBuffer(addr1.c_str(), addr1.length()));
    range.second.load(ts::ConstBuffer(addr2.c_str(), addr2.length()));
  }
  else { // Assume it is a single address
    newAddr.load(text);
    range.first = newAddr;
    range.second = newAddr; 
  }
}

int
Load_Config_File() {
  ts::Rv<Configuration> cv = Configuration::loadFromPath(ConfigPath.c_str());
  if (!cv.isOK()) {
    char error_buffer[1024];
    cv._errata.write(error_buffer, sizeof(error_buffer), 0, 0, 0, "");
    TSDebug("skh-cert","Failed to parse %s as TSConfig format", ConfigPath.c_str());
    TSError(PCP "Failed to parse %s as TSConfig format", ConfigPath.c_str());
    TSDebug("skh-cert", "Errors: %s", error_buffer);
    return -1;
  }
  Config = cv;
  return 1;
}

struct ParsedSslValues {
  std::string server_priv_key_file;
  std::string server_name;
  std::string server_cert_name;
  std::string action;
  IpRangeQueue server_ips;
};

void
Parse_Config_Rules(Value &parent, ParsedSslValues &orig_values);

int
Load_Configuration_Args(int argc, const char *argv[]) {
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
    static char const * const DEFAULT_CONFIG_PATH = "ssl_start.cfg";
    config_path = TSstrdup(DEFAULT_CONFIG_PATH);
    TSDebug(PN, "No config path set in arguments, using default: %s", DEFAULT_CONFIG_PATH);
  }

  // translate relative paths to absolute
  if (config_path[0] != '/') {
    ConfigPath = std::string(TSConfigDirGet()) + '/' + std::string(config_path);
  } else {
    ConfigPath = config_path;
  }

  TSDebug("skh-cert", "Load from %s", ConfigPath.c_str());
  // free up the path
  TSfree(config_path);
  return 0;
}

int
Load_Configuration() {
  int ret = Load_Config_File();
  if (ret != 0) {
    TSError(PCP "Failed to load the config file, check debug output for errata");
  }

  Value root = Config.getRoot();
  Value val = root["runtime-table-size"];
  if (val.isLiteral()) {
    // Not evicting yet
  }
  val = root["ssl-server-match"];
  if (val.isContainer()) {
    ParsedSslValues values;
    Parse_Config_Rules(val, values);
  }

  // Test values
  DomainNameTree::DomainNameNode *node = Lookup.tree.findFirstMatch("calendar.google.com");
  TSDebug("skh-cert", "Found node with key=%s and order=%d", node->key.c_str(), node->order);
  node = Lookup.tree.findFirstMatch("www.buseyil.com");
  TSDebug("skh-cert", "Found node with key=%s and order=%d", node->key.c_str(), node->order);

  IpAddr key_ip;
  key_ip.load(ts::ConstBuffer("107.23.60.186", strlen("107.23.60.186")));
  IpEndpoint key_endpoint;
  key_endpoint.assign(key_ip);
  void *payload;
  if (Lookup.ipmap.contains(&key_endpoint, &payload)) {
    TSDebug("skh-cert", "Found %p for 107.23.60.186", payload);
  }
  else {
    TSDebug("skh-cert", "Found nothing for 107.23.60.186");
  }

  return 0;
}

SSL_CTX *
Load_Certificate(SslEntry const *entry, std::deque<std::string> &names) {
  SSL_CTX *retval = SSL_CTX_new(SSLv23_client_method());
  X509* cert = NULL;
  if (entry->certFileName.length() > 0) {
    // Must load the cert file to fetch the names out later
    BIO *cert_bio = BIO_new_file(entry->certFileName.c_str(), "r");
    cert = PEM_read_bio_X509_AUX(cert_bio, NULL, NULL, NULL);
    BIO_free(cert_bio);

    if (SSL_CTX_use_certificate(retval, cert) < 1) {
      TSDebug("skh-cert", "Failed to load cert file %s", entry->certFileName.c_str());
      SSL_CTX_free(retval);
      return NULL;
    }
  }
  if (entry->keyFileName.length() > 0) {
    if (!SSL_CTX_use_PrivateKey_file(retval, entry->keyFileName.c_str(), SSL_FILETYPE_PEM)) {
      TSDebug("skh-cert", "Failed to load priv key file %s", entry->keyFileName.c_str());
      SSL_CTX_free(retval);
      return NULL;
    }
  }

  // Fetch out the names associated with the certificate
  if (cert != NULL) {
    X509_NAME *name = X509_get_subject_name(cert);
    char  subjectCn[256]; 
    if (X509_NAME_get_text_by_NID(name, NID_commonName, subjectCn, sizeof(subjectCn)) >= 0) {
      std::string tmp_name(subjectCn);
      names.push_back(tmp_name);
    }
    // Look for alt names
    GENERAL_NAMES *alt_names = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, NULL, NULL);
    if (alt_names) {
      unsigned count = sk_GENERAL_NAME_num(alt_names);
      for (unsigned i = 0; i < count; i++) {
        GENERAL_NAME *alt_name = sk_GENERAL_NAME_value(alt_names, i);
  
        if (alt_name->type == GEN_DNS) {
          // Current name is a DNS name, let's check it
          char *name_ptr = (char *) ASN1_STRING_data(alt_name->d.dNSName);
          std::string tmp_name(name_ptr);
          names.push_back(tmp_name);
        }
      } 
      sk_GENERAL_NAME_pop_free(alt_names, GENERAL_NAME_free);
    }
  }

  // Do we need to free cert? Did assigning to SSL_CTX increment its ref count
  return retval;
}

/*
 * Load the config information about the terminal config.
 * Only load the certificate if no server name or ip is specified
 */
SslEntry *
Load_Certificate_Entry(ParsedSslValues const &values, std::deque<std::string> &names) {
  SslEntry *retval = NULL; 
  std::string cert_file_path;
  std::string priv_file_path;
  retval = new SslEntry();
  if (values.server_cert_name.length() > 0) { 
    if (values.server_cert_name[0] != '/') {
      cert_file_path = std::string(TSConfigDirGet()) + '/' + values.server_cert_name; 
    }
    else { 
      cert_file_path = values.server_cert_name;
    }
    retval->certFileName = cert_file_path;
  }
  if (values.server_priv_key_file.length() > 0) {
    if (values.server_priv_key_file[0] != '/') {
      priv_file_path = std::string(TSConfigDirGet()) + '/' + values.server_priv_key_file; 
    }
    else { 
      priv_file_path = values.server_priv_key_file;
    }
    retval->keyFileName = priv_file_path;
  }
  // Must go ahead and load the cert to get the names
  if (values.server_name.length() == 0 &&
      values.server_ips.size() == 0) {
    retval->ctx = Load_Certificate(retval, names); 
  }
  if (values.action.length() > 0) {
    if (values.action == "tunnel") {
      retval->op = TS_SSL_HOOK_OP_TUNNEL;
    }
    else if (values.action == "teriminate") {
      retval-> op = TS_SSL_HOOK_OP_TERMINATE;
    }
  }
  return retval;
}

int Parse_order = 0;

void
Parse_Config(Value &parent, ParsedSslValues &orig_values) {
  ParsedSslValues cur_values(orig_values);
  Value val = parent.find("ssl-key-name");
  if (val.hasValue()) {
    cur_values.server_priv_key_file = std::string(val.getText()._ptr, val.getText()._size);
  }
  val = parent.find("server-ip");
  if (val) {
    IpRange ipRange;
    Parse_Addr_String(val.getText(), ipRange);
    cur_values.server_ips.push_back(ipRange);
  }
  val = parent.find("server-name");
  if (val) {
    cur_values.server_name = std::string(val.getText()._ptr, val.getText()._size);
  }
  val = parent.find("server-cert-name");
  if (val) {
    cur_values.server_cert_name = std::string(val.getText()._ptr, val.getText()._size);
  }
  val = parent.find("action");
  if (val) {
    cur_values.action = std::string(val.getText()._ptr, val.getText()._size);
  }

  val = parent.find("child-match");
  if (val) {
    Parse_Config_Rules(val, cur_values); 
  }
  else { // We are terminal, enter a match case
    TSDebug("skh-cert", "Terminal SSL Config: server_priv_key_file=%s server_name=%s server_cert_name=%s action=%s", 
      cur_values.server_priv_key_file.c_str(), 
      cur_values.server_name.c_str(), 
      cur_values.server_cert_name.c_str(), 
      cur_values.action.c_str() 
    );
    // Load the certificate and create a context if appropriate
    std::deque<std::string> cert_names;
    SslEntry *entry  = Load_Certificate_Entry(cur_values, cert_names);
    
    // Store in appropriate table
    if (cur_values.server_name.length() > 0) {
      Lookup.tree.insert(cur_values.server_name, entry, Parse_order++); 
    }
    if (cur_values.server_ips.size() > 0) {
      size_t i;
      for (i = 0; i < cur_values.server_ips.size(); i++) {
        IpEndpoint first, second;
        first.assign(cur_values.server_ips[i].first);
        second.assign(cur_values.server_ips[i].second);
        Lookup.ipmap.fill(&first, &second, entry);
        char val1[256], val2[256];
        cur_values.server_ips[i].first.toString(val1, sizeof(val1));
        cur_values.server_ips[i].second.toString(val2, sizeof(val2));
      }
    }
    if (entry != NULL) {
      size_t i;
      for (i = 0; i < cert_names.size(); i++) {
        Lookup.tree.insert(cert_names[i], entry, Parse_order++);
      }
    }
  }
}

void
Parse_Config_Rules(Value &parent, ParsedSslValues &orig_values) {
  size_t i;
  for (i = 0; i < parent.childCount(); i++) {
    Value child = parent[i];
    Parse_Config(child, orig_values);
  }
}

void *
Load_Certificate_Thread(void *arg) {
  SslEntry *entry = reinterpret_cast<SslEntry*>(arg);

  TSMutexLock(entry->mutex);
  if (entry->ctx == NULL) {
    // Must load certificate
    std::deque<std::string> cert_names;
    entry->ctx = Load_Certificate(entry, cert_names);
    while (entry->waitingVConns.begin() != entry->waitingVConns.end()) {
      TSVConn vc = entry->waitingVConns.back();
      entry->waitingVConns.pop_back();
      TSSslConnection sslobj = TSVConnSSLConnectionGet(vc);
      SSL *ssl = reinterpret_cast<SSL *>(sslobj);
      SSL_set_SSL_CTX(ssl, entry->ctx); 
      TSVConnReenable(vc);
    }
    TSMutexUnlock(entry->mutex);
    size_t i;
    for (i = 0; i < cert_names.size(); i++) {
      Lookup.tree.insert(cert_names[i], entry, Parse_order++);
    }
  }
  else {
    TSMutexUnlock(entry->mutex);
  }
  return (void *)1;
}

int
CB_Life_Cycle(TSCont , TSEvent , void *) {
  // By now the SSL library should have been initialized,
  // We can safely parse the config file and load the ctx tables
  Load_Configuration();
  return TS_SUCCESS;
}

int
CB_Pre_Accept(TSCont /*contp*/, TSEvent event, void *edata) {
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  IpAddr ip(TSNetVConnLocalAddrGet(ssl_vc));
  char buff[INET6_ADDRSTRLEN];
  IpAddr ip_client(TSNetVConnRemoteAddrGet(ssl_vc));
  char buff2[INET6_ADDRSTRLEN];

  TSDebug("skh-cert", "Pre accept callback %p - event is %s, target address %s, client address %s"
          , ssl_vc
          , event == TS_EVENT_VCONN_PRE_ACCEPT ? "good" : "bad"
          , ip.toString(buff, sizeof(buff))
          , ip_client.toString(buff2, sizeof(buff2))
    );

  // Is there a cert already defined for this IP?
  //
  IpEndpoint key_endpoint;
  key_endpoint.assign(ip);
  void *payload;
  if (Lookup.ipmap.contains(&key_endpoint, &payload)) {
    // Set the stored cert on this SSL object
    TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
    SSL *ssl = reinterpret_cast<SSL *>(sslobj);
    SslEntry *entry = reinterpret_cast<SslEntry *>(payload);
    TSMutexLock(entry->mutex);
    if (entry->op == TS_SSL_HOOK_OP_TUNNEL ||
        entry->op == TS_SSL_HOOK_OP_TERMINATE) {
      // Push everything to blind tunnel, or terminate
      if (entry->op == TS_SSL_HOOK_OP_TUNNEL) {
        TSVConnTunnel(ssl_vc);
      }
      TSMutexUnlock(entry->mutex);
    }
    else {
      if (entry->ctx == NULL) {
        if (entry->waitingVConns.begin() == entry->waitingVConns.end()) {
          entry->waitingVConns.push_back(ssl_vc);  
          TSMutexUnlock(entry->mutex);
    
          TSThreadCreate(Load_Certificate_Thread, entry);
        }
        else { // Just add yourself to the queue
          entry->waitingVConns.push_back(ssl_vc);  
          TSMutexUnlock(entry->mutex);
        }
        // Return before we reenable
        return TS_SUCCESS;
      }
      else { // if (entry->ctx != NULL) {
        SSL_set_SSL_CTX(ssl, entry->ctx); 
        TSDebug("skh-cert", "Replace cert based on IP");
        TSMutexUnlock(entry->mutex);
      }
    }
  }
  
  // All done, reactivate things
  TSVConnReenable(ssl_vc);
  return TS_SUCCESS;
}

int
CB_servername(TSCont /*contp*/, TSEvent /*event*/, void *edata) {
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  if (servername != NULL) {
    // Is there a certificated loaded up for this name
    DomainNameTree::DomainNameNode *node = Lookup.tree.findFirstMatch(servername);
    if (node != NULL && node->payload != NULL) {
      SslEntry *entry = reinterpret_cast<SslEntry *>(node->payload);
      if (entry->op == TS_SSL_HOOK_OP_TUNNEL ||
          entry->op == TS_SSL_HOOK_OP_TERMINATE) {
        // Push everything to blind tunnel
        if (entry->op == TS_SSL_HOOK_OP_TUNNEL) {
          TSVConnTunnel(ssl_vc);
        }
        // Make sure we stop out of the SNI callback
        // So return before re-enabling the SSL connection
        return TS_SUCCESS;
      }
      TSMutexLock(entry->mutex);
      if (entry->ctx == NULL) {
        // Spawn off a thread to load a potentially expensive certificate
        if (entry->waitingVConns.begin() == entry->waitingVConns.end()) {
          entry->waitingVConns.push_back(ssl_vc);  
          TSMutexUnlock(entry->mutex);
          TSThreadCreate(Load_Certificate_Thread, entry);
        }
        else { // Just add yourself to the queue
          entry->waitingVConns.push_back(ssl_vc);  
          TSMutexUnlock(entry->mutex);
        }
        // Won't reenable until the certificate has been loaded
        return TS_SUCCESS;
      }
      else { //if (entry->ctx != NULL) {
        SSL_set_SSL_CTX(ssl, entry->ctx); 
        TSDebug("skh-cert", "Replace cert based on name %s", servername);
      }
      TSMutexUnlock(entry->mutex);
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
  TSCont cb_pa = 0; // pre-accept callback continuation
  TSCont cb_lc = 0; // life cycle callback continuuation
  TSCont cb_sni = 0; // SNI callback continuuation

  info.plugin_name = const_cast<char*>("SSL Certificate Loader");
  info.vendor_name = const_cast<char*>("Network Geographics");
  info.support_email = const_cast<char*>("shinrich@network-geographics.com");

  if (TS_SUCCESS != TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError(PCP "registration failed.");
  } else if (TSTrafficServerVersionGetMajor() < 2) {
    TSError(PCP "requires Traffic Server 2.0 or later.");
  } else if (0 > Load_Configuration_Args(argc, argv)) {
    TSError(PCP "Failed to load config file.");
  } else if (0 == (cb_pa = TSContCreate(&CB_Pre_Accept, TSMutexCreate()))) {
    TSError(PCP "Failed to pre-accept callback.");
  } else if (0 == (cb_lc = TSContCreate(&CB_Life_Cycle, TSMutexCreate()))) {
    TSError(PCP "Failed to lifecycle callback.");
  } else if (0 == (cb_sni = TSContCreate(&CB_servername, TSMutexCreate()))) {
    TSError(PCP "Failed to create SNI callback.");
  } else {
    TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb_lc);
    TSHttpHookAdd(TS_VCONN_PRE_ACCEPT_HOOK, cb_pa);
    TSHttpHookAdd(TS_SSL_SNI_HOOK, cb_sni);
    success = true;
  }
 
  if (!success) {
    if (cb_pa) TSContDestroy(cb_pa);
    if (cb_lc) TSContDestroy(cb_lc);
    TSError(PCP "not initialized");
  }
  TSDebug(PN, "Plugin %s", success ? "online" : "offline");

  return;
}

