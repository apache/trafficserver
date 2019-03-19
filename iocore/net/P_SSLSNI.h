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
  std::string name;                                                                // name of the server
  YamlSNIConfig::Policy verifyServerPolicy       = YamlSNIConfig::Policy::UNSET;   // whether to verify the next hop
  YamlSNIConfig::Property verifyServerProperties = YamlSNIConfig::Property::UNSET; // what to verify on the next hop
  SSL_CTX *ctx                                   = nullptr; // ctx generated off the certificate to present to this server
  NextHopProperty() {}
};

using actionVector = std::vector<std::unique_ptr<ActionItem>>;

struct namedElement {
public:
  namedElement() {}

  void
  setGlobName(std::string name)
  {
    std::string::size_type pos = 0;
    while ((pos = name.find('.', pos)) != std::string::npos) {
      name.replace(pos, 1, "\\.");
      pos += 2;
    }
    pos = 0;
    while ((pos = name.find('*', pos)) != std::string::npos) {
      name.replace(pos, 1, ".{0,}");
    }
    Debug("ssl_sni", "Regexed fqdn=%s", name.c_str());
    setRegexName(name);
  }

  void
  setRegexName(const std::string &regexName)
  {
    const char *err_ptr;
    int err_offset = 0;
    if (!regexName.empty()) {
      match = pcre_compile(regexName.c_str(), 0, &err_ptr, &err_offset, nullptr);
    } else {
      match = nullptr;
    }
  }

  pcre *match = nullptr;
};

struct actionElement : public namedElement {
public:
  actionVector actions;
};

struct NextHopItem : public namedElement {
public:
  NextHopProperty prop;
};

// typedef HashMap<cchar *, StringHashFns, actionVector *> SNIMap;
typedef std::vector<actionElement> SNIList;
// typedef HashMap<cchar *, StringHashFns, NextHopProperty *> NextHopPropertyTable;
typedef std::vector<NextHopItem> NextHopPropertyList;

struct SNIConfigParams : public ConfigInfo {
  char *sni_filename = nullptr;
  SNIList sni_action_list;
  NextHopPropertyList next_hop_list;
  YamlSNIConfig Y_sni;
  const NextHopProperty *getPropertyConfig(const std::string &servername) const;
  SNIConfigParams();
  ~SNIConfigParams() override;
  void cleanup();
  int Initialize();
  void loadSNIConfig();
  const actionVector *get(const std::string &servername) const;
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
