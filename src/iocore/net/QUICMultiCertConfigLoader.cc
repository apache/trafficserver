/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "iocore/net/QUICMultiCertConfigLoader.h"
#include "P_SSLConfig.h"
#include "P_SSLNextProtocolSet.h"
#include "P_OCSPStapling.h"
#include "iocore/net/quic/QUICGlobals.h"
#include "iocore/net/quic/QUICConfig.h"
#include "iocore/net/quic/QUICConnection.h"
#include "iocore/net/quic/QUICTypes.h"
#include "tscore/Filenames.h"
// #include "iocore/net/quic/QUICGlobals.h"

int QUICCertConfig::_config_id = 0;

//
// QUICCertConfig
//
void
QUICCertConfig::startup()
{
  reconfigure();
}

void
QUICCertConfig::reconfigure()
{
  SSLConfig::scoped_config params;
  SSLCertLookup           *lookup = new SSLCertLookup();

  QUICMultiCertConfigLoader loader(params);
  loader.load(lookup);

  _config_id = configProcessor.set(_config_id, lookup);
}

SSLCertLookup *
QUICCertConfig::acquire()
{
  return static_cast<SSLCertLookup *>(configProcessor.get(_config_id));
}

void
QUICCertConfig::release(SSLCertLookup *lookup)
{
  configProcessor.release(_config_id, lookup);
}

//
// QUICMultiCertConfigLoader
//
SSL_CTX *
QUICMultiCertConfigLoader::default_server_ssl_ctx()
{
  return quic_new_ssl_ctx();
}

bool
QUICMultiCertConfigLoader::_setup_session_cache(SSL_CTX *ctx)
{
  // Disabled for now
  // TODO Check if the logic in SSLMultiCertConfigLoader is reusable
  return true;
}

bool
QUICMultiCertConfigLoader::_set_cipher_suites_for_legacy_versions(SSL_CTX *ctx)
{
  // Do not set this since QUIC only uses TLS 1.3
  return true;
}

bool
QUICMultiCertConfigLoader::_set_info_callback(SSL_CTX *ctx)
{
  // Disabled for now
  // TODO Check if we need this for QUIC
  return true;
}

bool
QUICMultiCertConfigLoader::_set_npn_callback(SSL_CTX *ctx)
{
  // Do not set a callback for NPN since QUIC doesn't use it
  return true;
}

const char *
QUICMultiCertConfigLoader::_debug_tag() const
{
  return "quic";
}
