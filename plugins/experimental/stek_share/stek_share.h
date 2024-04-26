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
#include <shared_mutex>
#include <chrono>

#include <libnuraft/nuraft.hxx>

#include "stek_utils.h"

class PluginConfig
{
public:
  PluginConfig()
    : server_id(1),
      address("localhost"),
      port(25000),
      endpoint("localhost:25000"),
      asio_thread_pool_size(4),          // Default ASIO thread pool size: 4.
      heart_beat_interval(100),          // Default heart beat interval: 100 ms.
      election_timeout_lower_bound(200), // Default election timeout: 200~400 ms.
      election_timeout_upper_bound(400),
      reserved_log_items(5),    // Up to 5 logs will be preserved ahead the last snapshot.
      snapshot_distance(5),     // Snapshot will be created for every 5 log appends.
      client_req_timeout(3000), // Client timeout: 3000 ms.
      key_update_interval(60)   // Generate new STEK every 60 s.
  {
  }

  // Server ID.
  int server_id;

  // Server address.
  std::string address;

  // Server port.
  int port;

  // Endpoint: "<address>:<port>".
  std::string endpoint;

  size_t asio_thread_pool_size;

  int heart_beat_interval;

  int election_timeout_lower_bound;
  int election_timeout_upper_bound;

  int reserved_log_items;

  int snapshot_distance;

  int client_req_timeout;

  // STEK update interval.
  std::chrono::seconds key_update_interval;

  // List of servers to auto add.
  std::map<int, std::string> server_list;

  // TLS related stuff.
  std::string root_cert_file;
  std::string server_cert_file;
  std::string server_key_file;
  std::string cert_verify_str;
};

class STEKShareServer
{
public:
  STEKShareServer() : sm_instance(nullptr), smgr_instance(nullptr), raft_instance(nullptr), current_log_idx(0)
  {
    std::memset(ticket_keys, 0, SSL_TICKET_KEY_SIZE * 2);
  }

  void
  reset()
  {
    {
      std::unique_lock lock(sm_mutex);
      sm_instance.reset();
    }

    {
      std::unique_lock lock(smgr_mutex);
      smgr_instance.reset();
    }

    {
      std::unique_lock lock(raft_mutex);
      raft_instance.reset();
    }
  }

  // State machine.
  nuraft::ptr<nuraft::state_machine> sm_instance;
  std::shared_mutex                  sm_mutex;

  // State manager.
  nuraft::ptr<nuraft::state_mgr> smgr_instance;
  std::shared_mutex              smgr_mutex;

  // Raft server instance.
  nuraft::ptr<nuraft::raft_server> raft_instance;
  std::shared_mutex                raft_mutex;

  // Raft launcher.
  nuraft::raft_launcher raft_launcher;

  std::atomic<bool> config_reloading = false;

  // When was STEK last updated.
  std::chrono::time_point<std::chrono::system_clock> last_updated;

  uint64_t current_log_idx;

  ssl_ticket_key_t ticket_keys[2];
};
