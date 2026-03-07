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
#pragma once

#include <unordered_map>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include <fstream>
#include <memory>
#include <cstdint>
#include <system_error>

#include "cripts/Context.hpp"
#include "cripts/Time.hpp"

// Implemented in the .cc file
int _cripts_cache_group_sync(TSCont cont, TSEvent event, void *edata);

namespace cripts::Cache
{

class Group
{
private:
  using self_type = Group;

  struct _Entry {
    cripts::Time::Point timestamp; // Timestamp of when the entry was created
    size_t              length;    // Length of the group ID
    uint32_t            prefix;    // First 4 characters of the group ID
    uint64_t            hash;      // Hash value of the group ID, needed when writing to disk
  };

  // Header structure for on-disk map files (after VERSION field)
  struct _MapHeader {
    int64_t  created_ts;
    int64_t  last_write_ts;
    int64_t  last_sync_ts;
    uint64_t count;
  };

  using _MapType = std::unordered_map<uint64_t, _Entry>;

  struct _MapSlot {
    std::unique_ptr<_MapType> map;
    std::string               path;
    cripts::Time::Point       created;
    cripts::Time::Point       last_write;
    cripts::Time::Point       last_sync;
  };

public:
  static constexpr uint64_t VERSION = (static_cast<uint64_t>('C') << 56) | (static_cast<uint64_t>('G') << 48) |
                                      (static_cast<uint64_t>('M') << 40) | (static_cast<uint64_t>('A') << 32) |
                                      (static_cast<uint64_t>('P') << 24) | (static_cast<uint64_t>('S') << 16) |
                                      (static_cast<uint64_t>('0') << 8) | 0x00; // Change this on version bump

  static constexpr std::chrono::seconds DEFAULT_MAX_AGE{63072000}; // 2 Years, max cache lifetime in ATS as well

  Group(const std::string &name, const std::string &base_dir, size_t max_entries = 1024, size_t num_maps = 3)
  {
    Initialize(name, base_dir, max_entries, num_maps, DEFAULT_MAX_AGE);
  }

  // Not used at the moment.
  Group() = default;

  ~Group() { WriteToDisk(); }

  Group(const self_type &)                = delete;
  self_type &operator=(const self_type &) = delete;

  void Initialize(const std::string &name, const std::string &base_dir, size_t max_entries = 1024, size_t num_maps = 3,
                  std::chrono::seconds max_age = DEFAULT_MAX_AGE);

  void
  SetMaxEntries(size_t max_entries)
  {
    std::unique_lock lock(_mutex);
    _max_entries = max_entries;
  }

  void
  SetMaxAge(std::chrono::seconds max_age)
  {
    std::unique_lock lock(_mutex);
    _max_age = max_age;
  }

  void Insert(cripts::string_view key);
  void Insert(const std::vector<cripts::string_view> &keys);
  bool Lookup(cripts::string_view key, cripts::Time::Point age) const;
  bool Lookup(const std::vector<cripts::string_view> &keys, cripts::Time::Point age) const;

  bool
  Lookup(cripts::string_view key, time_t age) const
  {
    return Lookup(key, cripts::Time::Clock::from_time_t(age));
  }

  bool
  Lookup(const std::vector<cripts::string_view> &keys, time_t age) const
  {
    return Lookup(keys, cripts::Time::Clock::from_time_t(age));
  }

  cripts::Time::Point
  LastSync() const
  {
    std::shared_lock lock(_mutex);
    return _last_sync;
  }

  void WriteToDisk();
  void LoadFromDisk();

private:
  mutable std::shared_mutex _mutex;
  std::string               _name        = "CacheGroup";
  size_t                    _num_maps    = 3;
  size_t                    _max_entries = 1024;
  std::chrono::seconds      _max_age     = DEFAULT_MAX_AGE;
  size_t                    _map_index   = 0;
  cripts::Time::Point       _last_sync   = cripts::Time::Point{};

  std::vector<_MapSlot> _slots;
  std::ofstream         _txn_log;
  std::string           _log_path;
  std::string           _base_dir;

  void appendLog(const _Entry &entry);
  void clearLog();
  bool syncMap(size_t index);

public:
  class Manager
  {
    friend int ::_cripts_cache_group_sync(TSCont cont, TSEvent event, void *edata);
    using self_type = Manager;

  public:
    static void *Factory(const std::string &name, size_t max_entries = 1024, size_t num_maps = 3);

    Manager(const self_type &)              = delete;
    self_type &operator=(const self_type &) = delete;

  protected:
    void _scheduleCont();

    std::unordered_map<std::string, std::weak_ptr<Group>> _groups;
    std::mutex                                            _mutex;

  private:
    Manager()
    {
      _base_dir = TSRuntimeDirGet();

      if (std::filesystem::exists(_base_dir)) {
        _base_dir += "/cache_groups";
        if (!std::filesystem::exists(_base_dir)) {
          std::error_code ec;

          std::filesystem::create_directories(_base_dir, ec);
          if (ec) {
            TSError("cripts::Cache::Group::Manager: Failed to create directory `%s': %s", _base_dir.c_str(), ec.message().c_str());
          } else {
            std::filesystem::permissions(_base_dir, std::filesystem::perms::group_write, std::filesystem::perm_options::add, ec);

            if (ec) {
              TSWarning("cripts::Cache::Group::Manager: Failed to set permissions on `%s': %s", _base_dir.c_str(),
                        ec.message().c_str());
            }
          }
        }
      }
      _scheduleCont(); // Kick it off
    }

    ~Manager()
    {
      if (_action) {
        TSActionCancel(_action);
        _action = nullptr;
      }
      if (_cont) {
        TSContDestroy(_cont);
        _cont = nullptr;
      }
    }

    static self_type &_instance();

    TSCont      _cont   = nullptr;
    TSAction    _action = nullptr;
    std::string _base_dir;
  };
};
} // namespace cripts::Cache
