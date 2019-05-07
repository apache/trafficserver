/** @file

  Implementation of Host Proxy routing

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
#include "HostStatus.h"
#include "ProcessManager.h"

static RecRawStatBlock *host_status_rsb = nullptr;

static void
getStatName(std::string &stat_name, const char *name, const char *reason)
{
  stat_name = stat_prefix + name + "_";

  if (reason == nullptr) {
    stat_name += Reasons::MANUAL;
  } else {
    stat_name += reason;
  }
}

static void
mgmt_host_status_up_callback(ts::MemSpan span)
{
  MgmtInt op;
  MgmtMarshallString name;
  MgmtMarshallInt down_time;
  MgmtMarshallString reason;
  std::string reason_stat;
  char *data                             = static_cast<char *>(span.data());
  auto len                               = span.size();
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
  Debug("host_statuses", "%s:%s:%d - data: %s, len: %ld\n", __FILE__, __func__, __LINE__, data, len);

  if (mgmt_message_parse(data, len, fields, countof(fields), &op, &name, &reason, &down_time) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  }
  Debug("host_statuses", "op: %ld, name: %s, down_time: %d, reason: %s", static_cast<long>(op), name, static_cast<int>(down_time),
        reason);
  if (data != nullptr) {
    Debug("host_statuses", "marking up server %s", data);
    HostStatus &hs = HostStatus::instance();
    if (hs.getHostStatId(reason_stat.c_str()) == -1) {
      hs.createHostStat(name);
    }
    hs.setHostStatus(name, HostStatus_t::HOST_STATUS_UP, down_time, reason);
  }
}

static void
mgmt_host_status_down_callback(ts::MemSpan span)
{
  MgmtInt op;
  MgmtMarshallString name;
  MgmtMarshallInt down_time;
  MgmtMarshallString reason;
  std::string reason_stat;
  char *data                             = static_cast<char *>(span.data());
  auto len                               = span.size();
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
  Debug("host_statuses", "%s:%s:%d - data: %s, len: %ld\n", __FILE__, __func__, __LINE__, data, len);

  if (mgmt_message_parse(data, len, fields, countof(fields), &op, &name, &reason, &down_time) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  }
  Debug("host_statuses", "op: %ld, name: %s, down_time: %d, reason: %s", static_cast<long>(op), name, static_cast<int>(down_time),
        reason);

  if (data != nullptr) {
    Debug("host_statuses", "marking down server %s", name);
    HostStatus &hs = HostStatus::instance();
    if (hs.getHostStatId(reason_stat.c_str()) == -1) {
      hs.createHostStat(name);
    }
    hs.setHostStatus(name, HostStatus_t::HOST_STATUS_DOWN, down_time, reason);
  }
}

static void
handle_record_read(const RecRecord *rec, void *edata)
{
  HostStatus &hs = HostStatus::instance();
  std::string hostname;
  std::string reason;

  if (rec) {
    // parse the hostname from the stat name
    char *s = const_cast<char *>(rec->name);
    // 1st move the pointer past the stat prefix.
    s += strlen(stat_prefix.c_str());
    hostname = s;
    // parse the reason from the stat name.
    reason = hostname.substr(hostname.find('_'));
    reason.erase(0, 1);
    // erase the reason tag
    hostname.erase(hostname.find('_'));

    // if the data loaded from stats indicates that the host was down,
    // then update the state so that the host remains down until
    // specifically marked up using traffic_ctl.
    if (rec->data.rec_int == 0 && Reasons::validReason(reason.c_str())) {
      hs.setHostStatus(hostname.c_str(), HOST_STATUS_DOWN, 0, reason.c_str());
    }
  }
}

HostStatus::HostStatus()
{
  ink_rwlock_init(&host_status_rwlock);
  ink_rwlock_init(&host_statids_rwlock);
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_UP, &mgmt_host_status_up_callback);
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_DOWN, &mgmt_host_status_down_callback);
  host_status_rsb = RecAllocateRawStatBlock((int)TS_MAX_API_STATS);
}

HostStatus::~HostStatus()
{
  for (auto &&it : hosts_statuses) {
    ats_free(it.second);
  }
  // release host_stats_ids hash and the read and writer locks.
  ink_rwlock_destroy(&host_status_rwlock);
  ink_rwlock_destroy(&host_statids_rwlock);
}

void
HostStatus::loadHostStatusFromStats()
{
  if (RecLookupMatchingRecords(RECT_ALL, stat_prefix.c_str(), handle_record_read, nullptr) != REC_ERR_OKAY) {
    Error("[HostStatus] - While loading HostStatus stats, there was an Error reading HostStatus stats.");
  }
}

void
HostStatus::setHostStatus(const char *name, HostStatus_t status, const unsigned int down_time, const char *reason)
{
  std::string reason_stat;

  getStatName(reason_stat, name, reason);

  if (getHostStatId(reason_stat.c_str()) == -1) {
    createHostStat(name);
  }

  int stat_id = getHostStatId(reason_stat.c_str());

  // update the stats
  if (stat_id != -1) {
    if (status == HostStatus_t::HOST_STATUS_UP) {
      Debug("host_statuses", "set status up for :  name: %s, status: %d, reason_stat: %s", name, status, reason_stat.c_str());
      RecSetRawStatCount(host_status_rsb, stat_id, 1);
      RecSetRawStatSum(host_status_rsb, stat_id, 1);
    } else {
      Debug("host_statuses", "set status down for :  name: %s, status: %d, reason_stat: %s", name, status, reason_stat.c_str());
      RecSetRawStatCount(host_status_rsb, stat_id, 0);
      RecSetRawStatSum(host_status_rsb, stat_id, 0);
    }
  }
  Debug("host_statuses", "name: %s, status: %d", name, status);

  // update / insert status.
  // using the hash table pointer to store the HostStatus_t value.
  HostStatRec_t *host_stat = nullptr;
  ink_rwlock_wrlock(&host_status_rwlock);
  if (auto it = hosts_statuses.find(name); it != hosts_statuses.end()) {
    host_stat = it->second;
  } else {
    host_stat = static_cast<HostStatRec_t *>(ats_malloc(sizeof(HostStatRec_t)));
    hosts_statuses.emplace(name, host_stat);
  }
  host_stat->status    = status;
  host_stat->down_time = down_time;
  if (status == HostStatus_t::HOST_STATUS_DOWN) {
    host_stat->marked_down = time(0);
  } else {
    host_stat->marked_down = 0;
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // log it.
  if (status == HostStatus_t::HOST_STATUS_DOWN) {
    Note("Host %s has been marked down, down_time: %d - %s.", name, down_time, down_time == 0 ? "indefinitely." : "seconds.");
  } else {
    Note("Host %s has been marked up.", name);
  }
}

HostStatus_t
HostStatus::getHostStatus(const char *name)
{
  HostStatRec_t *_status = 0;
  time_t now             = time(0);

  // the hash table value pointer has the HostStatus_t value.
  ink_rwlock_rdlock(&host_status_rwlock);
  auto it     = hosts_statuses.find(name);
  bool lookup = it != hosts_statuses.end();
  if (lookup) {
    _status = it->second;
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // if the host was marked down and it's down_time has elapsed, mark it up.
  if (lookup && _status->status == HostStatus_t::HOST_STATUS_DOWN && _status->down_time > 0) {
    if ((_status->down_time + _status->marked_down) < now) {
      Debug("host_statuses", "name: %s, now: %ld, down_time: %d, marked_down: %ld", name, now, _status->down_time,
            _status->marked_down);
      setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0, nullptr);
      return HostStatus_t::HOST_STATUS_UP;
    }
  }
  return lookup ? static_cast<HostStatus_t>(_status->status) : HostStatus_t::HOST_STATUS_INIT;
}

void
HostStatus::createHostStat(const char *name)
{
  ink_rwlock_wrlock(&host_statids_rwlock);
  {
    for (const char *i : Reasons::reasons) {
      std::string reason_stat;
      getStatName(reason_stat, name, i);
      if (hosts_stats_ids.find(reason_stat) == hosts_stats_ids.end()) {
        RecRegisterRawStat(host_status_rsb, RECT_PROCESS, (reason_stat).c_str(), RECD_INT, RECP_PERSISTENT, (int)next_stat_id,
                           RecRawStatSyncSum);
        RecSetRawStatCount(host_status_rsb, next_stat_id, 1);
        RecSetRawStatSum(host_status_rsb, next_stat_id, 1);

        hosts_stats_ids.emplace(reason_stat, next_stat_id);

        Debug("host_statuses", "stat name: %s, id: %d", reason_stat.c_str(), next_stat_id);
        next_stat_id++;
      }
    }
  }
  ink_rwlock_unlock(&host_statids_rwlock);
}

int
HostStatus::getHostStatId(const char *stat_name)
{
  int _id = -1;

  ink_rwlock_rdlock(&host_statids_rwlock);
  if (auto it = hosts_stats_ids.find(stat_name); it != hosts_stats_ids.end()) {
    _id = it->second;
  }
  ink_rwlock_unlock(&host_statids_rwlock);
  Debug("host_statuses", "name: %s, id: %d", stat_name, static_cast<int>(_id));

  return _id;
}
