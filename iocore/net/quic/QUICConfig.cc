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

int QUICConfig::_config_id                   = 0;
int QUICConfigParams::_connection_table_size = 65521;

SSL_CTX *
quic_new_ssl_ctx()
{
  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_method());

  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

#ifndef OPENSSL_IS_BORINGSSL
  // FIXME: OpenSSL (1.1.1-alpha) enable this option by default. But this should be removed when OpenSSL disable this by default.
  SSL_CTX_clear_options(ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);

  SSL_CTX_set_max_early_data(ssl_ctx, UINT32_C(0xFFFFFFFF));

#else
  // QUIC Transport Parameters are accessible with SSL_set_quic_transport_params and SSL_get_peer_quic_transport_params
#endif

#ifdef SSL_MODE_QUIC_HACK
  // tatsuhiro-t's custom OpenSSL for QUIC draft-13
  // https://github.com/tatsuhiro-t/openssl/tree/quic-draft-13
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_QUIC_HACK);
  SSL_CTX_add_custom_ext(ssl_ctx, QUICTransportParametersHandler::TRANSPORT_PARAMETER_ID,
                         SSL_EXT_TLS_ONLY | SSL_EXT_CLIENT_HELLO | SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS,
                         &QUICTransportParametersHandler::add, &QUICTransportParametersHandler::free, nullptr,
                         &QUICTransportParametersHandler::parse, nullptr);

#endif

  return ssl_ctx;
}

/**
   ALPN and SNI should be set to SSL object with NETVC_OPTIONS
 **/
static shared_SSL_CTX
quic_init_client_ssl_ctx(const QUICConfigParams *params)
{
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> ssl_ctx(nullptr, &SSL_CTX_free);
  ssl_ctx.reset(quic_new_ssl_ctx());

#if defined(SSL_CTX_set1_groups_list) || defined(SSL_CTX_set1_curves_list)
  if (params->client_supported_groups() != nullptr) {
#ifdef SSL_CTX_set1_groups_list
    if (SSL_CTX_set1_groups_list(ssl_ctx.get(), params->client_supported_groups()) != 1) {
#else
    if (SSL_CTX_set1_curves_list(ssl_ctx.get(), params->client_supported_groups()) != 1) {
#endif
      Error("SSL_CTX_set1_groups_list failed");
    }
  }
#endif

  if (params->client_session_file() != nullptr) {
    SSL_CTX_set_session_cache_mode(ssl_ctx.get(), SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    SSL_CTX_sess_set_new_cb(ssl_ctx.get(), QUIC::ssl_client_new_session);
  }

#ifdef SSL_MODE_QUIC_HACK
  if (params->client_keylog_file() != nullptr) {
    SSL_CTX_set_keylog_callback(ssl_ctx.get(), QUIC::ssl_client_keylog_cb);
  }
#endif

  return ssl_ctx;
}

//
// QUICConfigParams
//
QUICConfigParams::~QUICConfigParams()
{
  this->_server_supported_groups = (char *)ats_free_null(this->_server_supported_groups);
  this->_client_supported_groups = (char *)ats_free_null(this->_client_supported_groups);

  SSL_CTX_free(this->_client_ssl_ctx.get());
};

void
QUICConfigParams::initialize()
{
  REC_EstablishStaticConfigInt32U(this->_instance_id, "proxy.config.quic.instance_id");
  REC_EstablishStaticConfigInt32(this->_connection_table_size, "proxy.config.quic.connection_table.size");
  REC_EstablishStaticConfigInt32U(this->_stateless_retry, "proxy.config.quic.server.stateless_retry_enabled");
  REC_EstablishStaticConfigInt32U(this->_vn_exercise_enabled, "proxy.config.quic.client.vn_exercise_enabled");
  REC_EstablishStaticConfigInt32U(this->_cm_exercise_enabled, "proxy.config.quic.client.cm_exercise_enabled");
  REC_EstablishStaticConfigInt32U(this->_quantum_readiness_test_enabled_out,
                                  "proxy.config.quic.client.quantum_readiness_test_enabled");
  REC_EstablishStaticConfigInt32U(this->_quantum_readiness_test_enabled_in,
                                  "proxy.config.quic.server.quantum_readiness_test_enabled");

  REC_ReadConfigStringAlloc(this->_server_supported_groups, "proxy.config.quic.server.supported_groups");
  REC_ReadConfigStringAlloc(this->_client_supported_groups, "proxy.config.quic.client.supported_groups");
  REC_ReadConfigStringAlloc(this->_client_session_file, "proxy.config.quic.client.session_file");
  REC_ReadConfigStringAlloc(this->_client_keylog_file, "proxy.config.quic.client.keylog_file");
  REC_ReadConfigStringAlloc(this->_qlog_dir, "proxy.config.quic.qlog_dir");

  // Transport Parameters
  REC_EstablishStaticConfigInt32U(this->_no_activity_timeout_in, "proxy.config.quic.no_activity_timeout_in");
  REC_EstablishStaticConfigInt32U(this->_no_activity_timeout_out, "proxy.config.quic.no_activity_timeout_out");
  REC_ReadConfigStringAlloc(this->_preferred_address_ipv4, "proxy.config.quic.preferred_address_ipv4");
  if (this->_preferred_address_ipv4) {
    ats_ip_pton(this->_preferred_address_ipv4, &this->_preferred_endpoint_ipv4);
  }
  REC_ReadConfigStringAlloc(this->_preferred_address_ipv6, "proxy.config.quic.preferred_address_ipv6");
  if (this->_preferred_address_ipv6) {
    ats_ip_pton(this->_preferred_address_ipv6, &this->_preferred_endpoint_ipv6);
  }
  REC_EstablishStaticConfigInt32U(this->_initial_max_data_in, "proxy.config.quic.initial_max_data_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_data_out, "proxy.config.quic.initial_max_data_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_bidi_local_in,
                                  "proxy.config.quic.initial_max_stream_data_bidi_local_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_bidi_local_out,
                                  "proxy.config.quic.initial_max_stream_data_bidi_local_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_bidi_remote_in,
                                  "proxy.config.quic.initial_max_stream_data_bidi_remote_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_bidi_remote_out,
                                  "proxy.config.quic.initial_max_stream_data_bidi_remote_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_uni_in, "proxy.config.quic.initial_max_stream_data_uni_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_stream_data_uni_out, "proxy.config.quic.initial_max_stream_data_uni_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_streams_bidi_in, "proxy.config.quic.initial_max_streams_bidi_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_streams_bidi_out, "proxy.config.quic.initial_max_streams_bidi_out");
  REC_EstablishStaticConfigInt32U(this->_initial_max_streams_uni_in, "proxy.config.quic.initial_max_streams_uni_in");
  REC_EstablishStaticConfigInt32U(this->_initial_max_streams_uni_out, "proxy.config.quic.initial_max_streams_uni_out");
  REC_EstablishStaticConfigInt32U(this->_ack_delay_exponent_in, "proxy.config.quic.ack_delay_exponent_in");
  REC_EstablishStaticConfigInt32U(this->_ack_delay_exponent_out, "proxy.config.quic.ack_delay_exponent_out");
  REC_EstablishStaticConfigInt32U(this->_max_ack_delay_in, "proxy.config.quic.max_ack_delay_in");
  REC_EstablishStaticConfigInt32U(this->_max_ack_delay_out, "proxy.config.quic.max_ack_delay_out");
  REC_EstablishStaticConfigInt32U(this->_active_cid_limit_in, "proxy.config.quic.active_cid_limit_in");
  REC_EstablishStaticConfigInt32U(this->_active_cid_limit_out, "proxy.config.quic.active_cid_limit_out");
  REC_EstablishStaticConfigInt32U(this->_disable_active_migration, "proxy.config.quic.disable_active_migration");

  // Loss Detection
  REC_EstablishStaticConfigInt32U(this->_ld_packet_threshold, "proxy.config.quic.loss_detection.packet_threshold");
  REC_EstablishStaticConfigFloat(this->_ld_time_threshold, "proxy.config.quic.loss_detection.time_threshold");

  uint32_t timeout = 0;
  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.granularity");
  this->_ld_granularity = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.initial_rtt");
  this->_ld_initial_rtt = HRTIME_MSECONDS(timeout);

  // Congestion Control
  REC_EstablishStaticConfigInt32U(this->_cc_initial_window, "proxy.config.quic.congestion_control.initial_window");
  REC_EstablishStaticConfigInt32U(this->_cc_minimum_window, "proxy.config.quic.congestion_control.minimum_window");
  REC_EstablishStaticConfigFloat(this->_cc_loss_reduction_factor, "proxy.config.quic.congestion_control.loss_reduction_factor");
  REC_EstablishStaticConfigInt32U(this->_cc_persistent_congestion_threshold,
                                  "proxy.config.quic.congestion_control.persistent_congestion_threshold");

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

const IpEndpoint *
QUICConfigParams::preferred_address_ipv4() const
{
  if (!this->_preferred_address_ipv4) {
    return nullptr;
  }

  return &this->_preferred_endpoint_ipv4;
}

const IpEndpoint *
QUICConfigParams::preferred_address_ipv6() const
{
  if (!this->_preferred_address_ipv6) {
    return nullptr;
  }

  return &this->_preferred_endpoint_ipv6;
}

uint32_t
QUICConfigParams::instance_id() const
{
  return this->_instance_id;
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
QUICConfigParams::vn_exercise_enabled() const
{
  return this->_vn_exercise_enabled;
}

uint32_t
QUICConfigParams::cm_exercise_enabled() const
{
  return this->_cm_exercise_enabled;
}

uint32_t
QUICConfigParams::quantum_readiness_test_enabled_in() const
{
  return this->_quantum_readiness_test_enabled_in;
}

uint32_t
QUICConfigParams::quantum_readiness_test_enabled_out() const
{
  return this->_quantum_readiness_test_enabled_out;
}

uint32_t
QUICConfigParams::initial_max_data_in() const
{
  return this->_initial_max_data_in;
}

uint32_t
QUICConfigParams::initial_max_data_out() const
{
  return this->_initial_max_data_out;
}

uint32_t
QUICConfigParams::initial_max_stream_data_bidi_local_in() const
{
  return this->_initial_max_stream_data_bidi_local_in;
}

uint32_t
QUICConfigParams::initial_max_stream_data_bidi_local_out() const
{
  return this->_initial_max_stream_data_bidi_local_out;
}

uint32_t
QUICConfigParams::initial_max_stream_data_bidi_remote_in() const
{
  return this->_initial_max_stream_data_bidi_remote_in;
}

uint32_t
QUICConfigParams::initial_max_stream_data_bidi_remote_out() const
{
  return this->_initial_max_stream_data_bidi_remote_out;
}

uint32_t
QUICConfigParams::initial_max_stream_data_uni_in() const
{
  return this->_initial_max_stream_data_uni_in;
}

uint32_t
QUICConfigParams::initial_max_stream_data_uni_out() const
{
  return this->_initial_max_stream_data_uni_out;
}

uint64_t
QUICConfigParams::initial_max_streams_bidi_in() const
{
  return this->_initial_max_streams_bidi_in;
}

uint64_t
QUICConfigParams::initial_max_streams_bidi_out() const
{
  return this->_initial_max_streams_bidi_out;
}

uint64_t
QUICConfigParams::initial_max_streams_uni_in() const
{
  return this->_initial_max_streams_uni_in;
}

uint64_t
QUICConfigParams::initial_max_streams_uni_out() const
{
  return this->_initial_max_streams_uni_out;
}

uint8_t
QUICConfigParams::ack_delay_exponent_in() const
{
  return this->_ack_delay_exponent_in;
}

uint8_t
QUICConfigParams::ack_delay_exponent_out() const
{
  return this->_ack_delay_exponent_out;
}

uint8_t
QUICConfigParams::max_ack_delay_in() const
{
  return this->_max_ack_delay_in;
}

uint8_t
QUICConfigParams::max_ack_delay_out() const
{
  return this->_max_ack_delay_out;
}

uint8_t
QUICConfigParams::active_cid_limit_in() const
{
  return this->_active_cid_limit_in;
}

uint8_t
QUICConfigParams::active_cid_limit_out() const
{
  return this->_active_cid_limit_out;
}

bool
QUICConfigParams::disable_active_migration() const
{
  return this->_disable_active_migration;
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

shared_SSL_CTX
QUICConfigParams::client_ssl_ctx() const
{
  return this->_client_ssl_ctx;
}

uint32_t
QUICConfigParams::ld_packet_threshold() const
{
  return _ld_packet_threshold;
}

float
QUICConfigParams::ld_time_threshold() const
{
  return _ld_time_threshold;
}

ink_hrtime
QUICConfigParams::ld_granularity() const
{
  return _ld_granularity;
}

ink_hrtime
QUICConfigParams::ld_initial_rtt() const
{
  return _ld_initial_rtt;
}

uint32_t
QUICConfigParams::cc_initial_window() const
{
  return _cc_initial_window;
}

uint32_t
QUICConfigParams::cc_minimum_window() const
{
  return _cc_minimum_window;
}

float
QUICConfigParams::cc_loss_reduction_factor() const
{
  return _cc_loss_reduction_factor;
}

uint32_t
QUICConfigParams::cc_persistent_congestion_threshold() const
{
  return _cc_persistent_congestion_threshold;
}

uint8_t
QUICConfigParams::scid_len()
{
  return QUICConfigParams::_scid_len;
}

const char *
QUICConfigParams::client_session_file() const
{
  return this->_client_session_file;
}

const char *
QUICConfigParams::client_keylog_file() const
{
  return this->_client_keylog_file;
}

const char *
QUICConfigParams::qlog_dir() const
{
  return this->_qlog_dir;
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

  QUICConnectionId::SCID_LEN = params->scid_len();
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
