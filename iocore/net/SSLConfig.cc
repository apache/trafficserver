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
#include <openssl/ssl.h>

int SslConfig::id = 0;
bool SslConfig::serverSSLTermination = 0;

SslConfig sslTerminationConfig;

#ifndef USE_CONFIG_PROCESSOR
SslConfigParams *SslConfig::ssl_config_params;
#endif

SslConfigParams::SslConfigParams()
{
  serverCertPath = serverCertPathOnly =
    serverCertChainPath =
    serverKeyPath = configFilePath =
    CACertFilename = CACertPath =
    clientCertPath = clientKeyPath =
    clientCACertFilename = clientCACertPath =
    cipherSuite =
    serverKeyPathOnly = ncipherAccelLibPath = cswiftAccelLibPath = atallaAccelLibPath = broadcomAccelLibPath = NULL;

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = sslAccelerator = 0;

  ssl_accept_port_number = -1;
  termMode = SSL_TERM_MODE_NONE;
  ssl_ctx_options = 0;
  ssl_accelerator_required = SSL_ACCELERATOR_REQ_NO;
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
  if (serverCertPath) {
    xfree(serverCertPath);
    serverCertPath = NULL;
  }
  if (serverCertChainPath) {
    xfree(serverCertChainPath);
    serverCertChainPath = NULL;
  }
  if (serverKeyPath) {
    xfree(serverKeyPath);
    serverKeyPath = NULL;
  }
  if (CACertFilename) {
    xfree(CACertFilename);
    CACertFilename = NULL;
  }
  if (CACertPath) {
    xfree(CACertPath);
    CACertPath = NULL;
  }
  if (clientCertPath) {
    xfree(clientCertPath);
    clientCertPath = NULL;
  }
  if (clientKeyPath) {
    xfree(clientKeyPath);
    clientKeyPath = NULL;
  }
  if (clientCACertFilename) {
    xfree(clientCACertFilename);
    clientCACertFilename = NULL;
  }
  if (clientCACertPath) {
    xfree(clientCACertPath);
    clientCACertPath = NULL;
  }
  if (configFilePath) {
    xfree(configFilePath);
    configFilePath = NULL;
  }
  if (serverCertPathOnly) {
    xfree(serverCertPathOnly);
    serverCertPathOnly = NULL;
  }
  if (serverKeyPathOnly) {
    xfree(serverKeyPathOnly);
    serverKeyPathOnly = NULL;
  }
  if (ncipherAccelLibPath) {
    xfree(ncipherAccelLibPath);
    ncipherAccelLibPath = NULL;
  }
  if (cswiftAccelLibPath) {
    xfree(cswiftAccelLibPath);
    cswiftAccelLibPath = NULL;
  }
  if (atallaAccelLibPath) {
    xfree(atallaAccelLibPath);
    atallaAccelLibPath = NULL;
  }
  if (broadcomAccelLibPath) {
    xfree(broadcomAccelLibPath);
    broadcomAccelLibPath = NULL;
  }
  if (cipherSuite) {
    xfree(cipherSuite);
    cipherSuite = NULL;
  }

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = sslAccelerator = 0;
  ssl_accept_port_number = -1;
  termMode = SSL_TERM_MODE_NONE;
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
      *final_path = xstrdup(Layout::get()->sysconfdir);
    } else {
      *final_path = xstrdup(path);
    }
  }
  if (filename) {
    *final_filename = xstrdup(Layout::get()->relative_to(path, filename));
  } else {
    *final_filename = NULL;
  }

#ifdef _WIN32
  i = 0;
  while (final_path[i] != 0) {
    if (final_path[i] == '/')
      final_path[i] = '\\';
    i++;
  }

  i = 0;
  while (final_filename[i] != 0) {
    if (final_filename[i] == '/')
      final_filename[i] = '\\';
    i++;
  }
#endif
}
void
SslConfigParams::initialize()
{
  char serverCertFilename[PATH_NAME_MAX] = "";
  char serverCertRelativePath[PATH_NAME_MAX] = "";
  char *ssl_server_private_key_filename = NULL;
  char *ssl_server_private_key_path = NULL;
  char *CACertRelativePath = NULL;
  char *ssl_client_cert_filename = NULL;
  char *ssl_client_cert_path = NULL;
  char *ssl_client_private_key_filename = NULL;
  char *ssl_client_private_key_path = NULL;
  char *clientCACertRelativePath = NULL;
  char *multicert_config_file = NULL;

  int ssl_mode = SSL_TERM_MODE_NONE;
  int ret_val;

  cleanup();

  //+++++++++++++++++++++++++ Server part +++++++++++++++++++++++++++++++++
  verify_depth = 7;

  IOCORE_ReadConfigInteger(ssl_accelerator_required, "proxy.config.ssl.accelerator_required");
  ssl_accelerator_required &= SSL_ACCELERATOR_REQ_BOTH;

  IOCORE_ReadConfigInteger(ssl_mode, "proxy.config.ssl.enabled");
  ssl_mode &= SSL_TERM_MODE_BOTH;
  termMode = (SSL_TERMINATION_MODE) ssl_mode;

  IOCORE_ReadConfigStringAlloc(cipherSuite, "proxy.config.ssl.server.cipher_suite");

  /* if ssl is enabled and we require an accelerator */
  /* XXX: This code does not work */
  if ((termMode & SSL_TERM_MODE_BOTH) && (ssl_accelerator_required & SSL_ACCELERATOR_REQ_BOTH)) {
    if (system(NULL)) {
      ret_val = system("bin/openssl_accelerated >/dev/null 2>&1");
      Debug("ssl_accelerator_required", "bin/openssl_accelerated returned %d|%d|(%d)", ret_val,
            WIFEXITED(ret_val), WEXITSTATUS(ret_val));
      if (WEXITSTATUS(ret_val) != 1) {
        if (ssl_accelerator_required & SSL_ACCELERATOR_REQ_MEAN) {
          Error
            ("You asked to have ssl acceleration only if you have an accelerator card present (and wanted to exit if you didn't), but you don't appear to have one");
          exit(-1);
        } else {
          Error
            ("You asked to have ssl acceleration only if you have an accelerator card present, but you don't appear to have one [what does bin/openssl_accelerated return?]");
          ssl_mode = 0;
          termMode = (SSL_TERMINATION_MODE) ssl_mode;
        }
      }
    } else {
      Error
        ("You asked to have ssl acceleration only if you have an accelerator card present, but I can't determine either way. Disabling for now");
      ssl_mode = 0;
      termMode = (SSL_TERMINATION_MODE) ssl_mode;
    }
  }

  IOCORE_ReadConfigInteger(sslAccelerator, "proxy.config.ssl.accelerator.type");

  IOCORE_ReadConfigInt32(ssl_accept_port_number, "proxy.config.ssl.server_port");
  IOCORE_ReadConfigInt32(clientCertLevel, "proxy.config.ssl.client.certification_level");

  IOCORE_ReadConfigStringAlloc(atallaAccelLibPath, "proxy.config.ssl.atalla.lib.path");
#ifdef _WIN32
  i = 0;
  while (atallaAccelLibPath[i] != 0) {
    if (atallaAccelLibPath[i] == '/')
      atallaAccelLibPath[i] = '\\';
    i++;
  }
#endif

  IOCORE_ReadConfigStringAlloc(ncipherAccelLibPath, "proxy.config.ssl.ncipher.lib.path");
#ifdef _WIN32
  i = 0;
  while (ncipherAccelLibPath[i] != 0) {
    if (ncipherAccelLibPath[i] == '/')
      ncipherAccelLibPath[i] = '\\';
    i++;
  }
#endif

  IOCORE_ReadConfigStringAlloc(cswiftAccelLibPath, "proxy.config.ssl.cswift.lib.path");
#ifdef _WIN32
  i = 0;
  while (cswiftAccelLibPath[i] != 0) {
    if (cswiftAccelLibPath[i] == '/')
      cswiftAccelLibPath[i] = '\\';
    i++;
  }
#endif
  IOCORE_ReadConfigStringAlloc(broadcomAccelLibPath, "proxy.config.ssl.broadcom.lib.path");
#ifdef _WIN32
  i = 0;
  while (broadcomAccelLibPath[i] != 0) {
    if (broadcomAccelLibPath[i] == '/')
      broadcomAccelLibPath[i] = '\\';
    i++;
  }
#endif
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

  IOCORE_ReadConfigString(serverCertFilename, "proxy.config.ssl.server.cert.filename", PATH_NAME_MAX);
  IOCORE_ReadConfigString(serverCertRelativePath, "proxy.config.ssl.server.cert.path", PATH_NAME_MAX);
  set_paths_helper(serverCertRelativePath, serverCertFilename, &serverCertPathOnly, &serverCertPath);

  char *cert_chain = NULL;
  IOCORE_ReadConfigStringAlloc(cert_chain, "proxy.config.ssl.server.cert_chain.filename");
  set_paths_helper(serverCertRelativePath, cert_chain, &serverCertPathOnly, &serverCertChainPath);
  xfree(cert_chain);

  IOCORE_ReadConfigStringAlloc(multicert_config_file, "proxy.config.ssl.server.multicert.filename");
  set_paths_helper(Layout::get()->sysconfdir, multicert_config_file, NULL, &configFilePath);
  xfree(multicert_config_file);

  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_filename, "proxy.config.ssl.server.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");
  set_paths_helper(ssl_server_private_key_path, ssl_server_private_key_filename, &serverKeyPathOnly, &serverKeyPath);
  xfree(ssl_server_private_key_filename);
  xfree(ssl_server_private_key_path);


  IOCORE_ReadConfigStringAlloc(CACertFilename, "proxy.config.ssl.CA.cert.filename");
  IOCORE_ReadConfigStringAlloc(CACertRelativePath, "proxy.config.ssl.CA.cert.path");
  set_paths_helper(CACertRelativePath, CACertFilename, &CACertPath, &CACertFilename);
  xfree(CACertRelativePath);

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
  xfree_null(ssl_client_cert_filename);
  xfree_null(ssl_client_cert_path);

  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");
  set_paths_helper(ssl_client_private_key_path, ssl_client_private_key_filename, NULL, &clientKeyPath);
  xfree_null(ssl_client_private_key_filename);
  xfree_null(ssl_client_private_key_path);


  IOCORE_ReadConfigStringAlloc(clientCACertFilename, "proxy.config.ssl.client.CA.cert.filename");
  IOCORE_ReadConfigStringAlloc(clientCACertRelativePath, "proxy.config.ssl.client.CA.cert.path");
  set_paths_helper(clientCACertRelativePath, clientCACertFilename, &clientCACertPath, &clientCACertFilename);
  xfree(clientCACertRelativePath);
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
  serverSSLTermination = (params->termMode & SslConfigParams::SSL_TERM_MODE_SERVER) != 0;
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
