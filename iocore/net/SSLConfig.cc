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

#include "libts.h"
#include "I_Layout.h"

#include <string.h>
#include "P_Net.h"
#include "P_SSLConfig.h"
#include "P_SSLUtils.h"
#include "P_SSLCertLookup.h"
#include <records/I_RecHttp.h>

int SSLConfig::configid = 0;
int SSLCertificateConfig::configid = 0;
int SSLConfigParams::ssl_maxrecord = 0;
bool SSLConfigParams::ssl_allow_client_renegotiation = false;

static ConfigUpdateHandler<SSLCertificateConfig> * sslCertUpdate;

SSLConfigParams::SSLConfigParams()
{
  serverCertPathOnly =
    serverCertChainFilename =
    configFilePath =
    serverCACertFilename =
    serverCACertPath =
    clientCertPath =
    clientKeyPath =
    clientCACertFilename =
    clientCACertPath =
    cipherSuite =
    serverKeyPathOnly = NULL;

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = 0;

  ssl_ctx_options = 0;
  ssl_session_cache = SSL_SESSION_CACHE_MODE_SERVER;
  ssl_session_cache_size = 1024*20;
  ssl_session_cache_timeout = 0;
}

SSLConfigParams::~SSLConfigParams()
{
  cleanup();
}

void
SSLConfigParams::cleanup()
{
  ats_free_null(serverCertChainFilename);
  ats_free_null(serverCACertFilename);
  ats_free_null(serverCACertPath);
  ats_free_null(clientCertPath);
  ats_free_null(clientKeyPath);
  ats_free_null(clientCACertFilename);
  ats_free_null(clientCACertPath);
  ats_free_null(configFilePath);
  ats_free_null(serverCertPathOnly);
  ats_free_null(serverKeyPathOnly);
  ats_free_null(cipherSuite);

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = 0;
}

/**  set_paths_helper

 If path is *not* absolute, consider it relative to PREFIX
 if it's empty, just take SYSCONFDIR, otherwise we can take it as-is
 if final_path is NULL, it will not be updated.

 XXX: Add handling for Windows?
 */
static void
set_paths_helper(const char *path, const char *filename, char **final_path, char **final_filename)
{
  if (final_path) {
    if (path && path[0] != '/') {
      *final_path = Layout::get()->relative_to(Layout::get()->prefix, path);
    } else if (!path || path[0] == '\0'){
      *final_path = ats_strdup(Layout::get()->sysconfdir);
    } else {
      *final_path = ats_strdup(path);
    }
  }

  if (final_filename) {
    *final_filename = filename ? Layout::get()->relative_to(path, filename) : NULL;
  }

}

void
SSLConfigParams::initialize()
{
  char *serverCertRelativePath = NULL;
  char *ssl_server_private_key_path = NULL;
  char *CACertRelativePath = NULL;
  char *ssl_client_cert_filename = NULL;
  char *ssl_client_cert_path = NULL;
  char *ssl_client_private_key_filename = NULL;
  char *ssl_client_private_key_path = NULL;
  char *clientCACertRelativePath = NULL;
  char *multicert_config_file = NULL;

  cleanup();

  //+++++++++++++++++++++++++ Server part +++++++++++++++++++++++++++++++++
  verify_depth = 7;

  REC_ReadConfigInt32(clientCertLevel, "proxy.config.ssl.client.certification_level");
  REC_ReadConfigStringAlloc(cipherSuite, "proxy.config.ssl.server.cipher_suite");

  int options;
  REC_ReadConfigInteger(options, "proxy.config.ssl.SSLv2");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_SSLv2;
  REC_ReadConfigInteger(options, "proxy.config.ssl.SSLv3");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_SSLv3;
  REC_ReadConfigInteger(options, "proxy.config.ssl.TLSv1");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_TLSv1;

  // These are not available in all, older versions of OpenSSL (e.g. CentOS6)
#ifdef SSL_OP_NO_TLSv1_1
  REC_ReadConfigInteger(options, "proxy.config.ssl.TLSv1_1");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_TLSv1_1;
#endif
#ifdef SSL_OP_NO_TLSv1_2
  REC_ReadConfigInteger(options, "proxy.config.ssl.TLSv1_2");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_TLSv1_2;
#endif

#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
  REC_ReadConfigInteger(options, "proxy.config.ssl.server.honor_cipher_order");
  if (options)
    ssl_ctx_options |= SSL_OP_CIPHER_SERVER_PREFERENCE;
#endif

  REC_ReadConfigInteger(options, "proxy.config.ssl.compression");
  if (!options) {
#ifdef SSL_OP_NO_COMPRESSION
    /* OpenSSL >= 1.0 only */
    ssl_ctx_options |= SSL_OP_NO_COMPRESSION;
#elif OPENSSL_VERSION_NUMBER >= 0x00908000L
    sk_SSL_COMP_zero(SSL_COMP_get_compression_methods());
#endif
  }

  // Enable ephemeral DH parameters for the case where we use a cipher with DH forward security.
#ifdef SSL_OP_SINGLE_DH_USE
  ssl_ctx_options |= SSL_OP_SINGLE_DH_USE;
#endif

#ifdef SSL_OP_SINGLE_ECDH_USE
  ssl_ctx_options |= SSL_OP_SINGLE_ECDH_USE;
#endif

  // Enable all SSL compatibility workarounds.
  ssl_ctx_options |= SSL_OP_ALL;

  // According to OpenSSL source, applications must enable this if they support the Server Name extension. Since
  // we do, then we ought to enable this. Httpd also enables this unconditionally.
#ifdef SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION
  ssl_ctx_options |= SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION;
#endif

  REC_ReadConfigStringAlloc(serverCertChainFilename, "proxy.config.ssl.server.cert_chain.filename");
  REC_ReadConfigStringAlloc(serverCertRelativePath, "proxy.config.ssl.server.cert.path");
  set_paths_helper(serverCertRelativePath, NULL, &serverCertPathOnly, NULL);
  ats_free(serverCertRelativePath);

  REC_ReadConfigStringAlloc(multicert_config_file, "proxy.config.ssl.server.multicert.filename");
  set_paths_helper(Layout::get()->sysconfdir, multicert_config_file, NULL, &configFilePath);
  ats_free(multicert_config_file);

  REC_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");
  set_paths_helper(ssl_server_private_key_path, NULL, &serverKeyPathOnly, NULL);
  ats_free(ssl_server_private_key_path);

  REC_ReadConfigStringAlloc(serverCACertFilename, "proxy.config.ssl.CA.cert.filename");
  REC_ReadConfigStringAlloc(CACertRelativePath, "proxy.config.ssl.CA.cert.path");
  set_paths_helper(CACertRelativePath, serverCACertFilename, &serverCACertPath, &serverCACertFilename);
  ats_free(CACertRelativePath);

  // SSL session cache configurations
  REC_ReadConfigInteger(ssl_session_cache, "proxy.config.ssl.session_cache");
  REC_ReadConfigInteger(ssl_session_cache_size, "proxy.config.ssl.session_cache.size");
  REC_ReadConfigInteger(ssl_session_cache_timeout, "proxy.config.ssl.session_cache.timeout");

  // SSL record size
  REC_EstablishStaticConfigInt32(ssl_maxrecord, "proxy.config.ssl.max_record_size");

  // ++++++++++++++++++++++++ Client part ++++++++++++++++++++
  client_verify_depth = 7;
  REC_ReadConfigInt32(clientVerify, "proxy.config.ssl.client.verify.server");

  ssl_client_cert_filename = NULL;
  ssl_client_cert_path = NULL;
  REC_ReadConfigStringAlloc(ssl_client_cert_filename, "proxy.config.ssl.client.cert.filename");
  REC_ReadConfigStringAlloc(ssl_client_cert_path, "proxy.config.ssl.client.cert.path");
  set_paths_helper(ssl_client_cert_path, ssl_client_cert_filename, NULL, &clientCertPath);
  ats_free_null(ssl_client_cert_filename);
  ats_free_null(ssl_client_cert_path);

  REC_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  REC_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");
  set_paths_helper(ssl_client_private_key_path, ssl_client_private_key_filename, NULL, &clientKeyPath);
  ats_free_null(ssl_client_private_key_filename);
  ats_free_null(ssl_client_private_key_path);

  REC_ReadConfigStringAlloc(clientCACertFilename, "proxy.config.ssl.client.CA.cert.filename");
  REC_ReadConfigStringAlloc(clientCACertRelativePath, "proxy.config.ssl.client.CA.cert.path");
  set_paths_helper(clientCACertRelativePath, clientCACertFilename, &clientCACertPath, &clientCACertFilename);
  ats_free(clientCACertRelativePath);

  REC_ReadConfigInt32(ssl_allow_client_renegotiation, "proxy.config.ssl.allow_client_renegotiation");
}

void
SSLConfig::startup()
{
  reconfigure();
}

void
SSLConfig::reconfigure()
{
  SSLConfigParams *params;
  params = NEW(new SSLConfigParams);
  params->initialize();         // re-read configuration
  configid = configProcessor.set(configid, params);
}

SSLConfigParams *
SSLConfig::acquire()
{
  return ((SSLConfigParams *) configProcessor.get(configid));
}

void
SSLConfig::release(SSLConfigParams * params)
{
  configProcessor.release(configid, params);
}

void
SSLCertificateConfig::startup()
{
  sslCertUpdate = NEW(new ConfigUpdateHandler<SSLCertificateConfig>());
  sslCertUpdate->attach("proxy.config.ssl.server.multicert.filename");
  sslCertUpdate->attach("proxy.config.ssl.server.cert.path");
  sslCertUpdate->attach("proxy.config.ssl.server.private_key.path");
  sslCertUpdate->attach("proxy.config.ssl.server.cert_chain.filename");

  reconfigure();
}

void
SSLCertificateConfig::reconfigure()
{
  SSLConfig::scoped_config params;
  SSLCertLookup * lookup = NEW(new SSLCertLookup());

  if (SSLParseCertificateConfiguration(params, lookup)) {
    configid = configProcessor.set(configid, lookup);
  } else {
    delete lookup;
  }
}

SSLCertLookup *
SSLCertificateConfig::acquire()
{
  return (SSLCertLookup *)configProcessor.get(configid);
}

void
SSLCertificateConfig::release(SSLCertLookup * lookup)
{
  configProcessor.release(configid, lookup);
}

