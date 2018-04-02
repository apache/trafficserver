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

class QUICConfigParams : public ConfigInfo
{
public:
  QUICConfigParams(){};
  ~QUICConfigParams();

  void initialize();

  uint32_t no_activity_timeout_in() const;
  uint32_t no_activity_timeout_out() const;
  uint32_t initial_max_data() const;
  uint32_t initial_max_stream_data() const;
  uint32_t initial_max_stream_id_bidi_in() const;
  uint32_t initial_max_stream_id_bidi_out() const;
  uint32_t initial_max_stream_id_uni_in() const;
  uint32_t initial_max_stream_id_uni_out() const;
  uint32_t server_id() const;
  static int connection_table_size();
  uint32_t stateless_retry() const;
  const char *server_supported_groups() const;
  const char *client_supported_groups() const;

  SSL_CTX *server_ssl_ctx() const;
  SSL_CTX *client_ssl_ctx() const;

  uint32_t ld_max_tlps() const;
  uint32_t ld_reordering_threshold() const;
  float ld_time_reordering_fraction() const;
  uint32_t ld_time_loss_detection() const;
  ink_hrtime ld_min_tlp_timeout() const;
  ink_hrtime ld_min_rto_timeout() const;
  ink_hrtime ld_delayed_ack_timeout() const;
  ink_hrtime ld_default_initial_rtt() const;

  uint32_t cc_default_mss() const;
  uint32_t cc_initial_window() const;
  uint32_t cc_minimum_window() const;
  float cc_loss_reduction_factor() const;

private:
  static int _connection_table_size;

  // FIXME Fill appropriate default values in RecordsConfig.cc
  uint32_t _no_activity_timeout_in  = 0;
  uint32_t _no_activity_timeout_out = 0;
  uint32_t _initial_max_data        = 0;
  uint32_t _initial_max_stream_data = 0;
  uint32_t _server_id               = 0;
  uint32_t _stateless_retry         = 0;

  uint32_t _initial_max_stream_id_bidi_in  = 100;
  uint32_t _initial_max_stream_id_bidi_out = 101;
  uint32_t _initial_max_stream_id_uni_in   = 102;
  uint32_t _initial_max_stream_id_uni_out  = 103;

  char *_server_supported_groups = nullptr;
  char *_client_supported_groups = nullptr;

  // TODO: integrate with SSLCertLookup or SNIConfigParams
  SSL_CTX *_server_ssl_ctx = nullptr;
  SSL_CTX *_client_ssl_ctx = nullptr;

  // [draft-10 recovery] - 3.4.1.  Constants of interest
  uint32_t _ld_max_tlps              = 2;
  uint32_t _ld_reordering_threshold  = 3;
  float _ld_time_reordering_fraction = 0.125;
  uint32_t _ld_time_loss_detection   = 0;
  ink_hrtime _ld_min_tlp_timeout     = HRTIME_MSECONDS(10);
  ink_hrtime _ld_min_rto_timeout     = HRTIME_MSECONDS(200);
  ink_hrtime _ld_delayed_ack_timeout = HRTIME_MSECONDS(25);
  ink_hrtime _ld_default_initial_rtt = HRTIME_MSECONDS(100);

  // [draft-10 recovery] - 4.7.1.  Constants of interest
  uint32_t _cc_default_mss          = 1460;
  uint32_t _cc_initial_window_scale = 10; // Actual initial window size is this value multiplied by the _cc_default_mss
  uint32_t _cc_minimum_window_scale = 2;  // Actual minimum window size is this value multiplied by the _cc_default_mss
  float _cc_loss_reduction_factor   = 0.5;
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
