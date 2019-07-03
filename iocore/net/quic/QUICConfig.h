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

#include "ProxyConfig.h"
#include "P_SSLCertLookup.h"

class QUICConfigParams : public ConfigInfo
{
public:
  QUICConfigParams(){};
  ~QUICConfigParams();

  void initialize();

  uint32_t instance_id() const;
  uint32_t num_alt_connection_ids() const;
  uint32_t stateless_retry() const;
  uint32_t vn_exercise_enabled() const;
  uint32_t cm_exercise_enabled() const;

  const char *server_supported_groups() const;
  const char *client_supported_groups() const;
  const char *client_session_file() const;
  const char *client_keylog_file() const;

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

  // Loss Detection
  uint32_t ld_packet_threshold() const;
  float ld_time_threshold() const;
  ink_hrtime ld_granularity() const;
  ink_hrtime ld_initial_rtt() const;

  // Congestion Control
  uint32_t cc_max_datagram_size() const;
  uint32_t cc_initial_window() const;
  uint32_t cc_minimum_window() const;
  float cc_loss_reduction_factor() const;
  uint32_t cc_persistent_congestion_threshold() const;

  static int connection_table_size();
  static uint8_t scid_len();

private:
  static int _connection_table_size;
  // TODO: make configurable
  static const uint8_t _scid_len = 18; //< Length of Source Connection ID

  uint32_t _instance_id            = 0;
  uint32_t _num_alt_connection_ids = 0;
  uint32_t _stateless_retry        = 0;
  uint32_t _vn_exercise_enabled    = 0;
  uint32_t _cm_exercise_enabled    = 0;

  char *_server_supported_groups = nullptr;
  char *_client_supported_groups = nullptr;
  char *_client_session_file     = nullptr;
  char *_client_keylog_file      = nullptr;

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

  // [draft-17 recovery] 6.4.1.  Constants of interest
  uint32_t _ld_packet_threshold = 3;
  float _ld_time_threshold      = 1.25;
  ink_hrtime _ld_granularity    = HRTIME_MSECONDS(1);
  ink_hrtime _ld_initial_rtt    = HRTIME_MSECONDS(100);

  // [draft-11 recovery] 4.7.1.  Constants of interest
  uint32_t _cc_max_datagram_size               = 1200;
  uint32_t _cc_initial_window_scale            = 10; // Actual initial window size is this value multiplied by the _cc_default_mss
  uint32_t _cc_minimum_window_scale            = 2;  // Actual minimum window size is this value multiplied by the _cc_default_mss
  float _cc_loss_reduction_factor              = 0.5;
  uint32_t _cc_persistent_congestion_threshold = 2;
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
