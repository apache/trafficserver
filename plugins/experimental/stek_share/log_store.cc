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

#include <cassert>

#include <libnuraft/nuraft.hxx>

#include "log_store.h"

STEKShareLogStore::STEKShareLogStore() : start_idx_(1)
{
  // Dummy entry for index 0.
  nuraft::ptr<nuraft::buffer> buf = nuraft::buffer::alloc(sizeof(uint64_t));
  logs_[0]                        = nuraft::cs_new<nuraft::log_entry>(0, buf);
}

STEKShareLogStore::~STEKShareLogStore() {}

nuraft::ptr<nuraft::log_entry>
STEKShareLogStore::make_clone(const nuraft::ptr<nuraft::log_entry> &entry)
{
  nuraft::ptr<nuraft::log_entry> clone =
    nuraft::cs_new<nuraft::log_entry>(entry->get_term(), nuraft::buffer::clone(entry->get_buf()), entry->get_val_type());
  return clone;
}

uint64_t
STEKShareLogStore::next_slot() const
{
  std::lock_guard<std::mutex> l(logs_lock_);

  // Exclude the dummy entry.
  return start_idx_ + logs_.size() - 1;
}

uint64_t
STEKShareLogStore::start_index() const
{
  return start_idx_;
}

nuraft::ptr<nuraft::log_entry>
STEKShareLogStore::last_entry() const
{
  uint64_t next_idx = next_slot();
  std::lock_guard<std::mutex> l(logs_lock_);
  auto entry = logs_.find(next_idx - 1);
  if (entry == logs_.end()) {
    entry = logs_.find(0);
  }

  return make_clone(entry->second);
}

uint64_t
STEKShareLogStore::append(nuraft::ptr<nuraft::log_entry> &entry)
{
  nuraft::ptr<nuraft::log_entry> clone = make_clone(entry);

  std::lock_guard<std::mutex> l(logs_lock_);
  size_t idx = start_idx_ + logs_.size() - 1;
  logs_[idx] = clone;
  return idx;
}

void
STEKShareLogStore::write_at(uint64_t index, nuraft::ptr<nuraft::log_entry> &entry)
{
  nuraft::ptr<nuraft::log_entry> clone = make_clone(entry);

  // Discard all logs equal to or greater than "index".
  std::lock_guard<std::mutex> l(logs_lock_);
  auto itr = logs_.lower_bound(index);
  while (itr != logs_.end()) {
    itr = logs_.erase(itr);
  }
  logs_[index] = clone;
}

nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>>
STEKShareLogStore::log_entries(uint64_t start, uint64_t end)
{
  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> ret = nuraft::cs_new<std::vector<nuraft::ptr<nuraft::log_entry>>>();

  ret->resize(end - start);
  uint64_t cc = 0;
  for (uint64_t i = start; i < end; ++i) {
    nuraft::ptr<nuraft::log_entry> src = nullptr;
    {
      std::lock_guard<std::mutex> l(logs_lock_);
      auto entry = logs_.find(i);
      if (entry == logs_.end()) {
        entry = logs_.find(0);
        assert(0);
      }
      src = entry->second;
    }
    (*ret)[cc++] = make_clone(src);
  }
  return ret;
}

nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>>
STEKShareLogStore::log_entries_ext(uint64_t start, uint64_t end, int64_t batch_size_hint_in_bytes)
{
  nuraft::ptr<std::vector<nuraft::ptr<nuraft::log_entry>>> ret = nuraft::cs_new<std::vector<nuraft::ptr<nuraft::log_entry>>>();

  if (batch_size_hint_in_bytes < 0) {
    return ret;
  }

  size_t accum_size = 0;
  for (uint64_t i = start; i < end; ++i) {
    nuraft::ptr<nuraft::log_entry> src = nullptr;
    {
      std::lock_guard<std::mutex> l(logs_lock_);
      auto entry = logs_.find(i);
      if (entry == logs_.end()) {
        entry = logs_.find(0);
        assert(0);
      }
      src = entry->second;
    }
    ret->push_back(make_clone(src));
    accum_size += src->get_buf().size();
    if (batch_size_hint_in_bytes && accum_size >= (uint64_t)batch_size_hint_in_bytes) {
      break;
    }
  }
  return ret;
}

nuraft::ptr<nuraft::log_entry>
STEKShareLogStore::entry_at(uint64_t index)
{
  nuraft::ptr<nuraft::log_entry> src = nullptr;
  {
    std::lock_guard<std::mutex> l(logs_lock_);
    auto entry = logs_.find(index);
    if (entry == logs_.end()) {
      entry = logs_.find(0);
    }
    src = entry->second;
  }
  return make_clone(src);
}

uint64_t
STEKShareLogStore::term_at(uint64_t index)
{
  uint64_t term = 0;
  {
    std::lock_guard<std::mutex> l(logs_lock_);
    auto entry = logs_.find(index);
    if (entry == logs_.end()) {
      entry = logs_.find(0);
    }
    term = entry->second->get_term();
  }
  return term;
}

nuraft::ptr<nuraft::buffer>
STEKShareLogStore::pack(uint64_t index, int32_t cnt)
{
  std::vector<nuraft::ptr<nuraft::buffer>> logs;

  size_t size_total = 0;
  for (uint64_t i = index; i < index + cnt; ++i) {
    nuraft::ptr<nuraft::log_entry> le = nullptr;
    {
      std::lock_guard<std::mutex> l(logs_lock_);
      le = logs_[i];
    }
    assert(le.get());
    nuraft::ptr<nuraft::buffer> buf = le->serialize();
    size_total += buf->size();
    logs.push_back(buf);
  }

  nuraft::ptr<nuraft::buffer> buf_out = nuraft::buffer::alloc(sizeof(int32_t) + cnt * sizeof(int32_t) + size_total);
  buf_out->pos(0);
  buf_out->put((int32_t)cnt);

  for (auto &entry : logs) {
    nuraft::ptr<nuraft::buffer> &bb = entry;
    buf_out->put((int32_t)bb->size());
    buf_out->put(*bb);
  }
  return buf_out;
}

void
STEKShareLogStore::apply_pack(uint64_t index, nuraft::buffer &pack)
{
  pack.pos(0);
  int32_t num_logs = pack.get_int();

  for (int32_t i = 0; i < num_logs; ++i) {
    uint64_t cur_idx = index + i;
    int32_t buf_size = pack.get_int();

    nuraft::ptr<nuraft::buffer> buf_local = nuraft::buffer::alloc(buf_size);
    pack.get(buf_local);

    nuraft::ptr<nuraft::log_entry> le = nuraft::log_entry::deserialize(*buf_local);
    {
      std::lock_guard<std::mutex> l(logs_lock_);
      logs_[cur_idx] = le;
    }
  }

  {
    std::lock_guard<std::mutex> l(logs_lock_);
    auto entry = logs_.upper_bound(0);
    if (entry != logs_.end()) {
      start_idx_ = entry->first;
    } else {
      start_idx_ = 1;
    }
  }
}

bool
STEKShareLogStore::compact(uint64_t last_log_index)
{
  std::lock_guard<std::mutex> l(logs_lock_);
  for (uint64_t i = start_idx_; i <= last_log_index; ++i) {
    auto entry = logs_.find(i);
    if (entry != logs_.end()) {
      logs_.erase(entry);
    }
  }

  // WARNING:
  //   Even though nothing has been erased, we should set "start_idx_" to new index.
  if (start_idx_ <= last_log_index) {
    start_idx_ = last_log_index + 1;
  }

  return true;
}

void
STEKShareLogStore::close()
{
}
