/*
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
#include <iostream>
#include <filesystem>

#include "ts/ts.h"
#include "cripts/CacheGroup.hpp"

inline static uint32_t
_make_prefix_int(cripts::string_view key)
{
  uint32_t prefix = 0;

  std::memcpy(&prefix, key.data(), std::min<size_t>(4, key.size()));
  return prefix;
}

// Stuff around the disk sync contination
constexpr auto _CONT_SYNC_INTERVAL = 10; // How often to run the continuation
constexpr int  _SYNC_GROUP_EVERY   = 60; // Sync each group every 60s

int
_cripts_cache_group_sync(TSCont cont, TSEvent /* event */, void * /* edata */)
{
  auto           *mgr = static_cast<cripts::Cache::Group::Manager *>(TSContDataGet(cont));
  std::lock_guard lock(mgr->_mutex);
  auto           &groups = mgr->_groups;

  const size_t max_to_process = (groups.size() + (_SYNC_GROUP_EVERY - 1)) / _SYNC_GROUP_EVERY;
  size_t       processed      = 0;
  auto         now            = cripts::Time::Clock::now();

  for (auto it = groups.begin(); it != groups.end() && processed < max_to_process; ++it) {
    if (auto group = it->second.lock()) {
      if (group->LastSync() + std::chrono::seconds{_SYNC_GROUP_EVERY} < now) {
        group->WriteToDisk();
        ++processed;
      }
    } else {
      // The group has been deleted, remove it from the map ??
      it = groups.erase(it);
    }
  }

  return TS_SUCCESS;
}

namespace cripts
{

void
Cache::Group::Initialize(const std::string &name, const std::string &base_dir, size_t num_maps, size_t max_entries,
                         std::chrono::seconds max_age)
{
  cripts::Time::Point zero = cripts::Time::Point{};
  cripts::Time::Point now  = cripts::Time::Clock::now();

  _name        = name;
  _num_maps    = num_maps;
  _max_entries = max_entries;
  _max_age     = max_age;

  _base_dir = base_dir + "/" + _name;
  _log_path = _base_dir + "/" + "txn.log";

  if (!std::filesystem::exists(_base_dir)) {
    std::filesystem::create_directories(_base_dir);
    std::filesystem::permissions(_base_dir, std::filesystem::perms::group_write, std::filesystem::perm_options::add);
  }

  for (size_t ix = 0; ix < _num_maps; ++ix) {
    _slots.emplace_back(_MapSlot{.map        = std::make_unique<_MapType>(),
                                 .path       = _base_dir + "/map_" + std::to_string(ix) + ".bin",
                                 .created    = zero,
                                 .last_write = zero,
                                 .last_sync  = zero});
  }

  _slots[0].created    = now;
  _slots[0].last_write = now;

  LoadFromDisk();
  _last_sync = now;
}

void
Cache::Group::Insert(cripts::string_view key)
{
  static const std::hash<cripts::string_view> hasher;

  // Trim any "'s and white spaces from the key
  key.trim_if(&isspace).trim('"');

  auto     now    = cripts::Time::Clock::now();
  auto     hash   = static_cast<uint64_t>(hasher(key));
  uint32_t prefix = _make_prefix_int(key);

  std::unique_lock lock(_mutex);

  auto &slot = _slots[_map_index];
  auto  it   = slot.map->find(hash);

  if (it != slot.map->end() && it->second.length == key.size() && it->second.prefix == prefix) {
    it->second.timestamp = now;
    slot.last_write      = now;
    appendLog(it->second);

    return;
  }

  Cache::Group::_Entry entry{.timestamp = now, .length = key.size(), .prefix = prefix, .hash = hash};

  slot.map->insert_or_assign(hash, entry);
  slot.last_write = now;
  appendLog(entry);

  if (slot.map->size() > _max_entries || (now - slot.created) > _max_age) {
    _map_index = (_map_index + 1) % _slots.size();

    auto &next_slot = _slots[_map_index];

    if (next_slot.last_write > next_slot.last_sync) {
      TSWarning("cripts::Cache::Group: Rotating unsynced map for `%s'! This may lead to data loss.", _name.c_str());
    }
    next_slot.map->clear();
    next_slot.created    = now;
    next_slot.last_write = now;
    syncMap(_map_index); // Sync to disk will make sure it's empty on disk
  }
}

void
Cache::Group::Insert(const std::vector<cripts::string_view> &keys)
{
  for (auto &key : keys) {
    Insert(key);
  }
}

bool
Cache::Group::Lookup(cripts::string_view key, cripts::Time::Point age) const
{
  // Trim any "'s and whitespaces from the key
  key.trim_if(&isspace).trim('"');

  uint64_t         hash = static_cast<uint64_t>(std::hash<cripts::string_view>{}(key));
  std::shared_lock lock(_mutex);

  for (size_t i = 0; i < _slots.size(); ++i) {
    size_t      map_index = (_map_index + _slots.size() - i) % _slots.size();
    const auto &slot      = _slots[map_index];

    if (slot.last_write < age) {
      continue; // Skip maps that haven't been written to since this time
    }

    const auto &map = *slot.map;
    auto        it  = map.find(hash);

    if (it != map.end()) {
      const Cache::Group::_Entry &entry = it->second;

      if (entry.timestamp < age || entry.length != key.size() || entry.prefix != _make_prefix_int(key)) {
        continue;
      }

      return true;
    }
  }

  return false;
}

bool
Cache::Group::Lookup(const std::vector<cripts::string_view> &keys, cripts::Time::Point age) const
{
  for (auto &key : keys) {
    if (Lookup(key, age)) {
      return true;
    }
  }

  return false;
}

void
Cache::Group::LoadFromDisk()
{
  std::unique_lock lock(_mutex);
  std::ifstream    log(_log_path, std::ios::binary);

  for (size_t slot_ix = 0; slot_ix < _slots.size(); ++slot_ix) {
    auto         &slot          = _slots[slot_ix];
    uint64_t      version_id    = 0;
    size_t        count         = 0;
    time_t        created_ts    = 0;
    time_t        last_write_ts = 0;
    time_t        last_sync_ts  = 0;
    std::ifstream file(slot.path, std::ios::binary);

    if (!file) {
      continue;
    }

    file.read(reinterpret_cast<char *>(&version_id), sizeof(version_id));
    if (version_id != VERSION) {
      TSWarning("cripts::Cache::Group: Version mismatch for map file: %s, expected %llu, got %llu. Skipping this map.",
                slot.path.c_str(), static_cast<unsigned long long>(VERSION), static_cast<unsigned long long>(version_id));
      continue;
    }

    file.read(reinterpret_cast<char *>(&created_ts), sizeof(created_ts));
    file.read(reinterpret_cast<char *>(&last_write_ts), sizeof(last_write_ts));
    file.read(reinterpret_cast<char *>(&last_sync_ts), sizeof(last_sync_ts));
    file.read(reinterpret_cast<char *>(&count), sizeof(count));

    slot.created    = cripts::Time::Clock::from_time_t(created_ts);
    slot.last_write = cripts::Time::Clock::from_time_t(last_write_ts);
    slot.last_sync  = cripts::Time::Clock::from_time_t(last_sync_ts);

    for (size_t i = 0; i < count; ++i) {
      Cache::Group::_Entry entry;

      file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
      if (!std::ranges::any_of(_slots, [&](const auto &slot) { return slot.map->find(entry.hash) != slot.map->end(); })) {
        slot.map->insert_or_assign(entry.hash, entry);
      }
    }
  }

  // Sort the slots by creation time, newest first, since we'll start with index 0 upon loading
  std::ranges::sort(_slots, [](const _MapSlot &a, const _MapSlot &b) { return a.created > b.created; });

  // Replay any entries from the transaction log, and then truncate it
  if (log) {
    Cache::Group::_Entry entry;
    auto                 last_write = cripts::Time::Clock::from_time_t(0);

    while (log.read(reinterpret_cast<char *>(&entry), sizeof(entry))) {
      _slots[0].map->insert_or_assign(entry.hash, entry);
      last_write = std::max(last_write, entry.timestamp);
    }
    _slots[0].last_write = last_write;
    clearLog();
  }
}

void
Cache::Group::WriteToDisk()
{
  std::unique_lock unique_lock(_mutex);

  _last_sync = cripts::Time::Clock::now();
  for (size_t ix = 0; ix < _slots.size(); ++ix) {
    bool need_sync = false;

    if (_slots[ix].last_write > _slots[ix].last_sync) {
      _slots[ix].last_sync = _last_sync;
      need_sync            = true;
    }

    if (need_sync) {
      syncMap(ix);
    }
  }

  clearLog();
}

//
// Here comes the private member methods, these must never be called without
// already hodling an exclusive lock on the mutex.
//

void
Cache::Group::appendLog(const Cache::Group::_Entry &entry)
{
  if (!_txn_log.is_open() || !_txn_log.good()) {
    _txn_log.open(_log_path, std::ios::app | std::ios::out);
    if (!_txn_log) {
      TSWarning("cripts::Cache::Group: Failed to open transaction log `%s'.", _log_path.c_str());
      return;
    }
  }

  _txn_log.write(reinterpret_cast<const char *>(&entry), sizeof(entry));
  _txn_log.flush();
}

void
Cache::Group::syncMap(size_t index)
{
  constexpr size_t                   BUFFER_SIZE = 64 * 1024;
  std::array<std::byte, BUFFER_SIZE> buffer;
  size_t                             buf_pos  = 0;
  const auto                        &slot     = _slots[index];
  const std::string                  tmp_path = slot.path + ".tmp";
  std::ofstream                      o_file(tmp_path, std::ios::binary | std::ios::trunc);

  if (!o_file) {
    std::cerr << "Failed to open temp file for sync: " << tmp_path << "\n";
    return;
  }

  // Helper lambda to append data to the write buffer
  auto _AppendToBuffer = [&](const void *data, size_t size) {
    if (buf_pos + size > buffer.size()) {
      o_file.write(reinterpret_cast<const char *>(buffer.data()), buf_pos);
      buf_pos = 0;
    }
    std::memcpy(buffer.data() + buf_pos, static_cast<const std::byte *>(data), size);
    buf_pos += size;
  };

  time_t created_ts    = cripts::Time::Clock::to_time_t(slot.created);
  time_t last_write_ts = cripts::Time::Clock::to_time_t(slot.last_write);
  time_t last_sync_ts  = cripts::Time::Clock::to_time_t(slot.last_sync);
  size_t count         = slot.map->size();

  _AppendToBuffer(&VERSION, sizeof(VERSION));
  _AppendToBuffer(&created_ts, sizeof(created_ts));
  _AppendToBuffer(&last_write_ts, sizeof(last_write_ts));
  _AppendToBuffer(&last_sync_ts, sizeof(last_sync_ts));
  _AppendToBuffer(&count, sizeof(count));

  // Write entries
  for (const auto &[_, entry] : *slot.map) {
    _AppendToBuffer(&entry, sizeof(entry));
  }

  if (buf_pos > 0) {
    o_file.write(reinterpret_cast<const char *>(buffer.data()), buf_pos);
  }
  o_file.flush();
  o_file.close();

  if (std::rename(tmp_path.c_str(), slot.path.c_str()) != 0) {
    TSWarning("cripts::Cache::Group: Failed to rename temp file `%s' to `%s'.", tmp_path.c_str(), slot.path.c_str());
    std::filesystem::remove(tmp_path);
  }
}

void
Cache::Group::clearLog()
{
  std::error_code ec;

  _txn_log.close();
  std::filesystem::remove(_log_path, ec);
  if (ec) {
    TSWarning("cripts::Cache::Group: Failed to clear transaction log `%s': %s", _log_path.c_str(), ec.message().c_str());
  }
}

// Singleton instance for the Cache::Group::Manager
Cache::Group::Manager &
Cache::Group::Manager::_instance()
{
  static Cache::Group::Manager inst;
  return inst;
}

void *
Cache::Group::Manager::Factory(const std::string &name, size_t max_entries, size_t num_maps)
{
  std::lock_guard lock(_instance()._mutex);
  auto           &groups = _instance()._groups;

  if (auto it = groups.find(name); it != groups.end()) {
    if (auto group = it->second.lock()) {
      return new std::shared_ptr<Group>(std::move(group));
    }
  }

  if (!_instance()._base_dir.empty()) {
    auto group = std::make_shared<Group>(name, _instance()._base_dir, max_entries, num_maps);

    groups[name] = group;
    return new std::shared_ptr<Group>(std::move(group));
  } else {
    TSError("cripts::Cache::Group: Failed to get runtime directory for initialization.");
    return nullptr;
  }
}

void
Cache::Group::Manager::_scheduleCont()
{
  if (!_cont) {
    _cont = TSContCreate(_cripts_cache_group_sync, TSMutexCreate());
    TSContDataSet(_cont, this);
  }

  if (_action) {
    TSActionCancel(_action); // Can this even happen ?
    _action = nullptr;
  }

  _action = TSContScheduleEveryOnPool(_cont, _CONT_SYNC_INTERVAL * 1000, TS_THREAD_POOL_TASK);
}

} // namespace cripts
