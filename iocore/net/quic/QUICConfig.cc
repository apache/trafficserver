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
  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_method());

  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

#ifndef OPENSSL_IS_BORINGSSL
  // FIXME: OpenSSL (1.1.1-alpha) enable this option by default. But this shoule be removed when OpenSSL disable this by default.
  SSL_CTX_clear_options(ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
#endif

  SSL_CTX_set_max_early_data(ssl_ctx, UINT32_C(0xFFFFFFFF));

  SSL_CTX_add_custom_ext(ssl_ctx, QUICTransportParametersHandler::TRANSPORT_PARAMETER_ID,
                         SSL_EXT_TLS_ONLY | SSL_EXT_CLIENT_HELLO | SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                         &QUICTransportParametersHandler::add, &QUICTransportParametersHandler::free, nullptr,
                         &QUICTransportParametersHandler::parse, nullptr);
  return ssl_ctx;
}

static SSL_CTX *
quic_init_server_ssl_ctx(const QUICConfigParams *params)
{
  SSL_CTX *ssl_ctx = quic_new_ssl_ctx();

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

  if (params->server_supported_groups() != nullptr) {
    if (SSL_CTX_set1_groups_list(ssl_ctx, params->server_supported_groups()) != 1) {
      Error("SSL_CTX_set1_groups_list failed");
    }
  }

  return ssl_ctx;
}

static SSL_CTX *
quic_init_client_ssl_ctx(const QUICConfigParams *params)
{
  SSL_CTX *ssl_ctx = quic_new_ssl_ctx();

  if (SSL_CTX_set_alpn_protos(ssl_ctx, reinterpret_cast<const unsigned char *>(QUIC_ALPN_PROTO_LIST.data()),
                              QUIC_ALPN_PROTO_LIST.size()) != 0) {
    Error("SSL_CTX_set_alpn_protos failed");
  }

  if (params->client_supported_groups() != nullptr) {
    if (SSL_CTX_set1_groups_list(ssl_ctx, params->client_supported_groups()) != 1) {
      Error("SSL_CTX_set1_groups_list failed");
    }
  }

  return ssl_ctx;
}

//
// QUICConfigParams
//
QUICConfigParams::~QUICConfigParams()
{
  this->_server_supported_groups = (char *)ats_free_null(this->_server_supported_groups);
  this->_client_supported_groups = (char *)ats_free_null(this->_client_supported_groups);

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
  REC_EstablishStaticConfigInt32U(this->_max_alt_connection_ids, "proxy.config.quic.max_alt_connection_ids");
  REC_EstablishStaticConfigInt32U(this->_stateless_retry, "proxy.config.quic.stateless_retry");
  REC_EstablishStaticConfigInt32U(this->_vn_exercise_enabled, "proxy.config.quic.client.vn_exercise_enabled");

  REC_ReadConfigStringAlloc(this->_server_supported_groups, "proxy.config.quic.server.supported_groups");
  REC_ReadConfigStringAlloc(this->_client_supported_groups, "proxy.config.quic.client.supported_groups");

  REC_EstablishStaticConfigInt32U(this->_ld_max_tlps, "proxy.config.quic.loss_detection.max_tlps");
  REC_EstablishStaticConfigInt32U(this->_ld_reordering_threshold, "proxy.config.quic.loss_detection.reordering_threshold");
  REC_EstablishStaticConfigFloat(this->_ld_time_reordering_fraction, "proxy.config.quic.loss_detection.time_reordering_fraction");
  REC_EstablishStaticConfigInt32U(this->_ld_time_loss_detection, "proxy.config.quic.loss_detection.using_time_loss_detection");

  uint32_t timeout = 0;
  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.min_tlp_timeout");
  this->_ld_min_tlp_timeout = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.min_rto_timeout");
  this->_ld_min_rto_timeout = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.delayed_ack_timeout");
  this->_ld_delayed_ack_timeout = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.default_initial_rtt");
  this->_ld_default_initial_rtt = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(this->_cc_default_mss, "proxy.config.quic.congestion_control.default_mss");
  REC_EstablishStaticConfigInt32U(this->_cc_initial_window_scale, "proxy.config.quic.congestion_control.initial_window_scale");
  REC_EstablishStaticConfigInt32U(this->_cc_minimum_window_scale, "proxy.config.quic.congestion_control.minimum_window_scale");
  REC_EstablishStaticConfigFloat(this->_cc_loss_reduction_factor, "proxy.config.quic.congestion_control.loss_reduction_factor");

  QUICStatelessRetry::init();

  this->_server_ssl_ctx = quic_init_server_ssl_ctx(this);
  this->_client_ssl_ctx = quic_init_client_ssl_ctx(this);
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
QUICConfigParams::max_alt_connection_ids() const
{
  return this->_max_alt_connection_ids;
}

uint32_t
QUICConfigParams::stateless_retry() const
{
  return this->_stateless_retry;
}

uint32_t
QUICConfigParams::vn_exercise_enabled() const
{
  return this->_vn_exercise_enabled;
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

uint16_t
QUICConfigParams::initial_max_bidi_streams_in() const
{
  return this->_initial_max_bidi_streams_in;
}

uint16_t
QUICConfigParams::initial_max_bidi_streams_out() const
{
  return this->_initial_max_bidi_streams_out;
}

uint16_t
QUICConfigParams::initial_max_uni_streams_in() const
{
  return this->_initial_max_uni_streams_in;
}

uint16_t
QUICConfigParams::initial_max_uni_streams_out() const
{
  return this->_initial_max_uni_streams_out;
}

const char *
QUICConfigParams::server_supported_groups() const
{
  return this->_server_supported_groups;
}

const char *
QUICConfigParams::client_supported_groups() const
{
  return this->_client_supported_groups;
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

uint32_t
QUICConfigParams::ld_max_tlps() const
{
  return _ld_max_tlps;
}

uint32_t
QUICConfigParams::ld_reordering_threshold() const
{
  return _ld_reordering_threshold;
}

float
QUICConfigParams::ld_time_reordering_fraction() const
{
  return _ld_time_reordering_fraction;
}

uint32_t
QUICConfigParams::ld_time_loss_detection() const
{
  return _ld_time_loss_detection;
}

ink_hrtime
QUICConfigParams::ld_min_tlp_timeout() const
{
  return _ld_min_tlp_timeout;
}

ink_hrtime
QUICConfigParams::ld_min_rto_timeout() const
{
  return _ld_min_rto_timeout;
}

ink_hrtime
QUICConfigParams::ld_delayed_ack_timeout() const
{
  return _ld_delayed_ack_timeout;
}

ink_hrtime
QUICConfigParams::ld_default_initial_rtt() const
{
  return _ld_default_initial_rtt;
}

uint32_t
QUICConfigParams::cc_default_mss() const
{
  return _cc_default_mss;
}

uint32_t
QUICConfigParams::cc_initial_window() const
{
  return _cc_initial_window_scale * _cc_default_mss;
}

uint32_t
QUICConfigParams::cc_minimum_window() const
{
  return _cc_minimum_window_scale * _cc_default_mss;
}

float
QUICConfigParams::cc_loss_reduction_factor() const
{
  return _cc_loss_reduction_factor;
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
