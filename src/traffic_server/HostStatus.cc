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

inline void
getStatName(std::string &stat_name, const char *name)
{
  stat_name = stat_prefix + name;
}

static void
mgmt_host_status_up_callback(ts::MemSpan span)
{
  MgmtInt op;
  MgmtMarshallString name;
  MgmtMarshallInt down_time;
  MgmtMarshallString reason_str;
  std::string stat_name;
  char buf[1024]                         = {0};
  char *data                             = static_cast<char *>(span.data());
  auto len                               = span.size();
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
  Debug("host_statuses", "%s:%s:%d - data: %s, len: %ld\n", __FILE__, __func__, __LINE__, data, len);

  if (mgmt_message_parse(data, len, fields, countof(fields), &op, &name, &reason_str, &down_time) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  }
  Debug("host_statuses", "op: %ld, name: %s, down_time: %d, reason_str: %s", static_cast<long>(op), name,
        static_cast<int>(down_time), reason_str);

  unsigned int reason = Reason::getReason(reason_str);

  getStatName(stat_name, name);
  if (data != nullptr) {
    Debug("host_statuses", "marking up server %s", data);
    HostStatus &hs = HostStatus::instance();
    if (hs.getHostStat(stat_name, buf, 1024) == REC_ERR_FAIL) {
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
  MgmtMarshallString reason_str;
  std::string stat_name;
  char *data                             = static_cast<char *>(span.data());
  char buf[1024]                         = {0};
  auto len                               = span.size();
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
  Debug("host_statuses", "%s:%s:%d - data: %s, len: %ld\n", __FILE__, __func__, __LINE__, data, len);

  if (mgmt_message_parse(data, len, fields, countof(fields), &op, &name, &reason_str, &down_time) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  }
  Debug("host_statuses", "op: %ld, name: %s, down_time: %d, reason_str: %s", static_cast<long>(op), name,
        static_cast<int>(down_time), reason_str);

  unsigned int reason = Reason::getReason(reason_str);

  if (data != nullptr) {
    Debug("host_statuses", "marking down server %s", name);
    HostStatus &hs = HostStatus::instance();
    if (hs.getHostStat(stat_name, buf, 1024) == REC_ERR_FAIL) {
      hs.createHostStat(name);
    }
    hs.setHostStatus(name, HostStatus_t::HOST_STATUS_DOWN, down_time, reason);
  }
}

HostStatRec::HostStatRec()
  : status(HOST_STATUS_UP),
    reasons(0),
    active_marked_down(0),
    local_marked_down(0),
    manual_marked_down(0),
    self_detect_marked_down(0),
    active_down_time(0),
    local_down_time(0),
    manual_down_time(0){};

HostStatRec::HostStatRec(std::string str)
{
  std::vector<std::string> v1;
  std::stringstream ss1(str);

  reasons = 0;

  // parse the csv strings from the stat record value string.
  while (ss1.good()) {
    char b1[64];
    ss1.getline(b1, 64, ',');
    v1.push_back(b1);
  }

  // v1 contains 5 strings.
  ink_assert(v1.size() == 5);

  // set the status and reasons fields.
  for (unsigned int i = 0; i < v1.size(); i++) {
    if (i == 0) { // set the status field
      if (v1.at(i).compare("HOST_STATUS_UP") == 0) {
        status = HOST_STATUS_UP;
      } else if (v1.at(i).compare("HOST_STATUS_DOWN") == 0) {
        status = HOST_STATUS_DOWN;
      }
    } else { // parse and set remaining reason fields.
      std::vector<std::string> v2;
      v2.clear();
      std::stringstream ss2(v1.at(i));
      while (ss2.good()) {
        char b2[64];
        ss2.getline(b2, 64, ':');
        v2.push_back(b2);
      }
      // v2 contains 4 strings.
      ink_assert(v2.size() == 3 || v2.size() == 4);

      if (v2.at(0).compare("ACTIVE") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::ACTIVE;
        } else if (reasons & Reason::ACTIVE) {
          reasons ^= Reason::ACTIVE;
        }
        active_marked_down = atoi(v2.at(2).c_str());
        active_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("LOCAL") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::LOCAL;
        } else if (reasons & Reason::LOCAL) {
          reasons ^= Reason::LOCAL;
        }
        local_marked_down = atoi(v2.at(2).c_str());
        local_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("MANUAL") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::MANUAL;
        } else if (reasons & Reason::MANUAL) {
          reasons ^= Reason::MANUAL;
        }
        manual_marked_down = atoi(v2.at(2).c_str());
        manual_down_time   = atoi(v2.at(3).c_str());
      }
      if (v2.at(0).compare("SELF_DETECT") == 0) {
        if (v2.at(1).compare("DOWN") == 0) {
          reasons |= Reason::SELF_DETECT;
        } else if (reasons & Reason::SELF_DETECT) {
          reasons ^= Reason::SELF_DETECT;
        }
        self_detect_marked_down = atoi(v2.at(2).c_str());
      }
    }
  }
}

static void
handle_record_read(const RecRecord *rec, void *edata)
{
  HostStatus &hs = HostStatus::instance();
  std::string hostname;

  if (rec) {
    Debug("host_statuses", "name: %s", rec->name);

    // parse the hostname from the stat name
    char *s = const_cast<char *>(rec->name);
    // 1st move the pointer past the stat prefix.
    s += stat_prefix.length();
    hostname = s;
    hs.createHostStat(hostname.c_str(), rec->data.rec_string);
    HostStatRec h(rec->data.rec_string);
    hs.loadRecord(hostname, h);
  }
}

HostStatus::HostStatus()
{
  ink_rwlock_init(&host_status_rwlock);
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_UP, &mgmt_host_status_up_callback);
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_DOWN, &mgmt_host_status_down_callback);
  host_status_rsb = RecAllocateRawStatBlock((int)TS_MAX_API_STATS);
}

HostStatus::~HostStatus()
{
  for (auto &&it : hosts_statuses) {
    ats_free(it.second);
  }
  // release the read and writer locks.
  ink_rwlock_destroy(&host_status_rwlock);
}

void
HostStatus::loadHostStatusFromStats()
{
  if (RecLookupMatchingRecords(RECT_PROCESS, stat_prefix.c_str(), handle_record_read, nullptr) != REC_ERR_OKAY) {
    Error("[HostStatus] - While loading HostStatus stats, there was an Error reading HostStatus stats.");
  }
}

void
HostStatus::loadRecord(std::string &name, HostStatRec &h)
{
  HostStatRec *host_stat = nullptr;
  Debug("host_statuses", "loading host status record for %s", name.c_str());
  ink_rwlock_wrlock(&host_status_rwlock);
  {
    if (auto it = hosts_statuses.find(name.c_str()); it != hosts_statuses.end()) {
      host_stat = it->second;
    } else {
      host_stat  = static_cast<HostStatRec *>(ats_malloc(sizeof(HostStatRec)));
      *host_stat = h;
      hosts_statuses.emplace(name, host_stat);
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);

  *host_stat = h;
}

void
HostStatus::setHostStatus(const char *name, HostStatus_t status, const unsigned int down_time, const unsigned int reason)
{
  std::string stat_name;
  char buf[1024] = {0};

  getStatName(stat_name, name);

  if (getHostStat(stat_name, buf, 1024) == REC_ERR_FAIL) {
    createHostStat(name);
  }

  RecErrT result = getHostStat(stat_name, buf, 1024);

  // update / insert status.
  // using the hash table pointer to store the HostStatus_t value.
  HostStatRec *host_stat = nullptr;
  ink_rwlock_wrlock(&host_status_rwlock);
  {
    if (auto it = hosts_statuses.find(name); it != hosts_statuses.end()) {
      host_stat = it->second;
    } else {
      host_stat = static_cast<HostStatRec *>(ats_malloc(sizeof(HostStatRec)));
      bzero(host_stat, sizeof(HostStatRec));
      hosts_statuses.emplace(name, host_stat);
    }
    if (reason & Reason::ACTIVE) {
      Debug("host_statuses", "for host %s set status: %s, Reason:ACTIVE", name, HostStatusNames[status]);
      if (status == HostStatus_t::HOST_STATUS_DOWN) {
        host_stat->active_marked_down = time(0);
        host_stat->active_down_time   = down_time;
        host_stat->reasons |= Reason::ACTIVE;
      } else {
        host_stat->active_marked_down = 0;
        host_stat->active_down_time   = 0;
        if (host_stat->reasons & Reason::ACTIVE) {
          host_stat->reasons ^= Reason::ACTIVE;
        }
      }
    }
    if (reason & Reason::LOCAL) {
      Debug("host_statuses", "for host %s set status: %s, Reason:LOCAL", name, HostStatusNames[status]);
      if (status == HostStatus_t::HOST_STATUS_DOWN) {
        host_stat->local_marked_down = time(0);
        host_stat->local_down_time   = down_time;
        host_stat->reasons |= Reason::LOCAL;
      } else {
        host_stat->local_marked_down = 0;
        host_stat->local_down_time   = 0;
        if (host_stat->reasons & Reason::LOCAL) {
          host_stat->reasons ^= Reason::LOCAL;
        }
      }
    }
    if (reason & Reason::MANUAL) {
      Debug("host_statuses", "for host %s set status: %s, Reason:MANUAL", name, HostStatusNames[status]);
      if (status == HostStatus_t::HOST_STATUS_DOWN) {
        host_stat->manual_marked_down = time(0);
        host_stat->manual_down_time   = down_time;
        host_stat->reasons |= Reason::MANUAL;
      } else {
        host_stat->manual_marked_down = 0;
        host_stat->manual_down_time   = 0;
        if (host_stat->reasons & Reason::MANUAL) {
          host_stat->reasons ^= Reason::MANUAL;
        }
      }
    }
    if (reason & Reason::SELF_DETECT) {
      Debug("host_statuses", "for host %s set status: %s, Reason:SELF_DETECT", name, HostStatusNames[status]);
      if (status == HostStatus_t::HOST_STATUS_DOWN) {
        host_stat->self_detect_marked_down = time(0);
        host_stat->reasons |= Reason::SELF_DETECT;
      } else {
        host_stat->self_detect_marked_down = 0;
        if (host_stat->reasons & Reason::SELF_DETECT) {
          host_stat->reasons ^= Reason::SELF_DETECT;
        }
      }
    }
    if (status == HostStatus_t::HOST_STATUS_UP) {
      if (host_stat->reasons == 0) {
        host_stat->status = HostStatus_t::HOST_STATUS_UP;
      }
      Debug("host_statuses", "reasons: %d, status: %s", host_stat->reasons, HostStatusNames[host_stat->status]);
    } else {
      host_stat->status = status;
      Debug("host_statuses", "reasons: %d, status: %s", host_stat->reasons, HostStatusNames[host_stat->status]);
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // update the stats
  if (result == REC_ERR_OKAY) {
    std::stringstream status_rec;
    status_rec << *host_stat;
    RecSetRecordString(stat_name.c_str(), const_cast<char *>(status_rec.str().c_str()), REC_SOURCE_EXPLICIT, true, false);
    if (status == HostStatus_t::HOST_STATUS_UP) {
      Debug("host_statuses", "set status up for name: %s, status: %d, stat_name: %s", name, status, stat_name.c_str());
    } else {
      Debug("host_statuses", "set status down for name: %s, status: %d, stat_name: %s", name, status, stat_name.c_str());
    }
  }
  Debug("host_statuses", "name: %s, status: %d", name, status);

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
  HostStatRec *_status = 0;
  time_t now           = time(0);
  bool lookup          = false;

  // the hash table value pointer has the HostStatus_t value.
  ink_rwlock_rdlock(&host_status_rwlock);
  {
    auto it = hosts_statuses.find(name);
    lookup  = it != hosts_statuses.end();
    if (lookup) {
      _status = it->second;
    }
  }
  ink_rwlock_unlock(&host_status_rwlock);

  // if the host was marked down and it's down_time has elapsed, mark it up.
  if (lookup && _status->status == HostStatus_t::HOST_STATUS_DOWN) {
    unsigned int reasons = _status->reasons;
    if ((_status->reasons & Reason::ACTIVE) && _status->active_down_time > 0) {
      if ((_status->active_down_time + _status->active_marked_down) < now) {
        Debug("host_statuses", "name: %s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", name, now,
              _status->active_down_time, _status->active_marked_down, Reason::ACTIVE_REASON);
        setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0, Reason::ACTIVE);
        reasons ^= Reason::ACTIVE;
      }
    }
    if ((_status->reasons & Reason::LOCAL) && _status->local_down_time > 0) {
      if ((_status->local_down_time + _status->local_marked_down) < now) {
        Debug("host_statuses", "name: %s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", name, now,
              _status->local_down_time, _status->local_marked_down, Reason::LOCAL_REASON);
        setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0, Reason::LOCAL);
        reasons ^= Reason::LOCAL;
      }
    }
    if ((_status->reasons & Reason::MANUAL) && _status->manual_down_time > 0) {
      if ((_status->manual_down_time + _status->manual_marked_down) < now) {
        Debug("host_statuses", "name: %s, now: %ld, down_time: %d, marked_down: %ld, reason: %s", name, now,
              _status->manual_down_time, _status->manual_marked_down, Reason::MANUAL_REASON);
        setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0, Reason::MANUAL);
        reasons ^= Reason::MANUAL;
      }
    }
    if (reasons == 0) {
      return HostStatus_t::HOST_STATUS_UP;
    } else {
      return HostStatus_t::HOST_STATUS_DOWN;
    }
  }
  // didn't find this host in host status db, create the record
  if (!lookup) {
    createHostStat(name);
  }

  return lookup ? static_cast<HostStatus_t>(_status->status) : HostStatus_t::HOST_STATUS_UP;
}

void
HostStatus::createHostStat(const char *name, const char *data)
{
  char buf[1024] = {0};
  HostStatRec r;

  std::string stat_name;
  std::stringstream status_rec;
  if (data != nullptr) {
    HostStatRec h(data);
    r = h;
  }
  status_rec << r;
  getStatName(stat_name, name);

  if (getHostStat(stat_name, buf, 1024) == REC_ERR_FAIL) {
    RecRegisterStatString(RECT_PROCESS, stat_name.c_str(), const_cast<char *>(status_rec.str().c_str()), RECP_PERSISTENT);
    Debug("host_statuses", "stat name: %s, data: %s", stat_name.c_str(), status_rec.str().c_str());
  }
}

RecErrT
HostStatus::getHostStat(std::string &stat_name, char *buf, unsigned int buf_len)
{
  return RecGetRecordString(stat_name.c_str(), buf, buf_len, true);
}
