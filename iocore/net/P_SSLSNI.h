/** @file

  A brief file description

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

/*************************** -*- Mod: C++ -*- ******************************
  P_SSLSNI.h
   Created On      : 05/02/2017

   Description:
   SNI based Configuration in ATS
 ****************************************************************************/
#pragma once

#include "ProxyConfig.h"
#include "P_SNIActionPerformer.h"
#include "tscore/MatcherUtils.h"
#include "openssl/ossl_typ.h"
#include <vector>
#include <strings.h>
#include "YamlSNIConfig.h"

#include <unordered_map>

// Properties for the next hop server
struct NextHopProperty {
  const char *name                               = nullptr;                         // name of the server
  YamlSNIConfig::Policy verifyServerPolicy       = YamlSNIConfig::Policy::DISABLED; // whether to verify the next hop
  YamlSNIConfig::Property verifyServerProperties = YamlSNIConfig::Property::NONE;   // what to verify on the next hop
  SSL_CTX *ctx                                   = nullptr; // ctx generated off the certificate to present to this server
  NextHopProperty();
};

using actionVector         = std::vector<ActionItem *>;
using SNIMap               = std::unordered_map<std::string, actionVector *>;
using NextHopPropertyTable = std::unordered_map<std::string, NextHopProperty *>;

struct SNIConfigParams : public ConfigInfo {
  char *sni_filename = nullptr;
  SNIMap sni_action_map;
  SNIMap wild_sni_action_map;
  NextHopPropertyTable next_hop_table;
  NextHopPropertyTable wild_next_hop_table;
  YamlSNIConfig Y_sni;
  NextHopProperty *getPropertyConfig(cchar *servername) const;
  SNIConfigParams();
  ~SNIConfigParams() override;
  void cleanup();
  int Initialize();
  void loadSNIConfig();
  actionVector *get(cchar *servername) const;
  void printSNImap() const;
};

struct SNIConfig {
  static void startup();
  static void reconfigure();
  static SNIConfigParams *acquire();
  static void release(SNIConfigParams *params);
  static void cloneProtoSet(); // clones the protoset of all the netaccept objects created and unregisters h2

  typedef ConfigProcessor::scoped_config<SNIConfig, SNIConfigParams> scoped_config;

private:
  static int configid;
};
