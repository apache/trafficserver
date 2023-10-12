/** @file

  Pre-Warming NetVConnection

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

// inknet
#include "SSLTypes.h"
#include "YamlSNIConfig.h"

// api
#include "api/Metrics.h"

// records
#include "records/I_RecHttp.h"

// tscore
#include "tscore/CryptoHash.h"
#include "tscore/ink_hrtime.h"

#include <netinet/in.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PreWarm
{
struct Dst {
  Dst(std::string_view h, in_port_t p, SNIRoutingType t, int a) : host(h), port(p), type(t), alpn_index(a) {}

  std::string host;
  in_port_t port      = 0;
  SNIRoutingType type = SNIRoutingType::NONE;
  int alpn_index      = SessionProtocolNameRegistry::INVALID;
};

using SPtrConstDst = std::shared_ptr<const Dst>;

struct DstHash {
  size_t
  operator()(const PreWarm::SPtrConstDst &dst) const
  {
    CryptoHash hash;
    CryptoContext context{};

    context.update(dst->host.data(), dst->host.size());
    context.update(&dst->port, sizeof(in_port_t));
    context.update(&dst->type, sizeof(SNIRoutingType));
    context.update(&dst->alpn_index, sizeof(int));

    context.finalize(hash);

    return static_cast<size_t>(hash.fold());
  }
};

struct DstKeyEqual {
  bool
  operator()(const PreWarm::SPtrConstDst &x, const PreWarm::SPtrConstDst &y) const
  {
    return x->host == y->host && x->port == y->port && x->type == y->type && x->alpn_index == y->alpn_index;
  }
};

struct Conf {
  Conf(uint32_t min, int32_t max, double rate, ink_hrtime connect_timeout, ink_hrtime inactive_timeout, bool srv_enabled,
       YamlSNIConfig::Policy verify_server_policy, YamlSNIConfig::Property verify_server_properties, const std::string &sni)
    : min(min),
      max(max),
      rate(rate),
      connect_timeout(connect_timeout),
      inactive_timeout(inactive_timeout),
      srv_enabled(srv_enabled),
      verify_server_policy(verify_server_policy),
      verify_server_properties(verify_server_properties),
      sni(sni)
  {
  }

  uint32_t min                                     = 0;
  int32_t max                                      = 0;
  double rate                                      = 1.0;
  ink_hrtime connect_timeout                       = 0;
  ink_hrtime inactive_timeout                      = 0;
  bool srv_enabled                                 = false;
  YamlSNIConfig::Policy verify_server_policy       = YamlSNIConfig::Policy::UNSET;
  YamlSNIConfig::Property verify_server_properties = YamlSNIConfig::Property::UNSET;
  std::string sni;
};

using SPtrConstConf = std::shared_ptr<const Conf>;
using ParsedSNIConf = std::unordered_map<SPtrConstDst, SPtrConstConf, DstHash, DstKeyEqual>;

enum class Stat {
  INIT_LIST_SIZE = 0,
  OPEN_LIST_SIZE,
  HIT,
  MISS,
  HANDSHAKE_TIME,
  HANDSHAKE_COUNT,
  RETRY,
  LAST_ENTRY,
};

using StatsIds          = std::array<ts::Metrics::IntType *, static_cast<size_t>(PreWarm::Stat::LAST_ENTRY)>;
using SPtrConstStatsIds = std::shared_ptr<const StatsIds>;
using StatsIdMap        = std::unordered_map<SPtrConstDst, SPtrConstStatsIds, DstHash, DstKeyEqual>;
} // namespace PreWarm
