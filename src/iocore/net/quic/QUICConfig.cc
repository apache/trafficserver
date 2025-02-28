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

#include "iocore/net/quic/QUICConfig.h"

#include <openssl/ssl.h>

#include "records/RecHttp.h"

#include "../P_SSLConfig.h"
#include "../P_TLSKeyLogger.h"

#include "iocore/net/quic/QUICGlobals.h"
#include "iocore/net/quic/QUICTransportParameters.h"

int QUICConfig::_config_id                   = 0;
int QUICConfigParams::_connection_table_size = 65521;

SSL_CTX *
quic_new_ssl_ctx()
{
  SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_method());

  SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION);
  SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_3_VERSION);

#ifdef SSL_OP_ENABLE_MIDDLEBOX_COMPAT
  // FIXME: OpenSSL (1.1.1-alpha) enable this option by default. But this should be removed when OpenSSL disable this by default.
  SSL_CTX_clear_options(ssl_ctx, SSL_OP_ENABLE_MIDDLEBOX_COMPAT);
  SSL_CTX_set_max_early_data(ssl_ctx, UINT32_C(0xFFFFFFFF));
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

  if (unlikely(TLSKeyLogger::is_enabled())) {
    SSL_CTX_set_keylog_callback(ssl_ctx.get(), TLSKeyLogger::ssl_keylog_cb);
  }

  return ssl_ctx;
}

//
// QUICConfigParams
//
QUICConfigParams::~QUICConfigParams()
{
  this->_server_supported_groups = static_cast<char *>(ats_free_null(this->_server_supported_groups));
  this->_client_supported_groups = static_cast<char *>(ats_free_null(this->_client_supported_groups));
  this->_qlog_file_base_name     = static_cast<char *>(ats_free_null(this->_qlog_file_base_name));

  SSL_CTX_free(this->_client_ssl_ctx.get());
};

void
QUICConfigParams::initialize()
{
  RecEstablishStaticConfigInt32U("proxy.config.quic.instance_id", &this->_instance_id);
  RecEstablishStaticConfigInt32("proxy.config.quic.connection_table.size", &this->_connection_table_size);
  RecEstablishStaticConfigInt32U("proxy.config.quic.server.stateless_retry_enabled", &this->_stateless_retry);
  RecEstablishStaticConfigInt32U("proxy.config.quic.client.vn_exercise_enabled", &this->_vn_exercise_enabled);
  RecEstablishStaticConfigInt32U("proxy.config.quic.client.cm_exercise_enabled", &this->_cm_exercise_enabled);
  RecEstablishStaticConfigInt32U("proxy.config.quic.client.quantum_readiness_test_enabled",
                                 &this->_quantum_readiness_test_enabled_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.server.quantum_readiness_test_enabled",
                                 &this->_quantum_readiness_test_enabled_in);

  RecGetRecordString_Xmalloc("proxy.config.quic.server.supported_groups", &this->_server_supported_groups);
  RecGetRecordString_Xmalloc("proxy.config.quic.client.supported_groups", &this->_client_supported_groups);
  RecGetRecordString_Xmalloc("proxy.config.quic.client.session_file", &this->_client_session_file);

  // Qlog
  RecGetRecordString_Xmalloc("proxy.config.quic.qlog.file_base", &this->_qlog_file_base_name);

  // Transport Parameters
  RecEstablishStaticConfigInt32U("proxy.config.quic.no_activity_timeout_in", &this->_no_activity_timeout_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.no_activity_timeout_out", &this->_no_activity_timeout_out);
  RecGetRecordString_Xmalloc("proxy.config.quic.preferred_address_ipv4", const_cast<char **>(&this->_preferred_address_ipv4));
  if (this->_preferred_address_ipv4) {
    ats_ip_pton(this->_preferred_address_ipv4, &this->_preferred_endpoint_ipv4);
  }
  RecGetRecordString_Xmalloc("proxy.config.quic.preferred_address_ipv6", const_cast<char **>(&this->_preferred_address_ipv6));
  if (this->_preferred_address_ipv6) {
    ats_ip_pton(this->_preferred_address_ipv6, &this->_preferred_endpoint_ipv6);
  }
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_data_in", &this->_initial_max_data_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_data_out", &this->_initial_max_data_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_bidi_local_in",
                                 &this->_initial_max_stream_data_bidi_local_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_bidi_local_out",
                                 &this->_initial_max_stream_data_bidi_local_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_bidi_remote_in",
                                 &this->_initial_max_stream_data_bidi_remote_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_bidi_remote_out",
                                 &this->_initial_max_stream_data_bidi_remote_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_uni_in", &this->_initial_max_stream_data_uni_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_stream_data_uni_out", &this->_initial_max_stream_data_uni_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_streams_bidi_in", &this->_initial_max_streams_bidi_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_streams_bidi_out", &this->_initial_max_streams_bidi_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_streams_uni_in", &this->_initial_max_streams_uni_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.initial_max_streams_uni_out", &this->_initial_max_streams_uni_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.ack_delay_exponent_in", &this->_ack_delay_exponent_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.ack_delay_exponent_out", &this->_ack_delay_exponent_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_ack_delay_in", &this->_max_ack_delay_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_ack_delay_out", &this->_max_ack_delay_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.active_cid_limit_in", &this->_active_cid_limit_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.active_cid_limit_out", &this->_active_cid_limit_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.disable_active_migration", &this->_disable_active_migration);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_recv_udp_payload_size_in", &this->_max_recv_udp_payload_size_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_recv_udp_payload_size_out", &this->_max_recv_udp_payload_size_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_send_udp_payload_size_in", &this->_max_send_udp_payload_size_in);
  RecEstablishStaticConfigInt32U("proxy.config.quic.max_send_udp_payload_size_out", &this->_max_send_udp_payload_size_out);
  RecEstablishStaticConfigInt32U("proxy.config.quic.disable_http_0_9", &this->_disable_http_0_9);
  RecEstablishStaticConfigInt32U("proxy.config.quic.cc_algorithm", &this->_cc_algorithm);

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

uint32_t
QUICConfigParams::get_max_recv_udp_payload_size_in() const
{
  return this->_max_recv_udp_payload_size_in;
}

uint32_t
QUICConfigParams::get_max_recv_udp_payload_size_out() const
{
  return this->_max_recv_udp_payload_size_out;
}

uint32_t
QUICConfigParams::get_max_send_udp_payload_size_in() const
{
  return this->_max_send_udp_payload_size_in;
}

uint32_t
QUICConfigParams::get_max_send_udp_payload_size_out() const
{
  return this->_max_send_udp_payload_size_out;
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
QUICConfigParams::get_qlog_file_base_name() const
{
  return this->_qlog_file_base_name;
}

bool
QUICConfigParams::disable_http_0_9() const
{
  return this->_disable_http_0_9;
}

quiche_cc_algorithm
QUICConfigParams::get_cc_algorithm() const
{
  switch (this->_cc_algorithm) {
  case 0:
    return QUICHE_CC_RENO;
  case 1:
    return QUICHE_CC_CUBIC;
  default:
    return QUICHE_CC_RENO;
  }
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
