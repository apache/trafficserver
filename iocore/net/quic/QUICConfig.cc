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
#include "P_OCSPStapling.h"

#include "QUICGlobals.h"
#include "QUICTransportParameters.h"

#define QUICConfDebug(fmt, ...) Debug("quic_conf", fmt, ##__VA_ARGS__)


int QUICConfig::_config_id                   = 0;
int QUICConfigParams::_connection_table_size = 65521;
int QUICCertConfig::_config_id               = 0;

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

#ifdef SSL_MODE_QUIC_HACK
  // tatsuhiro-t's custom OpenSSL for QUIC draft-13
  // https://github.com/tatsuhiro-t/openssl/tree/quic-draft-13
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_QUIC_HACK);
#endif

  return ssl_ctx;
}

/**
   ALPN and SNI should be set to SSL object with NETVC_OPTIONS
 **/
static SSL_CTX *
quic_init_client_ssl_ctx(const QUICConfigParams *params)
{
  SSL_CTX *ssl_ctx = quic_new_ssl_ctx();

  if (params->client_supported_groups() != nullptr) {
    if (SSL_CTX_set1_groups_list(ssl_ctx, params->client_supported_groups()) != 1) {
      Error("SSL_CTX_set1_groups_list failed");
    }
  }

  if (params->session_file() != nullptr) {
    SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_CLIENT | SSL_SESS_CACHE_NO_INTERNAL_STORE);
    SSL_CTX_sess_set_new_cb(ssl_ctx, QUIC::ssl_client_new_session);
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

  SSL_CTX_free(this->_client_ssl_ctx);
};

void
QUICConfigParams::initialize()
{
  REC_EstablishStaticConfigInt32U(this->_instance_id, "proxy.config.quic.instance_id");
  REC_EstablishStaticConfigInt32(this->_connection_table_size, "proxy.config.quic.connection_table.size");
  REC_EstablishStaticConfigInt32U(this->_num_alt_connection_ids, "proxy.config.quic.num_alt_connection_ids");
  REC_EstablishStaticConfigInt32U(this->_stateless_retry, "proxy.config.quic.server.stateless_retry_enabled");
  REC_EstablishStaticConfigInt32U(this->_vn_exercise_enabled, "proxy.config.quic.client.vn_exercise_enabled");
  REC_EstablishStaticConfigInt32U(this->_cm_exercise_enabled, "proxy.config.quic.client.cm_exercise_enabled");

  REC_ReadConfigStringAlloc(this->_server_supported_groups, "proxy.config.quic.server.supported_groups");
  REC_ReadConfigStringAlloc(this->_client_supported_groups, "proxy.config.quic.client.supported_groups");
  REC_ReadConfigStringAlloc(this->_session_file, "proxy.config.quic.client.session_file");

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

  // Loss Detection
  REC_EstablishStaticConfigInt32U(this->_ld_packet_threshold, "proxy.config.quic.loss_detection.packet_threshold");
  REC_EstablishStaticConfigFloat(this->_ld_time_threshold, "proxy.config.quic.loss_detection.time_threshold");

  uint32_t timeout = 0;
  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.granularity");
  this->_ld_granularity = HRTIME_MSECONDS(timeout);

  REC_EstablishStaticConfigInt32U(timeout, "proxy.config.quic.loss_detection.initial_rtt");
  this->_ld_initial_rtt = HRTIME_MSECONDS(timeout);

  // Congestion Control
  REC_EstablishStaticConfigInt32U(this->_cc_max_datagram_size, "proxy.config.quic.congestion_control.max_datagram_size");
  REC_EstablishStaticConfigInt32U(this->_cc_initial_window_scale, "proxy.config.quic.congestion_control.initial_window_scale");
  REC_EstablishStaticConfigInt32U(this->_cc_minimum_window_scale, "proxy.config.quic.congestion_control.minimum_window_scale");
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
QUICConfigParams::num_alt_connection_ids() const
{
  return this->_num_alt_connection_ids;
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
QUICConfigParams::cc_max_datagram_size() const
{
  return _cc_max_datagram_size;
}

uint32_t
QUICConfigParams::cc_initial_window() const
{
  // kInitialWindow:  Default limit on the initial amount of data in
  // flight, in bytes.  Taken from [RFC6928].  The RECOMMENDED value is
  // the minimum of 10 * kMaxDatagramSize and max(2* kMaxDatagramSize,
  // 14600)).
  return std::min(_cc_initial_window_scale * _cc_max_datagram_size,
                  std::max(2 * _cc_max_datagram_size, static_cast<uint32_t>(14600)));
}

uint32_t
QUICConfigParams::cc_minimum_window() const
{
  return _cc_minimum_window_scale * _cc_max_datagram_size;
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
QUICConfigParams::session_file() const
{
  return _session_file;
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

SSL_CTX *
QUICMultiCertConfigLoader::init_server_ssl_ctx(std::vector<X509 *> &cert_list, const SSLMultiCertConfigParams *multi_cert_params)
{
  const SSLConfigParams *params = this->_params;

  SSL_CTX *ctx = this->default_server_ssl_ctx();

  if (multi_cert_params) {
    if (multi_cert_params->dialog) {
      // TODO: dialog support
    }

    if (multi_cert_params->cert) {
      if (!SSLMultiCertConfigLoader::load_certs(ctx, cert_list, params, multi_cert_params)) {
        goto fail;
      }
    }

    // SSL_CTX_load_verify_locations() builds the cert chain from the
    // serverCACertFilename if that is not nullptr.  Otherwise, it uses the hashed
    // symlinks in serverCACertPath.
    //
    // if ssl_ca_name is NOT configured for this cert in ssl_multicert.config
    //     AND
    // if proxy.config.ssl.CA.cert.filename and proxy.config.ssl.CA.cert.path
    //     are configured
    //   pass that file as the chain (include all certs in that file)
    // else if proxy.config.ssl.CA.cert.path is configured (and
    //       proxy.config.ssl.CA.cert.filename is nullptr)
    //   use the hashed symlinks in that directory to build the chain
    if (!multi_cert_params->ca && params->serverCACertPath != nullptr) {
      if ((!SSL_CTX_load_verify_locations(ctx, params->serverCACertFilename, params->serverCACertPath)) ||
          (!SSL_CTX_set_default_verify_paths(ctx))) {
        Error("invalid CA Certificate file or CA Certificate path");
        goto fail;
      }
    }
  }

  if (params->clientCertLevel != 0) {
    // TODO: client cert support
  }

  if (!SSLMultiCertConfigLoader::set_session_id_context(ctx, params, multi_cert_params)) {
    goto fail;
  }

  if (params->server_tls13_cipher_suites != nullptr) {
    if (!SSL_CTX_set_ciphersuites(ctx, params->server_tls13_cipher_suites)) {
      Error("invalid tls server cipher suites in records.config");
      goto fail;
    }
  }

  if (params->server_groups_list != nullptr) {
    if (!SSL_CTX_set1_groups_list(ctx, params->server_groups_list)) {
      Error("invalid groups list for server in records.config");
      goto fail;
    }
  }

  // SSL_CTX_set_info_callback(ctx, ssl_callback_info);

  SSL_CTX_set_alpn_select_cb(ctx, QUIC::ssl_select_next_protocol, nullptr);

  if (SSLConfigParams::ssl_ocsp_enabled) {
    QUICConfDebug("SSL OCSP Stapling is enabled");
    SSL_CTX_set_tlsext_status_cb(ctx, ssl_callback_ocsp_stapling);
    const char *cert_name = multi_cert_params ? multi_cert_params->cert.get() : nullptr;

    for (auto cert : cert_list) {
      if (!ssl_stapling_init_cert(ctx, cert, cert_name)) {
        Warning("failed to configure SSL_CTX for OCSP Stapling info for certificate at %s", cert_name);
      }
    }
  } else {
    QUICConfDebug("SSL OCSP Stapling is disabled");
  }

  if (SSLConfigParams::init_ssl_ctx_cb) {
    SSLConfigParams::init_ssl_ctx_cb(ctx, true);
  }

  return ctx;

fail:
  SSLReleaseContext(ctx);
  for (auto cert : cert_list) {
    X509_free(cert);
  }

  return nullptr;
}

SSL_CTX *
QUICMultiCertConfigLoader::_store_ssl_ctx(SSLCertLookup *lookup, const SSLMultiCertConfigParams *multi_cert_params)
{
  std::vector<X509 *> cert_list;
  SSL_CTX *ctx                   = this->init_server_ssl_ctx(cert_list, multi_cert_params);
  ssl_ticket_key_block *keyblock = nullptr;
  bool inserted                  = false;

  if (!ctx || !multi_cert_params) {
    lookup->is_valid = false;
    return nullptr;
  }

  const char *certname = multi_cert_params->cert.get();
  for (auto cert : cert_list) {
    if (0 > SSLMultiCertConfigLoader::check_server_cert_now(cert, certname)) {
      /* At this point, we know cert is bad, and we've already printed a
         descriptive reason as to why cert is bad to the log file */
      QUICConfDebug("Marking certificate as NOT VALID: %s", certname);
      lookup->is_valid = false;
    }
  }

  // Index this certificate by the specified IP(v6) address. If the address is "*", make it the default context.
  if (multi_cert_params->addr) {
    if (strcmp(multi_cert_params->addr, "*") == 0) {
      if (lookup->insert(multi_cert_params->addr, SSLCertContext(ctx, multi_cert_params->opt, keyblock)) >= 0) {
        inserted            = true;
        lookup->ssl_default = ctx;
        this->_set_handshake_callbacks(ctx);
      }
    } else {
      IpEndpoint ep;

      if (ats_ip_pton(multi_cert_params->addr, &ep) == 0) {
        QUICConfDebug("mapping '%s' to certificate %s", (const char *)multi_cert_params->addr, (const char *)certname);
        if (lookup->insert(ep, SSLCertContext(ctx, multi_cert_params->opt, keyblock)) >= 0) {
          inserted = true;
        }
      } else {
        Error("'%s' is not a valid IPv4 or IPv6 address", (const char *)multi_cert_params->addr);
        lookup->is_valid = false;
      }
    }
  }

  // Insert additional mappings. Note that this maps multiple keys to the same value, so when
  // this code is updated to reconfigure the SSL certificates, it will need some sort of
  // refcounting or alternate way of avoiding double frees.
  QUICConfDebug("importing SNI names from %s", (const char *)certname);
  for (auto cert : cert_list) {
    if (SSLMultiCertConfigLoader::index_certificate(lookup, SSLCertContext(ctx, multi_cert_params->opt), cert, certname)) {
      inserted = true;
    }
  }

  if (inserted) {
    if (SSLConfigParams::init_ssl_ctx_cb) {
      SSLConfigParams::init_ssl_ctx_cb(ctx, true);
    }
  }

  if (!inserted) {
    SSLReleaseContext(ctx);
    ctx = nullptr;
  }

  for (auto &i : cert_list) {
    X509_free(i);
  }

  return ctx;
}

void
QUICMultiCertConfigLoader::_set_handshake_callbacks(SSL_CTX *ssl_ctx)
{
  SSL_CTX_set_cert_cb(ssl_ctx, QUIC::ssl_cert_cb, nullptr);
  SSL_CTX_set_tlsext_servername_callback(ssl_ctx, QUIC::ssl_sni_cb);

  // Set client hello callback if needed
  // SSL_CTX_set_client_hello_cb(ctx, QUIC::ssl_client_hello_cb, nullptr);
}
