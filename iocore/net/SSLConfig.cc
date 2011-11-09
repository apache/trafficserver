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
    serverKeyPathOnly = NULL;

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = 0;

  ssl_accept_port_number = -1;
  termMode = SSL_TERM_MODE_NONE;
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
  if (serverCertPath) {
    ats_free(serverCertPath);
    serverCertPath = NULL;
  }
  if (serverCertChainPath) {
    ats_free(serverCertChainPath);
    serverCertChainPath = NULL;
  }
  if (serverKeyPath) {
    ats_free(serverKeyPath);
    serverKeyPath = NULL;
  }
  if (CACertFilename) {
    ats_free(CACertFilename);
    CACertFilename = NULL;
  }
  if (CACertPath) {
    ats_free(CACertPath);
    CACertPath = NULL;
  }
  if (clientCertPath) {
    ats_free(clientCertPath);
    clientCertPath = NULL;
  }
  if (clientKeyPath) {
    ats_free(clientKeyPath);
    clientKeyPath = NULL;
  }
  if (clientCACertFilename) {
    ats_free(clientCACertFilename);
    clientCACertFilename = NULL;
  }
  if (clientCACertPath) {
    ats_free(clientCACertPath);
    clientCACertPath = NULL;
  }
  if (configFilePath) {
    ats_free(configFilePath);
    configFilePath = NULL;
  }
  if (serverCertPathOnly) {
    ats_free(serverCertPathOnly);
    serverCertPathOnly = NULL;
  }
  if (serverKeyPathOnly) {
    ats_free(serverKeyPathOnly);
    serverKeyPathOnly = NULL;
  }
  if (cipherSuite) {
    ats_free(cipherSuite);
    cipherSuite = NULL;
  }

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = 0;
  ssl_accept_port_number = -1;
  termMode = SSL_TERM_MODE_NONE;
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
#ifdef _WIN32
  int i;
#endif

  cleanup();

  //+++++++++++++++++++++++++ Server part +++++++++++++++++++++++++++++++++
  verify_depth = 7;

  IOCORE_ReadConfigInteger(ssl_mode, "proxy.config.ssl.enabled");
  ssl_mode &= SSL_TERM_MODE_BOTH;
  termMode = (SSL_TERMINATION_MODE) ssl_mode;

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

  IOCORE_ReadConfigString(serverCertFilename, "proxy.config.ssl.server.cert.filename", PATH_NAME_MAX);
  IOCORE_ReadConfigString(serverCertRelativePath, "proxy.config.ssl.server.cert.path", PATH_NAME_MAX);

  serverCertPathOnly = Layout::get()->relative(serverCertRelativePath);
  serverCertPath = Layout::relative_to(serverCertPathOnly, serverCertFilename);

#ifdef _WIN32
  i = 0;
  while (serverCertPathOnly[i] != 0) {
    if (serverCertPathOnly[i] == '/')
      serverCertPathOnly[i] = '\\';
    i++;
  }

  i = 0;
  while (serverCertPath[i] != 0) {
    if (serverCertPath[i] == '/')
      serverCertPath[i] = '\\';
    i++;
  }
#endif


  char *cert_chain;
  IOCORE_ReadConfigStringAlloc(cert_chain, "proxy.config.ssl.server.cert_chain.filename");
  if (cert_chain != NULL) {
    serverCertChainPath = Layout::relative_to(serverCertPathOnly, cert_chain);

#ifdef _WIN32
    i = 0;
    while (serverCertChainPath[i] != 0) {
      if (serverCertChainPath[i] == '/')
        serverCertChainPath[i] = '\\';
      i++;
    }
#endif
    ats_free(cert_chain);
  }

  IOCORE_ReadConfigStringAlloc(multicert_config_file, "proxy.config.ssl.server.multicert.filename");
  if (multicert_config_file != NULL) {
    configFilePath = Layout::relative_to(Layout::get()->sysconfdir, multicert_config_file);

#ifdef _WIN32
    i = 0;
    while (configFilePath[i] != 0) {
      if (configFilePath[i] == '/')
        configFilePath[i] = '\\';
      i++;
    }
#endif
    ats_free(multicert_config_file);
  }
  // Added Alloc as a temp fix for warnings generated
  // by the ReadConfigString Macro when a string is NULL.

  ssl_server_private_key_filename = NULL;
  ssl_server_private_key_path = NULL;

  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_filename, "proxy.config.ssl.server.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");

  if (ssl_server_private_key_path != NULL) {
    serverKeyPathOnly = Layout::get()->relative(ssl_server_private_key_path);
    ats_free(ssl_server_private_key_path);
  }
  else {
    // XXX: private_key.filename is relative to prefix or sysconfdir?
    //
    serverKeyPathOnly = ats_strdup(Layout::get()->prefix);
  }
  if (ssl_server_private_key_filename != NULL) {
    serverKeyPath = Layout::relative_to(serverKeyPathOnly, ssl_server_private_key_filename);

#ifdef _WIN32
    i = 0;
    while (serverKeyPath[i] != 0) {
      if (serverKeyPath[i] == '/')
        serverKeyPath[i] = '\\';
      i++;
    }
#endif
    ats_free(ssl_server_private_key_filename);
  }

  ssl_server_private_key_path = NULL;

  IOCORE_ReadConfigStringAlloc(CACertFilename, "proxy.config.ssl.CA.cert.filename");
  if (CACertFilename && (*CACertFilename == 0)) {
    ats_free(CACertFilename);
    CACertFilename = NULL;
  }

  IOCORE_ReadConfigStringAlloc(CACertRelativePath, "proxy.config.ssl.CA.cert.pathname");

  if (CACertRelativePath != NULL) {
    char *abs_path = Layout::get()->relative(CACertRelativePath);
    CACertPath = Layout::relative_to(abs_path, CACertFilename);

#ifdef _WIN32
    i = 0;
    while (CACertPath[i] != 0) {
      if (CACertPath[i] == '/')
        CACertPath[i] = '\\';
      i++;
    }
#endif
    ats_free(abs_path);
    ats_free(CACertRelativePath);
  }

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

  if (ssl_client_cert_path == NULL) {
    ssl_client_cert_path = ats_strdup(Layout::get()->prefix);
  }
  if (ssl_client_cert_filename != NULL) {
    char *abs_path = Layout::get()->relative(ssl_client_cert_path);
    clientCertPath = Layout::relative_to(abs_path, ssl_client_cert_filename);

#ifdef _WIN32
    i = 0;
    while (clientCertPath[i] != 0) {
      if (clientCertPath[i] == '/')
        clientCertPath[i] = '\\';
      i++;
    }
#endif
    ats_free(abs_path);
    ats_free(ssl_client_cert_filename);
  }
  ats_free(ssl_client_cert_path);

  ssl_client_cert_filename = NULL;
  ssl_client_cert_path = NULL;

  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");

  if (ssl_client_private_key_path == NULL) {
    ssl_client_private_key_path = ats_strdup(Layout::get()->prefix);
  }

  if (ssl_client_private_key_filename != NULL) {
    char *abs_path = Layout::get()->relative(ssl_client_private_key_path);
    clientCertPath = Layout::relative_to(abs_path, ssl_client_private_key_filename);

#ifdef _WIN32
    i = 0;
    while (clientKeyPath[i] != 0) {
      if (clientKeyPath[i] == '/')
        clientKeyPath[i] = '\\';
      i++;
    }
#endif
    ats_free(abs_path);
    ats_free(ssl_client_private_key_filename);
  }
  ats_free(ssl_client_private_key_path);

  ssl_client_private_key_path = NULL;


  IOCORE_ReadConfigStringAlloc(clientCACertFilename, "proxy.config.ssl.client.CA.cert.filename");
  if (clientCACertFilename && (*clientCACertFilename == 0)) {
    ats_free(clientCACertFilename);
    clientCACertFilename = NULL;
  }

  IOCORE_ReadConfigStringAlloc(clientCACertRelativePath, "proxy.config.ssl.client.CA.cert.path");


// Notice that we don't put the filename at the
// end of this path.  Its a quirk of the SSL lib interface.
  if (clientCACertRelativePath != NULL) {
    clientCACertPath = Layout::get()->relative(clientCACertRelativePath);
#ifdef _WIN32
    i = 0;
    while (clientCACertPath[i] != 0) {
      if (clientCACertPath[i] == '/')
        clientCACertPath[i] = '\\';
      i++;
    }
#endif
    ats_free(clientCACertRelativePath);
  }
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
