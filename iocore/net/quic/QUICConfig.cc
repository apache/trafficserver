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

#include "QUICConfig.h"

#include <openssl/ssl.h>
#include <records/I_RecHttp.h>

#include "P_SSLConfig.h"

#include "QUICGlobals.h"
#include "QUICTransportParameters.h"
#include "QUICStatelessRetry.h"

int QUICConfig::_config_id                   = 0;
int QUICConfigParams::_connection_table_size = 65521;

static SSL_CTX *
quic_new_ssl_ctx()
{
#ifdef TLS1_3_VERSION_DRAFT_TXT
  // FIXME: remove this when TLS1_3_VERSION_DRAFT_TXT is removed
  Debug("quic_ps", "%s", TLS1_3_VERSION_DRAFT_TXT);
#endif

  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_method());

  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

  // FIXME: OpenSSL (1.1.1-alpha) enable this option by default. But this shoule be removed when OpenSSL disable this by default.
  SSL_CTX_clear_options(ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);

  SSL_CTX_set_max_early_data(ssl_ctx, UINT32_C(0xFFFFFFFF));

  SSL_CTX_add_custom_ext(ssl_ctx, QUICTransportParametersHandler::TRANSPORT_PARAMETER_ID,
                         SSL_EXT_TLS_ONLY | SSL_EXT_CLIENT_HELLO | SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                         &QUICTransportParametersHandler::add, &QUICTransportParametersHandler::free, nullptr,
                         &QUICTransportParametersHandler::parse, nullptr);
  return ssl_ctx;
}

static SSL_CTX *
quic_init_server_ssl_ctx(SSL_CTX *ssl_ctx)
{
  SSLConfig::scoped_config ssl_params;
  SSLParseCertificateConfiguration(ssl_params, ssl_ctx);

  if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
    Error("check private key failed");
  }

  // callbacks for cookie ext
  // Requires OpenSSL-1.1.1-pre3+ : https://github.com/openssl/openssl/pull/5463
  SSL_CTX_set_stateless_cookie_generate_cb(ssl_ctx, QUICStatelessRetry::generate_cookie);
  SSL_CTX_set_stateless_cookie_verify_cb(ssl_ctx, QUICStatelessRetry::verify_cookie);

  SSL_CTX_set_alpn_select_cb(ssl_ctx, QUIC::ssl_select_next_protocol, nullptr);

  return ssl_ctx;
}

static SSL_CTX *
quic_init_client_ssl_ctx(SSL_CTX *ssl_ctx)
{
  // SSL_CTX_set_alpn_protos()

  return ssl_ctx;
}

//
// QUICConfigParams
//
QUICConfigParams::~QUICConfigParams()
{
  SSL_CTX_free(this->_server_ssl_ctx);
  SSL_CTX_free(this->_client_ssl_ctx);
};

void
QUICConfigParams::initialize()
{
  REC_EstablishStaticConfigInt32U(this->_no_activity_timeout_in, "proxy.config.quic.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32U(this->_no_activity_timeout_out, "proxy.config.quic.no_activity_timeout_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_data, "proxy.config.quic.initial_max_data");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data, "proxy.config.quic.initial_max_stream_data");
  REC_EstablishStaticConfigInt32U(this->_server_id, "proxy.config.quic.server_id");
  REC_EstablishStaticConfigInt32(this->_connection_table_size, "proxy.config.quic.connection_table.size");
  REC_EstablishStaticConfigInt32U(this->_stateless_retry, "proxy.config.quic.stateless_retry");

  QUICStatelessRetry::init();

  this->_server_ssl_ctx = quic_init_server_ssl_ctx(quic_new_ssl_ctx());
  this->_client_ssl_ctx = quic_init_client_ssl_ctx(quic_new_ssl_ctx());
}

uint32_t
QUICConfigParams::no_activity_timeout_in() const
{
  return this->_no_activity_timeout_in;
}

uint32_t
QUICConfigParams::no_activity_timeout_out() const
{
  return this->_no_activity_timeout_out;
}

uint32_t
QUICConfigParams::server_id() const
{
  return this->_server_id;
}

int
QUICConfigParams::connection_table_size()
{
  return _connection_table_size;
}

uint32_t
QUICConfigParams::stateless_retry() const
{
  return this->_stateless_retry;
}

uint32_t
QUICConfigParams::initial_max_data() const
{
  return this->_initial_max_data;
}

uint32_t
QUICConfigParams::initial_max_stream_data() const
{
  return this->_initial_max_stream_data;
}

uint32_t
QUICConfigParams::initial_max_stream_id_bidi_in() const
{
  return this->_initial_max_stream_id_bidi_in;
}

uint32_t
QUICConfigParams::initial_max_stream_id_bidi_out() const
{
  return this->_initial_max_stream_id_bidi_out;
}

uint32_t
QUICConfigParams::initial_max_stream_id_uni_in() const
{
  return this->_initial_max_stream_id_uni_in;
}

uint32_t
QUICConfigParams::initial_max_stream_id_uni_out() const
{
  return this->_initial_max_stream_id_uni_out;
}

SSL_CTX *
QUICConfigParams::server_ssl_ctx() const
{
  return this->_server_ssl_ctx;
}

SSL_CTX *
QUICConfigParams::client_ssl_ctx() const
{
  return this->_client_ssl_ctx;
}

//
// QUICConfig
//
void
QUICConfig::startup()
{
  reconfigure();
}

void
QUICConfig::reconfigure()
{
  QUICConfigParams *params;
  params = new QUICConfigParams;
  // re-read configuration
  params->initialize();
  _config_id = configProcessor.set(_config_id, params);
}

QUICConfigParams *
QUICConfig::acquire()
{
  return static_cast<QUICConfigParams *>(configProcessor.get(_config_id));
}

void
QUICConfig::release(QUICConfigParams *params)
{
  configProcessor.release(_config_id, params);
}
