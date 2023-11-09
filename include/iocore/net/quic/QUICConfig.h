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

#pragma once

#include <openssl/ssl.h>

#include "iocore/eventsystem/ConfigProcessor.h"
#include "../../../../src/iocore/net/P_SSLCertLookup.h"

class QUICConfigParams : public ConfigInfo
{
public:
  QUICConfigParams(){};
  ~QUICConfigParams();

  void initialize();

  uint32_t instance_id() const;
  uint32_t stateless_retry() const;
  uint32_t vn_exercise_enabled() const;
  uint32_t cm_exercise_enabled() const;
  uint32_t quantum_readiness_test_enabled_in() const;
  uint32_t quantum_readiness_test_enabled_out() const;

  const char *server_supported_groups() const;
  const char *client_supported_groups() const;
  const char *client_session_file() const;

  // qlog
  const char *get_qlog_file_base_name() const;

  shared_SSL_CTX client_ssl_ctx() const;

  // Transport Parameters
  uint32_t no_activity_timeout_in() const;
  uint32_t no_activity_timeout_out() const;
  const IpEndpoint *preferred_address_ipv4() const;
  const IpEndpoint *preferred_address_ipv6() const;
  uint32_t initial_max_data_in() const;
  uint32_t initial_max_data_out() const;
  uint32_t initial_max_stream_data_bidi_local_in() const;
  uint32_t initial_max_stream_data_bidi_local_out() const;
  uint32_t initial_max_stream_data_bidi_remote_in() const;
  uint32_t initial_max_stream_data_bidi_remote_out() const;
  uint32_t initial_max_stream_data_uni_in() const;
  uint32_t initial_max_stream_data_uni_out() const;
  uint64_t initial_max_streams_bidi_in() const;
  uint64_t initial_max_streams_bidi_out() const;
  uint64_t initial_max_streams_uni_in() const;
  uint64_t initial_max_streams_uni_out() const;
  uint8_t ack_delay_exponent_in() const;
  uint8_t ack_delay_exponent_out() const;
  uint8_t max_ack_delay_in() const;
  uint8_t max_ack_delay_out() const;
  uint8_t active_cid_limit_in() const;
  uint8_t active_cid_limit_out() const;
  bool disable_active_migration() const;
  uint32_t get_max_recv_udp_payload_size_in() const;
  uint32_t get_max_recv_udp_payload_size_out() const;

  uint32_t get_max_send_udp_payload_size_in() const;
  uint32_t get_max_send_udp_payload_size_out() const;

  static int connection_table_size();
  static uint8_t scid_len();

  bool disable_http_0_9() const;

private:
  static int _connection_table_size;
  // TODO: make configurable
  static const uint8_t _scid_len = 18; //< Length of Source Connection ID

  uint32_t _instance_id                        = 0;
  uint32_t _stateless_retry                    = 0;
  uint32_t _vn_exercise_enabled                = 0;
  uint32_t _cm_exercise_enabled                = 0;
  uint32_t _quantum_readiness_test_enabled_in  = 0;
  uint32_t _quantum_readiness_test_enabled_out = 0;

  char *_server_supported_groups = nullptr;
  char *_client_supported_groups = nullptr;
  char *_client_session_file     = nullptr;
  // qlog
  char *_qlog_file_base_name = nullptr;

  shared_SSL_CTX _client_ssl_ctx = nullptr;

  // Transport Parameters
  uint32_t _no_activity_timeout_in    = 0;
  uint32_t _no_activity_timeout_out   = 0;
  const char *_preferred_address_ipv4 = nullptr;
  const char *_preferred_address_ipv6 = nullptr;
  IpEndpoint _preferred_endpoint_ipv4;
  IpEndpoint _preferred_endpoint_ipv6;
  uint32_t _initial_max_data_in                     = 0;
  uint32_t _initial_max_data_out                    = 0;
  uint32_t _initial_max_stream_data_bidi_local_in   = 0;
  uint32_t _initial_max_stream_data_bidi_local_out  = 0;
  uint32_t _initial_max_stream_data_bidi_remote_in  = 0;
  uint32_t _initial_max_stream_data_bidi_remote_out = 0;
  uint32_t _initial_max_stream_data_uni_in          = 0;
  uint32_t _initial_max_stream_data_uni_out         = 0;
  uint32_t _initial_max_streams_bidi_in             = 0;
  uint32_t _initial_max_streams_bidi_out            = 0;
  uint32_t _initial_max_streams_uni_in              = 0;
  uint32_t _initial_max_streams_uni_out             = 0;
  uint32_t _ack_delay_exponent_in                   = 0;
  uint32_t _ack_delay_exponent_out                  = 0;
  uint32_t _max_ack_delay_in                        = 0;
  uint32_t _max_ack_delay_out                       = 0;
  uint32_t _active_cid_limit_in                     = 0;
  uint32_t _active_cid_limit_out                    = 0;
  uint32_t _disable_active_migration                = 0;
  uint32_t _max_recv_udp_payload_size_in            = 0;
  uint32_t _max_recv_udp_payload_size_out           = 0;

  uint32_t _max_send_udp_payload_size_in  = 0;
  uint32_t _max_send_udp_payload_size_out = 0;

  uint32_t _disable_http_0_9 = 1;
};

class QUICConfig
{
public:
  static void startup();
  static void reconfigure();
  static QUICConfigParams *acquire();
  static void release(QUICConfigParams *params);

  using scoped_config = ConfigProcessor::scoped_config<QUICConfig, QUICConfigParams>;

private:
  static int _config_id;
};

SSL_CTX *quic_new_ssl_ctx();
