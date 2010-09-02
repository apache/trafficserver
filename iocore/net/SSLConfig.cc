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

#include "inktomi++.h"
#include "I_Layout.h"

#ifdef HAVE_LIBSSL
#include <string.h>
#include "P_Net.h"
#include <openssl/ssl.h>

int
  SslConfig::id = 0;
bool
  SslConfig::serverSSLTermination = 0;

SslConfig
  sslTerminationConfig;

#ifndef USE_CONFIG_PROCESSOR
SslConfigParams *
  SslConfig::ssl_config_params;
#endif

SslConfigParams::SslConfigParams()
{
  serverCertPath = serverCertPathOnly =
    serverCertChainPath =
    serverKeyPath = configFilePath =
    CACertFilename = CACertPath =
    clientCertPath = clientKeyPath =
    clientCACertFilename = clientCACertPath =
    serverKeyPathOnly = ncipherAccelLibPath = cswiftAccelLibPath = atallaAccelLibPath = broadcomAccelLibPath = NULL;

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = sslAccelerator = 0;

  ssl_accept_port_number = -1;
  termMode = SSL_TERM_MODE_NONE;
  ssl_ctx_options = 0;
  ssl_accelerator_required = SSL_ACCELERATOR_REQ_NO;
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

  clientCertLevel = client_verify_depth = verify_depth = clientVerify = sslAccelerator = 0;
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
  int ret_val = 0;
#ifdef _WIN32
  int i;
#endif

  cleanup();

//+++++++++++++++++++++++++ Server part +++++++++++++++++++++++++++++++++

  verify_depth = 7;

  IOCORE_ReadConfigInteger(ssl_accelerator_required, "proxy.config.ssl.accelerator_required");
  ssl_accelerator_required &= SSL_ACCELERATOR_REQ_BOTH;

  IOCORE_ReadConfigInteger(ssl_mode, "proxy.config.ssl.enabled");
  ssl_mode &= SSL_TERM_MODE_BOTH;
  termMode = (SSL_TERMINATION_MODE) ssl_mode;

  /* if ssl is enabled and we require an accelerator */
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
  int prot;
  IOCORE_ReadConfigInteger(prot, "proxy.config.ssl.SSLv2");
  if (!prot)
    ssl_ctx_options |= SSL_OP_NO_SSLv2;
  IOCORE_ReadConfigInteger(prot, "proxy.config.ssl.SSLv3");
  if (!prot)
    ssl_ctx_options |= SSL_OP_NO_SSLv3;
  IOCORE_ReadConfigInteger(prot, "proxy.config.ssl.TLSv1");
  if (!prot)
    ssl_ctx_options |= SSL_OP_NO_TLSv1;

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
    xfree(cert_chain);
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
    xfree(multicert_config_file);
  }
  // Added Alloc as a temp fix for warnings generated
  // by the ReadConfigString Macro when a string is NULL.

  ssl_server_private_key_filename = NULL;
  ssl_server_private_key_path = NULL;

  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_filename, "proxy.config.ssl.server.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_server_private_key_path, "proxy.config.ssl.server.private_key.path");

  if (ssl_server_private_key_path != NULL) {
    serverKeyPathOnly = Layout::get()->relative(ssl_server_private_key_path);
    xfree(ssl_server_private_key_path);
  }
  else {
    // XXX: private_key.filename is relative to prefix or sysconfdir?
    //
    serverKeyPathOnly = xstrdup(Layout::get()->prefix);
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
    xfree(ssl_server_private_key_filename);
  }

  ssl_server_private_key_path = NULL;

  IOCORE_ReadConfigStringAlloc(CACertFilename, "proxy.config.ssl.CA.cert.filename");
  if (CACertFilename && (*CACertFilename == 0)) {
    xfree(CACertFilename);
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
    xfree(abs_path);
    xfree(CACertRelativePath);
  }
// ++++++++++++++++++++++++ Client part ++++++++++++++++++++
  client_verify_depth = 7;
  IOCORE_ReadConfigInt32(clientVerify, "proxy.config.ssl.client.verify.server");

  ssl_client_cert_filename = NULL;
  ssl_client_cert_path = NULL;
  IOCORE_ReadConfigStringAlloc(ssl_client_cert_filename, "proxy.config.ssl.client.cert.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_cert_path, "proxy.config.ssl.client.cert.path");

  if (ssl_client_cert_path == NULL) {
    ssl_client_cert_path = xstrdup(Layout::get()->prefix);
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
    xfree(abs_path);
    xfree(ssl_client_cert_filename);
  }
  xfree(ssl_client_cert_path);

  ssl_client_cert_filename = NULL;
  ssl_client_cert_path = NULL;

  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_filename, "proxy.config.ssl.client.private_key.filename");
  IOCORE_ReadConfigStringAlloc(ssl_client_private_key_path, "proxy.config.ssl.client.private_key.path");

  if (ssl_client_private_key_path == NULL) {
    ssl_client_private_key_path = xstrdup(Layout::get()->prefix);
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
    xfree(abs_path);
    xfree(ssl_client_private_key_filename);
  }
  xfree(ssl_client_private_key_path);

  ssl_client_private_key_path = NULL;


  IOCORE_ReadConfigStringAlloc(clientCACertFilename, "proxy.config.ssl.client.CA.cert.filename");
  if (clientCACertFilename && (*clientCACertFilename == 0)) {
    xfree(clientCACertFilename);
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
    xfree(clientCACertRelativePath);
  }

}


static inline void
register_ssl_net_configs(void)
{

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.enabled", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.SSLv2", 1, RECU_RESTART_TS, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.SSLv3", 1, RECU_RESTART_TS, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.TLSv1", 1, RECU_RESTART_TS, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.accelerator.type", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.number.threads", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.atalla.lib.path", "/opt/atalla/lib", RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.ncipher.lib.path",
                              "/opt/nfast/toolkits/hwcrhk", RECU_DYNAMIC, RECC_NULL, NULL);


  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.cswift.lib.path", "/usr/lib", RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.broadcom.lib.path", "/usr/lib", RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.ssl.server_port", 4443, RECU_DYNAMIC, RECC_INT, "[0-65535]");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.ssl.client.certification_level", 0, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.cert.filename",
                              "server.pem", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]+$");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.cert.path",
                              "etc/trafficserver", RECU_DYNAMIC, RECC_STR, "^[^[:space:]]+$");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.cert_chain.filename", NULL, RECU_DYNAMIC, RECC_STR, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.multicert.filename",
                              "ssl_multicert.config", RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.private_key.filename",
                              NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.server.private_key.path", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.CA.cert.filename", NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  IOCORE_RegisterConfigString(RECT_CONFIG, "proxy.config.ssl.CA.cert.path", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.ssl.client.verify.server", 0, RECU_DYNAMIC, RECC_INT, "[0-1]");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.client.cert.filename", NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.client.cert.path", "etc/trafficserver", RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.client.private_key.filename",
                              NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.client.private_key.path", NULL, RECU_DYNAMIC, RECC_NULL, NULL);

  IOCORE_RegisterConfigString(RECT_CONFIG,
                              "proxy.config.ssl.client.CA.cert.filename",
                              NULL, RECU_DYNAMIC, RECC_STR, "^[^[:space:]]*$");

  IOCORE_RegisterConfigString(RECT_CONFIG, "proxy.config.ssl.client.CA.cert.path", NULL, RECU_DYNAMIC, RECC_NULL, NULL);


}

void
SslConfig::startup()
{
  register_ssl_net_configs();
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

#endif
