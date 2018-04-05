/** @file

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

#pragma once

#include <vector>
#include <string>

#include "tsconfig/Errata.h"

constexpr char TS_fqdn[]                 = "fqdn";
constexpr char TS_disable_H2[]           = "disable_h2";
constexpr char TS_verify_client[]        = "verify_client";
constexpr char TS_tunnel_route[]         = "tunnel_route";
constexpr char TS_verify_origin_server[] = "verify_origin_server";
constexpr char TS_client_cert[]          = "client_cert";
constexpr char TS_ip_allow[]             = "ip_allow";

const int start = 0;
struct YamlSNIConfig {
  enum class Action {
    disable_h2 = start,
    verify_client,
    tunnel_route,         // blind tunnel action
    verify_origin_server, // this applies to server side vc only
    client_cert

  };
  enum class Level { NONE = 0, MODERATE, STRICT };

  YamlSNIConfig() {}

  struct Item {
    std::string fqdn;
    bool disable_h2             = false;
    uint8_t verify_client_level = 0;
    std::string tunnel_destination;
    uint8_t verify_origin_server = 0;
    std::string client_cert;
    std::string ip_allow;
  };

  ts::Errata loader(const char *cfgFilename);

  std::vector<YamlSNIConfig::Item> items;
};
