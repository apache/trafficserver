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

#include <deque>
#include <mutex>
#include <time.h>

#include <libnuraft/nuraft.hxx>

#include "stek_utils.h"

class STEKShareServer
{
public:
  STEKShareServer() : server_id_(1), addr_("localhost"), port_(25000), sm_(nullptr), smgr_(nullptr), raft_instance_(nullptr)
  {
    last_updated_    = 0;
    current_log_idx_ = 0;

    // Default ASIO thread pool size: 4.
    asio_thread_pool_size_ = 4;

    // Default heart beat interval: 100 ms.
    heart_beat_interval_ = 100;

    // Default election timeout: 200~400 ms.
    election_timeout_lower_bound_ = 200;
    election_timeout_upper_bound_ = 400;

    // Up to 5 logs will be preserved ahead the last snapshot.
    reserved_log_items_ = 5;

    // Snapshot will be created for every 5 log appends.
    snapshot_distance_ = 5;

    // Client timeout: 3000 ms.
    client_req_timeout_ = 3000;

    std::memset(ticket_keys_, 0, SSL_TICKET_KEY_SIZE * 2);
  }

  void
  reset()
  {
    sm_.reset();
    smgr_.reset();
    raft_instance_.reset();
  }

  // Server ID.
  int server_id_;

  // Server address.
  std::string addr_;

  // Server port.
  int port_;

  // Endpoint: "<addr>:<port>".
  std::string endpoint_;

  // State machine.
  nuraft::ptr<nuraft::state_machine> sm_;

  // State manager.
  nuraft::ptr<nuraft::state_mgr> smgr_;

  // Raft launcher.
  nuraft::raft_launcher launcher_;

  // Raft server instance.
  nuraft::ptr<nuraft::raft_server> raft_instance_;

  // List of servers to auto add.
  std::map<int, std::string> server_list_;

  // STEK update interval.
  int key_update_interval_;

  // When was STEK last updated.
  time_t last_updated_;

  uint64_t current_log_idx_;

  size_t asio_thread_pool_size_;

  int heart_beat_interval_;

  int election_timeout_lower_bound_;
  int election_timeout_upper_bound_;

  int reserved_log_items_;

  int snapshot_distance_;

  int client_req_timeout_;

  // TLS related stuff.
  std::string root_cert_file_;
  std::string server_cert_file_;
  std::string server_key_file_;
  std::string cert_verify_str_;

  ssl_ticket_key_t ticket_keys_[2];
};
