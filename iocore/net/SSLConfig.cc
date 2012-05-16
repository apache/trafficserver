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
  SslConfig.cc
   Created On      : 07/20/2000

   Description:
   SSL Configurations
 ****************************************************************************/

#include "libts.h"
#include "I_Layout.h"

#include <string.h>
#include "P_Net.h"
#include <records/I_RecHttp.h>
#include <openssl/ssl.h>

int SslConfig::id = 0;

SslConfig sslTerminationConfig;

#ifndef USE_CONFIG_PROCESSOR
SslConfigParams *SslConfig::ssl_config_params;
#endif

SslConfigParams::SslConfigParams()
{
  serverCertPathOnly =
    serverCertChainPath =
    configFilePath =
    CACertFilename = CACertPath =
    clientCertPath = clientKeyPath =
    clientCACertFilename = clientCACertPath =
    cipherSuite =
    serverKeyPathOnly = NULL;

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = 0;

  ssl_ctx_options = 0;
  ssl_session_cache = SSL_SESSION_CACHE_MODE_SERVER;
  ssl_session_cache_size = 1024*20;
}

SslConfigParams::~SslConfigParams()
{
  cleanup();
}

void
SslConfigParams::cleanup()
{
  ats_free_null(serverCertChainPath);
  ats_free_null(CACertFilename);
  ats_free_null(CACertPath);
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
  if (final_path != NULL) {
    if (path && path[0] != '/') {
      *final_path = Layout::get()->relative_to(Layout::get()->prefix, path);
    } else if (!path || path[0] == '\0'){
      *final_path = ats_strdup(Layout::get()->sysconfdir);
    } else {
      *final_path = ats_strdup(path);
    }
  }

  if (final_filename) {
    *final_filename = filename ? ats_strdup(Layout::get()->relative_to(path, filename)) : NULL;
  }

}

void
SslConfigParams::initialize()
{
  char serverCertRelativePath[PATH_NAME_MAX] = "";
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

  IOCORE_ReadConfigInt32(clientCertLevel, "proxy.config.ssl.client.certification_level");
  IOCORE_ReadConfigStringAlloc(cipherSuite, "proxy.config.ssl.server.cipher_suite");

  int options;
  IOCORE_ReadConfigInteger(options, "proxy.config.ssl.SSLv2");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_SSLv2;
  IOCORE_ReadConfigInteger(options, "proxy.config.ssl.SSLv3");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_SSLv3;
  IOCORE_ReadConfigInteger(options, "proxy.config.ssl.TLSv1");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_TLSv1;
#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
  IOCORE_ReadConfigInteger(options, "proxy.config.ssl.server.honor_cipher_order");
  if (!options)
    ssl_ctx_options |= SSL_OP_CIPHER_SERVER_PREFERENCE;
#endif
#ifdef SSL_OP_NO_COMPRESSION
  IOCORE_ReadConfigInteger(options, "proxy.config.ssl.compression");
  if (!options)
    ssl_ctx_options |= SSL_OP_NO_COMPRESSION;
#endif

  IOCORE_ReadConfigString(serverCertRelativePath, "proxy.config.ssl.server.cert.path", PATH_NAME_MAX);
  set_paths_helper(serverCertRelativePath, NULL, &serverCertPathOnly, NULL);

  char *cert_chain = NULL;
  IOCORE_ReadConfigStringAlloc(cert_chain, "proxy.config.ssl.server.cert_chain.filename");
  set_paths_helper(serverCertRelativePath, cert_chain, &serverCertPathOnly, &serverCertChainPath);
  ats_free(cert_chain);

  IOCORE_ReadConfigStringAlloc(multicert_config_file, "proxy.config.ssl.server.multicert.filename");
  set_paths_helper(Layout::get()->sysconfdir, multicert_config_file, NULL, &configFilePath);
  ats_free(multicert_config_file);

  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");
  set_paths_helper(ssl_server_private_key_path, NULL, &serverKeyPathOnly, NULL);
  ats_free(ssl_server_private_key_path);

  IOCORE_ReadConfigStringAlloc(CACertFilename, "proxy.config.ssl.CA.cert.filename");
  IOCORE_ReadConfigStringAlloc(CACertRelativePath, "proxy.config.ssl.CA.cert.path");
  set_paths_helper(CACertRelativePath, CACertFilename, &CACertPath, &CACertFilename);
  ats_free(CACertRelativePath);

  // SSL session cache configurations
  IOCORE_ReadConfigInteger(ssl_session_cache, "proxy.config.ssl.session_cache");
  IOCORE_ReadConfigInteger(ssl_session_cache_size, "proxy.config.ssl.session_cache.size");

  // ++++++++++++++++++++++++ Client part ++++++++++++++++++++
  client_verify_depth = 7;
  IOCORE_ReadConfigInt32(clientVerify, "proxy.config.ssl.client.verify.server");

  ssl_client_cert_filename = NULL;
  ssl_client_cert_path = NULL;
  IOCORE_ReadConfigStringAlloc(ssl_client_cert_filename, "proxy.config.ssl.client.cert.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_cert_path, "proxy.config.ssl.client.cert.path");
  set_paths_helper(ssl_client_cert_path, ssl_client_cert_filename, NULL, &clientCertPath);
  ats_free_null(ssl_client_cert_filename);
  ats_free_null(ssl_client_cert_path);

  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");
  set_paths_helper(ssl_client_private_key_path, ssl_client_private_key_filename, NULL, &clientKeyPath);
  ats_free_null(ssl_client_private_key_filename);
  ats_free_null(ssl_client_private_key_path);


  IOCORE_ReadConfigStringAlloc(clientCACertFilename, "proxy.config.ssl.client.CA.cert.filename");
  IOCORE_ReadConfigStringAlloc(clientCACertRelativePath, "proxy.config.ssl.client.CA.cert.path");
  set_paths_helper(clientCACertRelativePath, clientCACertFilename, &clientCACertPath, &clientCACertFilename);
  ats_free(clientCACertRelativePath);
}


void
SslConfig::startup()
{
  reconfigure();
}


void
SslConfig::reconfigure()
{
  SslConfigParams *params;
  params = NEW(new SslConfigParams);
  params->initialize();         // re-read configuration
#ifdef USE_CONFIG_PROCESSOR
  id = configProcessor.set(id, params);
#else
  ssl_config_params = params;
#endif
}

SslConfigParams *
SslConfig::acquire()
{
#ifndef USE_CONFIG_PROCESSOR
  return ssl_config_params;
#else
  return ((SslConfigParams *) configProcessor.get(id));
#endif
}

void
SslConfig::release(SslConfigParams * params)
{
  (void) params;
#ifdef USE_CONFIG_PROCESSOR
  configProcessor.release(id, params);
#endif
}

