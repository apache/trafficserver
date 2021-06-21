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

#include <libnuraft/nuraft.hxx>

#include "log_store.h"

class STEKShareSMGR : public nuraft::state_mgr
{
public:
  STEKShareSMGR(int srv_id, const std::string &endpoint, std::map<int, std::string> server_list)
    : my_id_(srv_id), my_endpoint_(endpoint), cur_log_store_(nuraft::cs_new<STEKShareLogStore>())
  {
    my_srv_config_ = nuraft::cs_new<nuraft::srv_config>(srv_id, endpoint);

    // Initial cluster config, read from the list loaded from the configuration file.
    saved_config_ = nuraft::cs_new<nuraft::cluster_config>();
    for (auto const &s : server_list) {
      int server_id                              = s.first;
      std::string endpoint                       = s.second;
      nuraft::ptr<nuraft::srv_config> new_server = nuraft::cs_new<nuraft::srv_config>(server_id, endpoint);
      saved_config_->get_servers().push_back(new_server);
    }
  }

  ~STEKShareSMGR() {}

  nuraft::ptr<nuraft::cluster_config>
  load_config()
  {
    return saved_config_;
  }

  void
  save_config(const nuraft::cluster_config &config)
  {
    nuraft::ptr<nuraft::buffer> buf = config.serialize();
    saved_config_                   = nuraft::cluster_config::deserialize(*buf);
  }

  void
  save_state(const nuraft::srv_state &state)
  {
    nuraft::ptr<nuraft::buffer> buf = state.serialize();
    saved_state_                    = nuraft::srv_state::deserialize(*buf);
  }

  nuraft::ptr<nuraft::srv_state>
  read_state()
  {
    return saved_state_;
  }

  nuraft::ptr<nuraft::log_store>
  load_log_store()
  {
    return cur_log_store_;
  }

  int32_t
  server_id()
  {
    return my_id_;
  }

  void
  system_exit(const int exit_code)
  {
  }

  nuraft::ptr<nuraft::srv_config>
  get_srv_config() const
  {
    return my_srv_config_;
  }

private:
  int my_id_;
  std::string my_endpoint_;
  nuraft::ptr<STEKShareLogStore> cur_log_store_;
  nuraft::ptr<nuraft::srv_config> my_srv_config_;
  nuraft::ptr<nuraft::cluster_config> saved_config_;
  nuraft::ptr<nuraft::srv_state> saved_state_;
};
