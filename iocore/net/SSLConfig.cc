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
  SSLConfig.cc
   Created On      : 07/20/2000

   Description:
   SSL Configurations
 ****************************************************************************/

#include "P_SSLConfig.h"

#include <cstring>
#include <cmath>

#include "tscore/ink_config.h"
#include <openssl/pem.h>

#include "InkAPIInternal.h" // Added to include the ssl_hook and lifestyle_hook definitions

#include "tscore/ink_platform.h"
#include "tscore/I_Layout.h"
#include "records/I_RecHttp.h"

#include "HttpConfig.h"

#include "P_Net.h"
#include "P_SSLClientUtils.h"
#include "P_SSLSNI.h"
#include "P_SSLCertLookup.h"
#include "P_SSLSNI.h"
#include "P_TLSKeyLogger.h"
#include "SSLDiags.h"
#include "SSLSessionCache.h"
#include "SSLSessionTicket.h"
#include "YamlSNIConfig.h"

int SSLConfig::config_index                                 = 0;
int SSLConfig::configids[]                                  = {0, 0};
int SSLCertificateConfig::configid                          = 0;
int SSLTicketKeyConfig::configid                            = 0;
int SSLConfigParams::ssl_maxrecord                          = 0;
int SSLConfigParams::ssl_misc_max_iobuffer_size_index       = 8;
bool SSLConfigParams::ssl_allow_client_renegotiation        = false;
bool SSLConfigParams::ssl_ocsp_enabled                      = false;
int SSLConfigParams::ssl_ocsp_cache_timeout                 = 3600;
int SSLConfigParams::ssl_ocsp_request_timeout               = 10;
int SSLConfigParams::ssl_ocsp_update_period                 = 60;
char *SSLConfigParams::ssl_ocsp_user_agent                  = nullptr;
int SSLConfigParams::ssl_handshake_timeout_in               = 0;
int SSLConfigParams::origin_session_cache                   = 1;
size_t SSLConfigParams::origin_session_cache_size           = 10240;
size_t SSLConfigParams::session_cache_number_buckets        = 1024;
bool SSLConfigParams::session_cache_skip_on_lock_contention = false;
size_t SSLConfigParams::session_cache_max_bucket_size       = 100;
init_ssl_ctx_func SSLConfigParams::init_ssl_ctx_cb          = nullptr;
load_ssl_file_func SSLConfigParams::load_ssl_file_cb        = nullptr;
IpMap *SSLConfigParams::proxy_protocol_ipmap                = nullptr;

const uint32_t EARLY_DATA_DEFAULT_SIZE               = 16384;
uint32_t SSLConfigParams::server_max_early_data      = 0;
uint32_t SSLConfigParams::server_recv_max_early_data = EARLY_DATA_DEFAULT_SIZE;
bool SSLConfigParams::server_allow_early_data_params = false;

int SSLConfigParams::async_handshake_enabled = 0;
char *SSLConfigParams::engine_conf_file      = nullptr;

static std::unique_ptr<ConfigUpdateHandler<SSLTicketKeyConfig>> sslTicketKey;

SSLConfigParams::SSLConfigParams()
{
  ink_mutex_init(&ctxMapLock);
  reset();
}

SSLConfigParams::~SSLConfigParams()
{
  cleanup();
  ink_mutex_destroy(&ctxMapLock);
}

void
SSLConfigInit(IpMap *global)
{
  SSLConfigParams::proxy_protocol_ipmap = global;
}

void
SSLConfigParams::reset()
{
  serverCertPathOnly = serverCertChainFilename = configFilePath = serverCACertFilename = serverCACertPath = clientCertPath =
    clientKeyPath = clientCACertFilename = clientCACertPath = cipherSuite = client_cipherSuite = dhparamsFile = serverKeyPathOnly =
      clientKeyPathOnly = clientCertPathOnly = nullptr;
  ssl_ocsp_response_path_only                = nullptr;
  server_tls13_cipher_suites                 = nullptr;
  client_tls13_cipher_suites                 = nullptr;
  server_groups_list                         = nullptr;
  client_groups_list                         = nullptr;
  keylog_file                                = nullptr;
  client_ctx                                 = nullptr;
  clientCertLevel = client_verify_depth = verify_depth = 0;
  verifyServerPolicy                                   = YamlSNIConfig::Policy::DISABLED;
  verifyServerProperties                               = YamlSNIConfig::Property::NONE;
  ssl_ctx_options                                      = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
  ssl_client_ctx_options                               = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3;
  ssl_session_cache                                    = SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL;
  ssl_session_cache_size                               = 1024 * 100;
  ssl_session_cache_num_buckets = 1024; // Sessions per bucket is ceil(ssl_session_cache_size / ssl_session_cache_num_buckets)
  ssl_session_cache_skip_on_contention = 0;
  ssl_session_cache_timeout            = 0;
  ssl_session_cache_auto_clear         = 1;
  configExitOnLoadError                = 1;
}

void
SSLConfigParams::cleanup()
{
  serverCertChainFilename = static_cast<char *>(ats_free_null(serverCertChainFilename));
  serverCACertFilename    = static_cast<char *>(ats_free_null(serverCACertFilename));
  serverCACertPath        = static_cast<char *>(ats_free_null(serverCACertPath));
  clientCertPath          = static_cast<char *>(ats_free_null(clientCertPath));
  clientCertPathOnly      = static_cast<char *>(ats_free_null(clientCertPathOnly));
  clientKeyPath           = static_cast<char *>(ats_free_null(clientKeyPath));
  clientKeyPathOnly       = static_cast<char *>(ats_free_null(clientKeyPathOnly));
  clientCACertFilename    = static_cast<char *>(ats_free_null(clientCACertFilename));
  clientCACertPath        = static_cast<char *>(ats_free_null(clientCACertPath));
  configFilePath          = static_cast<char *>(ats_free_null(configFilePath));
  serverCertPathOnly      = static_cast<char *>(ats_free_null(serverCertPathOnly));
  serverKeyPathOnly       = static_cast<char *>(ats_free_null(serverKeyPathOnly));
  cipherSuite             = static_cast<char *>(ats_free_null(cipherSuite));
  client_cipherSuite      = static_cast<char *>(ats_free_null(client_cipherSuite));
  dhparamsFile            = static_cast<char *>(ats_free_null(dhparamsFile));

  ssl_ocsp_response_path_only = static_cast<char *>(ats_free_null(ssl_ocsp_response_path_only));

  server_tls13_cipher_suites = static_cast<char *>(ats_free_null(server_tls13_cipher_suites));
  client_tls13_cipher_suites = static_cast<char *>(ats_free_null(client_tls13_cipher_suites));
  server_groups_list         = static_cast<char *>(ats_free_null(server_groups_list));
  client_groups_list         = static_cast<char *>(ats_free_null(client_groups_list));
  keylog_file                = static_cast<char *>(ats_free_null(keylog_file));

  cleanupCTXTable();
  reset();
}

/**  set_paths_helper

 If path is *not* absolute, consider it relative to PREFIX
 if it's empty, just take SYSCONFDIR, otherwise we can take it as-is
 if final_path is nullptr, it will not be updated.

 XXX: Add handling for Windows?
 */
static void
set_paths_helper(const char *path, const char *filename, char **final_path, char **final_filename)
{
  if (final_path) {
    if (path && path[0] != '/') {
      *final_path = ats_stringdup(Layout::get()->relative_to(Layout::get()->prefix, path));
    } else if (!path || path[0] == '\0') {
      *final_path = ats_stringdup(RecConfigReadConfigDir());
    } else {
      *final_path = ats_strdup(path);
    }
  }

  if (final_filename && path) {
    *final_filename = filename ? ats_stringdup(Layout::get()->relative_to(path, filename)) : nullptr;
  }
}

int
UpdateServerPolicy(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  SSLConfigParams *params = SSLConfig::acquire();
  char *verify_server     = data.rec_string;
  if (params != nullptr && verify_server != nullptr) {
    Debug("ssl_load", "New Server Policy %s", verify_server);
    params->SetServerPolicy(verify_server);
  } else {
    Debug("ssl_load", "Failed to load new Server Policy %p %p", verify_server, params);
  }
  return 0;
}

int
UpdateServerPolicyProperties(const char * /* name ATS_UNUSED */, RecDataT /* data_type ATS_UNUSED */, RecData data, void *cookie)
{
  SSLConfigParams *params = SSLConfig::acquire();
  char *verify_server     = data.rec_string;
  if (params != nullptr && verify_server != nullptr) {
    params->SetServerPolicyProperties(verify_server);
  }
  return 0;
}

void
SSLConfigParams::SetServerPolicyProperties(const char *verify_server)
{
  if (strcmp(verify_server, "SIGNATURE") == 0) {
    verifyServerProperties = YamlSNIConfig::Property::SIGNATURE_MASK;
  } else if (strcmp(verify_server, "NAME") == 0) {
    verifyServerProperties = YamlSNIConfig::Property::NAME_MASK;
  } else if (strcmp(verify_server, "ALL") == 0) {
    verifyServerProperties = YamlSNIConfig::Property::ALL_MASK;
  } else if (strcmp(verify_server, "NONE") == 0) {
    verifyServerProperties = YamlSNIConfig::Property::NONE;
  } else {
    Warning("%s is invalid for proxy.config.ssl.client.verify.server.properties.  Should be one of ALL, SIGNATURE, NAME, or NONE. "
            "Default is ALL",
            verify_server);
    verifyServerProperties = YamlSNIConfig::Property::NONE;
  }
}

void
SSLConfigParams::SetServerPolicy(const char *verify_server)
{
  if (strcmp(verify_server, "DISABLED") == 0) {
    verifyServerPolicy = YamlSNIConfig::Policy::DISABLED;
  } else if (strcmp(verify_server, "PERMISSIVE") == 0) {
    verifyServerPolicy = YamlSNIConfig::Policy::PERMISSIVE;
  } else if (strcmp(verify_server, "ENFORCED") == 0) {
    verifyServerPolicy = YamlSNIConfig::Policy::ENFORCED;
  } else {
    Warning("%s is invalid for proxy.config.ssl.client.verify.server.policy.  Should be one of DISABLED, PERMISSIVE, or ENFORCED. "
            "Default is DISABLED",
            verify_server);
    verifyServerPolicy = YamlSNIConfig::Policy::DISABLED;
  }
}

void
SSLConfigParams::initialize()
{
  char *serverCertRelativePath          = nullptr;
  char *ssl_server_private_key_path     = nullptr;
  char *CACertRelativePath              = nullptr;
  char *ssl_client_cert_filename        = nullptr;
  char *ssl_client_cert_path            = nullptr;
  char *ssl_client_private_key_filename = nullptr;
  char *ssl_client_private_key_path     = nullptr;
  char *clientCACertRelativePath        = nullptr;
  char *ssl_server_ca_cert_filename     = nullptr;
  char *ssl_client_ca_cert_filename     = nullptr;
  char *ssl_ocsp_response_path          = nullptr;

  cleanup();

  //+++++++++++++++++++++++++ Server part +++++++++++++++++++++++++++++++++
  verify_depth = 7;

  REC_ReadConfigInt32(clientCertLevel, "proxy.config.ssl.client.certification_level");
  REC_ReadConfigStringAlloc(cipherSuite, "proxy.config.ssl.server.cipher_suite");
  REC_ReadConfigStringAlloc(client_cipherSuite, "proxy.config.ssl.client.cipher_suite");
  REC_ReadConfigStringAlloc(server_tls13_cipher_suites, "proxy.config.ssl.server.TLSv1_3.cipher_suites");
  REC_ReadConfigStringAlloc(client_tls13_cipher_suites, "proxy.config.ssl.client.TLSv1_3.cipher_suites");

  dhparamsFile = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.server.dhparams_file"));

  int option = 0;

  REC_ReadConfigInteger(option, "proxy.config.ssl.TLSv1");
  if (!option) {
    ssl_ctx_options |= SSL_OP_NO_TLSv1;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.client.TLSv1");
  if (!option) {
    ssl_client_ctx_options |= SSL_OP_NO_TLSv1;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.TLSv1_1");
  if (!option) {
    ssl_ctx_options |= SSL_OP_NO_TLSv1_1;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.client.TLSv1_1");
  if (!option) {
    ssl_client_ctx_options |= SSL_OP_NO_TLSv1_1;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.TLSv1_2");
  if (!option) {
    ssl_ctx_options |= SSL_OP_NO_TLSv1_2;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.client.TLSv1_2");
  if (!option) {
    ssl_client_ctx_options |= SSL_OP_NO_TLSv1_2;
  }

#ifdef SSL_OP_NO_TLSv1_3
  REC_ReadConfigInteger(option, "proxy.config.ssl.TLSv1_3");
  if (!option) {
    ssl_ctx_options |= SSL_OP_NO_TLSv1_3;
  }

  REC_ReadConfigInteger(option, "proxy.config.ssl.client.TLSv1_3");
  if (!option) {
    ssl_client_ctx_options |= SSL_OP_NO_TLSv1_3;
  }
#endif

#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
  REC_ReadConfigInteger(option, "proxy.config.ssl.server.honor_cipher_order");
  if (option) {
    ssl_ctx_options |= SSL_OP_CIPHER_SERVER_PREFERENCE;
  }
#endif

#ifdef SSL_OP_PRIORITIZE_CHACHA
  REC_ReadConfigInteger(option, "proxy.config.ssl.server.prioritize_chacha");
  if (option) {
    ssl_ctx_options |= SSL_OP_PRIORITIZE_CHACHA;
  }
#endif

#ifdef SSL_OP_NO_COMPRESSION
  ssl_ctx_options |= SSL_OP_NO_COMPRESSION;
  ssl_client_ctx_options |= SSL_OP_NO_COMPRESSION;
#else
  sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
#endif

// Enable ephemeral DH parameters for the case where we use a cipher with DH forward security.
#ifdef SSL_OP_SINGLE_DH_USE
  ssl_ctx_options |= SSL_OP_SINGLE_DH_USE;
  ssl_client_ctx_options |= SSL_OP_SINGLE_DH_USE;
#endif

#ifdef SSL_OP_SINGLE_ECDH_USE
  ssl_ctx_options |= SSL_OP_SINGLE_ECDH_USE;
  ssl_client_ctx_options |= SSL_OP_SINGLE_ECDH_USE;
#endif

  // Enable all SSL compatibility workarounds.
  ssl_ctx_options |= SSL_OP_ALL;
  ssl_client_ctx_options |= SSL_OP_ALL;

// According to OpenSSL source, applications must enable this if they support the Server Name extension. Since
// we do, then we ought to enable this. Httpd also enables this unconditionally.
#ifdef SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION
  ssl_ctx_options |= SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;
  ssl_client_ctx_options |= SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;
#endif

  REC_ReadConfigInteger(server_max_early_data, "proxy.config.ssl.server.max_early_data");
  REC_ReadConfigInt32(server_allow_early_data_params, "proxy.config.ssl.server.allow_early_data_params");

  // According to OpenSSL the default value is 16384,
  // we keep it unless "server_max_early_data" is higher.
  server_recv_max_early_data = std::max(server_max_early_data, EARLY_DATA_DEFAULT_SIZE);

  REC_ReadConfigStringAlloc(serverCertChainFilename, "proxy.config.ssl.server.cert_chain.filename");
  REC_ReadConfigStringAlloc(serverCertRelativePath, "proxy.config.ssl.server.cert.path");
  set_paths_helper(serverCertRelativePath, nullptr, &serverCertPathOnly, nullptr);
  ats_free(serverCertRelativePath);

  configFilePath = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.server.multicert.filename"));
  REC_ReadConfigInteger(configExitOnLoadError, "proxy.config.ssl.server.multicert.exit_on_load_fail");

  REC_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");
  set_paths_helper(ssl_server_private_key_path, nullptr, &serverKeyPathOnly, nullptr);
  ats_free(ssl_server_private_key_path);

  REC_ReadConfigStringAlloc(ssl_server_ca_cert_filename, "proxy.config.ssl.CA.cert.filename");
  REC_ReadConfigStringAlloc(CACertRelativePath, "proxy.config.ssl.CA.cert.path");
  set_paths_helper(CACertRelativePath, ssl_server_ca_cert_filename, &serverCACertPath, &serverCACertFilename);
  ats_free(ssl_server_ca_cert_filename);
  ats_free(CACertRelativePath);

  // SSL session cache configurations
  REC_ReadConfigInteger(ssl_origin_session_cache, "proxy.config.ssl.origin_session_cache");
  REC_ReadConfigInteger(ssl_origin_session_cache_size, "proxy.config.ssl.origin_session_cache.size");
  REC_ReadConfigInteger(ssl_session_cache, "proxy.config.ssl.session_cache");
  REC_ReadConfigInteger(ssl_session_cache_size, "proxy.config.ssl.session_cache.size");
  REC_ReadConfigInteger(ssl_session_cache_num_buckets, "proxy.config.ssl.session_cache.num_buckets");
  REC_ReadConfigInteger(ssl_session_cache_skip_on_contention, "proxy.config.ssl.session_cache.skip_cache_on_bucket_contention");
  REC_ReadConfigInteger(ssl_session_cache_timeout, "proxy.config.ssl.session_cache.timeout");
  REC_ReadConfigInteger(ssl_session_cache_auto_clear, "proxy.config.ssl.session_cache.auto_clear");

  SSLConfigParams::origin_session_cache      = ssl_origin_session_cache;
  SSLConfigParams::origin_session_cache_size = ssl_origin_session_cache_size;
  SSLConfigParams::session_cache_max_bucket_size =
    static_cast<size_t>(ceil(static_cast<double>(ssl_session_cache_size) / ssl_session_cache_num_buckets));
  SSLConfigParams::session_cache_skip_on_lock_contention = ssl_session_cache_skip_on_contention;
  SSLConfigParams::session_cache_number_buckets          = ssl_session_cache_num_buckets;

  if (ssl_session_cache == SSL_SESSION_CACHE_MODE_SERVER_ATS_IMPL) {
    session_cache = new SSLSessionCache();
  }

  if (ssl_origin_session_cache == 1 && ssl_origin_session_cache_size > 0) {
    origin_sess_cache = new SSLOriginSessionCache();
  }

  // SSL record size
  REC_EstablishStaticConfigInt32(ssl_maxrecord, "proxy.config.ssl.max_record_size");

  // SSL OCSP Stapling configurations
  REC_ReadConfigInt32(ssl_ocsp_enabled, "proxy.config.ssl.ocsp.enabled");
  REC_EstablishStaticConfigInt32(ssl_ocsp_cache_timeout, "proxy.config.ssl.ocsp.cache_timeout");
  REC_EstablishStaticConfigInt32(ssl_ocsp_request_timeout, "proxy.config.ssl.ocsp.request_timeout");
  REC_EstablishStaticConfigInt32(ssl_ocsp_update_period, "proxy.config.ssl.ocsp.update_period");
  REC_ReadConfigStringAlloc(ssl_ocsp_response_path, "proxy.config.ssl.ocsp.response.path");
  set_paths_helper(ssl_ocsp_response_path, nullptr, &ssl_ocsp_response_path_only, nullptr);
  ats_free(ssl_ocsp_response_path);
  REC_ReadConfigStringAlloc(ssl_ocsp_user_agent, "proxy.config.http.request_via_str");

  REC_ReadConfigInt32(ssl_handshake_timeout_in, "proxy.config.ssl.handshake_timeout_in");

  REC_ReadConfigInt32(async_handshake_enabled, "proxy.config.ssl.async.handshake.enabled");
  REC_ReadConfigStringAlloc(engine_conf_file, "proxy.config.ssl.engine.conf_file");

  REC_ReadConfigStringAlloc(server_groups_list, "proxy.config.ssl.server.groups_list");

  // ++++++++++++++++++++++++ Client part ++++++++++++++++++++
  client_verify_depth = 7;

  char *verify_server = nullptr;
  REC_ReadConfigStringAlloc(verify_server, "proxy.config.ssl.client.verify.server.policy");
  this->SetServerPolicy(verify_server);
  ats_free(verify_server);
  REC_RegisterConfigUpdateFunc("proxy.config.ssl.client.verify.server.policy", UpdateServerPolicy, nullptr);

  REC_ReadConfigStringAlloc(verify_server, "proxy.config.ssl.client.verify.server.properties");
  this->SetServerPolicyProperties(verify_server);
  ats_free(verify_server);
  REC_RegisterConfigUpdateFunc("proxy.config.ssl.client.verify.server.properties", UpdateServerPolicyProperties, nullptr);

  ssl_client_cert_filename = nullptr;
  ssl_client_cert_path     = nullptr;
  REC_ReadConfigStringAlloc(ssl_client_cert_filename, "proxy.config.ssl.client.cert.filename");
  REC_ReadConfigStringAlloc(ssl_client_cert_path, "proxy.config.ssl.client.cert.path");
  set_paths_helper(ssl_client_cert_path, ssl_client_cert_filename, &clientCertPathOnly, &clientCertPath);
  ats_free_null(ssl_client_cert_filename);
  ats_free_null(ssl_client_cert_path);

  REC_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  REC_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");
  set_paths_helper(ssl_client_private_key_path, ssl_client_private_key_filename, &clientKeyPathOnly, &clientKeyPath);
  ats_free_null(ssl_client_private_key_filename);
  ats_free_null(ssl_client_private_key_path);

  REC_ReadConfigStringAlloc(ssl_client_ca_cert_filename, "proxy.config.ssl.client.CA.cert.filename");
  REC_ReadConfigStringAlloc(clientCACertRelativePath, "proxy.config.ssl.client.CA.cert.path");
  set_paths_helper(clientCACertRelativePath, ssl_client_ca_cert_filename, &clientCACertPath, &clientCACertFilename);
  ats_free(clientCACertRelativePath);
  ats_free(ssl_client_ca_cert_filename);

  REC_ReadConfigStringAlloc(client_groups_list, "proxy.config.ssl.client.groups_list");

  REC_ReadConfigStringAlloc(keylog_file, "proxy.config.ssl.keylog_file");
  if (keylog_file == nullptr) {
    TLSKeyLogger::disable_keylogging();
  } else {
    TLSKeyLogger::enable_keylogging(keylog_file);
  }

  REC_ReadConfigInt32(ssl_allow_client_renegotiation, "proxy.config.ssl.allow_client_renegotiation");

  REC_ReadConfigInt32(ssl_misc_max_iobuffer_size_index, "proxy.config.ssl.misc.io.max_buffer_index");

  // Enable client regardless of config file settings as remap file
  // can cause HTTP layer to connect using SSL. But only if SSL
  // initialization hasn't failed already.
  client_ctx = this->getCTX(this->clientCertPath, this->clientKeyPath, this->clientCACertFilename, this->clientCACertPath);
  if (!client_ctx) {
    SSLError("Can't initialize the SSL client, HTTPS in remap rules will not function");
  }
}

shared_SSL_CTX
SSLConfigParams::getClientSSL_CTX() const
{
  return client_ctx;
}

int
SSLConfig::get_config_index()
{
  return config_index;
}

int
SSLConfig::get_loading_config_index()
{
  return config_index == 0 ? 1 : 0;
}

void
SSLConfig::commit_config_id()
{
  // Update the active config index
  config_index = get_loading_config_index();

  if (configids[get_loading_config_index()] != 0) {
    // Start draining to free the old config
    configProcessor.set(configids[get_loading_config_index()], nullptr);
  }
}

void
SSLConfig::startup()
{
  reconfigure();
}

void
SSLConfig::reconfigure()
{
  Debug("ssl_load", "Reload SSLConfig");
  SSLConfigParams *params;
  params = new SSLConfigParams;
  // start loading the next config
  int loading_config_index        = get_loading_config_index();
  configids[loading_config_index] = configProcessor.set(configids[loading_config_index], params);
  params->initialize(); // re-read configuration
  // Make the new config available for use.
  commit_config_id();
}

SSLConfigParams *
SSLConfig::acquire()
{
  return static_cast<SSLConfigParams *>(configProcessor.get(configids[get_config_index()]));
}

SSLConfigParams *
SSLConfig::load_acquire()
{
  return static_cast<SSLConfigParams *>(configProcessor.get(configids[get_loading_config_index()]));
}

void
SSLConfig::release(SSLConfigParams *params)
{
  configProcessor.release(configids[get_config_index()], params);
}

void
SSLConfig::load_release(SSLConfigParams *params)
{
  configProcessor.release(configids[get_loading_config_index()], params);
}

bool
SSLCertificateConfig::startup()
{
  // Exit if there are problems on the certificate loading and the
  // proxy.config.ssl.server.multicert.exit_on_load_fail is true
  SSLConfig::scoped_config params;
  if (!reconfigure() && params->configExitOnLoadError) {
    Fatal("failed to load SSL certificate file, %s", params->configFilePath);
  }

  return true;
}

bool
SSLCertificateConfig::reconfigure()
{
  bool retStatus = true;
  SSLConfig::scoped_config params;
  SSLCertLookup *lookup = new SSLCertLookup();

  // Test SSL certificate loading startup. With large numbers of certificates, reloading can take time, so delay
  // twice the healthcheck period to simulate a loading a large certificate set.
  if (is_action_tag_set("test.multicert.delay")) {
    const int secs = 60;
    Debug("ssl_load", "delaying certificate reload by %d secs", secs);
    ink_hrtime_sleep(HRTIME_SECONDS(secs));
  }

  SSLMultiCertConfigLoader loader(params);
  if (!loader.load(lookup)) {
    retStatus = false;
  }

  if (!lookup->is_valid) {
    retStatus = false;
  }

  // If there are errors in the certificate configs force the load anyway if there
  // is no configuration at all (i.e. this is the initial load).
  if (retStatus || 0 == configid) {
    configid = configProcessor.set(configid, lookup);
  } else {
    delete lookup;
  }

  if (retStatus) {
    Note("%s finished loading", params->configFilePath);
  } else {
    Error("%s failed to load", params->configFilePath);
  }

  return retStatus;
}

SSLCertLookup *
SSLCertificateConfig::acquire()
{
  return static_cast<SSLCertLookup *>(configProcessor.get(configid));
}

void
SSLCertificateConfig::release(SSLCertLookup *lookup)
{
  if (lookup == nullptr) {
    return;
  }
  configProcessor.release(configid, lookup);
}

bool
SSLTicketParams::LoadTicket(bool &nochange)
{
  cleanup();
  nochange = true;

#if TS_HAS_TLS_SESSION_TICKET
  ssl_ticket_key_block *keyblock = nullptr;

  SSLConfig::scoped_config params;
  time_t last_load_time    = 0;
  bool no_default_keyblock = true;

  SSLTicketKeyConfig::scoped_config ticket_params;
  if (ticket_params) {
    last_load_time      = ticket_params->load_time;
    no_default_keyblock = ticket_params->default_global_keyblock == nullptr;
  }

  // elevate/allow file access to root read only files/certs
  uint32_t elevate_setting = 0;
  REC_ReadConfigInteger(elevate_setting, "proxy.config.ssl.cert.load_elevated");
  ElevateAccess elevate_access(elevate_setting ? ElevateAccess::FILE_PRIVILEGE : 0); // destructor will demote for us

  if (REC_ReadConfigStringAlloc(ticket_key_filename, "proxy.config.ssl.server.ticket_key.filename") == REC_ERR_OKAY &&
      ticket_key_filename != nullptr) {
    ats_scoped_str ticket_key_path(Layout::relative_to(params->serverCertPathOnly, ticket_key_filename));
    // See if the file changed since we last loaded
    struct stat sdata;
    if (last_load_time && (stat(ticket_key_filename, &sdata) >= 0)) {
      if (sdata.st_mtime && sdata.st_mtime <= last_load_time) {
        Debug("ssl_load", "ticket key %s has not changed", ticket_key_filename);
        // No updates since last load
        return true;
      }
    }
    nochange = false;
    keyblock = ssl_create_ticket_keyblock(ticket_key_path);
    // Initialize if we don't have one yet
  } else if (no_default_keyblock) {
    nochange = false;
    keyblock = ssl_create_ticket_keyblock(nullptr);
  } else {
    // No need to update.  Keep the previous ticket param
    return true;
  }
  if (!keyblock) {
    Error("Could not load ticket key from %s", ticket_key_filename);
    return false;
  }
  default_global_keyblock = keyblock;
  load_time               = time(nullptr);

  Debug("ssl_load", "ticket key reloaded from %s", ticket_key_filename);
#endif
  return true;
}

bool
SSLTicketParams::LoadTicketData(char *ticket_data, int ticket_data_len)
{
  cleanup();
#if TS_HAS_TLS_SESSION_TICKET
  if (ticket_data != nullptr && ticket_data_len > 0) {
    default_global_keyblock = ticket_block_create(ticket_data, ticket_data_len);
  } else {
    default_global_keyblock = ssl_create_ticket_keyblock(nullptr);
  }
  load_time = time(nullptr);

  if (default_global_keyblock == nullptr) {
    return false;
  }
#endif
  return true;
}

void
SSLTicketKeyConfig::startup()
{
  sslTicketKey.reset(new ConfigUpdateHandler<SSLTicketKeyConfig>());

  sslTicketKey->attach("proxy.config.ssl.server.ticket_key.filename");
  SSLConfig::scoped_config params;
  if (!reconfigure() && params->configExitOnLoadError) {
    Fatal("Failed to load SSL ticket key file");
  }
}

bool
SSLTicketKeyConfig::reconfigure()
{
  SSLTicketParams *ticketKey = new SSLTicketParams();

  if (ticketKey) {
    bool nochange = false;
    if (!ticketKey->LoadTicket(nochange)) {
      delete ticketKey;
      return false;
    }
    // Nothing updated, leave the original configuration
    if (nochange) {
      delete ticketKey;
      return true;
    }
  }
  configid = configProcessor.set(configid, ticketKey);
  return true;
}

bool
SSLTicketKeyConfig::reconfigure_data(char *ticket_data, int ticket_data_len)
{
  SSLTicketParams *ticketKey = new SSLTicketParams();
  if (ticketKey) {
    if (ticketKey->LoadTicketData(ticket_data, ticket_data_len) == false) {
      delete ticketKey;
      return false;
    }
  }
  configid = configProcessor.set(configid, ticketKey);
  return true;
}

void
SSLTicketParams::cleanup()
{
  ticket_block_free(default_global_keyblock);
  ticket_key_filename = static_cast<char *>(ats_free_null(ticket_key_filename));
}

void
cleanup_bio(BIO *&biop)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-value"
  BIO_set_close(biop, BIO_NOCLOSE);
#pragma GCC diagnostic pop
  BIO_free(biop);
  biop = nullptr;
}

void
SSLConfigParams::updateCTX(const std::string &cert_secret_name) const
{
  Debug("ssl_config_updateCTX", "Update cert %s, %p", cert_secret_name.c_str(), this);

  // Instances of SSLConfigParams should access by one thread at a time only.  secret_for_updateCTX is accessed
  // atomically as a fail-safe.
  //
  char const *expected = nullptr;
  if (!secret_for_updateCTX.compare_exchange_strong(expected, cert_secret_name.c_str())) {
    if (is_debug_tag_set("ssl_config_updateCTX")) {
      // As a fail-safe, handle it if secret_for_updateCTX doesn't or no longer points to a null-terminated string.
      //
      char const *s{expected};
      for (; *s && (std::size_t(s - expected) < cert_secret_name.size()); ++s) {
      }
      Debug("ssl_config_updateCTX", "Update cert, indirect recusive call caused by call for %.*s", int(s - expected), expected);
    }
    return;
  }

  // Clear the corresponding client CTXs.  They will be lazy loaded later
  Debug("ssl_load", "Update cert %s", cert_secret_name.c_str());
  this->clearCTX(cert_secret_name);

  // Update the server cert
  SSLMultiCertConfigLoader loader(this);
  loader.update_ssl_ctx(cert_secret_name);

  secret_for_updateCTX = nullptr;
}

void
SSLConfigParams::clearCTX(const std::string &client_cert) const
{
  ink_mutex_acquire(&ctxMapLock);
  for (auto ctx_map_iter = top_level_ctx_map.begin(); ctx_map_iter != top_level_ctx_map.end(); ++ctx_map_iter) {
    auto ctx_iter = ctx_map_iter->second.find(client_cert);
    if (ctx_iter != ctx_map_iter->second.end()) {
      ctx_iter->second = nullptr;
      Debug("ssl_load", "Clear client cert %s %s", ctx_map_iter->first.c_str(), ctx_iter->first.c_str());
    }
  }
  ink_mutex_release(&ctxMapLock);
}

shared_SSL_CTX
SSLConfigParams::getCTX(const char *client_cert, const char *key_file, const char *ca_bundle_file, const char *ca_bundle_path) const
{
  return this->getCTX(std::string(client_cert ? client_cert : ""), std::string(key_file ? key_file : ""), ca_bundle_file,
                      ca_bundle_path);
}

shared_SSL_CTX
SSLConfigParams::getCTX(const std::string &client_cert, const std::string &key_file, const char *ca_bundle_file,
                        const char *ca_bundle_path) const
{
  shared_SSL_CTX client_ctx = nullptr;
  std::string top_level_key, ctx_key;
  ctx_key = client_cert;
  ts::bwprint(top_level_key, "{}:{}", ca_bundle_file, ca_bundle_path);

  Debug("ssl_client_ctx", "Look for client cert %s %s", top_level_key.c_str(), ctx_key.c_str());

  ink_mutex_acquire(&ctxMapLock);
  auto ctx_map_iter = top_level_ctx_map.find(top_level_key);
  if (ctx_map_iter != top_level_ctx_map.end()) {
    auto ctx_iter = ctx_map_iter->second.find(ctx_key);
    if (ctx_iter != ctx_map_iter->second.end()) {
      client_ctx = ctx_iter->second;
    }
  }
  ink_mutex_release(&ctxMapLock);

  BIO *biop     = nullptr;
  X509 *cert    = nullptr;
  EVP_PKEY *key = nullptr;
  // Create context if doesn't exists
  if (!client_ctx) {
    Debug("ssl_client_ctx", "Load new cert for %s %s", top_level_key.c_str(), ctx_key.c_str());
    client_ctx = shared_SSL_CTX(SSLInitClientContext(this), SSLReleaseContext);

    // Set public and private keys
    if (!client_cert.empty()) {
      std::string secret_data;
      std::string secret_key_data;

      // Fetch the client_cert data
      std::string completeSecretPath{Layout::get()->relative_to(this->clientCertPathOnly, client_cert)};
      std::string completeKeySecretPath{!key_file.empty() ? Layout::get()->relative_to(this->clientKeyPathOnly, key_file) : ""};
      secrets.getOrLoadSecret(completeSecretPath, completeKeySecretPath, secret_data, secret_key_data);
      if (secret_data.empty()) {
        SSLError("failed to access cert %s", client_cert.c_str());
        goto fail;
      }

      biop = BIO_new_mem_buf(secret_data.data(), secret_data.size());

      cert = PEM_read_bio_X509(biop, nullptr, nullptr, nullptr);
      if (!cert) {
        SSLError("failed to load cert %s", client_cert.c_str());
        goto fail;
      }
      if (!SSL_CTX_use_certificate(client_ctx.get(), cert)) {
        SSLError("failed to attach client certificate from %s", client_cert.c_str());
        goto fail;
      }
      X509_free(cert);

      // Continue to fetch certs to associate intermediate certificates
      cert = PEM_read_bio_X509(biop, nullptr, nullptr, nullptr);
      while (cert) {
        if (!SSL_CTX_add_extra_chain_cert(client_ctx.get(), cert)) {
          SSLError("failed to attach client chain certificate from %s", client_cert.c_str());
          goto fail;
        }
        // Do not free cert becasue SSL_CTX_add_extra_chain_cert takes ownership of cert if it succeeds, unlike
        // SSL_CTX_use_certificate.
        cert = PEM_read_bio_X509(biop, nullptr, nullptr, nullptr);
      }

      cleanup_bio(biop);

      const std::string &key_file_name = (secret_key_data.empty()) ? client_cert : key_file;

      // If there is a separate key file, fetch the new content
      // otherwise, continue on with the cert data and hope for the best
      if (!secret_key_data.empty()) {
        biop = BIO_new_mem_buf(secret_key_data.data(), secret_key_data.size());
      } else {
        biop = BIO_new_mem_buf(secret_data.data(), secret_data.size());
      }

      key = PEM_read_bio_PrivateKey(biop, nullptr, nullptr, nullptr);
      if (!key) {
        SSLError("failed to load client private key file from %s", key_file_name.c_str());
        goto fail;
      }
      if (!SSL_CTX_use_PrivateKey(client_ctx.get(), key)) {
        SSLError("failed to use client private key file from %s", key_file_name.c_str());
        goto fail;
      }
      EVP_PKEY_free(key);
      key = nullptr;

      if (!SSL_CTX_check_private_key(client_ctx.get())) {
        SSLError("client private key (%s) does not match the certificate public key (%s)", key_file_name.c_str(),
                 client_cert.c_str());
        goto fail;
      }
      cleanup_bio(biop);
    }

    // Set CA information for verifying peer cert
    if (ca_bundle_file != nullptr || ca_bundle_path != nullptr) {
      if (!SSL_CTX_load_verify_locations(client_ctx.get(), ca_bundle_file, ca_bundle_path)) {
        SSLError("invalid client CA Certificate file (%s) or CA Certificate path (%s)", ca_bundle_file, ca_bundle_path);
        goto fail;
      }
    } else if (!SSL_CTX_set_default_verify_paths(client_ctx.get())) {
      SSLError("failed to set the default verify paths");
      goto fail;
    }

    // Try to update the context in mapping with lock acquired. If a valid context exists, return it without changing the structure.
    ink_mutex_acquire(&ctxMapLock);
    auto ctx_iter = top_level_ctx_map[top_level_key].find(ctx_key);
    if (ctx_iter == top_level_ctx_map[top_level_key].end() || ctx_iter->second == nullptr) {
      top_level_ctx_map[top_level_key][ctx_key] = client_ctx;
    } else {
      client_ctx = ctx_iter->second;
    }
    ink_mutex_release(&ctxMapLock);
  }
  return client_ctx;

fail:
  if (biop) {
    cleanup_bio(biop);
  }
  if (cert) {
    X509_free(cert);
  }
  if (key) {
    EVP_PKEY_free(key);
  }
  return nullptr;
}

void
SSLConfigParams::cleanupCTXTable()
{
  ink_mutex_acquire(&ctxMapLock);
  top_level_ctx_map.clear();
  ink_mutex_release(&ctxMapLock);
}
