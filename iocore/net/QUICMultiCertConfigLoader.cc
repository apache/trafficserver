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

#include "QUICMultiCertConfigLoader.h"
#include "P_SSLConfig.h"
#include "P_SSLNextProtocolSet.h"
#include "P_OCSPStapling.h"
#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICConnection.h"
#include "QUICTypes.h"
#include "tscore/Filenames.h"
// #include "QUICGlobals.h"

#define QUICConfDebug(fmt, ...) Debug("quic_conf", fmt, ##__VA_ARGS__)
#define QUICGlobalQCDebug(qc, fmt, ...) Debug("quic_global", "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

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
  SSLCertLookup *lookup = new SSLCertLookup();

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

void
QUICMultiCertConfigLoader::_set_handshake_callbacks(SSL_CTX *ssl_ctx)
{
  SSL_CTX_set_cert_cb(ssl_ctx, QUICMultiCertConfigLoader::ssl_cert_cb, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ssl_ctx, QUICMultiCertConfigLoader::ssl_sni_cb);

  // Set client hello callback if needed
  // SSL_CTX_set_client_hello_cb(ctx, QUIC::ssl_client_hello_cb, nullptr);
}

int
QUICMultiCertConfigLoader::ssl_sni_cb(SSL *ssl, int * /*ad*/, void * /*arg*/)
{
  // XXX: add SNIConfig support ?
  // XXX: add TRANSPORT_BLIND_TUNNEL support ?
  return 1;
}

int
QUICMultiCertConfigLoader::ssl_cert_cb(SSL *ssl, void * /*arg*/)
{
  shared_SSL_CTX ctx = nullptr;
  SSLCertContext *cc = nullptr;
  QUICCertConfig::scoped_config lookup;
  const char *servername = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
  QUICConnection *qc     = static_cast<QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));

  if (servername == nullptr) {
    servername = "";
  }
  QUICGlobalQCDebug(qc, "SNI=%s", servername);

  // The incoming SSL_CTX is either the one mapped from the inbound IP address or the default one. If we
  // don't find a name-based match at this point, we *do not* want to mess with the context because we've
  // already made a best effort to find the best match.
  if (likely(servername)) {
    cc = lookup->find(const_cast<char *>(servername));
    if (cc && cc->getCtx()) {
      ctx = cc->getCtx();
    }
  }

  // If there's no match on the server name, try to match on the peer address.
  if (ctx == nullptr) {
    QUICFiveTuple five_tuple = qc->five_tuple();
    IpEndpoint ip            = five_tuple.destination();
    cc                       = lookup->find(ip);

    if (cc && cc->getCtx()) {
      ctx = cc->getCtx();
    }
  }

  bool found = true;
  if (ctx != nullptr) {
    SSL_set_SSL_CTX(ssl, ctx.get());
  } else {
    found = false;
  }

  SSL_CTX *verify_ctx = nullptr;
  verify_ctx          = SSL_get_SSL_CTX(ssl);
  QUICGlobalQCDebug(qc, "%s SSL_CTX %p for requested name '%s'", found ? "found" : "using", verify_ctx, servername);

  if (verify_ctx == nullptr) {
    return 0;
  }

  return 1;
}

const char *
QUICMultiCertConfigLoader::_debug_tag() const
{
  return "quic";
}
