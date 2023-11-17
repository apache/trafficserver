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

static DbgCtl dbg_ctl{PLUGIN_NAME};

PluginThreads plugin_threads;

static STEKShareServer stek_share_server;
static const nuraft::raft_params::return_method_type CALL_TYPE = nuraft::raft_params::blocking;
// static const nuraft::raft_params::return_method_type CALL_TYPE = nuraft::raft_params::async_handler;

static std::string conf_file_path;

std::shared_ptr<PluginConfig> plugin_config;
std::shared_mutex plugin_config_mutex;

std::shared_ptr<PluginConfig> plugin_config_old;
std::shared_mutex plugin_config_old_mutex;

int load_config_from_file();
int init_raft(nuraft::ptr<nuraft::state_machine> sm_instance, std::shared_ptr<PluginConfig> config);
static void *stek_updater(void *arg);

std::shared_ptr<PluginConfig>
get_scoped_config(bool backup = false)
{
  if (backup) {
    std::shared_lock lock(plugin_config_old_mutex);
    auto config = plugin_config_old;
    return config;
  } else {
    std::shared_lock lock(plugin_config_mutex);
    auto config = plugin_config;
    return config;
  }
}

void
backup_config(std::shared_ptr<PluginConfig> config)
{
  std::unique_lock lock(plugin_config_old_mutex);
  plugin_config_old = config;
}

void
restore_config(std::shared_ptr<PluginConfig> config)
{
  std::unique_lock lock(plugin_config_mutex);
  plugin_config = config;
}

static int
shutdown_handler(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_SHUTDOWN) {
    stek_share_server.raft_launcher.shutdown();
    stek_share_server.reset();
    plugin_threads.terminate();
  }
  return 0;
}

static int
message_handler(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_MSG) {
    TSPluginMsg *msg = static_cast<TSPluginMsg *>(edata);
    Dbg(dbg_ctl, "Message to '%s' - %zu bytes of data", msg->tag, msg->data_size);
    if (strcmp(PLUGIN_NAME, msg->tag) == 0) { // Message is for us
      if (msg->data_size) {
        if (strncmp(reinterpret_cast<char *>(const_cast<void *>(msg->data)), "reload", msg->data_size) == 0) {
          // TODO: If in the middle of generating a new STEK, maybe block until new STEK has been generated?
          //       Not a big problem since new STEK is only generated once every few hours.
          Dbg(dbg_ctl, "Reloading configurations...");
          if (load_config_from_file() == 0) {
            stek_share_server.config_reloading = true;
            stek_share_server.raft_launcher.shutdown();
            stek_share_server.reset();
            auto config = get_scoped_config();
            if (init_raft(nuraft::cs_new<STEKShareSM>(), config) == 0) {
              backup_config(config);
              Dbg(dbg_ctl, "Server ID: %d, Endpoint: %s", config->server_id, config->endpoint.c_str());
            } else {
              TSError("[%s] Raft initialization failed with new config, retrying with old config.", PLUGIN_NAME);
              auto config_old = get_scoped_config(true);
              restore_config(config_old);
              if (init_raft(nuraft::cs_new<STEKShareSM>(), config_old) == 0) {
                Dbg(dbg_ctl, "Server ID: %d, Endpoint: %s", config->server_id, config->endpoint.c_str());
              } else {
                TSEmergency("[%s] Raft initialization failed with old config.", PLUGIN_NAME);
              }
            }
          } else {
            TSError("[%s] Config reload failed.", PLUGIN_NAME);
          }
        } else {
          TSError("[%s] Unexpected msg %*.s", PLUGIN_NAME, static_cast<int>(msg->data_size),
                  reinterpret_cast<char *>(const_cast<void *>(msg->data)));
        }
      }
    }
  } else {
    TSError("[%s] Unexpected event %d", PLUGIN_NAME, event);
  }
  return TS_EVENT_NONE;
}

bool
cert_verification(const std::string &sn)
{
  auto config = get_scoped_config();
  if (!config->cert_verify_str.empty() && sn.compare(config->cert_verify_str) != 0) {
    Dbg(dbg_ctl, "Cert incorrect, expecting: %s, got: %s", config->cert_verify_str.c_str(), sn.c_str());
    return false;
  }
  return true;
}

int
init_raft(nuraft::ptr<nuraft::state_machine> sm_instance, std::shared_ptr<PluginConfig> config)
{
  // State manager.
  {
    std::unique_lock lock(stek_share_server.smgr_mutex);
    stek_share_server.smgr_instance = nuraft::cs_new<STEKShareSMGR>(config->server_id, config->endpoint, config->server_list);
  }

  // State machine.
  {
    std::unique_lock lock(stek_share_server.sm_mutex);
    stek_share_server.sm_instance = sm_instance;
  }

  // ASIO options.
  nuraft::asio_service::options asio_opts;
  asio_opts.thread_pool_size_ = config->asio_thread_pool_size;
  asio_opts.enable_ssl_       = true;
  asio_opts.verify_sn_        = cert_verification;
  asio_opts.root_cert_file_   = config->root_cert_file;
  asio_opts.server_cert_file_ = config->server_cert_file;
  asio_opts.server_key_file_  = config->server_key_file;

  // Raft parameters.
  nuraft::raft_params params;
  params.heart_beat_interval_          = config->heart_beat_interval;
  params.election_timeout_lower_bound_ = config->election_timeout_lower_bound;
  params.election_timeout_upper_bound_ = config->election_timeout_upper_bound;
  params.reserved_log_items_           = config->reserved_log_items;
  params.snapshot_distance_            = config->snapshot_distance;
  params.client_req_timeout_           = config->client_req_timeout;

  // According to this method, "append_log" function should be handled differently.
  params.return_method_ = CALL_TYPE;

  // Initialize Raft server.
  {
    std::unique_lock lock(stek_share_server.raft_mutex);
    stek_share_server.raft_instance = stek_share_server.raft_launcher.init(
      stek_share_server.sm_instance, stek_share_server.smgr_instance, nullptr, config->port, asio_opts, params);
  }

  std::shared_lock lock(stek_share_server.raft_mutex);
  if (!stek_share_server.raft_instance) {
    Dbg(dbg_ctl, "Failed to initialize launcher.");
    return -1;
  }

  return 0;
}

int
load_config_from_file()
{
  auto new_config = std::make_shared<PluginConfig>();

  YAML::Node server_conf;
  try {
    server_conf = YAML::LoadFile(conf_file_path);
  } catch (YAML::BadFile &e) {
    Dbg(dbg_ctl, "Cannot load configuration file: %s.", e.what());
    return -1;
  } catch (std::exception &e) {
    Dbg(dbg_ctl, "Unknown error while loading configuration file: %s.", e.what());
    return -1;
  }

  // Get server id.
  if (server_conf["server_id"]) {
    new_config->server_id = server_conf["server_id"].as<int>();
    if (new_config->server_id < 1) {
      Dbg(dbg_ctl, "Wrong server id (must be >= 1): %d", new_config->server_id);
      return -1;
    }
  } else {
    Dbg(dbg_ctl, "Must specify server id in the configuration file.");
    return -1;
  }

  // Get server address and port.
  if (server_conf["address"]) {
    new_config->address = server_conf["address"].as<std::string>();
  } else {
    Dbg(dbg_ctl, "Must specify server address in the configuration file.");
    return -1;
  }

  if (server_conf["port"]) {
    new_config->port = server_conf["port"].as<int>();
  } else {
    Dbg(dbg_ctl, "Must specify server port in the configuration file.");
    return -1;
  }

  new_config->endpoint = new_config->address + ":" + std::to_string(new_config->port);

  if (server_conf["asio_thread_pool_size"]) {
    new_config->asio_thread_pool_size = server_conf["asio_thread_pool_size"].as<size_t>();
  }

  if (server_conf["heart_beat_interval"]) {
    new_config->heart_beat_interval = server_conf["heart_beat_interval"].as<int>();
  }

  if (server_conf["election_timeout_lower_bound"]) {
    new_config->election_timeout_lower_bound = server_conf["election_timeout_lower_bound"].as<int>();
  }

  if (server_conf["election_timeout_upper_bound"]) {
    new_config->election_timeout_upper_bound = server_conf["election_timeout_upper_bound"].as<int>();
  }

  if (server_conf["reserved_log_items"]) {
    new_config->reserved_log_items = server_conf["reserved_log_items"].as<int>();
  }

  if (server_conf["snapshot_distance"]) {
    new_config->snapshot_distance = server_conf["snapshot_distance"].as<int>();
  }

  if (server_conf["client_req_timeout"]) {
    new_config->client_req_timeout = server_conf["client_req_timeout"].as<int>();
  }

  if (server_conf["key_update_interval"]) {
    new_config->key_update_interval = std::chrono::seconds(server_conf["key_update_interval"].as<int>());
  } else {
    Dbg(dbg_ctl, "Must specify server key update interval in the configuration file.");
    return -1;
  }

  if (server_conf["server_list_file"]) {
    YAML::Node server_list;
    try {
      server_list = YAML::LoadFile(server_conf["server_list_file"].as<std::string>());
    } catch (YAML::BadFile &e) {
      Dbg(dbg_ctl, "Cannot load server list file: %s.", e.what());
      return -1;
    } catch (std::exception &e) {
      Dbg(dbg_ctl, "Unknown error while loading server list file: %s.", e.what());
      return -1;
    }

    std::string cluster_list_str  = "";
    cluster_list_str             += "\nSTEK Share Cluster Server List:";
    for (auto it = server_list.begin(); it != server_list.end(); ++it) {
      YAML::Node server_info = it->as<YAML::Node>();
      if (server_info["server_id"] && server_info["address"] && server_info["port"]) {
        int server_id                       = server_info["server_id"].as<int>();
        std::string address                 = server_info["address"].as<std::string>();
        int port                            = server_info["port"].as<int>();
        std::string endpoint                = address + ":" + std::to_string(port);
        new_config->server_list[server_id]  = endpoint;
        cluster_list_str                   += "\n  " + std::to_string(server_id) + ", " + endpoint;
      } else {
        Dbg(dbg_ctl, "Wrong server list format.");
        return -1;
      }
    }
    Dbg(dbg_ctl, "%s", cluster_list_str.c_str());
  } else {
    Dbg(dbg_ctl, "Must specify server list file in the configuration file.");
    return -1;
  }

  // TODO: check cert and key files exist
  if (server_conf["root_cert_file"]) {
    new_config->root_cert_file = server_conf["root_cert_file"].as<std::string>();
  } else {
    Dbg(dbg_ctl, "Must specify root ca file in the configuration file.");
    return -1;
  }

  if (server_conf["server_cert_file"]) {
    new_config->server_cert_file = server_conf["server_cert_file"].as<std::string>();
  } else {
    Dbg(dbg_ctl, "Must specify server cert file in the configuration file.");
    return -1;
  }

  if (server_conf["server_key_file"]) {
    new_config->server_key_file = server_conf["server_key_file"].as<std::string>();
  } else {
    Dbg(dbg_ctl, "Must specify server key file in the configuration file.");
    return -1;
  }

  if (server_conf["cert_verify_str"]) {
    new_config->cert_verify_str = server_conf["cert_verify_str"].as<std::string>();
  }

  std::unique_lock lock(plugin_config_mutex);
  plugin_config = new_config;

  return 0;
}

void
handle_result(raft_result &result, nuraft::ptr<std::exception> &err)
{
  if (result.get_result_code() != nuraft::cmd_result_code::OK) {
    // Something went wrong.
    // This means committing this log failed, but the log itself is still in the log store.
    Dbg(dbg_ctl, "Replication failed: %d", result.get_result_code());
    return;
  }
  Dbg(dbg_ctl, "Replication succeeded.");
}

void
append_log(const void *data, int data_len)
{
  // Create a new log which will contain 4-byte length and string data.
  nuraft::ptr<nuraft::buffer> new_log = nuraft::buffer::alloc(sizeof(int) + data_len);
  nuraft::buffer_serializer bs(new_log);
  bs.put_bytes(data, data_len);

  // Do append.
  std::shared_lock lock(stek_share_server.raft_mutex);
  nuraft::ptr<raft_result> ret = stek_share_server.raft_instance->append_entries({new_log});

  if (!ret->get_accepted()) {
    // Log append rejected, usually because this node is not a leader.
    Dbg(dbg_ctl, "Replication failed: %d", ret->get_result_code());
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
  auto config = get_scoped_config();
  // For debugging
  std::shared_lock smgr_lock(stek_share_server.smgr_mutex);
  std::shared_lock raft_lock(stek_share_server.raft_mutex);
  nuraft::ptr<nuraft::log_store> ls  = stek_share_server.smgr_instance->load_log_store();
  std::string status_str             = "";
  status_str                        += "\n  Server ID: " + std::to_string(config->server_id);
  status_str                        += "\n  Leader ID: " + std::to_string(stek_share_server.raft_instance->get_leader());
  status_str += "\n  Raft log range: " + std::to_string(ls->start_index()) + " - " + std::to_string((ls->next_slot() - 1));
  status_str += "\n  Last committed index: " + std::to_string(stek_share_server.raft_instance->get_committed_log_idx());
  Dbg(dbg_ctl, "%s", status_str.c_str());
}

static void *
stek_updater(void *arg)
{
  plugin_threads.store(::pthread_self());
  ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
  ::pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, nullptr);

  Dbg(dbg_ctl, "Starting STEK updater thread");

  while (!plugin_threads.is_shut_down()) {
    ssl_ticket_key_t curr_stek;
    std::chrono::time_point<std::chrono::system_clock> init_key_time;

    // Initial key to use before syncing up.
    Dbg(dbg_ctl, "Generating initial STEK...");
    if (generate_new_stek(&curr_stek, 0 /* fast start */) == 0) {
      Dbg(dbg_ctl, "Generate initial STEK succeeded: %s",
          hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

      std::memcpy(&stek_share_server.ticket_keys[0], &curr_stek, SSL_TICKET_KEY_SIZE);

      Dbg(dbg_ctl, "Updating SSL Ticket Key...");
      if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys), SSL_TICKET_KEY_SIZE) == TS_ERROR) {
        Dbg(dbg_ctl, "Update SSL Ticket Key failed.");
      } else {
        Dbg(dbg_ctl, "Update SSL Ticket Key succeeded.");
        init_key_time = std::chrono::system_clock::now();
      }
    } else {
      TSFatal("[%s] Generate initial STEK failed.", PLUGIN_NAME);
    }

    // Since we're using a pre-configured cluster, we need to have >= 2 nodes in the cluster
    // to initialize. Busy check before that.
    auto config = get_scoped_config();
    while (!stek_share_server.config_reloading && !plugin_threads.is_shut_down()) {
      std::shared_lock init_lock(stek_share_server.raft_mutex);
      if (!stek_share_server.raft_instance || !stek_share_server.raft_instance->is_initialized()) {
        init_lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        continue;
      }
      init_lock.unlock();

      std::shared_lock leader_lock(stek_share_server.raft_mutex);
      if (stek_share_server.raft_instance->is_leader()) {
        // We only need to generate new STEK if this server is the leader.
        // Otherwise we wake up every 10 seconds to see whether a new STEK has been received.
        if (std::chrono::duration_cast<std::chrono::seconds>(init_key_time.time_since_epoch()).count() != 0 &&
            std::chrono::system_clock::now() - init_key_time < config->key_update_interval) {
          // If we got here after starting up, that means the initial key is still valid and we can send it to everyone else.
          stek_share_server.last_updated = init_key_time;
          Dbg(dbg_ctl, "Using initial STEK: %s",
              hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());
          append_log(reinterpret_cast<const void *>(&curr_stek), SSL_TICKET_KEY_SIZE);

        } else if (std::chrono::system_clock::now() - stek_share_server.last_updated >= config->key_update_interval) {
          // Generate a new key as the last one has expired.
          // Move the old key from ticket_keys_[0] to ticket_keys_[1], then put the new key in ticket_keys_[0].
          Dbg(dbg_ctl, "Generating new STEK...");
          if (generate_new_stek(&curr_stek, 1) == 0) {
            Dbg(dbg_ctl, "Generate new STEK succeeded: %s",
                hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

            std::memcpy(&stek_share_server.ticket_keys[1], &stek_share_server.ticket_keys[0], SSL_TICKET_KEY_SIZE);
            std::memcpy(&stek_share_server.ticket_keys[0], &curr_stek, SSL_TICKET_KEY_SIZE);

            Dbg(dbg_ctl, "Updating SSL Ticket Key...");
            if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys), SSL_TICKET_KEY_SIZE * 2) ==
                TS_ERROR) {
              Dbg(dbg_ctl, "Update SSL Ticket Key failed.");
            } else {
              stek_share_server.last_updated = std::chrono::system_clock::now();
              Dbg(dbg_ctl, "Update SSL Ticket Key succeeded.");
              Dbg(dbg_ctl, "Using new STEK: %s",
                  hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());
              append_log(reinterpret_cast<const void *>(&curr_stek), SSL_TICKET_KEY_SIZE);
            }
          } else {
            TSFatal("[%s] Generate new STEK failed.", PLUGIN_NAME);
          }
        }
        // Set this to 0 so we won't enter the code path where we use the initial STEK.
        init_key_time = std::chrono::time_point<std::chrono::system_clock>();
      } else {
        init_key_time = std::chrono::time_point<std::chrono::system_clock>();

        std::shared_lock sm_lock(stek_share_server.sm_mutex);
        auto sm = dynamic_cast<STEKShareSM *>(stek_share_server.sm_instance.get());

        // Check whether we received a new key.
        // TODO: retry updating STEK when failed
        if (sm->received_stek(&curr_stek)) {
          Dbg(dbg_ctl, "Received new STEK: %s",
              hex_str(std::string(reinterpret_cast<char *>(&curr_stek), SSL_TICKET_KEY_SIZE)).c_str());

          // Move the old key from ticket_keys_[0] to ticket_keys_[1], then put the new key in ticket_keys_[0].
          std::memcpy(&stek_share_server.ticket_keys[1], &stek_share_server.ticket_keys[0], SSL_TICKET_KEY_SIZE);
          std::memcpy(&stek_share_server.ticket_keys[0], &curr_stek, SSL_TICKET_KEY_SIZE);

          Dbg(dbg_ctl, "Updating SSL Ticket Key...");
          if (TSSslTicketKeyUpdate(reinterpret_cast<char *>(stek_share_server.ticket_keys), SSL_TICKET_KEY_SIZE * 2) == TS_ERROR) {
            Dbg(dbg_ctl, "Update SSL Ticket Key failed.");
          } else {
            stek_share_server.last_updated = std::chrono::system_clock::now();
            Dbg(dbg_ctl, "Update SSL Ticket Key succeeded.");
          }
        }
      }

      leader_lock.unlock();

      // Wakeup every 10 seconds to check whether there is a new key to use.
      // We do this because if a server is lagging behind, either by losing connection or joining late,
      // that server might receive multiple keys (the ones it missed) when it reconnects. Since we only need the
      // most recent one, and to save time, we check back every 10 seconds in hope that the barrage of incoming
      // keys has finished, and if not the second time around it'll definitely has.
      std::this_thread::sleep_for(std::chrono::seconds(10));
    }

    stek_share_server.config_reloading = false;
  }

  Dbg(dbg_ctl, "Stopping STEK updater thread");

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
    TSError("[%s] Plugin registration failed.", PLUGIN_NAME);
    return;
  }

  TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, TSContCreate(message_handler, nullptr));

  if (argc < 2) {
    TSError("[%s] Must specify config file.", PLUGIN_NAME);
  } else {
    conf_file_path = argv[1];
    if (load_config_from_file() == 0) {
      auto config = get_scoped_config();
      if (init_raft(nuraft::cs_new<STEKShareSM>(), config) == 0) {
        backup_config(config);
        Dbg(dbg_ctl, "Server ID: %d, Endpoint: %s", config->server_id, config->endpoint.c_str());
        TSThreadCreate(stek_updater, nullptr);
      } else {
        TSError("[%s] Raft initialization failed.", PLUGIN_NAME);
      }
    } else {
      TSError("[%s] Config load failed.", PLUGIN_NAME);
    }
  }
}
