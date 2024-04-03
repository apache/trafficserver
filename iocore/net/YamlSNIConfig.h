/** @file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#pragma once

#include <vector>
#include <string>
#include <optional>
#include <memory>

#include "SSLTypes.h"

#include "tscore/Errata.h"

#define TSDECL(id) constexpr char TS_##id[] = #id
TSDECL(fqdn);
TSDECL(disable_h2);
TSDECL(verify_client);
TSDECL(verify_client_ca_certs);
TSDECL(tunnel_route);
TSDECL(forward_route);
TSDECL(partial_blind_route);
TSDECL(tunnel_alpn);
TSDECL(tunnel_prewarm);
TSDECL(tunnel_prewarm_min);
TSDECL(tunnel_prewarm_max);
TSDECL(tunnel_prewarm_rate);
TSDECL(tunnel_prewarm_connect_timeout);
TSDECL(tunnel_prewarm_inactive_timeout);
TSDECL(tunnel_prewarm_srv);
TSDECL(verify_server_policy);
TSDECL(verify_server_properties);
TSDECL(verify_origin_server);
TSDECL(client_cert);
TSDECL(client_key);
TSDECL(client_sni_policy);
TSDECL(ip_allow);
TSDECL(valid_tls_versions_in);
TSDECL(http2);
TSDECL(http2_buffer_water_mark);
TSDECL(http2_max_settings_frames_per_minute);
TSDECL(http2_max_ping_frames_per_minute);
TSDECL(http2_max_priority_frames_per_minute);
TSDECL(http2_max_rst_stream_frames_per_minute);
TSDECL(http2_max_continuation_frames_per_minute);
TSDECL(host_sni_policy);
#undef TSDECL

struct YamlSNIConfig {
  enum class Policy : uint8_t { DISABLED = 0, PERMISSIVE, ENFORCED, UNSET };
  enum class Property : uint8_t { NONE = 0, SIGNATURE_MASK = 0x1, NAME_MASK = 0x2, ALL_MASK = 0x3, UNSET };
  enum class TLSProtocol : uint8_t { TLSv1 = 0, TLSv1_1, TLSv1_2, TLSv1_3, TLS_MAX = TLSv1_3 };
  enum class TunnelPreWarm : uint8_t { DISABLED = 0, ENABLED, UNSET };

  YamlSNIConfig() {}

  struct Item {
    std::string fqdn;
    std::optional<bool> offer_h2; // Has no value by default, so do not initialize!
    uint8_t verify_client_level = 255;
    std::string verify_client_ca_file;
    std::string verify_client_ca_dir;
    uint8_t host_sni_policy    = 255;
    SNIRoutingType tunnel_type = SNIRoutingType::NONE;
    std::string tunnel_destination;
    Policy verify_server_policy       = Policy::UNSET;
    Property verify_server_properties = Property::UNSET;
    std::string client_cert;
    std::string client_key;
    std::string client_sni_policy;
    std::string ip_allow;
    bool protocol_unset = true;
    unsigned long protocol_mask;
    std::vector<int> tunnel_alpn{};
    std::optional<int> http2_buffer_water_mark;
    std::optional<int> http2_max_settings_frames_per_minute;
    std::optional<int> http2_max_ping_frames_per_minute;
    std::optional<int> http2_max_priority_frames_per_minute;
    std::optional<int> http2_max_rst_stream_frames_per_minute;
    std::optional<int> http2_max_continuation_frames_per_minute;

    bool tunnel_prewarm_srv                  = false;
    uint32_t tunnel_prewarm_min              = 0;
    int32_t tunnel_prewarm_max               = -1;
    double tunnel_prewarm_rate               = 1.0;
    uint32_t tunnel_prewarm_connect_timeout  = 0;
    uint32_t tunnel_prewarm_inactive_timeout = 0;
    TunnelPreWarm tunnel_prewarm             = TunnelPreWarm::UNSET;

    void EnableProtocol(YamlSNIConfig::TLSProtocol proto);
  };

  ts::Errata loader(const std::string &cfgFilename);

  std::vector<YamlSNIConfig::Item> items;
};
