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
#include <records/P_RecDefs.h>
#include "HttpConnectionCount.h"
#include <ts/bwf_std_format.h>
#include <ts/BufferWriter.h>

using namespace std::literals;

OutboundConnTrack::Imp OutboundConnTrack::_imp;

OutboundConnTrack::GlobalConfig *OutboundConnTrack::_global_config{nullptr};

const MgmtConverter OutboundConnTrack::MAX_CONV{
  [](void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<decltype(TxnConfig::max) *>(data)); },
  [](void *data, MgmtInt i) -> void { *static_cast<decltype(TxnConfig::max) *>(data) = static_cast<decltype(TxnConfig::max)>(i); },
  nullptr,
  nullptr,
  nullptr,
  nullptr};

// Do integer and string conversions.
const MgmtConverter OutboundConnTrack::MATCH_CONV{
  [](void *data) -> MgmtInt { return static_cast<MgmtInt>(*static_cast<decltype(TxnConfig::match) *>(data)); },
  [](void *data, MgmtInt i) -> void {
    // Problem - the InkAPITest requires being able to set an arbitrary value, so this can either
    // correctly clamp or pass the regression tests. Currently it passes the tests.
    //    *static_cast<decltype(TxnConfig::match) *>(data) = std::clamp(static_cast<decltype(TxnConfig::match)>(i), MATCH_IP,
    //    MATCH_BOTH);
    *static_cast<decltype(TxnConfig::match) *>(data) = static_cast<decltype(TxnConfig::match)>(i);
  },
  nullptr,
  nullptr,
  [](void *data) -> std::string_view {
    auto t = *static_cast<OutboundConnTrack::MatchType *>(data);
    return t < 0 || t > OutboundConnTrack::MATCH_BOTH ? "Invalid"sv : OutboundConnTrack::MATCH_TYPE_NAME[t];
  },
  [](void *data, std::string_view src) -> void {
    OutboundConnTrack::MatchType t;
    if (OutboundConnTrack::lookup_match_type(src, t)) {
      *static_cast<OutboundConnTrack::MatchType *>(data) = t;
    } else {
      OutboundConnTrack::Warning_Bad_Match_Type(src);
    }
  }};

const std::array<std::string_view, static_cast<int>(OutboundConnTrack::MATCH_BOTH) + 1> OutboundConnTrack::MATCH_TYPE_NAME{
  {"ip"sv, "port"sv, "host"sv, "both"sv}};

// Make sure the clock is millisecond resolution or finer.
static_assert(OutboundConnTrack::Group::Clock::period::num == 1);
static_assert(OutboundConnTrack::Group::Clock::period::den >= 1000);

// Configuration callback functions.
namespace
{
int
Config_Update_Conntrack_Max(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<OutboundConnTrack::TxnConfig *>(cookie);

  if (RECD_INT == dtype) {
    config->max = data.rec_int;
  }
  return REC_ERR_OKAY;
}

int
Config_Update_Conntrack_Queue_Size(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<OutboundConnTrack::GlobalConfig *>(cookie);

  if (RECD_INT == dtype) {
    config->queue_size = data.rec_int;
  }
  return REC_ERR_OKAY;
}

int
Config_Update_Conntrack_Queue_Delay(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<OutboundConnTrack::GlobalConfig *>(cookie);

  if (RECD_INT == dtype && data.rec_int > 0) {
    config->queue_delay = std::chrono::milliseconds(data.rec_int);
  }
  return REC_ERR_OKAY;
}

int
Config_Update_Conntrack_Match(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<OutboundConnTrack::TxnConfig *>(cookie);

  if (RECD_STRING == dtype) {
    OutboundConnTrack::MatchType match_type;
    std::string_view tag{data.rec_string};
    if (OutboundConnTrack::lookup_match_type(tag, match_type)) {
      config->match = match_type;
    } else {
      OutboundConnTrack::Warning_Bad_Match_Type(tag);
    }
  } else {
    Warning("Invalid type for '%s' - must be 'INT'", OutboundConnTrack::CONFIG_VAR_MATCH.data());
  }
  return REC_ERR_OKAY;
}

int
Config_Update_Conntrack_Alert_Delay(const char *name, RecDataT dtype, RecData data, void *cookie)
{
  auto config = static_cast<OutboundConnTrack::GlobalConfig *>(cookie);

  if (RECD_INT == dtype && data.rec_int >= 0) {
    config->alert_delay = std::chrono::seconds(data.rec_int);
  }
  return REC_ERR_OKAY;
}

// Do the initial load of a configuration var by grabbing the raw value from the records data
// and calling the update callback. This must be a function because that's how the records
// interface works. Everything needed is already in the record @a r.
void
Load_Config_Var(RecRecord const *r, void *)
{
  for (auto cb = r->config_meta.update_cb_list; nullptr != cb; cb = cb->next) {
    cb->update_cb(r->name, r->data_type, r->data, cb->update_cookie);
  }
}

} // namespace

void
OutboundConnTrack::config_init(GlobalConfig *global, TxnConfig *txn)
{
  _global_config = global; // remember this for later retrieval.
                           // Per transaction lookup must be done at call time because it changes.

  RecRegisterConfigUpdateCb(CONFIG_VAR_MAX.data(), &Config_Update_Conntrack_Max, txn);
  RecRegisterConfigUpdateCb(CONFIG_VAR_MATCH.data(), &Config_Update_Conntrack_Match, txn);
  RecRegisterConfigUpdateCb(CONFIG_VAR_QUEUE_SIZE.data(), &Config_Update_Conntrack_Queue_Size, global);
  RecRegisterConfigUpdateCb(CONFIG_VAR_QUEUE_DELAY.data(), &Config_Update_Conntrack_Queue_Delay, global);
  RecRegisterConfigUpdateCb(CONFIG_VAR_ALERT_DELAY.data(), &Config_Update_Conntrack_Alert_Delay, global);

  // Load 'em up by firing off the config update callback.
  RecLookupRecord(CONFIG_VAR_MAX.data(), &Load_Config_Var, nullptr, true);
  RecLookupRecord(CONFIG_VAR_MATCH.data(), &Load_Config_Var, nullptr, true);
  RecLookupRecord(CONFIG_VAR_QUEUE_SIZE.data(), &Load_Config_Var, nullptr, true);
  RecLookupRecord(CONFIG_VAR_QUEUE_DELAY.data(), &Load_Config_Var, nullptr, true);
  RecLookupRecord(CONFIG_VAR_ALERT_DELAY.data(), &Load_Config_Var, nullptr, true);
}

OutboundConnTrack::TxnState
OutboundConnTrack::obtain(TxnConfig const &txn_cnf, std::string_view fqdn, IpEndpoint const &addr)
{
  TxnState zret;
  CryptoHash hash;
  CryptoContext().hash_immediate(hash, fqdn.data(), fqdn.size());
  Group::Key key{addr, hash, txn_cnf.match};
  std::lock_guard<std::mutex> lock(_imp._mutex); // Table lock
  auto loc = _imp._table.find(key);
  if (loc.isValid()) {
    zret._g = loc;
  } else {
    zret._g = new Group(key, fqdn);
    _imp._table.insert(zret._g);
  }
  return zret;
}

bool
OutboundConnTrack::Group::equal(const Key &lhs, const Key &rhs)
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
    ts::LocalBufferWriter<256> w;
    w.print("Comparing {} to {} -> {}\0", lhs, rhs, zret ? "match" : "fail");
    Debug(DEBUG_TAG, "%s", w.data());
  }

  return zret;
}

bool
OutboundConnTrack::Group::should_alert(std::time_t *lat)
{
  bool zret = false;
  // This is a bit clunky because the goal is to store just the tick count as an atomic.
  // Might check to see if an atomic time_point is really atomic and avoid this.
  Ticker last_tick{_last_alert};                  // Load the most recent alert time in ticks.
  TimePoint last{TimePoint::duration{last_tick}}; // Most recent alert time in a time_point.
  TimePoint now = Clock::now();                   // Current time_point.
  if (last + _global_config->alert_delay <= now) {
    // it's been long enough, swap out our time for the last time. The winner of this swap
    // does the actual alert, leaving its current time as the last alert time.
    zret = _last_alert.compare_exchange_strong(last_tick, now.time_since_epoch().count());
    if (zret && lat) {
      *lat = Clock::to_time_t(last);
    }
  }
  return zret;
}

std::time_t
OutboundConnTrack::Group::get_last_alert_epoch_time() const
{
  return Clock::to_time_t(TimePoint{TimePoint::duration{Ticker{_last_alert}}});
}

void
OutboundConnTrack::get(std::vector<Group const *> &groups)
{
  std::lock_guard<std::mutex> lock(_imp._mutex); // TABLE LOCK
  groups.resize(0);
  groups.reserve(_imp._table.count());
  for (Group const &g : _imp._table) {
    groups.push_back(&g);
  }
}

std::string
OutboundConnTrack::to_json_string()
{
  std::string text;
  size_t extent = 0;
  static const ts::BWFormat header_fmt{R"({{"count": {}, "list": [
)"};
  static const ts::BWFormat item_fmt{
    R"(  {{"type": "{}", "ip": "{}", "fqdn": "{}", "current": {}, "max": {}, "blocked": {}, "queued": {}, "alert": {}}},
)"};
  static const std::string_view trailer{" \n]}"};

  static const auto printer = [](ts::BufferWriter &w, Group const *g) -> ts::BufferWriter & {
    w.print(item_fmt, g->_match_type, g->_addr, g->_fqdn, g->_count.load(), g->_count_max.load(), g->_blocked.load(),
            g->_rescheduled.load(), g->get_last_alert_epoch_time());
    return w;
  };

  ts::FixedBufferWriter null_bw{nullptr}; // Empty buffer for sizing work.
  std::vector<Group const *> groups;

  self_type::get(groups);

  null_bw.print(header_fmt, groups.size()).extent();
  for (auto g : groups) {
    printer(null_bw, g);
  }
  extent = null_bw.extent() + trailer.size() - 2; // 2 for the trailing comma newline that will get clipped.

  text.resize(extent);
  ts::FixedBufferWriter w(const_cast<char *>(text.data()), text.size());
  w.clip(trailer.size());
  w.print(header_fmt, groups.size());
  for (auto g : groups) {
    printer(w, g);
  }
  w.extend(trailer.size());
  w.write(trailer);
  return text;
}

void
OutboundConnTrack::dump(FILE *f)
{
  std::vector<Group const *> groups;

  self_type::get(groups);

  if (groups.size()) {
    fprintf(f, "\nUpstream Connection Tracking\n%7s | %5s | %10s | %24s | %33s | %8s |\n", "Current", "Block", "Queue", "Address",
            "Hostname Hash", "Match");
    fprintf(f, "------|-------|---------|--------------------------|-----------------------------------|----------|\n");

    for (Group const *g : groups) {
      ts::LocalBufferWriter<128> w;
      w.print("{:7} | {:5} | {:5} | {:24} | {:33} | {:8} |\n", g->_count.load(), g->_blocked.load(), g->_rescheduled.load(),
              g->_addr, g->_hash, g->_match_type);
      fwrite(w.data(), w.size(), 1, f);
    }

    fprintf(f, "------|-------|-------|--------------------------|-----------------------------------|----------|\n");
  }
}

struct ShowConnectionCount : public ShowCont {
  ShowConnectionCount(Continuation *c, HTTPHdr *h) : ShowCont(c, h) { SET_HANDLER(&ShowConnectionCount::showHandler); }
  int
  showHandler(int event, Event *e)
  {
    CHECK_SHOW(show(OutboundConnTrack::to_json_string().c_str()));
    return completeJson(event, e);
  }
};

Action *
register_ShowConnectionCount(Continuation *c, HTTPHdr *h)
{
  ShowConnectionCount *s = new ShowConnectionCount(c, h);
  this_ethread()->schedule_imm(s);
  return &s->action;
}

bool
OutboundConnTrack::lookup_match_type(std::string_view tag, OutboundConnTrack::MatchType &type)
{
  // Search the array for the tag.
  for (OutboundConnTrack::MatchType idx :
       {OutboundConnTrack::MATCH_IP, OutboundConnTrack::MATCH_PORT, OutboundConnTrack::MATCH_HOST, OutboundConnTrack::MATCH_BOTH}) {
    if (tag == MATCH_TYPE_NAME[idx]) {
      type = idx;
      return true;
    }
  }
  return false;
}

void
OutboundConnTrack::Warning_Bad_Match_Type(std::string_view tag)
{
  ts::LocalBufferWriter<256> w;
  w.print("Invalid value '{}' for '{}' - must be one of", tag, CONFIG_VAR_MATCH);
  for (auto n : MATCH_TYPE_NAME) {
    w.write(" '"sv);
    w.write(n);
    w.write("',"sv);
  }
  w.auxBuffer()[-1] = '\0'; // clip trailing comma and null terminate.
  Warning("%s", w.data());
}

void
OutboundConnTrack::TxnState::Note_Unblocked(TxnConfig *config, int count, sockaddr const *addr)
{
  time_t lat; // last alert time (epoch seconds)

  if ((_g->_blocked > 0 || _g->_rescheduled > 0) && _g->should_alert(&lat)) {
    auto blocked     = _g->_blocked.exchange(0);
    auto rescheduled = _g->_rescheduled.exchange(0);
    ts::LocalBufferWriter<256> w;
    w.print("upstream unblocked: [{}] count={} limit={} group=({}) blocked={} queued={} upstream={}\0",
            ts::bwf::Date(lat, "%b %d %H:%M:%S"sv), count, config->max, *_g, blocked, rescheduled, addr);
    Debug(DEBUG_TAG, "%s", w.data());
    Note("%s", w.data());
  }
}

void
OutboundConnTrack::TxnState::Warn_Blocked(TxnConfig *config, int64_t sm_id, int count, sockaddr const *addr, char const *debug_tag)
{
  bool alert_p     = _g->should_alert();
  auto blocked     = alert_p ? _g->_blocked.exchange(0) : _g->_blocked.load();
  auto rescheduled = alert_p ? _g->_rescheduled.exchange(0) : _g->_rescheduled.load();

  if (alert_p || debug_tag) {
    ts::LocalBufferWriter<256> w;
    w.print("[{}] too many connections: count={} limit={} group=({}) blocked={} queued={} upstream={}\0", sm_id, count, config->max,
            *_g, blocked, rescheduled, addr);

    if (debug_tag) {
      Debug(debug_tag, "%s", w.data());
    }
    if (alert_p) {
      Warning("%s", w.data());
    }
  }
}

namespace ts
{
BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, OutboundConnTrack::Group::Key const &key)
{
  switch (key._match_type) {
  case OutboundConnTrack::MATCH_BOTH:
    w.print("{:s} {},{}", key._match_type, key._addr, key._hash);
    break;
  case OutboundConnTrack::MATCH_HOST:
    w.print("{:s} {}", key._match_type, key._hash);
    break;
  case OutboundConnTrack::MATCH_PORT:
    w.print("{:s} {}", key._match_type, key._addr);
    break;
  case OutboundConnTrack::MATCH_IP:
    w.print("{:s} {::a}", key._match_type, key._addr);
    break;
  }
  return w;
}

BufferWriter &
bwformat(BufferWriter &w, BWFSpec const &spec, OutboundConnTrack::Group const &g)
{
  switch (g._match_type) {
  case OutboundConnTrack::MATCH_BOTH:
    w.print("{:s} {},{}", g._match_type, g._addr, g._fqdn);
    break;
  case OutboundConnTrack::MATCH_HOST:
    w.print("{:s} {}", g._match_type, g._fqdn);
    break;
  case OutboundConnTrack::MATCH_PORT:
    w.print("{:s} {}", g._match_type, g._addr);
    break;
  case OutboundConnTrack::MATCH_IP:
    w.print("{:s} {::a}", g._match_type, g._addr);
    break;
  }
  return w;
}

} // namespace ts
