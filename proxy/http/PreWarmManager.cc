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

#include "PreWarmManager.h"
#include "PreWarmConfig.h"

#include "HttpConfig.h"
#include "P_SSLSNI.h"

#include "tscpp/util/PostScript.h"

#include <algorithm>

#define PreWarmSMDebug(fmt, ...) Debug("prewarm_sm", "[%p] " fmt, this, ##__VA_ARGS__);
#define PreWarmSMVDebug(fmt, ...) Debug("v_prewarm_sm", "[%p] " fmt, this, ##__VA_ARGS__);

ClassAllocator<PreWarmSM> preWarmSMAllocator("preWarmSMAllocator");
PreWarmManager prewarmManager;

namespace
{
using namespace std::literals;

constexpr int DOWN_SERVER_TIMEOUT  = 300;
constexpr size_t STAT_NAME_BUF_LEN = 1024;

constexpr std::string_view SRV_TUNNEL_TCP                = "_tunnel._tcp."sv;
constexpr std::string_view CLIENT_SNI_POLICY_SERVER_NAME = "server_name"sv;

std::string_view
alpn_name_for_stat(int alpn_id)
{
  if (alpn_id == TS_ALPN_PROTOCOL_INDEX_HTTP_1_0) {
    return "http1_0"sv;
  } else if (alpn_id == TS_ALPN_PROTOCOL_INDEX_HTTP_1_1) {
    return "http1_1"sv;
  } else if (alpn_id == TS_ALPN_PROTOCOL_INDEX_HTTP_2_0) {
    return "http2"sv;
  } else if (alpn_id == TS_ALPN_PROTOCOL_INDEX_HTTP_3) {
    return "http3"sv;
  } else {
    return "unknown"sv;
  }
}

void
parse_authority(std::string &fqdn, int32_t &port, std::string_view authority)
{
  if (auto pos = authority.find(":"); pos != std::string::npos) {
    fqdn = authority.substr(0, pos);
    port = static_cast<in_port_t>(std::stoi(authority.substr(pos + 1).data()));
  } else {
    fqdn = authority;
    port = -1;
  }
}

////
// Stats
//
constexpr std::string_view STAT_NAME_PREFIX = "proxy.process.tunnel.prewarm"sv;

struct StatEntry {
  std::string_view name;
  RecRawStatSyncCb cb;
};

// the order is the same as PreWarm::Stat
// clang-format off
constexpr StatEntry STAT_ENTRIES[] = {
  {"current_init"sv, RecRawStatSyncSum},
  {"current_open"sv, RecRawStatSyncSum},
  {"total_hit"sv, RecRawStatSyncSum},
  {"total_miss"sv, RecRawStatSyncSum},
  {"total_handshake_time"sv, RecRawStatSyncSum},
  {"total_handshake_count"sv, RecRawStatSyncSum},
  {"total_retry"sv, RecRawStatSyncSum},
};
// clang-format on

} // namespace

////
// PreWarmSM
//
PreWarmSM::PreWarmSM(const PreWarm::SPtrConstDst &dst, const PreWarm::SPtrConstConf &conf,
                     const PreWarm::SPtrConstStatsIds &stats_ids)
  : Continuation(new_ProxyMutex()), _dst(dst), _conf(conf), _stats_ids(stats_ids)
{
  SET_HANDLER(&PreWarmSM::state_init);

  Debug("v_prewarm_conf", "host=%p _dst=%ld _conf=%ld _stats_ids=%ld", dst->host.data(), _dst.use_count(), _conf.use_count(),
        _stats_ids.use_count());
}

PreWarmSM::~PreWarmSM() {}

/**
   Start opening netvc directly
 */
void
PreWarmSM::start()
{
  _reset();
  _retry_counter = 0;

  handleEvent(EVENT_IMMEDIATE);
}

/**
   Retry with Exponential Backoff (through EventProcessor)
 */
void
PreWarmSM::retry()
{
  _reset();

  ink_hrtime delay = HRTIME_SECONDS(1 << _retry_counter);
  ++_retry_counter;
  prewarmManager.stats.increment(_stats_ids->at(static_cast<int>(PreWarm::Stat::RETRY)), 1);

  EThread *ethread = this_ethread();
  _retry_event     = ethread->schedule_in_local(this, delay, EVENT_IMMEDIATE);

  if (_retry_counter % 10 == 0) {
    Warning("retry pre-warming dst=%.*s:%d type=%d alpn=%d retry=%" PRIu32, (int)_dst->host.size(), _dst->host.data(), _dst->port,
            (int)_dst->type, _dst->alpn_index, _retry_counter);
  }
}

/**
   Stop pre-warming. Move to state_closed from any state.
 */
void
PreWarmSM::stop()
{
  if (handler == &PreWarmSM::state_closed) {
    // do nothing
    return;
  }

  _reset();
  SET_HANDLER(&PreWarmSM::state_closed);
  _milestones.mark(Milestone::CLOSED);
}

void
PreWarmSM::destroy()
{
  _reset();

  _dst.reset();
  _conf.reset();
  _stats_ids.reset();

  this->mutex = nullptr;
}

/**
   @brief Give ownership of netvc to the caller

   PreWarmSM will not reveice any event as Continuation anymore.
   Caller can read _read_buf through server_buf_reader().
 */
NetVConnection *
PreWarmSM::move_netvc()
{
  if (handler != &PreWarmSM::state_open) {
    return nullptr;
  }

  NetVConnection *netvc = _netvc;
  _netvc                = nullptr;

  // clear the reference from netvc
  netvc->do_io_read(nullptr, 0, nullptr);
  netvc->do_io_write(nullptr, 0, nullptr);

  _timeout.cancel_active_timeout();
  _timeout.cancel_inactive_timeout();

  if (_pending_action != nullptr) {
    _pending_action->cancel();
    _pending_action = nullptr;
  }

  SET_HANDLER(&PreWarmSM::state_closed);

  return netvc;
}

int
PreWarmSM::state_init(int event, void *data)
{
  switch (event) {
  case EVENT_IMMEDIATE: {
    if (_retry_event != nullptr && data == _retry_event) {
      _retry_event = nullptr;
    }

    SET_HANDLER(&PreWarmSM::state_dns_lookup);
    _timeout.set_active_timeout(_conf->connect_timeout);
    _milestones.mark(Milestone::INIT);

    PreWarmSMDebug("pre-warming a netvc dst=%.*s:%d type=%d alpn=%d retry=%" PRIu32, (int)_dst->host.size(), _dst->host.data(),
                   _dst->port, (int)_dst->type, _dst->alpn_index, _retry_counter);

    if (_conf->srv_enabled) {
      char target[MAXDNAME];
      size_t target_len = 0;

      memcpy(target, SRV_TUNNEL_TCP.data(), SRV_TUNNEL_TCP.size());
      target_len += SRV_TUNNEL_TCP.size();

      memcpy(target + target_len, _dst->host.data(), _dst->host.size());
      target_len += _dst->host.size();

      PreWarmSMVDebug("lookup SRV by %.*s", (int)target_len, target);

      Action *srv_lookup_action_handle = hostDBProcessor.getSRVbyname_imm(
        this, static_cast<cb_process_result_pfn>(&PreWarmSM::process_srv_info), target, target_len);
      if (srv_lookup_action_handle != ACTION_RESULT_DONE) {
        _pending_action = srv_lookup_action_handle;
      }
    } else {
      PreWarmSMVDebug("lookup A/AAAA by %.*s", (int)_dst->host.size(), _dst->host.data());

      Action *dns_lookup_action_handle = hostDBProcessor.getbyname_imm(
        this, static_cast<cb_process_result_pfn>(&PreWarmSM::process_hostdb_info), _dst->host.data(), _dst->host.size());
      if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
        _pending_action = dns_lookup_action_handle;
      }
    }

    break;
  }
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

int
PreWarmSM::state_dns_lookup(int event, void *data)
{
  HostDBInfo *info = static_cast<HostDBInfo *>(data);

  switch (event) {
  case EVENT_HOST_DB_LOOKUP: {
    _pending_action = nullptr;

    if (info == nullptr || info->is_failed()) {
      PreWarmSMVDebug("hostdb lookup is failed");

      retry();
      return EVENT_DONE;
    }

    IpEndpoint addr;

    ats_ip_copy(addr, info->ip());
    addr.network_order_port() = htons(_dst->port);

    if (is_debug_tag_set("v_prewarm_sm")) {
      char addrbuf[INET6_ADDRPORTSTRLEN];
      PreWarmSMVDebug("hostdb lookup is done %s", ats_ip_nptop(addr, addrbuf, sizeof(addrbuf)));
    }

    SET_HANDLER(&PreWarmSM::state_net_open);
    _milestones.mark(Milestone::DNS_LOOKUP_DONE);

    Action *connect_action_handle = _connect(addr);
    if (connect_action_handle != ACTION_RESULT_DONE) {
      _pending_action = connect_action_handle;
    }

    break;
  }
  case EVENT_SRV_LOOKUP: {
    _pending_action = nullptr;
    std::string_view hostname;

    if (info == nullptr || !info->is_srv || !info->round_robin) {
      // no SRV record, fallback to default lookup
      hostname = _dst->host;
    } else {
      HostDBRoundRobin *rr = info->rr();
      HostDBInfo *srv      = nullptr;
      if (rr) {
        char srv_hostname[MAXDNAME] = {0};

        ink_hrtime now = Thread::get_hrtime();
        srv = rr->select_best_srv(srv_hostname, &mutex->thread_holding->generator, ink_hrtime_to_sec(now), DOWN_SERVER_TIMEOUT);
        hostname = std::string_view(srv_hostname);

        if (srv == nullptr) {
          // lookup SRV record failed, fallback to default lookup
          hostname = _dst->host;
        }
      }
    }

    Action *dns_lookup_action_handle = hostDBProcessor.getbyname_imm(
      this, static_cast<cb_process_result_pfn>(&PreWarmSM::process_hostdb_info), hostname.data(), hostname.size());
    if (dns_lookup_action_handle != ACTION_RESULT_DONE) {
      _pending_action = dns_lookup_action_handle;
    }

    break;
  }
  case VC_EVENT_ACTIVE_TIMEOUT:
    retry();
    break;
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

int
PreWarmSM::state_net_open(int event, void *data)
{
  switch (event) {
  case NET_EVENT_OPEN: {
    _pending_action = nullptr;
    _netvc          = static_cast<NetVConnection *>(data);

    // set buffers and (re)enable read/write for check status
    // when TCP/TLS connection is established, VC_EVENT_WRITE_READY will be signaled
    _read_buf        = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    _read_buf_reader = _read_buf->alloc_reader();
    _netvc->do_io_read(this, INT64_MAX, _read_buf);

    _write_buf        = new_MIOBuffer(BUFFER_SIZE_INDEX_4K);
    _write_buf_reader = _write_buf->alloc_reader();
    _netvc->do_io_write(this, INT64_MAX, _write_buf_reader);

    break;
  }
  case VC_EVENT_READ_READY:
    [[fallthrough]];
  case VC_EVENT_WRITE_READY: {
    VIO *vio              = static_cast<VIO *>(data);
    NetVConnection *netvc = static_cast<NetVConnection *>(vio->vc_server);

    ink_release_assert(netvc == _netvc);
    PreWarmSMVDebug("%s Handshake is done netvc=%p", (_dst->type == SNIRoutingType::FORWARD) ? "TCP" : "TLS", _netvc);

    SET_HANDLER(&PreWarmSM::state_open);
    _timeout.cancel_active_timeout();
    _timeout.set_inactive_timeout(_conf->inactive_timeout);
    _milestones.mark(Milestone::ESTABLISHED);
    _record_handshake_time();

    // disable write op of pre-warmed connection
    // keep read op enabled to get EOS event from origin server
    netvc->do_io_write(nullptr, 0, nullptr);

    EThread *ethread = this_ethread();
    ethread->prewarm_queue->push(_dst, this);

    break;
  }
  case NET_EVENT_OPEN_FAILED: {
    if (data != nullptr) {
      const int errnum = -(reinterpret_cast<intptr_t>(data));
      if (errnum == EADDRNOTAVAIL) {
        // exhaust all ephemeral ports, do not retry
        stop();
        break;
      }
      Warning("NET_EVENT_OPEN_FAILED: error message=%s (%d)", strerror(errnum), errnum);
    }
    [[fallthrough]];
  }
  case VC_EVENT_ACTIVE_TIMEOUT:
    [[fallthrough]];
  case VC_EVENT_ERROR:
    [[fallthrough]];
  case VC_EVENT_EOS:
    retry();
    break;
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

int
PreWarmSM::state_open(int event, void *data)
{
  switch (event) {
  case VC_EVENT_READ_READY: {
    // When the origin server sends something, keep it in the buffer. Forward it to the UA when a tunnel is setup
    // (HttpSM::setup_blind_tunnel)
    // - e.g. some HTTP/2 implementations send SETTINGS frame & WINDOW_UPDATE frame immediately when TLS handshake is done.
    VIO *vio              = static_cast<VIO *>(data);
    NetVConnection *netvc = static_cast<NetVConnection *>(vio->vc_server);

    ink_release_assert(netvc == _netvc);

    if (is_debug_tag_set("prewarm_sm")) {
      if (_read_buf_reader->is_read_avail_more_than(0)) {
        uint64_t read_len = _read_buf_reader->read_avail();
        PreWarmSMDebug("buffering data from origin server len=%" PRIu64, read_len);

        if (is_debug_tag_set("v_prewarm_sm")) {
          uint8_t buf[1024];
          read_len = std::min(static_cast<uint64_t>(sizeof(buf)), read_len);
          _read_buf_reader->memcpy(buf, read_len);

          ts::LocalBufferWriter<2048> bw;
          bw.print("{}", ts::bwf::Hex_Dump(buf));

          PreWarmSMVDebug("\n%.*s\n", (int)read_len * 2, bw.data());
        }
      }
    }

    break;
  }
  case VC_EVENT_EOS:
    // possibly inactive timeout at origin server
    [[fallthrough]];
  case VC_EVENT_INACTIVITY_TIMEOUT: {
    PreWarmSMDebug("%s (%d)", get_vc_event_name(event), event);
    stop();
    break;
  }
  case VC_EVENT_ACTIVE_TIMEOUT:
    [[fallthrough]];
  case VC_EVENT_ERROR:
    [[fallthrough]];
  case VC_EVENT_WRITE_READY:
    [[fallthrough]];
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

int
PreWarmSM::state_closed(int event, void *data)
{
  switch (event) {
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

IOBufferReader *
PreWarmSM::server_buf_reader()
{
  return _read_buf_reader;
}

bool
PreWarmSM::has_data_from_origin_server() const
{
  if (_read_buf_reader == nullptr) {
    return false;
  }

  return _read_buf_reader->is_read_avail_more_than(0);
}

bool
PreWarmSM::is_active_timeout_expired(ink_hrtime now)
{
  return _timeout.is_active_timeout_expired(now);
}

bool
PreWarmSM::is_inactive_timeout_expired(ink_hrtime now)
{
  return _timeout.is_inactive_timeout_expired(now);
}

void
PreWarmSM::process_hostdb_info(HostDBInfo *r)
{
  ink_release_assert(this->handler == &PreWarmSM::state_dns_lookup);

  this->handleEvent(EVENT_HOST_DB_LOOKUP, r);
}

void
PreWarmSM::process_srv_info(HostDBInfo *r)
{
  ink_release_assert(this->handler == &PreWarmSM::state_dns_lookup);

  this->handleEvent(EVENT_SRV_LOOKUP, r);
}

Action *
PreWarmSM::_connect(const IpEndpoint &addr)
{
  Action *connect_action_handle = nullptr;

  HttpConfig::scoped_config http_conf_params;

  NetVCOptions opt;
  opt.reset();
  opt.f_blocking_connect = false;
  opt.set_sock_param(http_conf_params->oride.sock_recv_buffer_size_out, http_conf_params->oride.sock_send_buffer_size_out,
                     http_conf_params->oride.sock_option_flag_out, http_conf_params->oride.sock_packet_mark_out,
                     http_conf_params->oride.sock_packet_tos_out);
  opt.f_tcp_fastopen = (http_conf_params->oride.sock_option_flag_out & NetVCOptions::SOCK_OPT_TCP_FAST_OPEN);

  switch (_dst->type) {
  case SNIRoutingType::FORWARD: {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    // TODO: constify UnixNetProcessor::connect_re_internal()
    connect_action_handle = netProcessor.connect_re(this, &addr.sa, &opt);
    break;
  }
  case SNIRoutingType::PARTIAL_BLIND: {
    // SNI
    opt.set_sni_servername(_conf->sni.data(), _conf->sni.size());

    // ALPN
    opt.alpn_protos = SessionProtocolNameRegistry::convert_openssl_alpn_wire_format(_dst->alpn_index);

    // Verify Server Configs
    opt.verifyServerPolicy     = _conf->verify_server_policy;
    opt.verifyServerProperties = _conf->verify_server_properties;

    // Client Cert
    opt.ssl_client_cert_name        = http_conf_params->oride.ssl_client_cert_filename;
    opt.ssl_client_private_key_name = http_conf_params->oride.ssl_client_private_key_filename;
    opt.ssl_client_ca_cert_name     = http_conf_params->oride.ssl_client_ca_cert_filename;

    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    connect_action_handle = sslNetProcessor.connect_re(this, &addr.sa, &opt);
    break;
  }
  default:
    // do nothing
    break;
  }

  return connect_action_handle;
}

/**
   Reset state & *some* members
 */
void
PreWarmSM::_reset()
{
  SET_HANDLER(&PreWarmSM::state_init);

  _timeout.cancel_active_timeout();
  _timeout.cancel_inactive_timeout();

  if (_netvc != nullptr) {
    _netvc->do_io_close();
    _netvc = nullptr;
  }

  if (_pending_action != nullptr) {
    _pending_action->cancel();
    _pending_action = nullptr;
  }

  if (_read_buf != nullptr) {
    // free_MIOBuffer dealloc all readers
    free_MIOBuffer(_read_buf);
    _read_buf        = nullptr;
    _read_buf_reader = nullptr;
  }

  if (_write_buf != nullptr) {
    // free_MIOBuffer dealloc all readers
    free_MIOBuffer(_write_buf);
    _write_buf        = nullptr;
    _write_buf_reader = nullptr;
  }

  if (_retry_event != nullptr) {
    _retry_event->cancel();
    _retry_event = nullptr;
  }
}

void
PreWarmSM::_record_handshake_time()
{
  ink_hrtime duration = _milestones.elapsed(Milestone::INIT, Milestone::ESTABLISHED);

  ink_assert(duration > 0);
  if (duration <= 0) {
    return;
  }

  prewarmManager.stats.increment(_stats_ids->at(static_cast<int>(PreWarm::Stat::HANDSHAKE_TIME)), duration);
  prewarmManager.stats.increment(_stats_ids->at(static_cast<int>(PreWarm::Stat::HANDSHAKE_COUNT)), 1);
}

////
// PreWarmQueue
//
PreWarmQueue::PreWarmQueue() : Continuation(new_ProxyMutex())
{
  SET_HANDLER(&PreWarmQueue::state_init);
}

PreWarmQueue::~PreWarmQueue()
{
  _tick_event->cancel();
  _tick_event = nullptr;

  for (auto &e : _map) {
    Info info = e.second;

    _make_queue_empty(info.init_list);
    delete info.init_list;

    _make_queue_empty(info.open_list);
    delete info.open_list;
  }

  this->mutex = nullptr;
}

int
PreWarmQueue::state_init(int event, void *data)
{
  switch (event) {
  case EVENT_IMMEDIATE: {
    _reconfigure();

    _cop = ActivityCop<PreWarmSM>(this->mutex, &_cop_list, 1);
    _cop.start();

    // schedule tick event
    EThread *ethread = this_ethread();
    _tick_event      = ethread->schedule_every_local(this, _event_period);

    SET_HANDLER(&PreWarmQueue::state_running);

    break;
  }
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

int
PreWarmQueue::state_running(int event, void *data)
{
  switch (event) {
  case EVENT_INTERVAL: {
    for (auto &[dst, info] : _map) {
      // mentain queues
      _delete_closed_sm(info.init_list);
      _delete_closed_sm(info.open_list);

      // pre-warm new connections
      _prewarm_on_event_interval(dst, info);

      // set prewarmManager.stats
      Debug("v_prewarm_q", "dst=%.*s:%d type=%d alpn=%d miss=%d hit=%d init=%d open=%d", (int)dst->host.size(), dst->host.data(),
            dst->port, (int)dst->type, dst->alpn_index, info.stat.miss, info.stat.hit, (int)info.init_list->size(),
            (int)info.open_list->size());

      prewarmManager.stats.set_sum(info.stats_ids->at(static_cast<int>(PreWarm::Stat::INIT_LIST_SIZE)), info.init_list->size());
      prewarmManager.stats.set_sum(info.stats_ids->at(static_cast<int>(PreWarm::Stat::OPEN_LIST_SIZE)), info.open_list->size());
      prewarmManager.stats.increment(info.stats_ids->at(static_cast<int>(PreWarm::Stat::HIT)), info.stat.hit);
      prewarmManager.stats.increment(info.stats_ids->at(static_cast<int>(PreWarm::Stat::MISS)), info.stat.miss);

      // clear PreWarmQueue::Stat
      info.stat.miss = 0;
      info.stat.hit  = 0;
    }
    break;
  }
  case EVENT_IMMEDIATE: {
    _reconfigure();

    // reschedule tick event
    EThread *ethread = this_ethread();
    _tick_event->cancel();
    _tick_event = ethread->schedule_every_local(this, _event_period);

    break;
  }
  default:
    ink_abort("unsupported event=%s (%d)", get_vc_event_name(event), event);
    break;
  }

  return EVENT_DONE;
}

void
PreWarmQueue::push(const PreWarm::SPtrConstDst &dst, PreWarmSM *sm)
{
  ink_release_assert(sm->handler == &PreWarmSM::state_open);

  if (auto res = _map.find(dst); res != _map.end()) {
    Queue *init_list = res->second.init_list;

    // expecting init_list.front() is sm in many cases, if not we need to change container
    for (auto it = init_list->begin(); it != init_list->end(); ++it) {
      if (*it == sm) {
        it = init_list->erase(it);
        break;
      }
    }

    res->second.open_list->push_front(sm);
  }
}

/**
   Use open_list as FILO to adjust size of list. ( A new sm is pushed in front of the list by PreWarmQeueu::push() )
   When the list has redundant sm(s), they will be closed by inactivity timeout.
 */
PreWarmSM *
PreWarmQueue::dequeue(const PreWarm::SPtrConstDst &target)
{
  PreWarmSM *sm = nullptr;

  auto res = _map.find(target);
  if (res == _map.end()) {
    // no such pool
    return nullptr;
  }

  const PreWarm::SPtrConstDst &dst = res->first;
  Info &info                       = res->second;

  Queue *q = info.open_list;
  while (!q->empty()) {
    sm = q->front();
    q->pop_front();

    if (sm->handler == &PreWarmSM::state_open) {
      _cop_list.remove(sm);
      break;
    }

    _delete_prewarm_sm(sm);
    sm = nullptr;
  }

  // stat
  if (sm == nullptr) {
    ++info.stat.miss;
  } else {
    ++info.stat.hit;
  }

  _prewarm_on_dequeue(dst, info);

  return sm;
}

void
PreWarmQueue::_new_prewarm_sm(const PreWarm::SPtrConstDst &dst, const PreWarm::SPtrConstConf &conf,
                              const PreWarm::SPtrConstStatsIds &stats_ids)
{
  EThread *ethread = this_ethread();

  PreWarmSM *sm = THREAD_ALLOC(preWarmSMAllocator, ethread);
  new (sm) PreWarmSM(dst, conf, stats_ids);
  _cop_list.push(sm);

  _map[dst].init_list->push_back(sm);

  SCOPED_MUTEX_LOCK(lock, sm->mutex, ethread);
  sm->start();
}

void
PreWarmQueue::_delete_prewarm_sm(PreWarmSM *sm)
{
  ink_release_assert(sm->handler == &PreWarmSM::state_closed);

  _cop_list.remove(sm);

  sm->destroy();
  THREAD_FREE(sm, preWarmSMAllocator, this_ethread());
}

/**
   Periodical pre-warming

   Try to keep (min <= pool size && pool size <= max)

   V1: Expand the pool size to requested size
   V2: Expand the pool size to current size + miss * rate
 */
void
PreWarmQueue::_prewarm_on_event_interval(const PreWarm::SPtrConstDst &dst, const Info &info)
{
  const uint32_t current_size = info.init_list->size() + info.open_list->size();
  uint32_t n                  = 0;

  switch (_algorithm) {
  case PreWarm::Algorithm::V2: {
    n = PreWarm::prewarm_size_v2_on_event_interval(info.stat.hit, info.stat.miss, current_size, info.conf->min, info.conf->max,
                                                   info.conf->rate);
    break;
  }
  case PreWarm::Algorithm::V1:
    [[fallthrough]];
  default:
    n = PreWarm::prewarm_size_v1_on_event_interval(info.stat.miss + info.stat.hit, current_size, info.conf->min, info.conf->max);
    break;
  }

  Debug("v_prewarm_q", "prewarm_size=%" PRId32, n);

  for (uint32_t i = 0; i < n; ++i) {
    _new_prewarm_sm(dst, info.conf, info.stats_ids);
  }
}

/**
   Event based pre-warming

   V1: Do nothing
   V2: Start pre-warming a new netvc
 */
void
PreWarmQueue::_prewarm_on_dequeue(const PreWarm::SPtrConstDst &dst, const Info &info)
{
  switch (_algorithm) {
  case PreWarm::Algorithm::V2: {
    const int32_t current_size = info.init_list->size() + info.open_list->size();
    if (current_size < info.conf->max) {
      _new_prewarm_sm(dst, info.conf, info.stats_ids);
    }
    break;
  }
  case PreWarm::Algorithm::V1:
    [[fallthrough]];
  default:
    // do nothing
    break;
  }
}

/**
   Reconfigure _map based on new SNIConfig
 */
void
PreWarmQueue::_reconfigure()
{
  {
    PreWarmConfig::scoped_config prewarm_conf;

    _event_period = HRTIME_MSECONDS(prewarm_conf->event_period);
    _algorithm    = PreWarm::algorithm_version(prewarm_conf->algorithm);
  }

  // build new map based on new SNIConfig
  const PreWarm::ParsedSNIConf &new_conf_list = prewarmManager.get_parsed_conf();
  const PreWarm::StatsIdMap &new_stats_id_map = prewarmManager.get_stats_id_map();

  Map new_map;

  for (auto &entry : new_conf_list) {
    const PreWarm::SPtrConstDst &dst = entry.first;
    PreWarm::SPtrConstConf conf      = entry.second;

    if (const auto &res = _map.find(dst); res != _map.end()) {
      // copy from old info
      const Info &old_info = res->second;

      new_map[dst] = Info{old_info.init_list, old_info.open_list, conf, old_info.stats_ids, old_info.stat};
    } else {
      // make new info
      PreWarm::SPtrConstStatsIds stats_ids;
      if (const auto &res = new_stats_id_map.find(dst); res != new_stats_id_map.end()) {
        stats_ids = res->second;
      } else {
        Error("no stats ids found for %s", dst->host.c_str());
        continue;
      }

      Queue *init_list = new Queue();
      Queue *open_list = new Queue();
      new_map[dst]     = Info{init_list, open_list, conf, stats_ids, {}};
    }
  }

  // free unexisting entries
  for (auto &[dst, info] : _map) {
    if (auto entry = new_conf_list.find(dst); entry == new_conf_list.end()) {
      prewarmManager.stats.set_sum(info.stats_ids->at(static_cast<int>(PreWarm::Stat::INIT_LIST_SIZE)), 0);
      prewarmManager.stats.set_sum(info.stats_ids->at(static_cast<int>(PreWarm::Stat::OPEN_LIST_SIZE)), 0);

      _make_queue_empty(info.init_list);
      delete info.init_list;

      _make_queue_empty(info.open_list);
      delete info.open_list;

      info.conf.reset();
      info.stats_ids.reset();
    }
  }

  std::swap(_map, new_map);
}

/**
   Delete all PreWarmSM in the queue
 */
void
PreWarmQueue::_make_queue_empty(Queue *q)
{
  while (!q->empty()) {
    PreWarmSM *sm = q->front();
    q->pop_front();
    sm->stop();
    _delete_prewarm_sm(sm);
  }
}

/**
   Delete closed state PreWarmSM in the queue
 */
void
PreWarmQueue::_delete_closed_sm(Queue *q)
{
  for (auto it = q->begin(); it != q->end();) {
    if ((*it)->handler == &PreWarmSM::state_closed) {
      _delete_prewarm_sm(*it);
      it = q->erase(it);
    } else {
      ++it;
    }
  }
}

////
// PreWarmManager
//
void
PreWarmManager::reconfigure_prewarming_on_threads()
{
  EventProcessor::ThreadGroupDescriptor *tg = &(eventProcessor.thread_group[0]);
  ink_release_assert(memcmp(tg->_name.data(), "ET_NET", 6) == 0);

  Debug("prewarm", "reconfigure prewarming");

  for (int i = 0; i < tg->_count; ++i) {
    EThread *ethread = tg->_thread[i];
    if (ethread->prewarm_queue == nullptr) {
      ethread->prewarm_queue = new PreWarmQueue();
    }
    ethread->schedule_imm_local(ethread->prewarm_queue);
  }
}

void
PreWarmManager::start()
{
  PreWarmConfig::startup();

  _mutex = new_ProxyMutex();

  this->reconfigure();
}

void
PreWarmManager::reconfigure()
{
  if (_mutex == nullptr) {
    // don't reconfigure before start
    return;
  }

  SCOPED_MUTEX_LOCK(lock, _mutex, this_ethread());

  PreWarmConfig::scoped_config prewarm_conf;
  bool is_prewarm_enabled = prewarm_conf->enabled;

  SNIConfig::scoped_config sni_conf;
  for (const auto &item : sni_conf->yaml_sni.items) {
    if (item.tunnel_prewarm == YamlSNIConfig::TunnelPreWarm::ENABLED) {
      is_prewarm_enabled = true;
      break;
    }
  }

  if (is_prewarm_enabled) {
    if (!stats.is_allocated()) {
      stats.init(prewarm_conf->max_stats_size);
    }

    _parsed_conf.clear();
    _parse_sni_conf(_parsed_conf, sni_conf);
    _register_stats(_parsed_conf);

    reconfigure_prewarming_on_threads();
  }
}

/**
   TODO: stop pre-warming
 */
void
PreWarmManager::stop()
{
  _mutex->free();
}

const PreWarm::ParsedSNIConf &
PreWarmManager::get_parsed_conf() const
{
  return _parsed_conf;
}

const PreWarm::StatsIdMap &
PreWarmManager::get_stats_id_map() const
{
  return _stats_id_map;
}

/**
   Convert SNIConfigParams to PreWarm::ParsedSNIConf
 */
void
PreWarmManager::_parse_sni_conf(PreWarm::ParsedSNIConf &parsed_conf, const SNIConfigParams *sni_conf) const
{
  PreWarmConfig::scoped_config prewarm_conf;

  for (const auto &item : sni_conf->yaml_sni.items) {
    if (item.tunnel_type != SNIRoutingType::FORWARD && item.tunnel_type != SNIRoutingType::PARTIAL_BLIND) {
      continue;
    }

    if (item.tunnel_prewarm == YamlSNIConfig::TunnelPreWarm::DISABLED ||
        (item.tunnel_prewarm == YamlSNIConfig::TunnelPreWarm::UNSET && !prewarm_conf->enabled)) {
      continue;
    }

    std::vector<int> alpn_ids = {SessionProtocolNameRegistry::INVALID};
    if (!item.tunnel_alpn.empty() && item.tunnel_type == SNIRoutingType::PARTIAL_BLIND) {
      for (auto &id : item.tunnel_alpn) {
        alpn_ids.push_back(id);
      }
    }

    for (int id : alpn_ids) {
      Debug("prewarm_m", "sni=%s dst=%s type=%d alpn=%d min=%d max=%d c_timeout=%d i_timeout=%d srv=%d", item.fqdn.c_str(),
            item.tunnel_destination.c_str(), (int)item.tunnel_type, id, (int)item.tunnel_prewarm_min, (int)item.tunnel_prewarm_max,
            (int)item.tunnel_prewarm_connect_timeout, (int)item.tunnel_prewarm_inactive_timeout, item.tunnel_prewarm_srv);

      std::string dst_fqdn;
      int32_t port;
      parse_authority(dst_fqdn, port, item.tunnel_destination);

      if (port < 0) {
        if (item.tunnel_type == SNIRoutingType::PARTIAL_BLIND) {
          port = 443;
        } else {
          port = 80;
        }
      }

      PreWarm::SPtrConstDst dst = std::make_shared<const PreWarm::Dst>(dst_fqdn, port, item.tunnel_type, id);

      // clang-format off
      PreWarm::SPtrConstConf conf = std::make_shared<const PreWarm::Conf>(
        item.tunnel_prewarm_min,
        item.tunnel_prewarm_max,
        item.tunnel_prewarm_rate,
        HRTIME_SECONDS(item.tunnel_prewarm_connect_timeout),
        HRTIME_SECONDS(item.tunnel_prewarm_inactive_timeout),
        item.tunnel_prewarm_srv,
        item.verify_server_policy,
        item.verify_server_properties,
        (strcmp(item.client_sni_policy, CLIENT_SNI_POLICY_SERVER_NAME) == 0)? item.fqdn : dst_fqdn
      );
      // clang-format on

      parsed_conf[dst] = conf;
    }
  }
}

/**
   Create stats per pool.
   Registered stats id is stored in _stat_id_map.
 */
void
PreWarmManager::_register_stats(const PreWarm::ParsedSNIConf &parsed_conf)
{
  int stats_counter = 0;

  for (auto &entry : parsed_conf) {
    const PreWarm::SPtrConstDst &dst = entry.first;

    PreWarm::StatsIds ids;
    for (int j = 0; j < static_cast<int>(PreWarm::Stat::LAST_ENTRY); ++j) {
      char name[STAT_NAME_BUF_LEN];

      if (dst->alpn_index != SessionProtocolNameRegistry::INVALID) {
        std::string_view alpn_name = alpn_name_for_stat(dst->alpn_index);

        snprintf(name, sizeof(name), "%s.%.*s:%d.tls.%s.%s", STAT_NAME_PREFIX.data(), static_cast<int>(dst->host.size()),
                 dst->host.data(), dst->port, alpn_name.data(), STAT_ENTRIES[j].name.data());
      } else {
        snprintf(name, sizeof(name), "%s.%.*s:%d.%s.%s", STAT_NAME_PREFIX.data(), static_cast<int>(dst->host.size()),
                 dst->host.data(), dst->port, (dst->type == SNIRoutingType::PARTIAL_BLIND) ? "tls" : "tcp",
                 STAT_ENTRIES[j].name.data());
      }

      int stats_id = stats.find(name);
      if (stats_id < 0) {
        stats_id = stats.create(RECT_PROCESS, name, RECD_INT, STAT_ENTRIES[j].cb);

        if (stats_id < 0) {
          // proxy.config.tunnel.prewarm.max_stats_size is enough?
          Error("couldn't register stat name=%s", name);
        } else {
          ++stats_counter;
        }
      }

      ids[j] = stats_id;

      Debug("v_prewarm_init", "stat id=%d name=%s", stats_id, name);
    }

    _stats_id_map[dst] = std::make_shared<const PreWarm::StatsIds>(ids);
  }

  Note("%d dynamic stats are registered for pre-warming tunnel", stats_counter);
}
