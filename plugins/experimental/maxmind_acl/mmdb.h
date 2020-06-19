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

//#pragma once

#include <cstdio>
#include <cstring>
#include <arpa/inet.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <string>
#include <cstring>
#include <iostream>
#include <fstream>
#include <yaml-cpp/yaml.h>
#include <unordered_map>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <iterator>
#include <maxminddb.h>
#include "tscore/IpMap.h"

#define PLUGIN_NAME "maxmind_acl"
#define CONFIG_TMOUT 60000

typedef struct {
  std::string config_file;
  time_t last_load;
  bool db_loaded;
} plugin_state_t;

typedef enum { ALLOW_IP, DENY_IP, UNKNOWN_IP } ipstate;

// Base class for all ACLs
class Acl
{
public:
  Acl() {}
  ~Acl()
  {
    if (plugin_state.db_loaded) {
      MMDB_close(&_mmdb);
    }
  }

  bool eval(TSRemapRequestInfo *rri, TSHttpTxn txnp);
  bool init(char *filename);
  plugin_state_t *
  get_state()
  {
    return &plugin_state;
  }

protected:
  // Class members
  YAML::Node _config;
  MMDB_s _mmdb;
  std::unordered_map<std::string, bool> allow_country;
  IpMap allow_ip_map;
  IpMap deny_ip_map;

  // Do we want to allow by default or not? Useful
  // for deny only rules
  bool default_allow = false;

  plugin_state_t plugin_state = {"", 0, false};

  bool loaddb(YAML::Node dbNode);
  bool loadallow(YAML::Node allowNode);
  bool loaddeny(YAML::Node denyNode);
  bool eval_country(MMDB_entry_data_s *entry_data);
  ipstate eval_ip(const sockaddr *sock);
};
