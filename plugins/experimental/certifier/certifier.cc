/** @certifier.cc
  This plugin performs two basic tasks:
  1) Loads SSL certificates from file storage on demand. The total number of loaded certificates kept in memory can be configured.
  2) (Optional) Generates SSL certificates on demand. Generated certificates are written to file storage for later retrieval.

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
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <getopt.h>

#include <sys/stat.h>

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/conf.h>
#include <openssl/x509v3.h>
#include <openssl/x509.h>
#include <openssl/rsa.h>
#include <openssl/md5.h>

#include <unordered_map> // cnDataMap
#include <queue>         // vconnQ
#include <string>        // std::string
#include <fstream>       // ofstream
#include <memory>
#include <algorithm>

#include "ts/ts.h"

const char *PLUGIN_NAME = "certifier";

/// Override default delete for unique ptrs to openSSL objects
namespace std
{
template <> struct default_delete<X509> {
  void
  operator()(X509 *n)
  {
    X509_free(n);
  }
};
template <> struct default_delete<X509_REQ> {
  void
  operator()(X509_REQ *n)
  {
    X509_REQ_free(n);
  }
};
template <> struct default_delete<EVP_PKEY> {
  void
  operator()(EVP_PKEY *n)
  {
    EVP_PKEY_free(n);
  }
};
template <> struct default_delete<SSL_CTX> {
  void
  operator()(SSL_CTX *n)
  {
    SSL_CTX_free(n);
  }
};
} // namespace std

/// Name aliases for unique pts to openSSL objects
using scoped_X509     = std::unique_ptr<X509>;
using scoped_X509_REQ = std::unique_ptr<X509_REQ>;
using scoped_EVP_PKEY = std::unique_ptr<EVP_PKEY>;
using scoped_SSL_CTX  = std::unique_ptr<SSL_CTX>;

class SslLRUList
{
private:
  struct SslData {
    std::queue<void *> vconnQ;    ///< Current queue of connections waiting for cert
    std::unique_ptr<SSL_CTX> ctx; ///< Context generated
    std::unique_ptr<X509> cert;   ///< Cert generated
    std::string commonName;       ///< SNI
    bool scheduled = false;       ///< If a TASK thread has been scheduled to generate cert
                                  ///< The first thread might fail to do so, this flag will help reschedule
    bool wontdo = false;          ///< if certs not on disk and dynamic gen is disabled
    /// Doubly Linked List pointers for LRU
    SslData *prev = nullptr;
    SslData *next = nullptr;

    SslData() {}
    ~SslData() { TSDebug(PLUGIN_NAME, "Deleting ssl data for [%s]", commonName.c_str()); }
  };

  using scoped_SslData = std::unique_ptr<SslLRUList::SslData>;

  // unordered_map is much faster in terms of insertion/lookup/removal
  // Althogh it uses more space than map, the time efficiency should be more important
  std::unordered_map<std::string, scoped_SslData> cnDataMap; ///< Map from CN to sslData
  TSMutex list_mutex;

  int size = 0;
  int limit;
  SslData *head = nullptr;
  SslData *tail = nullptr;

public:
  SslLRUList(int in_limit = 4096) : limit(in_limit) { list_mutex = TSMutexCreate(); }

  ~SslLRUList() { TSMutexDestroy(list_mutex); }

  // Returns valid ptr to SSL_CTX if successful lookup
  //         nullptr if not found and create SslData in the map
  SSL_CTX *
  lookup_and_create(const char *servername, void *edata, bool &wontdo)
  {
    SslData *ssl_data              = nullptr;
    scoped_SslData scoped_ssl_data = nullptr;
    SSL_CTX *ref_ctx               = nullptr;
    std::string commonName(servername);
    TSMutexLock(list_mutex);
    auto dataItr = cnDataMap.find(commonName);
    /// If such a context exists in dict
    if (dataItr != cnDataMap.end()) {
      /// Reuse context if already built, self queued if not
      if ((ssl_data = dataItr->second.get())->wontdo) {
        wontdo = true;
      } else if (ssl_data->ctx) {
        ref_ctx = ssl_data->ctx.get();
      } else {
        ssl_data->vconnQ.push(edata);
      }
    } else {
      /// Add a new ssl_data to dict if not exist
      scoped_ssl_data.reset(new SslData);
      ssl_data             = scoped_ssl_data.get();
      ssl_data->commonName = std::move(commonName);
      ssl_data->vconnQ.push(edata);

      cnDataMap[ssl_data->commonName] = std::move(scoped_ssl_data);
    }
    // With a valid sslData pointer
    if (ssl_data != nullptr) {
      // Add to the list and set scheduled flag
      prepend(ssl_data);
      if (ref_ctx == nullptr || !ssl_data->scheduled) {
        ssl_data->scheduled = true;
      }
    }
    TSMutexUnlock(list_mutex);
    return ref_ctx;
  }

  // Setup ssldata 1) ctx 2) cert 3) swapping queue
  // Ownership of unique pointers are transferred into this function
  // Then if the entry is found, the ownership is further transferred to the entry
  // if not, the objects are destroyed here. (As per design, this is caused by LRU management deleting oldest entry)
  void
  setup_data_ctx(const std::string &commonName, std::queue<void *> &localQ, std::unique_ptr<SSL_CTX> ctx,
                 std::unique_ptr<X509> cert, const bool &wontdo)
  {
    TSMutexLock(list_mutex);
    auto iter = cnDataMap.find(commonName);
    if (iter != cnDataMap.end()) {
      std::swap(localQ, iter->second->vconnQ);
      iter->second->ctx    = std::move(ctx);
      iter->second->cert   = std::move(cert); ///< We might not need cert, can be easily removed
      iter->second->wontdo = wontdo;
    }
    TSMutexUnlock(list_mutex);
  }

  // Prepend to the LRU list
  void
  prepend(SslData *data)
  {
    TSMutexLock(list_mutex);
    std::unique_ptr<SslData> local = nullptr;
    if (data != nullptr) {
      // If data is the most recent node in the list,
      // we leave it unchanged.
      if (head != data) {
        // Remove data from the list (does size decrement)
        remove_from_list(data);

        // Prepend to head
        data->prev = nullptr;
        data->next = head;
        if (data->next != nullptr) {
          data->next->prev = data;
        }
        head = data;
        if (tail == nullptr) {
          tail = data;
        }

        // Remove oldest node if size exceeds limit
        if (++size > limit) {
          TSDebug(PLUGIN_NAME, "Removing %s", tail->commonName.c_str());
          auto iter = cnDataMap.find(tail->commonName);
          if (iter != cnDataMap.end()) {
            local = std::move(iter->second); // copy ownership
            cnDataMap.erase(iter);
          }
          if ((tail = tail->prev) != nullptr) {
            tail->next = nullptr;
          }
          size -= 1;
        }
      }
    }
    TSDebug(PLUGIN_NAME, "%s Prepend to LRU list...List Size:%d Map Size: %d", data->commonName.c_str(), size,
            static_cast<int>(cnDataMap.size()));

    TSMutexUnlock(list_mutex);
  }

  // Remove list node
  void
  remove_from_list(SslData *data)
  {
    TSMutexLock(list_mutex);
    // If data and list are both valid
    if (data != nullptr) {
      // If data is linked in list
      if (data->prev != nullptr || data->next != nullptr || head == data) {
        if (data->prev != nullptr) {
          data->prev->next = data->next;
        }
        if (data->next != nullptr) {
          data->next->prev = data->prev;
        }
        if (head == data) {
          head = data->next;
        }
        if (tail == data) {
          tail = data->prev;
        }
        data->prev = nullptr;
        data->next = nullptr;
        size -= 1;
      }
    }
    TSMutexUnlock(list_mutex);
  }

  SslData *
  get_newest()
  {
    TSMutexLock(list_mutex);
    SslData *ret = head;
    TSMutexUnlock(list_mutex);
    return ret;
  }

  SslData *
  get_oldest()
  {
    TSMutexLock(list_mutex);
    SslData *ret = tail;
    TSMutexUnlock(list_mutex);
    return ret;
  }

  int
  get_size()
  {
    TSMutexLock(list_mutex);
    int ret = size;
    TSMutexUnlock(list_mutex);
    return ret;
  }

  // Set scheduled flag
  int
  set_schedule(const std::string &commonName, bool flag)
  {
    int ret = -1;
    TSMutexLock(list_mutex);
    auto iter = cnDataMap.find(commonName);
    if (iter != cnDataMap.end()) {
      iter->second->scheduled = flag;
      ret                     = 0;
    }
    TSMutexUnlock(list_mutex);
    return ret;
  }
};

// Flag for dynamic cert generation
static bool sign_enabled = false;

// Trusted CA private key and cert
static scoped_X509 ca_cert_scoped;
static scoped_EVP_PKEY ca_pkey_scoped;
// static scoped_EVP_PKEY  ts_pkey_scoped;

static int ca_serial;            ///< serial number
static std::fstream serial_file; ///< serial number file
static TSMutex serial_mutex;     ///< serial number mutex

// Management Object
static std::unique_ptr<SslLRUList> ssl_list = nullptr;
static std::string store_path;

/// Local helper function that generates a CSR based on common name
static scoped_X509_REQ
mkcsr(const char *cn)
{
  TSDebug(PLUGIN_NAME, "Entering mkcsr()...");
  X509_NAME *n;
  scoped_X509_REQ req;
  req.reset(X509_REQ_new());

  /// Set X509 version
  X509_REQ_set_version(req.get(), 1);

  /// Get handle to subject name
  n = X509_REQ_get_subject_name(req.get());

  /// Set common name field
  if (X509_NAME_add_entry_by_txt(n, "CN", MBSTRING_ASC, (unsigned char *)cn, -1, -1, 0) != 1) {
    TSError("[%s] mkcsr(): Failed to add entry.", PLUGIN_NAME);
    return nullptr;
  }
  /// Set Traffic Server public key
  if (X509_REQ_set_pubkey(req.get(), ca_pkey_scoped.get()) != 1) {
    TSError("[%s] mkcsr(): Failed to set pubkey.", PLUGIN_NAME);
    return nullptr;
  }
  /// Sign with Traffic Server private key
  if (X509_REQ_sign(req.get(), ca_pkey_scoped.get(), EVP_sha256()) <= 0) {
    TSError("[%s] mkcsr(): Failed to Sign.", PLUGIN_NAME);
    return nullptr;
  }
  return req;
}

/// Local helper function that generates a X509 certificate based on CSR
static scoped_X509
mkcrt(X509_REQ *req, int serial)
{
  TSDebug(PLUGIN_NAME, "Entering mkcrt()...");
  X509_NAME *subj, *tmpsubj;
  scoped_EVP_PKEY pktmp;
  scoped_X509 cert;

  cert.reset(X509_new());

  /// Set X509V3
  if (X509_set_version(cert.get(), 2) == 0) {
    TSError("[%s] mkcrt(): Failed to set X509V3.", PLUGIN_NAME);
    return nullptr;
  }

  /// Set serial number
  // TSDebug("txn_monitor", "serial: %d", serial);
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial);

  /// Set issuer from CA cert
  if (X509_set_issuer_name(cert.get(), X509_get_subject_name(ca_cert_scoped.get())) == 0) {
    TSError("[%s] mkcrt(): Failed to set issuer.", PLUGIN_NAME);
    return nullptr;
  }
  /// Set certificate time
  X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), (long)3650 * 24 * 3600);

  /// Get a handle to csr subject name
  subj = X509_REQ_get_subject_name(req);
  if ((tmpsubj = X509_NAME_dup(subj)) == nullptr) {
    TSDebug(PLUGIN_NAME, "mkcrt(): Failed to duplicate subject name.");
    return nullptr;
  }
  if ((X509_set_subject_name(cert.get(), tmpsubj)) == 0) {
    TSDebug(PLUGIN_NAME, "mkcrt(): Failed to set X509 subject name");
    X509_NAME_free(tmpsubj); ///< explicit call to free X509_NAME object
    return nullptr;
  }
  pktmp.reset(X509_REQ_get_pubkey(req));
  if (pktmp == nullptr) {
    TSDebug(PLUGIN_NAME, "mkcrt(): Failed to get CSR public key.");
    X509_NAME_free(tmpsubj);
    return nullptr;
  }
  if (X509_set_pubkey(cert.get(), pktmp.get()) == 0) {
    TSDebug(PLUGIN_NAME, "mkcrt(): Failed to set X509 public key.");
    X509_NAME_free(tmpsubj);
    return nullptr;
  }

  X509_sign(cert.get(), ca_pkey_scoped.get(), EVP_sha256());

  return cert;
}

static int
shadow_cert_generator(TSCont contp, TSEvent event, void *edata)
{
  const char *servername = reinterpret_cast<const char *>(TSContDataGet(contp));
  std::string commonName(servername);

  std::queue<void *> localQ;
  SSL_CTX *ref_ctx;
  scoped_SSL_CTX ctx;
  scoped_X509_REQ req;
  scoped_X509 cert;

  /// Calculate hash and path, try certs on disk first
  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5(reinterpret_cast<unsigned char const *>(commonName.data()), commonName.length(), digest);
  char md5String[5];
  sprintf(md5String, "%02hhx%02hhx", digest[0], digest[1]);
  std::string path          = store_path + "/" + std::string(md5String, 3);
  std::string cert_filename = path + '/' + commonName + ".crt";

  struct stat st;
  FILE *fp = nullptr;
  /// If directory doesn't exist, creat one
  if (stat(path.c_str(), &st) == -1) {
    mkdir(path.c_str(), 0755);
  } else {
    /// Try open the file if directory exists
    fp = fopen(cert_filename.c_str(), "rt");
  }
  TSDebug(PLUGIN_NAME, "shadow_cert_generator(): Cert file is expected at %s", cert_filename.c_str());
  /// If cert file exists and is readable
  if (fp != nullptr) {
    cert.reset(PEM_read_X509(fp, nullptr, nullptr, nullptr));
    fclose(fp);

    if (cert == nullptr) {
      /// Problem with cert file / openssl read
      TSError("[%s] [shadow_cert_generator] Problem with loading certs", PLUGIN_NAME);
      std::remove(cert_filename.c_str());
    } else {
      TSDebug(PLUGIN_NAME, "shadow_cert_generator(): Loaded cert from file");
    }
  }

  /// No valid certs available from disk, create one and write to file
  if (cert == nullptr) {
    if (!sign_enabled) {
      TSDebug(PLUGIN_NAME, "shadow_cert_generator(): No certs found and dynamic generation disabled. Marked as wontdo.");
      // There won't be certs avaiable. Mark this servername as wontdo
      // Pass on as if plugin doesn't exist
      ssl_list->setup_data_ctx(commonName, localQ, nullptr, nullptr, true);
      while (!localQ.empty()) {
        // TSDebug(PLUGIN_NAME, "\tClearing the queue size %lu", localQ.size());
        TSVConn ssl_vc = reinterpret_cast<TSVConn>(localQ.front());
        localQ.pop();
        TSVConnReenable(ssl_vc);
      }
      TSContDestroy(contp);
      return TS_SUCCESS;
    }
    TSDebug(PLUGIN_NAME, "shadow_cert_generator(): Creating shadow certs");

    /// Get serial number
    TSMutexLock(serial_mutex);
    int serial = ca_serial++;

    /// Write to serial file with lock held
    if (serial_file) {
      serial_file.seekp(0, serial_file.beg); ///< Reset to beginning fo file
      serial_file << serial << "\n";
    }

    TSMutexUnlock(serial_mutex);

    /// Create CSR and cert
    req = mkcsr(commonName.c_str());
    if (req == nullptr) {
      TSDebug(PLUGIN_NAME, "[shadow_cert_generator] CSR generation failed");
      TSContDestroy(contp);
      ssl_list->set_schedule(commonName, false);
      return TS_ERROR;
    }

    cert = mkcrt(req.get(), serial);

    if (cert == nullptr) {
      TSDebug(PLUGIN_NAME, "[shadow_cert_generator] Cert generation failed");
      TSContDestroy(contp);
      ssl_list->set_schedule(commonName, false);
      return TS_ERROR;
    }

    /// Write certs to file
    if ((fp = fopen(cert_filename.c_str(), "w+")) == nullptr) {
      TSDebug(PLUGIN_NAME, "shadow_cert_generator(): Error opening file: %s\n", strerror(errno));
    } else {
      if (!PEM_write_X509(fp, cert.get())) {
        TSDebug(PLUGIN_NAME, "shadow_cert_generator(): Error writing cert to disk");
      }
      fclose(fp);
    }
  }

  /// Create SSL context based on cert
  ref_ctx = SSL_CTX_new(SSLv23_server_method());
  ctx.reset(ref_ctx);

  if (SSL_CTX_use_certificate(ref_ctx, cert.get()) < 1) {
    TSError("[%s] shadow_cert_handler(): Failed to use certificate in SSL_CTX.", PLUGIN_NAME);
    TSContDestroy(contp);
    ssl_list->set_schedule(commonName, false);
    return TS_ERROR;
  }
  if (SSL_CTX_use_PrivateKey(ref_ctx, ca_pkey_scoped.get()) < 1) {
    TSError("[%s] shadow_cert_handler(): Failed to use private key in SSL_CTX.", PLUGIN_NAME);
    TSContDestroy(contp);
    ssl_list->set_schedule(commonName, false);
    return TS_ERROR;
  }
  TSDebug(PLUGIN_NAME, "shadow_cert_generator(): cert and context ready, clearing the queue");
  ssl_list->setup_data_ctx(commonName, localQ, std::move(ctx), std::move(cert), false);

  /// Clear the queue by setting context for each and reenable them
  while (!localQ.empty()) {
    TSDebug(PLUGIN_NAME, "\tClearing the queue size %lu", localQ.size());
    TSVConn ssl_vc = reinterpret_cast<TSVConn>(localQ.front());
    localQ.pop();
    TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
    SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
    SSL_set_SSL_CTX(ssl, ref_ctx);
    TSVConnReenable(ssl_vc);
  }

  TSContDestroy(contp);
  return TS_SUCCESS;
}

/// Callback at TS_SSL_CERT_HOOK, generate/look up shadow certificates based on SNI/FQDN
static int
cert_retriever(TSCont contp, TSEvent event, void *edata)
{
  TSVConn ssl_vc         = reinterpret_cast<TSVConn>(edata);
  TSSslConnection sslobj = TSVConnSSLConnectionGet(ssl_vc);
  SSL *ssl               = reinterpret_cast<SSL *>(sslobj);
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  SSL_CTX *ref_ctx       = nullptr;

  if (servername == nullptr) {
    TSError("[%s] cert_retriever(): No SNI available.", PLUGIN_NAME);
    return TS_ERROR;
  }
  bool wontdo = false;
  ref_ctx     = ssl_list->lookup_and_create(servername, edata, wontdo);
  if (wontdo) {
    TSDebug(PLUGIN_NAME, "cert_retriever(): Won't generate cert for %s", servername);
    TSVConnReenable(ssl_vc);
  } else if (nullptr == ref_ctx) {
    // If no existing context, schedule TASK thread to generate
    TSDebug(PLUGIN_NAME, "cert_retriever(): schedule thread to generate/retrieve cert for %s", servername);
    TSCont schedule_cont = TSContCreate(shadow_cert_generator, TSMutexCreate());
    TSContDataSet(schedule_cont, (void *)servername);
    TSContScheduleOnPool(schedule_cont, 0, TS_THREAD_POOL_TASK);
  } else {
    // Use existing context
    TSDebug(PLUGIN_NAME, "cert_retriever(): Reuse existing cert and context for %s", servername);
    SSL_set_SSL_CTX(ssl, ref_ctx);
    TSVConnReenable(ssl_vc);
  }

  /// For scheduled connections, the schduled continuation will handle the reenabling
  return TS_SUCCESS;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "initializing plugin");
  // Initialization data and callback
  TSPluginRegistrationInfo info;
  TSCont cb_shadow   = nullptr;
  info.plugin_name   = "certifier";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  const char *key    = nullptr;
  const char *cert   = nullptr;
  const char *serial = nullptr;

  // Read options from plugin.config
  static const struct option longopts[] = {
    {"sign-cert", required_argument, nullptr, 'c'},   {"sign-key", required_argument, nullptr, 'k'},
    {"sign-serial", required_argument, nullptr, 'r'}, {"max", required_argument, nullptr, 'm'},
    {"store", required_argument, nullptr, 's'},       {nullptr, no_argument, nullptr, 0}};

  int opt = 0;

  while (opt >= 0) {
    opt = getopt_long(argc, (char *const *)argv, "c:k:r:m:s:", longopts, nullptr);
    switch (opt) {
    case 'c': {
      cert = optarg;
      break;
    }
    case 'k': {
      key = optarg;
      break;
    }
    case 'r': {
      serial = optarg;
      break;
    }
    case 'm': {
      ssl_list.reset(new SslLRUList(static_cast<int>(std::strtol(optarg, nullptr, 0))));
      break;
    }
    case 's': {
      store_path = std::string(optarg);
      break;
    }
    case -1:
    case '?':
      break;
    default:
      TSDebug(PLUGIN_NAME, "Unexpected options.");
      TSError("[%s] Unexpected options error.", PLUGIN_NAME);
      return;
    }
  }

  // Register plugin and create callback
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to register plugin.", PLUGIN_NAME);
  } else if ((cb_shadow = TSContCreate(cert_retriever, nullptr)) == nullptr) {
    TSError("[%s] Unable to initialize plugin (disabled). Failed to create shadow cert cb.", PLUGIN_NAME);
  } else {
    if ((sign_enabled = cert && key && serial)) {
      // Dynamic cert generation enabled. Initialize CA key, cert and serial
      // To comply to openssl, key and cert file are opened as FILE*
      FILE *fp = nullptr;
      if ((fp = fopen(cert, "rt")) == nullptr) {
        TSDebug(PLUGIN_NAME, "fopen() error is %d: %s for %s", errno, strerror(errno), cert);
        TSError("[%s] Unable to initialize plugin. Failed to open ca cert.", PLUGIN_NAME);
        return;
      }
      ca_cert_scoped.reset(PEM_read_X509(fp, nullptr, nullptr, nullptr));
      fclose(fp);

      if ((fp = fopen(key, "rt")) == nullptr) {
        TSDebug(PLUGIN_NAME, "fopen() error is %d: %s for %s", errno, strerror(errno), key);
        TSError("[%s] Unable to initialize plugin. Failed to open ca key.", PLUGIN_NAME);
        return;
      }
      ca_pkey_scoped.reset(PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr));
      fclose(fp);

      if (ca_pkey_scoped == nullptr || ca_cert_scoped == nullptr) {
        TSDebug(PLUGIN_NAME, "PEM_read failed to read %s %s", ca_pkey_scoped ? "" : "pkey", ca_cert_scoped ? "" : "cert");
        TSError("[%s] Unable to initialize plugin. Failed to read ca key/cert.", PLUGIN_NAME);
        return;
      }

      // Read serial file
      serial_file.open(serial, std::fstream::in | std::fstream::out);
      if (!serial_file.is_open()) {
        TSDebug(PLUGIN_NAME, "Failed to open serial file.");
        TSError("[%s] Unable to initialize plugin. Failed to open serial.", PLUGIN_NAME);
        return;
      }
      /// Initialize mutex and serial number
      serial_mutex = TSMutexCreate();
      ca_serial    = 0;

      serial_file.seekg(0, serial_file.beg);
      serial_file >> ca_serial;
      if (serial_file.bad() || serial_file.fail()) {
        ca_serial = 0;
      }
    }
    TSDebug(PLUGIN_NAME, "Dynamic cert generation %s", sign_enabled ? "enabled" : "disabled");

    /// Add global hooks
    TSHttpHookAdd(TS_SSL_CERT_HOOK, cb_shadow);
  }

  return;
}
