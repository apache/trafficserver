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
    TSDebug(PLUGIN, "commit %lu: %s", log_idx, hex_str(std::string(reinterpret_cast<char *>(byte_array), len)).c_str());

    assert(len == sizeof(struct ssl_ticket_key_t));

    {
      std::lock_guard<std::mutex> l(stek_lock_);
      std::memcpy(&stek_, byte_array, len);
    }

    received_stek_ = true;

    // Update last committed index number.
    last_committed_idx_ = log_idx;

    nuraft::ptr<nuraft::buffer> ret = nuraft::buffer::alloc(sizeof(log_idx));
    nuraft::buffer_serializer bs_ret(ret);
    bs_ret.put_u64(log_idx);

    return ret;
  }

  bool
  received_stek()
  {
    return received_stek_;
  }

  std::shared_ptr<struct ssl_ticket_key_t>
  get_stek()
  {
    std::lock_guard<std::mutex> l(stek_lock_);
    std::shared_ptr<struct ssl_ticket_key_t> key = std::make_shared<struct ssl_ticket_key_t>(stek_);

    return key;
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
    // Put dummy data.
    data_out = nuraft::buffer::alloc(sizeof(int32_t));
    nuraft::buffer_serializer bs(data_out);
    bs.put_i32(0);

    is_last_obj = true;
    return 0;
  }

  void
  save_logical_snp_obj(nuraft::snapshot &s, uint64_t &obj_id, nuraft::buffer &data, bool is_first_obj, bool is_last_obj)
  {
    TSDebug(PLUGIN, "save snapshot %lu term %lu object ID %lu", s.get_last_log_idx(), s.get_last_log_term(), obj_id);

    // Request next object.
    obj_id++;
  }

  bool
  apply_snapshot(nuraft::snapshot &s)
  {
    TSDebug(PLUGIN, "apply snapshot %lu term %lu", s.get_last_log_idx(), s.get_last_log_term());

    // Clone snapshot from "s".
    {
      std::lock_guard<std::mutex> l(last_snapshot_lock_);
      nuraft::ptr<nuraft::buffer> snp_buf = s.serialize();
      last_snapshot_                      = nuraft::snapshot::deserialize(*snp_buf);
    }
    return true;
  }

  void
  free_user_snp_ctx(void *&user_snp_ctx)
  {
  }

  nuraft::ptr<nuraft::snapshot>
  last_snapshot()
  {
    // Just return the latest snapshot.
    std::lock_guard<std::mutex> l(last_snapshot_lock_);
    return last_snapshot_;
  }

  uint64_t
  last_commit_index()
  {
    return last_committed_idx_;
  }

  void
  create_snapshot(nuraft::snapshot &s, nuraft::async_result<bool>::handler_type &when_done)
  {
    TSDebug(PLUGIN, "create snapshot %lu term %lu", s.get_last_log_idx(), s.get_last_log_term());

    // Clone snapshot from "s".
    {
      std::lock_guard<std::mutex> l(last_snapshot_lock_);
      nuraft::ptr<nuraft::buffer> snp_buf = s.serialize();
      last_snapshot_                      = nuraft::snapshot::deserialize(*snp_buf);
    }
    nuraft::ptr<std::exception> except(nullptr);
    bool ret = true;
    when_done(ret, except);
  }

private:
  // Last committed Raft log number.
  std::atomic<uint64_t> last_committed_idx_;

  // Last snapshot.
  nuraft::ptr<nuraft::snapshot> last_snapshot_;

  // Mutex for last snapshot.
  std::mutex last_snapshot_lock_;

  std::atomic<bool> received_stek_ = false;

  struct ssl_ticket_key_t stek_;

  std::mutex stek_lock_;
};
