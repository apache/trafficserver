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

#include <vector>
#include <string_view>
#include <strings.h>
#include <memory>

#include "ProxyConfig.h"
#include "P_SNIActionPerformer.h"
#include "YamlSNIConfig.h"

// Properties for the next hop server
struct NextHopProperty {
  std::string client_cert_file;                                                      // full path to client cert file for lookup
  std::string client_key_file;                                                       // full path to client key file for lookup
  YamlSNIConfig::Policy verify_server_policy       = YamlSNIConfig::Policy::UNSET;   // whether to verify the next hop
  YamlSNIConfig::Property verify_server_properties = YamlSNIConfig::Property::UNSET; // what to verify on the next hop
};

using ActionVector = std::vector<std::unique_ptr<ActionItem>>;

struct PcreFreer {
  void
  operator()(void *p)
  {
    pcre_free(p);
  }
};

struct NamedElement {
  NamedElement() {}

  NamedElement(NamedElement &&other);
  NamedElement &operator=(NamedElement &&other);

  void set_glob_name(std::string name);
  void set_regex_name(const std::string &regex_name);

  std::unique_ptr<pcre, PcreFreer> match;
};

struct ActionElement : public NamedElement {
  ActionVector actions;
};

struct NextHopItem : public NamedElement {
  NextHopProperty prop;
};

using SNIList             = std::vector<ActionElement>;
using NextHopPropertyList = std::vector<NextHopItem>;

struct SNIConfigParams : public ConfigInfo {
  SNIConfigParams() = default;
  ~SNIConfigParams() override;

  const NextHopProperty *get_property_config(const std::string &servername) const;
  int initialize();
  /** Walk sni.yaml config and populate sni_action_list
      @return 0 for success, 1 is failure
   */
  int load_sni_config();
  std::pair<const ActionVector *, ActionItem::Context> get(std::string_view servername) const;

  SNIList sni_action_list;
  NextHopPropertyList next_hop_list;
  YamlSNIConfig yaml_sni;
};

class SNIConfig
{
public:
  using scoped_config = ConfigProcessor::scoped_config<SNIConfig, SNIConfigParams>;

  static void startup();
  /** Loads sni.yaml and swap into place if successful
      @return 0 for success, 1 is failure
   */
  static int reconfigure();
  static SNIConfigParams *acquire();
  static void release(SNIConfigParams *params);

  static bool test_client_action(const char *servername, const IpEndpoint &ep, int &enforcement_policy);

private:
  static int _configid;
};
