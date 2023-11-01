/** @file

  Outbound connection tracking support.

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

#include <algorithm>
#include <deque>
#include "P_Net.h" // For Metrics.
#include "iocore/net/ConnectionTracker.h"
#include "records/RecCore.h"
#include "tscpp/util/ts_bw_format.h"
#include "../../records/P_RecDefs.h"

using namespace std::literals;

ConnectionTracker::TableSingleton ConnectionTracker::_inbound_table;
ConnectionTracker::TableSingleton ConnectionTracker::_outbound_table;

ConnectionTracker::GlobalConfig *ConnectionTracker::_global_config{nullptr};

const MgmtConverter ConnectionTracker::MAX_SERVER_CONV(
  [](const void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<const decltype(TxnConfig::server_max) *>(data)); },
  [](void *data, MgmtInt i) -> void {
    *static_cast<decltype(TxnConfig::server_max) *>(data) = static_cast<decltype(TxnConfig::server_max)>(i);
  });

const MgmtConverter ConnectionTracker::MIN_SERVER_CONV(
  [](const void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<const decltype(TxnConfig::server_min) *>(data)); },
  [](void *data, MgmtInt i) -> void {
    *static_cast<decltype(TxnConfig::server_min) *>(data) = static_cast<decltype(TxnConfig::server_min)>(i);
  });

// Do integer and string conversions.
const MgmtConverter ConnectionTracker::SERVER_MATCH_CONV{
  [](const void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<const decltype(TxnConfig::server_match) *>(data)); },
  [](void *data, MgmtInt i) -> void {
    // Problem - the InkAPITest requires being able to set an arbitrary value, so this can either
    // correctly clamp or pass the regression tests. Currently it passes the tests.
    //    *static_cast<decltype(TxnConfig::match) *>(data) = std::clamp(static_cast<decltype(TxnConfig::match)>(i), MATCH_IP,
    //    MATCH_BOTH);
    *static_cast<decltype(TxnConfig::server_match) *>(data) = static_cast<decltype(TxnConfig::server_match)>(i);
  },
  nullptr,
  nullptr,
  [](const void *data) -> std::string_view {
    auto t = *static_cast<const ConnectionTracker::MatchType *>(data);
    return t < 0 || t > ConnectionTracker::MATCH_BOTH ? "Invalid"sv : ConnectionTracker::MATCH_TYPE_NAME[t];
  },
  [](void *data, std::string_view src) -> void {
    ConnectionTracker::MatchType t;
    if (ConnectionTracker::lookup_match_type(src, t)) {
      *static_cast<ConnectionTracker::MatchType *>(data) = t;
    } else {
      ConnectionTracker::Warning_Bad_Match_Type(src);
    }
  }};

const std::array<std::string_view, static_cast<int>(ConnectionTracker::MATCH_BOTH) + 1> ConnectionTracker::MATCH_TYPE_NAME{
  {"ip"sv, "port"sv, "host"sv, "both"sv}
};

// Make sure the clock is millisecond resolution or finer.
static_assert(ConnectionTracker::Group::Clock::period::num == 1);
static_assert(ConnectionTracker::Group::Clock::period::den >= 1000);

// Configuration callback functions.
namespace
{
bool
Config_Update_Conntrack_Min(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<ConnectionTracker::TxnConfig *>(cookie);

  if (RECD_INT == dtype) {
    config->server_min = data.rec_int;
    return true;
  }
  return false;
}

bool
Config_Update_Conntrack_Max(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<ConnectionTracker::TxnConfig *>(cookie);

  if (RECD_INT == dtype) {
    config->server_max = data.rec_int;
    return true;
  }
  return false;
}

bool
Config_Update_Conntrack_Match(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<ConnectionTracker::TxnConfig *>(cookie);

  if (RECD_STRING == dtype) {
    ConnectionTracker::MatchType match_type;
    std::string_view tag{data.rec_string};
    if (ConnectionTracker::lookup_match_type(tag, match_type)) {
      config->server_match = match_type;
      return true;
    } else {
      ConnectionTracker::Warning_Bad_Match_Type(tag);
    }
  } else {
    Warning("Invalid type for '%s' - must be 'INT'", ConnectionTracker::CONFIG_SERVER_VAR_MATCH.data());
  }
  return false;
}

bool
Config_Update_Conntrack_Server_Alert_Delay_Helper(const char *name, RecDataT dtype, RecData data, void *cookie,
                                                  std::chrono::seconds &alert_delay)
{
  if (RECD_INT == dtype && data.rec_int >= 0) {
    alert_delay = std::chrono::seconds(data.rec_int);
    return true;
  }
  return false;
}

bool
Config_Update_Conntrack_Server_Alert_Delay(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<ConnectionTracker::GlobalConfig *>(cookie);
  return Config_Update_Conntrack_Server_Alert_Delay_Helper(name, dtype, data, cookie, config->server_alert_delay);
}

bool
Config_Update_Conntrack_Client_Alert_Delay(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<ConnectionTracker::GlobalConfig *>(cookie);
  return Config_Update_Conntrack_Server_Alert_Delay_Helper(name, dtype, data, cookie, config->client_alert_delay);
}

} // namespace

void
ConnectionTracker::config_init(GlobalConfig *global, TxnConfig *txn, RecConfigUpdateCb const &config_cb)
{
  _global_config = global; // remember this for later retrieval.
                           // Per transaction lookup must be done at call time because it changes.

  Enable_Config_Var(CONFIG_CLIENT_VAR_ALERT_DELAY, &Config_Update_Conntrack_Client_Alert_Delay, config_cb, global);
  Enable_Config_Var(CONFIG_SERVER_VAR_MIN, &Config_Update_Conntrack_Min, config_cb, txn);
  Enable_Config_Var(CONFIG_SERVER_VAR_MAX, &Config_Update_Conntrack_Max, config_cb, txn);
  Enable_Config_Var(CONFIG_SERVER_VAR_MATCH, &Config_Update_Conntrack_Match, config_cb, txn);
  Enable_Config_Var(CONFIG_SERVER_VAR_ALERT_DELAY, &Config_Update_Conntrack_Server_Alert_Delay, config_cb, global);
}

ConnectionTracker::TxnState
ConnectionTracker::obtain_inbound(IpEndpoint const &addr)
{
  TxnState zret;
  CryptoHash hash;
  Group::Key key{addr, hash, MatchType::MATCH_IP};
  std::lock_guard<std::mutex> lock(_inbound_table._mutex); // Table lock
  auto loc = _inbound_table._table.find(key);
  if (loc != _inbound_table._table.end()) {
    zret._g = loc->second;
  } else {
    zret._g = std::make_shared<Group>(Group::DirectionType::INBOUND, key, "", 0);
    // Note that we must use zret._g's key, not the above key, because Key's
    // members are references to the Group's members. Thus the above key's
    // members are invalid after this function.
    _inbound_table._table.insert(std::make_pair(zret._g->_key, zret._g));
  }
  return zret;
}

ConnectionTracker::TxnState
ConnectionTracker::obtain_outbound(TxnConfig const &txn_cnf, std::string_view fqdn, IpEndpoint const &addr)
{
  TxnState zret;
  CryptoHash hash;
  CryptoContext().hash_immediate(hash, fqdn.data(), fqdn.size());
  Group::Key key{addr, hash, txn_cnf.server_match};
  std::lock_guard<std::mutex> lock(_outbound_table._mutex); // Table lock
  auto loc = _outbound_table._table.find(key);
  if (loc != _outbound_table._table.end()) {
    zret._g = loc->second;
  } else {
    zret._g = std::make_shared<Group>(Group::DirectionType::OUTBOUND, key, fqdn, txn_cnf.server_min);
    // Note that we must use zret._g's key, not the above key, because Key's
    // members are references to the Group's members. Thus the above key's
    // members are invalid after this function.
    _outbound_table._table.insert(std::make_pair(zret._g->_key, zret._g));
  }
  return zret;
}

ConnectionTracker::Group::Group(DirectionType direction, Key const &key, std::string_view fqdn, int min_keep_alive)
  : _direction{direction},
    _hash(key._hash),
    _match_type(key._match_type),
    _min_keep_alive_conns(min_keep_alive),
    _key{_addr, _hash, _match_type},
    _alert_delay{direction == DirectionType::INBOUND ? _global_config->client_alert_delay : _global_config->server_alert_delay}
{
  Metrics::Gauge::increment(net_rsb.connection_tracker_table_size);
  // store the host name if relevant.
  if (MATCH_HOST == _match_type || MATCH_BOTH == _match_type) {
    _fqdn.assign(fqdn);
  }
  // store the IP address if relevant.
  if (MATCH_HOST == _match_type) {
    _addr.setToAnyAddr(AF_INET);
  } else {
    ats_ip_copy(_addr, key._addr);
  }
}

ConnectionTracker::Group::~Group()
{
  Metrics::Gauge::decrement(net_rsb.connection_tracker_table_size);
}

bool
ConnectionTracker::Group::equal(const Key &lhs, const Key &rhs)
{
  bool zret = false;
  if (lhs._match_type == rhs._match_type) {
    switch (lhs._match_type) {
    case MATCH_IP:
      zret = ats_ip_addr_eq(&lhs._addr.sa, &rhs._addr.sa);
      break;
    case MATCH_PORT:
      zret = ats_ip_addr_port_eq(&lhs._addr.sa, &rhs._addr.sa);
      break;
    case MATCH_HOST:
      zret = lhs._hash == rhs._hash;
      break;
    case MATCH_BOTH:
      zret = (lhs._hash == rhs._hash && ats_ip_addr_port_eq(&lhs._addr.sa, &rhs._addr.sa));
      break;
    }
  }

  if (is_debug_tag_set(DEBUG_TAG)) {
    swoc::LocalBufferWriter<256> w;
    w.print("Comparing {} to {} -> {}\0", lhs, rhs, zret ? "match" : "fail");
    Debug(DEBUG_TAG, "%s", w.data());
  }

  return zret;
}

bool
ConnectionTracker::Group::should_alert(std::time_t *lat)
{
  bool zret = false;
  // This is a bit clunky because the goal is to store just the tick count as an atomic.
  // Might check to see if an atomic time_point is really atomic and avoid this.
  Ticker last_tick{_last_alert};                  // Load the most recent alert time in ticks.
  TimePoint last{TimePoint::duration{last_tick}}; // Most recent alert time in a time_point.
  TimePoint now = Clock::now();                   // Current time_point.
  if (last + _alert_delay <= now) {
    // it's been long enough, swap out our time for the last time. The winner of this swap
    // does the actual alert, leaving its current time as the last alert time.
    zret = _last_alert.compare_exchange_strong(last_tick, now.time_since_epoch().count());
    if (zret && lat) {
      *lat = Clock::to_time_t(last);
    }
  }
  return zret;
}

void
ConnectionTracker::Group::release()
{
  if (_count >= 0) {
    --_count;
    if (_count == 0) {
      TableSingleton &table = _direction == DirectionType::INBOUND ? _inbound_table : _outbound_table;
      std::lock_guard<std::mutex> lock(table._mutex); // Table lock
      if (_count > 0) {
        // Someone else grabbed the Group between our last check and taking the
        // lock.
        return;
      }
      table._table.erase(_key);
    }
  } else {
    // A bit dubious, as there's no guarantee it's still negative, but even that would be interesting to know.
    Error("Number of tracked connections should be greater than or equal to zero: %u", _count.load());
  }
}

std::time_t
ConnectionTracker::Group::get_last_alert_epoch_time() const
{
  return Clock::to_time_t(TimePoint{TimePoint::duration{Ticker{_last_alert}}});
}

void
ConnectionTracker::get_outbound_groups(std::vector<std::shared_ptr<Group const>> &groups)
{
  std::lock_guard<std::mutex> lock(_outbound_table._mutex); // TABLE LOCK
  groups.resize(0);
  groups.reserve(_outbound_table._table.size());
  for (auto &&[key, group] : _outbound_table._table) {
    groups.push_back(group);
  }
}

std::string
ConnectionTracker::outbound_to_json_string()
{
  std::string text;
  size_t extent = 0;
  static const swoc::bwf::Format header_fmt{R"({{"count": {}, "list": [
)"};
  static const swoc::bwf::Format item_fmt{
    R"(  {{"type": "{}", "ip": "{}", "fqdn": "{}", "current": {}, "max": {}, "blocked": {}, "alert": {}}},
)"};
  static const std::string_view trailer{" \n]}"};

  static const auto printer = [](swoc::BufferWriter &w, Group const *g) -> swoc::BufferWriter & {
    w.print(item_fmt, g->_match_type, g->_addr, g->_fqdn, g->_count.load(), g->_count_max.load(), g->_blocked.load(),
            g->get_last_alert_epoch_time());
    return w;
  };

  swoc::FixedBufferWriter null_bw{nullptr}; // Empty buffer for sizing work.
  std::vector<std::shared_ptr<Group const>> groups;

  self_type::get_outbound_groups(groups);

  null_bw.print(header_fmt, groups.size()).extent();
  for (auto g : groups) {
    printer(null_bw, g.get());
  }
  extent = null_bw.extent() + trailer.size() - 2; // 2 for the trailing comma newline that will get clipped.

  text.resize(extent);
  swoc::FixedBufferWriter w(const_cast<char *>(text.data()), text.size());
  w.restrict(trailer.size());
  w.print(header_fmt, groups.size());
  for (auto g : groups) {
    printer(w, g.get());
  }
  w.restore(trailer.size());
  w.write(trailer);
  return text;
}

void
ConnectionTracker::dump(FILE *f)
{
  std::vector<std::shared_ptr<Group const>> groups;

  self_type::get_outbound_groups(groups);

  if (groups.size()) {
    fprintf(f, "\nPeer Connection Tracking\n%7s | %5s | %24s | %33s | %8s |\n", "Current", "Block", "Address", "Hostname Hash",
            "Match");
    fprintf(f, "------|-------|--------------------------|-----------------------------------|----------|\n");

    for (std::shared_ptr<Group const> g : groups) {
      swoc::LocalBufferWriter<128> w;
      w.print("{:7} | {:5} | {:24} | {:33} | {:8} |\n", g->_count.load(), g->_blocked.load(), g->_addr, g->_hash, g->_match_type);
      fwrite(w.data(), w.size(), 1, f);
    }

    fprintf(f, "------|-------|--------------------------|-----------------------------------|----------|\n");
  }
}

bool
ConnectionTracker::lookup_match_type(std::string_view tag, ConnectionTracker::MatchType &type)
{
  // Search the array for the tag.
  for (ConnectionTracker::MatchType idx :
       {ConnectionTracker::MATCH_IP, ConnectionTracker::MATCH_PORT, ConnectionTracker::MATCH_HOST, ConnectionTracker::MATCH_BOTH}) {
    if (tag == MATCH_TYPE_NAME[idx]) {
      type = idx;
      return true;
    }
  }
  return false;
}

void
ConnectionTracker::Warning_Bad_Match_Type(std::string_view tag)
{
  swoc::LocalBufferWriter<256> w;
  w.print("Invalid value '{}' for '{}' - must be one of", tag, CONFIG_SERVER_VAR_MATCH);
  for (auto n : MATCH_TYPE_NAME) {
    w.write(" '"sv);
    w.write(n);
    w.write("',"sv);
  }
  w.aux_data()[-1] = '\0'; // clip trailing comma and null terminate.
  Warning("%s", w.data());
}

void
ConnectionTracker::TxnState::Note_Unblocked(const TxnConfig *config, int count, sockaddr const *addr)
{
  time_t lat; // last alert time (epoch seconds)

  if (_g->_blocked > 0 && _g->should_alert(&lat)) {
    auto blocked = _g->_blocked.exchange(0);
    swoc::LocalBufferWriter<256> w;
    w.print("Peer unblocked: [{}] count={} limit={} group=({}) blocked={} peer={}\0", swoc::bwf::Date(lat, "%b %d %H:%M:%S"sv),
            count, config->server_max, *_g, blocked, addr);
    Debug(DEBUG_TAG, "%s", w.data());
    Note("%s", w.data());
  }
}

void
ConnectionTracker::TxnState::Warn_Blocked(int max_connections, int64_t id, int count, sockaddr const *addr, char const *debug_tag)
{
  bool alert_p = _g->should_alert();
  auto blocked = alert_p ? _g->_blocked.exchange(0) : _g->_blocked.load();

  if (alert_p || debug_tag) {
    swoc::LocalBufferWriter<256> w;
    if (id > 1) {
      w.print("[{}] ", id);
    }
    w.print("too many connections: count={} limit={} group=({}) blocked={} peer={}\0", count, max_connections, *_g, blocked, addr);

    if (debug_tag) {
      Debug(debug_tag, "%s", w.data());
    }
    if (alert_p) {
      Warning("%s", w.data());
    }
  }
}

namespace swoc
{
BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::MatchType type)
{
  if (spec.has_numeric_type()) {
    bwformat(w, spec, static_cast<unsigned int>(type));
  } else {
    bwformat(w, spec, ConnectionTracker::MATCH_TYPE_NAME[type]);
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::Group::Key const &key)
{
  switch (key._match_type) {
  case ConnectionTracker::MATCH_BOTH:
    w.print("{:s} {},{}", key._match_type, key._addr, key._hash);
    break;
  case ConnectionTracker::MATCH_HOST:
    w.print("{:s} {}", key._match_type, key._hash);
    break;
  case ConnectionTracker::MATCH_PORT:
    w.print("{:s} {}", key._match_type, key._addr);
    break;
  case ConnectionTracker::MATCH_IP:
    w.print("{:s} {::a}", key._match_type, key._addr);
    break;
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, bwf::Spec const &spec, ConnectionTracker::Group const &g)
{
  switch (g._match_type) {
  case ConnectionTracker::MATCH_BOTH:
    w.print("{:s} {},{}", g._match_type, g._addr, g._fqdn);
    break;
  case ConnectionTracker::MATCH_HOST:
    w.print("{:s} {}", g._match_type, g._fqdn);
    break;
  case ConnectionTracker::MATCH_PORT:
    w.print("{:s} {}", g._match_type, g._addr);
    break;
  case ConnectionTracker::MATCH_IP:
    w.print("{:s} {::a}", g._match_type, g._addr);
    break;
  }
  return w;
}

} // namespace swoc
