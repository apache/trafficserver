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

static void *
mgmt_host_status_up_callback(void *x, char *data, int len)
{
  if (data != nullptr) {
    Debug("host_statuses", "marking up server %s", data);
    HostStatus &hs = HostStatus::instance();
    hs.setHostStatus(data, HostStatus_t::HOST_STATUS_UP, 0);
  }
  return nullptr;
}

static void *
mgmt_host_status_down_callback(void *x, char *data, int len)
{
  MgmtInt op;
  MgmtMarshallString name;
  MgmtMarshallInt down_time;
  static const MgmtMarshallType fields[] = {MGMT_MARSHALL_INT, MGMT_MARSHALL_STRING, MGMT_MARSHALL_INT};
  Debug("host_statuses", "%s:%s:%d - data: %s, len: %d\n", __FILE__, __func__, __LINE__, data, len);

  if (mgmt_message_parse(data, len, fields, countof(fields), &op, &name, &down_time) == -1) {
    Error("Plugin message - RPC parsing error - message discarded.");
  }
  Debug("host_statuses", "op: %ld, name: %s, down_time: %d", static_cast<long>(op), name, static_cast<int>(down_time));

  if (data != nullptr) {
    Debug("host_statuses", "marking down server %s", name);
    HostStatus &hs = HostStatus::instance();
    hs.setHostStatus(name, HostStatus_t::HOST_STATUS_DOWN, down_time);
  }
  return nullptr;
}

HostStatus::HostStatus()
{
  hosts_statuses  = ink_hash_table_create(InkHashTableKeyType_String);
  hosts_stats_ids = ink_hash_table_create(InkHashTableKeyType_String);
  ink_rwlock_init(&host_status_rwlock);
  ink_rwlock_init(&host_statids_rwlock);
  Debug("host_statuses", "registering ostas");
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_UP, mgmt_host_status_up_callback, nullptr);
  pmgmt->registerMgmtCallback(MGMT_EVENT_HOST_STATUS_DOWN, mgmt_host_status_down_callback, nullptr);
  host_status_rsb = RecAllocateRawStatBlock((int)TS_MAX_API_STATS);
}

HostStatus::~HostStatus()
{
  // release host_statues hash table.
  InkHashTableIteratorState ht_iter;
  InkHashTableEntry *ht_entry = nullptr;
  ht_entry                    = ink_hash_table_iterator_first(hosts_statuses, &ht_iter);

  while (ht_entry != nullptr) {
    HostStatRec_t *value = static_cast<HostStatRec_t *>(ink_hash_table_entry_value(hosts_statuses, ht_entry));
    ats_free(value);
    ht_entry = ink_hash_table_iterator_next(hosts_statuses, &ht_iter);
  }
  ink_hash_table_destroy(hosts_statuses);

  // release host_stats_ids hash and the read and writer locks.
  ink_hash_table_destroy(hosts_stats_ids);
  ink_rwlock_destroy(&host_status_rwlock);
  ink_rwlock_destroy(&host_statids_rwlock);
}

void
HostStatus::setHostStatus(const char *name, HostStatus_t status, const unsigned int down_time)
{
  int stat_id = getHostStatId(name);
  if (stat_id != -1) {
    if (status == HostStatus_t::HOST_STATUS_UP) {
      Debug("host_statuses", "set stat for :  name: %s, status: %d", name, status);
      RecSetRawStatCount(host_status_rsb, stat_id, 1);
      RecSetRawStatSum(host_status_rsb, stat_id, 1);
    } else {
      RecSetRawStatCount(host_status_rsb, stat_id, 0);
      RecSetRawStatSum(host_status_rsb, stat_id, 0);
      Debug("host_statuses", "clear stat for :  name: %s, status: %d", name, status);
    }
  }
  Debug("host_statuses", "name: %s, status: %d", name, status);
  // update / insert status.
  // using the hash table pointer to store the HostStatus_t value.
  HostStatRec_t *host_stat = nullptr;
  ink_rwlock_wrlock(&host_status_rwlock);
  if (ink_hash_table_lookup(hosts_statuses, name, reinterpret_cast<InkHashTableValue *>(&host_stat)) == 0) {
    host_stat = static_cast<HostStatRec_t *>(ats_malloc(sizeof(HostStatRec_t)));
    ink_hash_table_insert(hosts_statuses, name, reinterpret_cast<InkHashTableValue *>(host_stat));
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
  int lookup             = 0;
  time_t now             = time(0);

  // the hash table value pointer has the HostStatus_t value.
  ink_rwlock_rdlock(&host_status_rwlock);
  lookup = ink_hash_table_lookup(hosts_statuses, name, reinterpret_cast<void **>(&_status));
  ink_rwlock_unlock(&host_status_rwlock);

  // if the host was marked down and it's down_time has elapsed, mark it up.
  if (lookup == 1 && _status->status == HostStatus_t::HOST_STATUS_DOWN && _status->down_time > 0) {
    if ((_status->down_time + _status->marked_down) < now) {
      Debug("host_statuses", "name: %s, now: %ld, down_time: %d, marked_down: %ld", name, now, _status->down_time,
            _status->marked_down);
      setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0);
      return HostStatus_t::HOST_STATUS_UP;
    }
  }
  return lookup == 1 ? static_cast<HostStatus_t>(_status->status) : HostStatus_t::HOST_STATUS_INIT;
}

void
HostStatus::createHostStat(const char *name)
{
  InkHashTableEntry *entry;
  entry = ink_hash_table_lookup_entry(hosts_stats_ids, name);
  if (entry == nullptr) {
    RecRegisterRawStat(host_status_rsb, RECT_PROCESS, (stat_prefix + name).c_str(), RECD_INT, RECP_NON_PERSISTENT,
                       (int)next_stat_id, RecRawStatSyncSum);
    Debug("host_statuses", "name: %s, id: %d", name, next_stat_id);
    ink_rwlock_wrlock(&host_statids_rwlock);
    ink_hash_table_insert(hosts_stats_ids, name, reinterpret_cast<void *>(next_stat_id));
    ink_rwlock_unlock(&host_statids_rwlock);
    setHostStatus(name, HostStatus_t::HOST_STATUS_UP, 0);
    next_stat_id++;
  }
}

int
HostStatus::getHostStatId(const char *name)
{
  int lookup   = 0;
  intptr_t _id = -1;

  ink_rwlock_rdlock(&host_statids_rwlock);
  lookup = ink_hash_table_lookup(hosts_stats_ids, name, reinterpret_cast<void **>(&_id));
  ink_rwlock_unlock(&host_statids_rwlock);
  Debug("host_statuses", "name: %s, id: %d", name, static_cast<int>(_id));

  return lookup == 1 ? static_cast<int>(_id) : -1;
}
