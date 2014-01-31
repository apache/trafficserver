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

#include "ProxyConfig.h"

struct SSLCertLookup;

/////////////////////////////////////////////////////////////
//
// struct SSLConfigParams
//
// configuration parameters as they apear in the global
// configuration file.
/////////////////////////////////////////////////////////////


struct SSLConfigParams : public ConfigInfo
{
  enum SSL_SESSION_CACHE_MODE
  {
    SSL_SESSION_CACHE_MODE_OFF = 0,
    SSL_SESSION_CACHE_MODE_SERVER = 1
  };

  SSLConfigParams();
  virtual ~SSLConfigParams();

  char *  serverCertPathOnly;
  char *  serverCertChainFilename;
  char *  serverKeyPathOnly;
  char *  serverCACertFilename;
  char *  serverCACertPath;
  char *  configFilePath;
  char *  cipherSuite;
  int     clientCertLevel;
  int     verify_depth;
  int     ssl_session_cache; // SSL_SESSION_CACHE_MODE
  int     ssl_session_cache_size;
  int     ssl_session_cache_timeout;

  char *  clientCertPath;
  char *  clientKeyPath;
  char *  clientCACertFilename;
  char *  clientCACertPath;
  int     clientVerify;
  int     client_verify_depth;
  long    ssl_ctx_options;

  static int ssl_maxrecord;
  static bool ssl_allow_client_renegotiation;

  void initialize();
  void cleanup();
};

/////////////////////////////////////////////////////////////
//
// class SSLConfig
//
/////////////////////////////////////////////////////////////

struct SSLConfig
{
  static void startup();
  static void reconfigure();
  static SSLConfigParams * acquire();
  static void release(SSLConfigParams * params);

  typedef ConfigProcessor::scoped_config<SSLConfig, SSLConfigParams> scoped_config;

private:
  static int configid;
};

struct SSLCertificateConfig
{
  static void startup();
  static void reconfigure();
  static SSLCertLookup * acquire();
  static void release(SSLCertLookup * params);

  typedef ConfigProcessor::scoped_config<SSLCertificateConfig, SSLCertLookup> scoped_config;

private:
  static int configid;
};

#endif
