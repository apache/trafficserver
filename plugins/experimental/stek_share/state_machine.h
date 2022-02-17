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
#include <cassert>
#include <iostream>
#include <mutex>
#include <cstring>

#include <libnuraft/nuraft.hxx>

#include "common.h"
#include "stek_utils.h"

class STEKShareSM : public nuraft::state_machine
{
public:
  STEKShareSM() : last_committed_idx_(0) {}

  ~STEKShareSM() {}

  nuraft::ptr<nuraft::buffer>
  pre_commit(const uint64_t log_idx, nuraft::buffer &data)
  {
    return nullptr;
  }

  nuraft::ptr<nuraft::buffer>
  commit(const uint64_t log_idx, nuraft::buffer &data)
  {
    // Extract bytes from "data".
    size_t len = 0;
    nuraft::buffer_serializer bs_data(data);
    void *byte_array = bs_data.get_bytes(len);
    // TSDebug(PLUGIN, "commit %lu: %s", log_idx, hex_str(std::string(reinterpret_cast<char *>(byte_array), len)).c_str());

    assert(len == SSL_TICKET_KEY_SIZE);

    {
      std::lock_guard<std::mutex> l(stek_lock_);
      std::memcpy(&stek_, byte_array, len);
      received_stek_ = true;
    }

    // Update last committed index number.
    last_committed_idx_ = log_idx;

    nuraft::ptr<nuraft::buffer> ret = nuraft::buffer::alloc(sizeof(log_idx));
    nuraft::buffer_serializer bs_ret(ret);
    bs_ret.put_u64(log_idx);

    return ret;
  }

  bool
  received_stek(ssl_ticket_key_t *curr_stek)
  {
    std::lock_guard<std::mutex> l(stek_lock_);
    if (!received_stek_) {
      return false;
    }

    received_stek_ = false;

    if (std::memcmp(curr_stek, &stek_, SSL_TICKET_KEY_SIZE != 0)) {
      std::memcpy(curr_stek, &stek_, SSL_TICKET_KEY_SIZE);
      return true;
    }

    return false;
  }

  void
  commit_config(const uint64_t log_idx, nuraft::ptr<nuraft::cluster_config> &new_conf)
  {
    // Nothing to do with configuration change. Just update committed index.
    last_committed_idx_ = log_idx;
  }

  void
  rollback(const uint64_t log_idx, nuraft::buffer &data)
  {
    // Nothing to do here since we don't have pre-commit.
  }

  int
  read_logical_snp_obj(nuraft::snapshot &s, void *&user_snp_ctx, uint64_t obj_id, nuraft::ptr<nuraft::buffer> &data_out,
                       bool &is_last_obj)
  {
    // TSDebug(PLUGIN, "read snapshot %lu term %lu object ID %lu", s.get_last_log_idx(), s.get_last_log_term(), obj_id);

    is_last_obj = true;

    {
      std::lock_guard<std::mutex> l(snapshot_lock_);
      if (snapshot_ == nullptr || snapshot_->snapshot_->get_last_log_idx() != s.get_last_log_idx()) {
        data_out = nullptr;
        return -1;
      } else {
        data_out = nuraft::buffer::alloc(sizeof(int) + SSL_TICKET_KEY_SIZE);
        nuraft::buffer_serializer bs(data_out);
        bs.put_bytes(reinterpret_cast<const void *>(&snapshot_->stek_), SSL_TICKET_KEY_SIZE);
        return 0;
      }
    }
  }

  void
  save_logical_snp_obj(nuraft::snapshot &s, uint64_t &obj_id, nuraft::buffer &data, bool is_first_obj, bool is_last_obj)
  {
    // TSDebug(PLUGIN, "save snapshot %lu term %lu object ID %lu", s.get_last_log_idx(), s.get_last_log_term(), obj_id);

    size_t len = 0;
    nuraft::buffer_serializer bs_data(data);
    void *byte_array = bs_data.get_bytes(len);

    assert(len == SSL_TICKET_KEY_SIZE);

    ssl_ticket_key_t local_stek;
    std::memcpy(&local_stek, byte_array, len);

    nuraft::ptr<nuraft::buffer> snp_buf  = s.serialize();
    nuraft::ptr<nuraft::snapshot> ss     = nuraft::snapshot::deserialize(*snp_buf);
    nuraft::ptr<struct snapshot_ctx> ctx = nuraft::cs_new<struct snapshot_ctx>(ss, local_stek);

    {
      std::lock_guard<std::mutex> l(snapshot_lock_);
      snapshot_ = ctx;
    }

    obj_id++;
  }

  bool
  apply_snapshot(nuraft::snapshot &s)
  {
    // TSDebug(PLUGIN, "apply snapshot %lu term %lu", s.get_last_log_idx(), s.get_last_log_term());

    {
      std::lock_guard<std::mutex> l(snapshot_lock_);
      if (snapshot_ != nullptr) {
        std::lock_guard<std::mutex> ll(stek_lock_);
        std::memcpy(&stek_, &snapshot_->stek_, SSL_TICKET_KEY_SIZE);
        received_stek_ = true;
        return true;
      } else {
        return false;
      }
    }
  }

  void
  free_user_snp_ctx(void *&user_snp_ctx)
  {
  }

  nuraft::ptr<nuraft::snapshot>
  last_snapshot()
  {
    // Just return the latest snapshot.
    std::lock_guard<std::mutex> l(snapshot_lock_);
    if (snapshot_ != nullptr) {
      return snapshot_->snapshot_;
    }
    return nullptr;
  }

  uint64_t
  last_commit_index()
  {
    return last_committed_idx_;
  }

  void
  create_snapshot(nuraft::snapshot &s, nuraft::async_result<bool>::handler_type &when_done)
  {
    // TSDebug(PLUGIN, "create snapshot %lu term %lu", s.get_last_log_idx(), s.get_last_log_term());

    ssl_ticket_key_t local_stek;
    {
      std::lock_guard<std::mutex> l(stek_lock_);
      std::memcpy(&local_stek, &stek_, SSL_TICKET_KEY_SIZE);
    }

    nuraft::ptr<nuraft::buffer> snp_buf  = s.serialize();
    nuraft::ptr<nuraft::snapshot> ss     = nuraft::snapshot::deserialize(*snp_buf);
    nuraft::ptr<struct snapshot_ctx> ctx = nuraft::cs_new<struct snapshot_ctx>(ss, local_stek);

    {
      std::lock_guard<std::mutex> l(snapshot_lock_);
      snapshot_ = ctx;
    }

    nuraft::ptr<std::exception> except(nullptr);
    bool ret = true;
    when_done(ret, except);
  }

private:
  struct snapshot_ctx {
    snapshot_ctx(nuraft::ptr<nuraft::snapshot> &s, ssl_ticket_key_t key) : snapshot_(s), stek_(key) {}
    nuraft::ptr<nuraft::snapshot> snapshot_;
    ssl_ticket_key_t stek_;
  };

  // Last committed Raft log number.
  std::atomic<uint64_t> last_committed_idx_;

  nuraft::ptr<struct snapshot_ctx> snapshot_;

  // Mutex for snapshot.
  std::mutex snapshot_lock_;

  bool received_stek_ = false;

  ssl_ticket_key_t stek_;

  std::mutex stek_lock_;
};
