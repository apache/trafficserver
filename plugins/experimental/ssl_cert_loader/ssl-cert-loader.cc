/** @file

    SSL dynamic certificate loader
    Loads certificates into a hash table as they are requested

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
#include <tsconfig/TsValue.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <getopt.h>
#include "domain-tree.h"

#include "ts/ink_inet.h"
#include "ts/ink_config.h"
#include "ts/IpMap.h"

using ts::config::Configuration;
using ts::config::Value;

#define PN "ssl-cert-loader"
#define PCP "[" PN " Plugin] "

namespace
{
class CertLookup
{
public:
  DomainNameTree tree;
  IpMap ipmap;
} Lookup;

typedef enum {
  SSL_HOOK_OP_DEFAULT,                     ///< Null / initialization value. Do normal processing.
  SSL_HOOK_OP_TUNNEL,                      ///< Switch to blind tunnel
  SSL_HOOK_OP_TERMINATE,                   ///< Termination connection / transaction.
  SSL_HOOK_OP_LAST = SSL_HOOK_OP_TERMINATE ///< End marker value.
} SslVConnOp;

class SslEntry
{
public:
  SslEntry() : ctx(nullptr), op(SSL_HOOK_OP_DEFAULT) { this->mutex = TSMutexCreate(); }
  ~SslEntry() {}
  SSL_CTX *ctx;
  SslVConnOp op;
  // If the CTX is not already created, use these
  // files to load things up
  std::string certFileName;
  std::string keyFileName;
  TSMutex mutex;
  std::deque<TSVConn> waitingVConns;
};

std::string ConfigPath;
typedef std::pair<IpAddr, IpAddr> IpRange;
using IpRangeQueue = std::deque<IpRange>;

Configuration Config; // global configuration

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
Load_Config_File()
{
  ts::Rv<Configuration> cv = Configuration::loadFromPath(ConfigPath.c_str());

  if (!cv.isOK()) {
    char error_buffer[1024];

    cv._errata.write(error_buffer, sizeof(error_buffer), 0, 0, 0, "");
    TSDebug(PN, "Failed to parse %s as TSConfig format", ConfigPath.c_str());
    TSError(PCP "Failed to parse %s as TSConfig format", ConfigPath.c_str());
    TSDebug(PN, "Errors: %s", error_buffer);
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

void Parse_Config_Rules(Value &parent, ParsedSslValues &orig_values);

int
Load_Configuration()
{
  int ret = Load_Config_File();

  if (ret != 0) {
    TSError(PCP "Failed to load the config file, check debug output for errata");
  }

  Value root = Config.getRoot();
  Value val  = root["runtime-table-size"];
  if (val.isLiteral()) {
    // Not evicting yet
  }
  val = root["ssl-server-match"];
  if (val.isContainer()) {
    ParsedSslValues values;
    Parse_Config_Rules(val, values);
  }

  return 0;
}

SSL_CTX *
Load_Certificate(SslEntry const *entry, std::deque<std::string> &names)
{
  SSL_CTX *retval = SSL_CTX_new(SSLv23_client_method());
  X509 *cert      = nullptr;

  if (entry->certFileName.length() > 0) {
    // Must load the cert file to fetch the names out later
    BIO *cert_bio = BIO_new_file(entry->certFileName.c_str(), "r");
    cert          = PEM_read_bio_X509_AUX(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);

    if (SSL_CTX_use_certificate(retval, cert) < 1) {
      TSDebug(PN, "Failed to load cert file %s", entry->certFileName.c_str());
      SSL_CTX_free(retval);
      return nullptr;
    }
  }
  if (entry->keyFileName.length() > 0) {
    if (!SSL_CTX_use_PrivateKey_file(retval, entry->keyFileName.c_str(), SSL_FILETYPE_PEM)) {
      TSDebug(PN, "Failed to load priv key file %s", entry->keyFileName.c_str());
      SSL_CTX_free(retval);
      return nullptr;
    }
  }

  // Fetch out the names associated with the certificate
  if (cert != nullptr) {
    X509_NAME *name = X509_get_subject_name(cert);
    char subjectCn[256];

    if (X509_NAME_get_text_by_NID(name, NID_commonName, subjectCn, sizeof(subjectCn)) >= 0) {
      std::string tmp_name(subjectCn);
      names.push_back(tmp_name);
    }
    // Look for alt names
    GENERAL_NAMES *alt_names = (GENERAL_NAMES *)X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr);
    if (alt_names) {
      unsigned count = sk_GENERAL_NAME_num(alt_names);
      for (unsigned i = 0; i < count; i++) {
        GENERAL_NAME *alt_name = sk_GENERAL_NAME_value(alt_names, i);

        if (alt_name->type == GEN_DNS) {
          // Current name is a DNS name, let's check it
          char *name_ptr = (char *)ASN1_STRING_get0_data(alt_name->d.dNSName);
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
Load_Certificate_Entry(ParsedSslValues const &values, std::deque<std::string> &names)
{
  SslEntry *retval = nullptr;
  std::string cert_file_path;
  std::string priv_file_path;

  retval = new SslEntry();
  if (values.server_cert_name.length() > 0) {
    if (values.server_cert_name[0] != '/') {
      cert_file_path = std::string(TSConfigDirGet()) + '/' + values.server_cert_name;
    } else {
      cert_file_path = values.server_cert_name;
    }
    retval->certFileName = cert_file_path;
  }
  if (values.server_priv_key_file.length() > 0) {
    if (values.server_priv_key_file[0] != '/') {
      priv_file_path = std::string(TSConfigDirGet()) + '/' + values.server_priv_key_file;
    } else {
      priv_file_path = values.server_priv_key_file;
    }
    retval->keyFileName = priv_file_path;
  }
  // Must go ahead and load the cert to get the names
  if (values.server_name.length() == 0 && values.server_ips.size() == 0) {
    retval->ctx = Load_Certificate(retval, names);
  }
  if (values.action.length() > 0) {
    if (values.action == "tunnel") {
      retval->op = SSL_HOOK_OP_TUNNEL;
    } else if (values.action == "teriminate") {
      retval->op = SSL_HOOK_OP_TERMINATE;
    }
  }

  return retval;
}

int Parse_order = 0;

void
Parse_Config(Value &parent, ParsedSslValues &orig_values)
{
  ParsedSslValues cur_values(orig_values);
  Value val = parent.find("ssl-key-name");

  if (val.hasValue()) {
    cur_values.server_priv_key_file = std::string(val.getText()._ptr, val.getText()._size);
  }
  val = parent.find("server-ip");
  if (val) {
    IpRange ipRange;
    auto txt = val.getText();

    Parse_Addr_String(std::string_view(txt._ptr, txt._size), ipRange);
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
  } else { // We are terminal, enter a match case
    TSDebug(PN, "Terminal SSL Config: server_priv_key_file=%s server_name=%s server_cert_name=%s action=%s",
            cur_values.server_priv_key_file.c_str(), cur_values.server_name.c_str(), cur_values.server_cert_name.c_str(),
            cur_values.action.c_str());
    // Load the certificate and create a context if appropriate
    std::deque<std::string> cert_names;
    SslEntry *entry = Load_Certificate_Entry(cur_values, cert_names);

    if (entry) {
      bool inserted = false;

      // Store in appropriate table
      if (cur_values.server_name.length() > 0) {
        Lookup.tree.insert(cur_values.server_name, entry, Parse_order++);
        inserted = true;
      }
      if (cur_values.server_ips.size() > 0) {
        for (auto &server_ip : cur_values.server_ips) {
          IpEndpoint first, second;
          first.assign(server_ip.first);
          second.assign(server_ip.second);
          Lookup.ipmap.fill(&first, &second, entry);
          char val1[256], val2[256];
          server_ip.first.toString(val1, sizeof(val1));
          server_ip.second.toString(val2, sizeof(val2));
          inserted = true;
        }
      }

      if (cert_names.size() > 0) {
        for (const auto &cert_name : cert_names) {
          Lookup.tree.insert(cert_name, entry, Parse_order++);
          inserted = true;
        }
      }

      if (!inserted) {
        delete entry;
        TSError(PCP "cert_names is empty and entry not otherwise inserted!");
      }
    } else {
      TSError(PCP "failed to load the certificate entry");
    }
  }
}

void
Parse_Config_Rules(Value &parent, ParsedSslValues &orig_values)
{
  for (size_t i = 0; i < parent.childCount(); i++) {
    Value child = parent[i];
    Parse_Config(child, orig_values);
  }
}

void *
Load_Certificate_Thread(void *arg)
{
  SslEntry *entry = reinterpret_cast<SslEntry *>(arg);

  TSMutexLock(entry->mutex);
  if (entry->ctx == nullptr) {
    // Must load certificate
    std::deque<std::string> cert_names;

    entry->ctx = Load_Certificate(entry, cert_names);
    while (entry->waitingVConns.begin() != entry->waitingVConns.end()) {
      TSVConn vc = entry->waitingVConns.back();
      entry->waitingVConns.pop_back();
      TSSslConnection sslobj = TSVConnSSLConnectionGet(vc);
      SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
      SSL_set_SSL_CTX(ssl, entry->ctx);
      TSVConnReenable(vc);
    }
    TSMutexUnlock(entry->mutex);
    for (const auto &cert_name : cert_names) {
      Lookup.tree.insert(cert_name, entry, Parse_order++);
    }
  } else {
    TSMutexUnlock(entry->mutex);
  }

  return (void *)1;
}

int
CB_Life_Cycle(TSCont, TSEvent, void *)
{
  // By now the SSL library should have been initialized,
  // We can safely parse the config file and load the ctx tables
  Load_Configuration();

  return TS_SUCCESS;
}

int
CB_Pre_Accept(TSCont /*contp*/, TSEvent event, void *edata)
{
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);
  IpAddr ip(TSNetVConnLocalAddrGet(ssl_vc));
  char buff[INET6_ADDRSTRLEN];
  IpAddr ip_client(TSNetVConnRemoteAddrGet(ssl_vc));
  char buff2[INET6_ADDRSTRLEN];

  TSDebug(PN, "Pre accept callback %p - event is %s, target address %s, client address %s", ssl_vc,
          event == TS_EVENT_VCONN_START ? "good" : "bad", ip.toString(buff, sizeof(buff)),
          ip_client.toString(buff2, sizeof(buff2)));

  // Is there a cert already defined for this IP?
  //
  IpEndpoint key_endpoint;
  key_endpoint.assign(ip);
  void *payload;
  if (Lookup.ipmap.contains(&key_endpoint, &payload)) {
    // Set the stored cert on this SSL object
    TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    SslEntry *entry        = reinterpret_cast<SslEntry *>(payload);
    TSMutexLock(entry->mutex);
    if (entry->op == SSL_HOOK_OP_TUNNEL || entry->op == SSL_HOOK_OP_TERMINATE) {
      // Push everything to blind tunnel, or terminate
      if (entry->op == SSL_HOOK_OP_TUNNEL) {
        TSVConnTunnel(ssl_vc);
      }
      TSMutexUnlock(entry->mutex);
    } else {
      if (entry->ctx == nullptr) {
        if (entry->waitingVConns.begin() == entry->waitingVConns.end()) {
          entry->waitingVConns.push_back(ssl_vc);
          TSMutexUnlock(entry->mutex);

          TSThreadCreate(Load_Certificate_Thread, entry);
        } else { // Just add yourself to the queue
          entry->waitingVConns.push_back(ssl_vc);
          TSMutexUnlock(entry->mutex);
        }
        // Return before we reenable
        return TS_SUCCESS;
      } else { // if (entry->ctx != NULL) {
        SSL_set_SSL_CTX(ssl, entry->ctx);
        TSDebug(PN, "Replace cert based on IP");
        TSMutexUnlock(entry->mutex);
      }
    }
  }

  // All done, reactivate things
  TSVConnReenable(ssl_vc);

  return TS_SUCCESS;
}

int
CB_servername(TSCont /*contp*/, TSEvent /*event*/, void *edata)
{
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);

  TSDebug(PN, "SNI callback %s", servername);
  if (servername != nullptr) {
    // Is there a certificated loaded up for this name
    DomainNameTree::DomainNameNode *node = Lookup.tree.findFirstMatch(servername);
    if (node != nullptr && node->payload != nullptr) {
      SslEntry *entry = reinterpret_cast<SslEntry *>(node->payload);
      if (entry->op == SSL_HOOK_OP_TUNNEL || entry->op == SSL_HOOK_OP_TERMINATE) {
        // Push everything to blind tunnel
        if (entry->op == SSL_HOOK_OP_TUNNEL) {
          TSVConnTunnel(ssl_vc);
        }
        // Make sure we stop out of the SNI callback
        // So return before re-enabling the SSL connection
        return TS_SUCCESS;
      }
      TSMutexLock(entry->mutex);
      if (entry->ctx == nullptr) {
        // Spawn off a thread to load a potentially expensive certificate
        if (entry->waitingVConns.begin() == entry->waitingVConns.end()) {
          entry->waitingVConns.push_back(ssl_vc);
          TSMutexUnlock(entry->mutex);
          TSThreadCreate(Load_Certificate_Thread, entry);
        } else { // Just add yourself to the queue
          entry->waitingVConns.push_back(ssl_vc);
          TSMutexUnlock(entry->mutex);
        }
        // Won't reenable until the certificate has been loaded
        return TS_SUCCESS;
      } else { // if (entry->ctx != NULL) {
        SSL_set_SSL_CTX(ssl, entry->ctx);
        TSDebug(PN, "Replace cert based on name %s", servername);
      }
      TSMutexUnlock(entry->mutex);
    }
  }
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
  TSPluginRegistrationInfo info;
  TSCont cb_pa                         = nullptr; // pre-accept callback continuation
  TSCont cb_lc                         = nullptr; // life cycle callback continuuation
  TSCont cb_sni                        = nullptr; // SNI callback continuuation
  static const struct option longopt[] = {
    {const_cast<char *>("config"), required_argument, nullptr, 'c'},
    {nullptr, no_argument, nullptr, '\0'},
  };

  info.plugin_name   = const_cast<char *>("SSL Certificate Loader");
  info.vendor_name   = const_cast<char *>("Network Geographics");
  info.support_email = const_cast<char *>("shinrich@network-geographics.com");

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
    static const char *const DEFAULT_CONFIG_PATH = "ssl_start.cfg";
    ConfigPath                                   = std::string(TSConfigDirGet()) + '/' + std::string(DEFAULT_CONFIG_PATH);
    TSDebug(PN, "No config path set in arguments, using default: %s", DEFAULT_CONFIG_PATH);
  }

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError(PCP "registration failed");
  } else if (TSTrafficServerVersionGetMajor() < 5) {
    TSError(PCP "requires Traffic Server 5.0 or later");
  } else if (nullptr == (cb_pa = TSContCreate(&CB_Pre_Accept, TSMutexCreate()))) {
    TSError(PCP "Failed to pre-accept callback");
  } else if (nullptr == (cb_lc = TSContCreate(&CB_Life_Cycle, TSMutexCreate()))) {
    TSError(PCP "Failed to lifecycle callback");
  } else if (nullptr == (cb_sni = TSContCreate(&CB_servername, TSMutexCreate()))) {
    TSError(PCP "Failed to create SNI callback");
  } else {
    TSLifecycleHookAdd(TS_LIFECYCLE_PORTS_INITIALIZED_HOOK, cb_lc);
    TSHttpHookAdd(TS_VCONN_START_HOOK, cb_pa);
    TSHttpHookAdd(TS_SSL_SNI_HOOK, cb_sni);
    success = true;
  }

  if (!success) {
    if (cb_pa) {
      TSContDestroy(cb_pa);
    }
    if (cb_lc) {
      TSContDestroy(cb_lc);
    }
    TSError(PCP "not initialized");
  }
  TSDebug(PN, "Plugin %s", success ? "online" : "offline");

  return;
}
