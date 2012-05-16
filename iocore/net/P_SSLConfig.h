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
#ifndef __P_SSLCONFIG_H__
#define __P_SSLCONFIG_H__

#include "libts.h"


//
// Dynamic updates of SSL settings are not implemented yet.
//
/////////////////////////////////////////////////////////////
//
// struct SslConfigParams
//
// configuration parameters as they apear in the global
// configuration file.
/////////////////////////////////////////////////////////////


struct SslConfigParams
#ifdef USE_CONFIG_PROCESSOR
: public ConfigInfo
#endif
{
public:
  enum SSL_SESSION_CACHE_MODE
  {
    SSL_SESSION_CACHE_MODE_OFF = 0,
    SSL_SESSION_CACHE_MODE_SERVER = 1
  };

  char *getConfigFilePath(void) const { return configFilePath; }
  char *getServerCertPathOnly(void) const { return serverCertPathOnly; }
  char *getServerCACertPathOnly(void) const { return CACertPath; }
  char *getServerKeyPathOnly(void) const { return serverKeyPathOnly; }

  SslConfigParams();
  virtual ~SslConfigParams();

private:
  void initialize();
  void cleanup();

  char *serverCertPathOnly;
  char *serverCertChainPath;
  char *serverKeyPathOnly;
  char *CACertFilename;
  char *CACertPath;
  char *configFilePath;
  char *cipherSuite;
  int clientCertLevel;
  int verify_depth;
  int ssl_session_cache;
  int ssl_session_cache_size;

  char *clientCertPath;
  char *clientKeyPath;
  char *clientCACertFilename;
  char *clientCACertPath;
  int clientVerify;
  int client_verify_depth;

  long ssl_ctx_options;

  friend struct SSLNetProcessor;
  friend class SslConfig;
};

/////////////////////////////////////////////////////////////
//
// class SslConfig
//
/////////////////////////////////////////////////////////////
class SslConfig
{
public:
  static void startup();
  static void reconfigure();
  static SslConfigParams *acquire();
  static void release(SslConfigParams * params);

private:
  static int id;
#ifndef USE_CONFIG_PROCESSOR
  static SslConfigParams *ssl_config_params;
#endif
  friend struct SSLNetProcessor;
};

extern SslConfig sslTerminationConfig;

#include "Diags.h"

TS_INLINE void
DebugBufferPrint(const char *tag, char *buff, int blen, const char *message = NULL)
{
  (void) tag;
  (void) buff;
  (void) blen;
  (void) message;
#if defined (_DEBUG)
  if (is_debug_tag_set(tag)) {
    if (message != NULL)
      fprintf(stdout, "%s\n", message);
    for (int ii = 0; ii < blen; ii++) {
      putc(buff[ii], stdout);
    }
    putc('\n', stdout);
  }
#endif
}

#endif
