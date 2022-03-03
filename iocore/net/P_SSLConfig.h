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

#include <openssl/rand.h>

#include "tscore/ink_inet.h"
#include "tscore/IpMap.h"

#include "ProxyConfig.h"

#include "SSLSessionCache.h"
#include "YamlSNIConfig.h"

#include "P_SSLUtils.h"
#include "P_SSLSecret.h"

struct SSLCertLookup;
struct ssl_ticket_key_block;

/////////////////////////////////////////////////////////////
//
// struct SSLConfigParams
//
// configuration parameters as they appear in the global
// configuration file.
/////////////////////////////////////////////////////////////

typedef void (*init_ssl_ctx_func)(void *, bool);
typedef void (*load_ssl_file_func)(const char *);

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
  int ssl_origin_session_cache;
  int ssl_origin_session_cache_size;
  int ssl_session_cache; // SSL_SESSION_CACHE_MODE
  int ssl_session_cache_size;
  int ssl_session_cache_num_buckets;
  int ssl_session_cache_skip_on_contention;
  int ssl_session_cache_timeout;
  int ssl_session_cache_auto_clear;

  char *clientCertPath;
  char *clientCertPathOnly;
  char *clientKeyPath;
  char *clientKeyPathOnly;
  char *clientCACertFilename;
  char *clientCACertPath;
  YamlSNIConfig::Policy verifyServerPolicy;
  YamlSNIConfig::Property verifyServerProperties;
  bool tls_server_connection;
  int client_verify_depth;
  long ssl_ctx_options;
  long ssl_client_ctx_options;

  char *server_tls13_cipher_suites;
  char *client_tls13_cipher_suites;
  char *server_groups_list;
  char *client_groups_list;

  char *keylog_file;

  static uint32_t server_max_early_data;
  static uint32_t server_recv_max_early_data;
  static bool server_allow_early_data_params;

  static int ssl_maxrecord;
  static int ssl_misc_max_iobuffer_size_index;
  static bool ssl_allow_client_renegotiation;

  static bool ssl_ocsp_enabled;
  static int ssl_ocsp_cache_timeout;
  static int ssl_ocsp_request_timeout;
  static int ssl_ocsp_update_period;
  static int ssl_handshake_timeout_in;
  char *ssl_ocsp_response_path_only;
  static char *ssl_ocsp_user_agent;

  static int origin_session_cache;
  static size_t origin_session_cache_size;
  static size_t session_cache_number_buckets;
  static size_t session_cache_max_bucket_size;
  static bool session_cache_skip_on_lock_contention;

  static IpMap *proxy_protocol_ipmap;

  static init_ssl_ctx_func init_ssl_ctx_cb;
  static load_ssl_file_func load_ssl_file_cb;

  static int async_handshake_enabled;
  static char *engine_conf_file;

  shared_SSL_CTX client_ctx;

  // Client contexts are held by 2-level map:
  // The first level maps from CA bundle file&path to next level map;
  // The second level maps from cert&key to actual SSL_CTX;
  // The second level map owns the client SSL_CTX objects and is responsible for cleaning them up
  using CTX_MAP = std::unordered_map<std::string, shared_SSL_CTX>;
  mutable std::unordered_map<std::string, CTX_MAP> top_level_ctx_map;
  mutable ink_mutex ctxMapLock;

  mutable SSLSecret secrets;

  shared_SSL_CTX getClientSSL_CTX() const;
  shared_SSL_CTX getCTX(const std::string &client_cert, const std::string &key_file, const char *ca_bundle_file,
                        const char *ca_bundle_path) const;
  shared_SSL_CTX getCTX(const char *client_cert, const char *key_file, const char *ca_bundle_file,
                        const char *ca_bundle_path) const;
  void updateCTX(const std::string &secret_string_name) const;

  void clearCTX(const std::string &client_cert) const;

  void cleanupCTXTable();

  void initialize();
  void cleanup();
  void reset();
  void SSLConfigInit(IpMap *global);
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
  static SSLConfigParams *load_acquire();
  static void release(SSLConfigParams *params);
  static void load_release(SSLConfigParams *params);

  // These methods manipulate the double buffering of the configs
  // The "loading" version is only active during loading.  Once
  // it is fliped to the active by comit_config_id, it/ becomes the
  // version accessble to the rest of the system.
  static int get_config_index();
  static int get_loading_config_index();
  static void commit_config_id();
  typedef ConfigProcessor::scoped_config<SSLConfig, SSLConfigParams> scoped_config;

private:
  static int config_index;
  static int configids[2];
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
  bool LoadTicket(bool &nochange);
  bool LoadTicketData(char *ticket_data, int ticket_data_len);
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
extern SSLOriginSessionCache *origin_sess_cache;
