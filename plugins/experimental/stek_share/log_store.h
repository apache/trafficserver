/************************************************************************
Copyright 2017-2019 eBay Inc.
Author/Developer(s): Jung-Sang Ahn

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
**************************************************************************/

// This file is based on the example code from https://github.com/eBay/NuRaft/tree/master/examples

#pragma once

#include <atomic>
#include <map>
#include <mutex>

#include <libnuraft/log_store.hxx>

class STEKShareLogStore : public nuraft::log_store
{
public:
  STEKShareLogStore();

  ~STEKShareLogStore();

  __nocopy__(STEKShareLogStore);

  uint64_t next_slot() const;

  uint64_t start_index() const;

  nuraft::ptr<nuraft::log_entry> last_entry() const;

  uint64_t append(nuraft::ptr<nuraft::log_entry> &entry);

  void write_at(uint64_t index, nuraft::ptr<nuraft::log_entry> &entry);

  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries(uint64_t start, uint64_t end);

  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> log_entries_ext(uint64_t start, uint64_t end,
                                                                           int64_t batch_size_hint_in_bytes = 0);

  nuraft::ptr<nuraft::log_entry> entry_at(uint64_t index);

  uint64_t term_at(uint64_t index);

  nuraft::ptr<nuraft::buffer> pack(uint64_t index, int32_t cnt);

  void apply_pack(uint64_t index, nuraft::buffer &pack);

  bool compact(uint64_t last_log_index);

  bool
  flush()
  {
    return true;
  }

  void close();

private:
  static nuraft::ptr<nuraft::log_entry> make_clone(const nuraft::ptr<nuraft::log_entry> &entry);

  std::map<uint64_t, nuraft::ptr<nuraft::log_entry>> logs_;
  mutable std::mutex logs_lock_;
  std::atomic<uint64_t> start_idx_;
};
