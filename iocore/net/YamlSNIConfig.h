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

#define TSDECL(id) constexpr char TS_##id[] = #id
TSDECL(fqdn);
TSDECL(disable_h2);
TSDECL(verify_client);
TSDECL(tunnel_route);
TSDECL(forward_route);
TSDECL(verify_server_policy);
TSDECL(verify_server_properties);
TSDECL(verify_origin_server);
TSDECL(client_cert);
TSDECL(client_key);
TSDECL(ip_allow);
#undef TSDECL

const int start = 0;
struct YamlSNIConfig {
  enum class Action {
    disable_h2 = start,
    verify_client,
    tunnel_route,             // blind tunnel action
    forward_route,            // decrypt data and then blind tunnel action
    verify_server_policy,     // this applies to server side vc only
    verify_server_properties, // this applies to server side vc only
    client_cert
  };
  enum class Level { NONE = 0, MODERATE, STRICT };
  enum class Policy : uint8_t { DISABLED = 0, PERMISSIVE, ENFORCED, UNSET };
  enum class Property : uint8_t { NONE = 0, SIGNATURE_MASK = 0x1, NAME_MASK = 0x2, ALL_MASK = 0x3, UNSET };

  YamlSNIConfig() {}

  struct Item {
    std::string fqdn;
    bool disable_h2             = false;
    uint8_t verify_client_level = 255;
    std::string tunnel_destination;
    bool tunnel_decrypt               = false;
    Policy verify_server_policy       = Policy::UNSET;
    Property verify_server_properties = Property::UNSET;
    std::string client_cert;
    std::string client_key;
    std::string ip_allow;
  };

  ts::Errata loader(const char *cfgFilename);

  std::vector<YamlSNIConfig::Item> items;
};
