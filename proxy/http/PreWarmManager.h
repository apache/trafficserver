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

#include "PreWarmAlgorithm.h"

#include "P_Net.h"
#include "I_NetVConnection.h"
#include "P_SSLSNI.h"
#include "P_HostDB.h"
#include "YamlSNIConfig.h"
#include "NetTimeout.h"
#include "Milestones.h"

#include "records/DynamicStats.h"

#include <map>
#include <memory>
#include <queue>
#include <string_view>

class PreWarmSM;
class PreWarmManager;

extern ClassAllocator<PreWarmSM> preWarmSMAllocator;
extern PreWarmManager prewarmManager;

namespace PreWarm
{
////
// Dst
//
struct Dst {
  Dst(const std::string &h, in_port_t p, SNIRoutingType t, int a) : host(h), port(p), type(t), alpn_index(a) {}

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

////
// Conf
//
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

////
// Stats
//
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

using StatsIds          = std::array<int, static_cast<size_t>(PreWarm::Stat::LAST_ENTRY)>;
using SPtrConstStatsIds = std::shared_ptr<const StatsIds>;
using StatsIdMap        = std::unordered_map<SPtrConstDst, SPtrConstStatsIds, DstHash, DstKeyEqual>;

} // namespace PreWarm

/**
   @class PreWarmSM
   @brief A state machine to pre-warm connection

   @startuml
   hide empty description
   [*]              --> state_init       : new
   state_init       --> state_dns_lookup : start()
   state_init       --> state_closed     : stop()
   state_dns_lookup --> state_net_open   : HostDB lookup is done
   state_dns_lookup --> state_init       : retry()
   state_dns_lookup --> state_closed     : stop()
   state_net_open   --> state_open       : TCP/TLS Handshake is done
   state_net_open   --> state_init       : retry()
   state_net_open   --> state_closed     : stop()
   state_open       --> state_closed     : move_netvc()\nstop()
   state_closed     --> [*]              : delete
   @enduml
 */
class PreWarmSM : public Continuation
{
public:
  PreWarmSM(){};
  PreWarmSM(const PreWarm::SPtrConstDst &dst, const PreWarm::SPtrConstConf &conf, const PreWarm::SPtrConstStatsIds &stats_ids);
  ~PreWarmSM() override;

  // States
  int state_init(int event, void *data);
  int state_dns_lookup(int event, void *data);
  int state_net_open(int event, void *data);
  int state_open(int event, void *data);
  int state_closed(int event, void *data);

  // Controllers
  void start();
  void retry();
  void stop();
  void destroy();

  // Modifiers
  NetVConnection *move_netvc();
  IOBufferReader *server_buf_reader();

  // References
  bool has_data_from_origin_server() const;

  // NetTimeout
  // TODO: constify
  bool is_active_timeout_expired(ink_hrtime now);
  bool is_inactive_timeout_expired(ink_hrtime now);

  // HostDB inline completion functions
  void process_hostdb_info(HostDBInfo *r);
  void process_srv_info(HostDBInfo *r);

private:
  enum class Milestone {
    INIT = 0,
    DNS_LOOKUP_DONE,
    ESTABLISHED,
    CLOSED,
    LAST_ENTRY,
  };

  Action *_connect(const IpEndpoint &addr);
  void _reset();
  void _record_handshake_time();

  ////
  // Variables
  //
  NetTimeout _timeout{};
  Milestones<Milestone, static_cast<size_t>(Milestone::LAST_ENTRY)> _milestones;

  uint32_t _retry_counter = 0;

  PreWarm::SPtrConstDst _dst;
  PreWarm::SPtrConstConf _conf;
  PreWarm::SPtrConstStatsIds _stats_ids;

  NetVConnection *_netvc            = nullptr;
  Action *_pending_action           = nullptr;
  MIOBuffer *_read_buf              = nullptr;
  IOBufferReader *_read_buf_reader  = nullptr;
  MIOBuffer *_write_buf             = nullptr;
  IOBufferReader *_write_buf_reader = nullptr;
  Event *_retry_event               = nullptr;
};

/**
   @class PreWarmQueue
   @detail
   - Each ET_NET thread has this queue
   - Responsible for the life cycle of PreWarmSM until giving it to HttpSM

   @startuml
   hide empty description
   [*]              --> state_init    : new
   state_init       --> state_running : start pre-warming
   @enduml
 */
class PreWarmQueue : public Continuation
{
public:
  PreWarmQueue();
  ~PreWarmQueue();

  // States
  int state_init(int event, void *data);
  int state_running(int event, void *data);

  // Modifiers for queue
  void push(const PreWarm::SPtrConstDst &dst, PreWarmSM *sm);
  PreWarmSM *dequeue(const PreWarm::SPtrConstDst &dst);

private:
  using Queue = std::deque<PreWarmSM *>;

  struct Stat {
    uint32_t miss = 0;
    uint32_t hit  = 0;
  };

  struct Info {
    Queue *init_list;
    Queue *open_list;
    PreWarm::SPtrConstConf conf;
    PreWarm::SPtrConstStatsIds stats_ids;
    Stat stat;
  };

  using Map = std::unordered_map<PreWarm::SPtrConstDst, Info, PreWarm::DstHash, PreWarm::DstKeyEqual>;

  // construct/destruct PreWarmSM
  void _new_prewarm_sm(const PreWarm::SPtrConstDst &dst, const PreWarm::SPtrConstConf &conf,
                       const PreWarm::SPtrConstStatsIds &stats_ids);
  void _delete_prewarm_sm(PreWarmSM *sm);

  void _reconfigure();
  void _make_queue_empty(Queue *q);
  void _delete_closed_sm(Queue *q);

  // hooks for pre-warming pool size algorithm
  void _prewarm_on_event_interval(const PreWarm::SPtrConstDst &dst, const Info &info);
  void _prewarm_on_dequeue(const PreWarm::SPtrConstDst &dst, const Info &info);

  ////
  // Variables
  //
  PreWarm::Algorithm _algorithm = PreWarm::Algorithm::V1;

  Event *_tick_event       = nullptr;
  ink_hrtime _event_period = HRTIME_SECONDS(1);

  // Force PreWarmSM to open new netvc to keep the connection warm periodically
  ActivityCop<PreWarmSM> _cop;
  DLL<PreWarmSM> _cop_list;

  Map _map;
};

/**
   @class PreWarmManager
   @details
   - Global singleton object
   - Responsible for stats & configs management
 */
class PreWarmManager
{
public:
  static void reconfigure_prewarming_on_threads();

  // Controllers
  void start();
  void reconfigure();
  void stop();

  // References
  const PreWarm::ParsedSNIConf &get_parsed_conf() const;
  const PreWarm::StatsIdMap &get_stats_id_map() const;

  ////
  // Variables
  //
  DynamicStats stats;

private:
  void _parse_sni_conf(PreWarm::ParsedSNIConf &parsed_conf, const SNIConfigParams *sni_conf) const;
  void _register_stats(const PreWarm::ParsedSNIConf &parsed_conf);

  ////
  // Variables
  //
  // For the race of Main Thread (start up) vs Task Thread (config reload)
  Ptr<ProxyMutex> _mutex;

  PreWarm::ParsedSNIConf _parsed_conf;
  PreWarm::StatsIdMap _stats_id_map;
};
