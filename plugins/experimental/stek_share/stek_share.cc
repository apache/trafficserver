/** @file

  stek_share.cc

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

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

#include <openssl/ssl.h>
#include <ts/ts.h>
#include <ts/apidefs.h>
#include <libnuraft/nuraft.hxx>
#include <yaml-cpp/yaml.h>

#include "state_machine.h"
#include "state_manager.h"
#include "stek_share.h"
#include "stek_utils.h"
#include "common.h"

using raft_result = nuraft::cmd_result<nuraft::ptr<nuraft::buffer>>;

PluginThreads plugin_threads;

static STEKShareServer stek_share_server;
static const nuraft::raft_params::return_method_type CALL_TYPE = nuraft::raft_params::blocking;
// static const nuraft::raft_params::return_method_type CALL_TYPE = nuraft::raft_params::async_handler;

static int
shutdown_handler(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_SHUTDOWN) {
    plugin_threads.terminate();
    stek_share_server.launcher_.shutdown();
  }
  return 0;
}

bool
cert_verification(const std::string &sn)
{
  if (sn.compare(stek_share_server.cert_verify_str_) != 0) {
    TSDebug(PLUGIN, "Cert incorrect, expecting: %s, got: %s", stek_share_server.cert_verify_str_.c_str(), sn.c_str());
    return false;
  }
  return true;
}

int
init_raft(nuraft::ptr<nuraft::state_machine> sm_instance)
{
  // State machine.
  stek_share_server.smgr_ =
    nuraft::cs_new<STEKShareSMGR>(stek_share_server.server_id_, stek_share_server.endpoint_, stek_share_server.server_list_);

  // State manager.
  stek_share_server.sm_ = sm_instance;

  // ASIO options.
  nuraft::asio_service::options asio_opts;
  asio_opts.thread_pool_size_ = stek_share_server.asio_thread_pool_size_;
  asio_opts.enable_ssl_       = true;
  asio_opts.verify_sn_        = cert_verification;
  asio_opts.root_cert_file_   = stek_share_server.root_cert_file_;
  asio_opts.server_cert_file_ = stek_share_server.server_cert_file_;
  asio_opts.server_key_file_  = stek_share_server.server_key_file_;

  // Raft parameters.
  nuraft::raft_params params;
  params.heart_beat_interval_          = stek_share_server.heart_beat_interval_;
  params.election_timeout_lower_bound_ = stek_share_server.election_timeout_lower_bound_;
  params.election_timeout_upper_bound_ = stek_share_server.election_timeout_upper_bound_;
  params.reserved_log_items_           = stek_share_server.reserved_log_items_;
  params.snapshot_distance_            = stek_share_server.snapshot_distance_;
  params.client_req_timeout_           = stek_share_server.client_req_timeout_;

  // According to this method, "append_log" function should be handled differently.
  params.return_method_ = CALL_TYPE;

  // Initialize Raft server.
  stek_share_server.raft_instance_ = stek_share_server.launcher_.init(stek_share_server.sm_, stek_share_server.smgr_, nullptr,
                                                                      stek_share_server.port_, asio_opts, params);

  if (!stek_share_server.raft_instance_) {
    TSDebug(PLUGIN, "Failed to initialize launcher.");
    return -1;
  }

  TSDebug(PLUGIN, "Raft instance initialization done.");
  return 0;
}

int
set_server_info(int argc, const char *argv[])
{
  // Get server ID.
  YAML::Node server_conf;
  try {
    server_conf = YAML::LoadFile(argv[1]);
  } catch (YAML::BadFile &e) {
    TSEmergency("[%s] Cannot load configuration file: %s.", PLUGIN, e.what());
  } catch (std::exception &e) {
    TSEmergency("[%s] Unknown error while loading configuration file: %s.", PLUGIN, e.what());
  }

  if (server_conf["server_id"]) {
    stek_share_server.server_id_ = server_conf["server_id"].as<int>();
    if (stek_share_server.server_id_ < 1) {
      TSDebug(PLUGIN, "Wrong server id (must be >= 1): %d", stek_share_server.server_id_);
      return -1;
    }
  } else {
    TSDebug(PLUGIN, "Must specify server id in the configuration file.");
    return -1;
  }

  // Get server address and port.
  if (server_conf["address"]) {
    stek_share_server.addr_ = server_conf["address"].as<std::string>();
  } else {
    TSDebug(PLUGIN, "Must specify server address in the configuration file.");
    return -1;
  }

  if (server_conf["port"]) {
    stek_share_server.port_ = server_conf["port"].as<int>();
  } else {
    TSDebug(PLUGIN, "Must specify server port in the configuration file.");
    return -1;
  }

  stek_share_server.endpoint_ = stek_share_server.addr_ + ":" + std::to_string(stek_share_server.port_);

  if (server_conf["asio_thread_pool_size"]) {
    stek_share_server.asio_thread_pool_size_ = server_conf["asio_thread_pool_size"].as<size_t>();
  }

  if (server_conf["heart_beat_interval"]) {
    stek_share_server.heart_beat_interval_ = server_conf["heart_beat_interval"].as<int>();
  }

  if (server_conf["election_timeout_lower_bound"]) {
    stek_share_server.election_timeout_lower_bound_ = server_conf["election_timeout_lower_bound"].as<int>();
  }

  if (server_conf["election_timeout_upper_bound"]) {
    stek_share_server.election_timeout_upper_bound_ = server_conf["election_timeout_upper_bound"].as<int>();
  }

  if (server_conf["reserved_log_items"]) {
    stek_share_server.reserved_log_items_ = server_conf["reserved_log_items"].as<int>();
  }

  if (server_conf["snapshot_distance"]) {
    stek_share_server.snapshot_distance_ = server_conf["snapshot_distance"].as<int>();
  }

  if (server_conf["client_req_timeout"]) {
    stek_share_server.client_req_timeout_ = server_conf["client_req_timeout"].as<int>();
  }

  if (server_conf["key_update_interval"]) {
    stek_share_server.key_update_interval_ = server_conf["key_update_interval"].as<int>();
  } else {
    TSDebug(PLUGIN, "Must specify server key update interval in the configuration file.");
    return -1;
  }

  if (server_conf["server_list_file"]) {
    YAML::Node server_list;
    try {
      server_list = YAML::LoadFile(server_conf["server_list_file"].as<std::string>());
    } catch (YAML::BadFile &e) {
      TSEmergency("[%s] Cannot load server list file: %s.", PLUGIN, e.what());
    } catch (std::exception &e) {
      TSEmergency("[%s] Unknown error while loading server list file: %s.", PLUGIN, e.what());
    }

    std::string cluster_list_str = "";
    cluster_list_str += "\nSTEK Share Cluster Server List:";
    for (auto it = server_list.begin(); it != server_list.end(); ++it) {
      YAML::Node server_info = it->as<YAML::Node>();
      if (server_info["server_id"] && server_info["address"] && server_info["port"]) {
        int server_id                             = server_info["server_id"].as<int>();
        std::string address                       = server_info["address"].as<std::string>();
        int port                                  = server_info["port"].as<int>();
        std::string endpoint                      = address + ":" + std::to_string(port);
        stek_share_server.server_list_[server_id] = endpoint;
        cluster_list_str += "\n  " + std::to_string(server_id) + ", " + endpoint;
      } else {
        TSDebug(PLUGIN, "Wrong server list format.");
        return -1;
      }
    }
    TSDebug(PLUGIN, "%s", cluster_list_str.c_str());
  } else {
    TSDebug(PLUGIN, "Must specify server list file in the configuration file.");
    return -1;
  }

  // TODO: check cert and key files exist
  if (server_conf["root_cert_file"]) {
    stek_share_server.root_cert_file_ = server_conf["root_cert_file"].as<std::string>();
  } else {
    TSDebug(PLUGIN, "Must specify root ca file in the configuration file.");
    return -1;
  }

  if (server_conf["server_cert_file"]) {
    stek_share_server.server_cert_file_ = server_conf["server_cert_file"].as<std::string>();
  } else {
    TSDebug(PLUGIN, "Must specify server cert file in the configuration file.");
    return -1;
  }

  if (server_conf["server_key_file"]) {
    stek_share_server.server_key_file_ = server_conf["server_key_file"].as<std::string>();
  } else {
    TSDebug(PLUGIN, "Must specify server key file in the configuration file.");
    return -1;
  }

  if (server_conf["cert_verify_str"]) {
    stek_share_server.cert_verify_str_ = server_conf["cert_verify_str"].as<std::string>();
  } else {
    TSDebug(PLUGIN, "Must specify cert verify string in the configuration file.");
    return -1;
  }

  return 0;
}

void
handle_result(raft_result &result, nuraft::ptr<std::exception> &err)
{
  if (result.get_result_code() != nuraft::cmd_result_code::OK) {
    // Something went wrong.
    // This means committing this log failed, but the log itself is still in the log store.
    TSDebug(PLUGIN, "Replication failed: %d", result.get_result_code());
    return;
  }
  TSDebug(PLUGIN, "Replication succeeded.");
}

void
append_log(const void *data, int data_len)
{
  // Create a new log which will contain 4-byte length and string data.
  nuraft::ptr<nuraft::buffer> new_log = nuraft::buffer::alloc(sizeof(int) + data_len);
  nuraft::buffer_serializer bs(new_log);
  bs.put_bytes(data, data_len);

  // Do append.
  nuraft::ptr<raft_result> ret = stek_share_server.raft_instance_->append_entries({new_log});

  if (!ret->get_accepted()) {
    // Log append rejected, usually because this node is not a leader.
    TSDebug(PLUGIN, "Replication failed: %d", ret->get_result_code());
    return;
  }

  // Log append accepted, but that doesn't mean the log is committed.
  // Commit result can be obtained below.
  if (CALL_TYPE == nuraft::raft_params::blocking) {
    // Blocking mode:
    //   "append_entries" returns after getting a consensus, so that "ret" already has the result from state machine.
    nuraft::ptr<std::exception> err(nullptr);
    handle_result(*ret, err);
  } else if (CALL_TYPE == nuraft::raft_params::async_handler) {
    // Async mode:
    //   "append_entries" returns immediately. "handle_result" will be invoked asynchronously, after getting a consensus.
    ret->when_ready(std::bind(handle_result, std::placeholders::_1, std::placeholders::_2));
  } else {
    assert(0);
  }
}

void
print_status()
{
  // For debugging
  nuraft::ptr<nuraft::log_store> ls = stek_share_server.smgr_->load_log_store();
  std::string status_str            = "";
  status_str += "\n  Server ID: " + std::to_string(stek_share_server.server_id_);
  status_str += "\n  Leader ID: " + std::to_string(stek_share_server.raft_instance_->get_leader());
  status_str += "\n  Raft log range: " + std::to_string(ls->start_index()) + " - " + std::to_string((ls->next_slot() - 1));
  status_str += "\n  Last committed index: " + std::to_string(stek_share_server.raft_instance_->get_committed_log_idx());
  TSDebug(PLUGIN, "%s", status_str.c_str());
}

static void *
stek_updater(void *arg)
{
  plugin_threads.store(::pthread_self());
  ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  ssl_ticket_key_t curr_stek;
  time_t init_key_time = 0;

  // Initial key to use before syncing up.
  TSDebug(PLUGIN, "Generating initial STEK...");
  if (generate_new_stek(&curr_stek, 0 /* fast start */) == 0) {
    TSDebug(PLUGIN, "Generate initial STEK succeeded: %s",
            hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

    std::memcpy(&stek_share_server.ticket_keys_[0], &curr_stek, SSL_TICKET_KEY_SIZE);

    TSDebug(PLUGIN, "Updating SSL Ticket Key...");
    if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys_), SSL_TICKET_KEY_SIZE) == TS_ERROR) {
      TSDebug(PLUGIN, "Update SSL Ticket Key failed.");
    } else {
      TSDebug(PLUGIN, "Update SSL Ticket Key succeeded.");
      init_key_time = time(nullptr);
    }
  } else {
    TSFatal("Generate initial STEK failed.");
  }

  // Since we're using a pre-configured cluster, we need to have >= 3 nodes in the clust
  // to initialize. Busy check before that.
  while (!plugin_threads.is_shut_down()) {
    if (!stek_share_server.raft_instance_->is_initialized()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      continue;
    }

    if (stek_share_server.raft_instance_->is_leader()) {
      // We only need to generate new STEK if this server is the leader.
      // Otherwise we wake up every 10 seconds to see whether a new STEK has been received.
      if (init_key_time != 0 && time(nullptr) - init_key_time < stek_share_server.key_update_interval_) {
        // If we got here after starting up, that means the initial key is still valid and we can send it to everyone else.
        stek_share_server.last_updated_ = init_key_time;
        TSDebug(PLUGIN, "Using initial STEK: %s",
                hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());
        append_log(reinterpret_cast<const void *>(&curr_stek), SSL_TICKET_KEY_SIZE);

      } else if (time(nullptr) - stek_share_server.last_updated_ >= stek_share_server.key_update_interval_) {
        // Generate a new key as the last one has expired.
        // Move the old key from ticket_keys_[0] to ticket_keys_[1], then put the new key in ticket_keys_[0].
        TSDebug(PLUGIN, "Generating new STEK...");
        if (generate_new_stek(&curr_stek, 1) == 0) {
          TSDebug(PLUGIN, "Generate new STEK succeeded: %s",
                  hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

          std::memcpy(&stek_share_server.ticket_keys_[1], &stek_share_server.ticket_keys_[0], SSL_TICKET_KEY_SIZE);
          std::memcpy(&stek_share_server.ticket_keys_[0], &curr_stek, SSL_TICKET_KEY_SIZE);

          TSDebug(PLUGIN, "Updating SSL Ticket Key...");
          if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys_), SSL_TICKET_KEY_SIZE * 2) == TS_ERROR) {
            TSDebug(PLUGIN, "Update SSL Ticket Key failed.");
          } else {
            stek_share_server.last_updated_ = time(nullptr);
            TSDebug(PLUGIN, "Update SSL Ticket Key succeeded.");
            TSDebug(PLUGIN, "Using new STEK: %s",
                    hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());
            append_log(reinterpret_cast<const void *>(&curr_stek), SSL_TICKET_KEY_SIZE);
          }
        } else {
          TSFatal("Generate new STEK failed.");
        }
      }
      init_key_time = 0;

    } else {
      init_key_time = 0;
      auto sm       = dynamic_cast<STEKShareSM *>(stek_share_server.sm_.get());

      // Check whether we received a new key.
      // TODO: retry updating STEK when failed
      if (sm->received_stek(&curr_stek)) {
        TSDebug(PLUGIN, "Received new STEK: %s",
                hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

        // Move the old key from ticket_keys_[0] to ticket_keys_[1], then put the new key in ticket_keys_[0].
        std::memcpy(&stek_share_server.ticket_keys_[1], &stek_share_server.ticket_keys_[0], SSL_TICKET_KEY_SIZE);
        std::memcpy(&stek_share_server.ticket_keys_[0], &curr_stek, SSL_TICKET_KEY_SIZE);

        TSDebug(PLUGIN, "Updating SSL Ticket Key...");
        if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys_), SSL_TICKET_KEY_SIZE * 2) == TS_ERROR) {
          TSDebug(PLUGIN, "Update SSL Ticket Key failed.");
        } else {
          stek_share_server.last_updated_ = time(nullptr);
          TSDebug(PLUGIN, "Update SSL Ticket Key succeeded.");
        }
      }
    }

    // Wakeup every 10 seconds to check whether there is a new key to use.
    // We do this because if a server is lagging behind, either by losing connection or joining late,
    // that server might receive multiple keys (the ones it missed) when it reconnects. Since we only need the
    // most recent one, and to save time, we check back every 10 seconds in hope that the barrage of incoming
    // keys has finished, and if not the second time around it'll definitely has.
    std::this_thread::sleep_for(std::chrono::seconds(10));
  }

  return nullptr;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)("stek_share");
  info.vendor_name   = (char *)("ats");
  info.support_email = (char *)("ats-devel@yahooinc.com");

  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(shutdown_handler, nullptr));

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("Plugin registration failed.");
    return;
  }

  if (argc < 2) {
    TSError("Must specify config file.");
  } else if (set_server_info(argc, argv) == 0 && init_raft(nuraft::cs_new<STEKShareSM>()) == 0) {
    TSDebug(PLUGIN, "Server ID: %d, Endpoint: %s", stek_share_server.server_id_, stek_share_server.endpoint_.c_str());
    TSThreadCreate(stek_updater, nullptr);
  } else {
    TSError("Raft initialization failed.");
  }
}
