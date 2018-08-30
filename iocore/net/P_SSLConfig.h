/** @file

  A brief file description

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

/*************************** -*- Mod: C++ -*- ******************************
  P_SSLConfig.h
   Created On      : 07/20/2000

   Description:
   SSL Configurations
 ****************************************************************************/
#pragma once

#include "ProxyConfig.h"
#include "SSLSessionCache.h"
#include "tscore/ink_inet.h"
#include <openssl/rand.h>
#include "P_SSLCertLookup.h"

struct SSLCertLookup;
struct ssl_ticket_key_block;
/////////////////////////////////////////////////////////////
//
// struct SSLConfigParams
//
// configuration parameters as they apear in the global
// configuration file.
/////////////////////////////////////////////////////////////

typedef void (*init_ssl_ctx_func)(void *, bool);
typedef void (*load_ssl_file_func)(const char *, unsigned int);

struct SSLConfigParams : public ConfigInfo {
  enum SSL_SESSION_CACHE_MODE {
    SSL_SESSION_CACHE_MODE_OFF                 = 0,
    SSL_SESSION_CACHE_MODE_SERVER_OPENSSL_IMPL = 1,
    SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL     = 2
  };

  SSLConfigParams();
  ~SSLConfigParams() override;

  char *serverCertPathOnly;
  char *serverCertChainFilename;
  char *serverKeyPathOnly;
  char *serverCACertFilename;
  char *serverCACertPath;
  char *configFilePath;
  char *dhparamsFile;
  char *cipherSuite;
  char *client_cipherSuite;
  int configExitOnLoadError;
  int clientCertLevel;
  int verify_depth;
  int ssl_session_cache; // SSL_SESSION_CACHE_MODE
  int ssl_session_cache_size;
  int ssl_session_cache_num_buckets;
  int ssl_session_cache_skip_on_contention;
  int ssl_session_cache_timeout;
  int ssl_session_cache_auto_clear;

  char *clientCertPath;
  char *clientKeyPath;
  char *clientCACertFilename;
  char *clientCACertPath;
  int8_t clientVerify;
  int client_verify_depth;
  long ssl_ctx_options;
  long ssl_client_ctx_options;

  char *server_tls13_cipher_suites;
  char *client_tls13_cipher_suites;

  static int ssl_maxrecord;
  static bool ssl_allow_client_renegotiation;

  static bool ssl_ocsp_enabled;
  static int ssl_ocsp_cache_timeout;
  static int ssl_ocsp_request_timeout;
  static int ssl_ocsp_update_period;
  static int ssl_handshake_timeout_in;

  static size_t session_cache_number_buckets;
  static size_t session_cache_max_bucket_size;
  static bool session_cache_skip_on_lock_contention;
  static bool sni_map_enable;

  // TS-3435 Wiretracing for SSL Connections
  static int ssl_wire_trace_enabled;
  static char *ssl_wire_trace_addr;
  static IpAddr *ssl_wire_trace_ip;
  static int ssl_wire_trace_percentage;
  static char *ssl_wire_trace_server_name;

  static init_ssl_ctx_func init_ssl_ctx_cb;
  static load_ssl_file_func load_ssl_file_cb;

  static int async_handshake_enabled;
  static char *engine_conf_file;

  SSL_CTX *client_ctx;

  mutable HashMap<cchar *, class StringHashFns, SSL_CTX *> ctx_map;
  mutable ink_mutex ctxMapLock;

  SSL_CTX *getCTX(cchar *client_cert) const;
  void deleteKey(cchar *key) const;
  void freeCTXmap() const;
  void printCTXmap() const;
  bool InsertCTX(cchar *client_cert, SSL_CTX *cctx) const;
  SSL_CTX *getClientSSL_CTX(void) const;
  SSL_CTX *getNewCTX(cchar *client_cert) const;

  void initialize();
  void cleanup();
  void reset();
};

/////////////////////////////////////////////////////////////
//
// class SSLConfig
//
/////////////////////////////////////////////////////////////

struct SSLConfig {
  static void startup();
  static void reconfigure();
  static SSLConfigParams *acquire();
  static void release(SSLConfigParams *params);
  typedef ConfigProcessor::scoped_config<SSLConfig, SSLConfigParams> scoped_config;

private:
  static int configid;
};

struct SSLCertificateConfig {
  static bool startup();
  static bool reconfigure();
  static SSLCertLookup *acquire();
  static void release(SSLCertLookup *params);

  typedef ConfigProcessor::scoped_config<SSLCertificateConfig, SSLCertLookup> scoped_config;

private:
  static int configid;
};

struct SSLTicketParams : public ConfigInfo {
  ssl_ticket_key_block *default_global_keyblock = nullptr;
  time_t load_time                              = 0;
  char *ticket_key_filename;
  bool LoadTicket();
  void LoadTicketData(char *ticket_data, int ticket_data_len);
  void cleanup();

  ~SSLTicketParams() override { cleanup(); }
};

struct SSLTicketKeyConfig {
  static void startup();
  static bool reconfigure();
  static bool reconfigure_data(char *ticket_data, int ticket_data_len);

  static SSLTicketParams *
  acquire()
  {
    return static_cast<SSLTicketParams *>(configProcessor.get(configid));
  }

  static void
  release(SSLTicketParams *params)
  {
    if (configid > 0) {
      configProcessor.release(configid, params);
    }
  }

  typedef ConfigProcessor::scoped_config<SSLTicketKeyConfig, SSLTicketParams> scoped_config;

private:
  static int configid;
};

extern SSLSessionCache *session_cache;
