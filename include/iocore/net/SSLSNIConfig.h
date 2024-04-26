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
  SSLSNIConfig.h
   Created On      : 05/02/2017

   Description:
   SNI based Configuration in ATS
 ****************************************************************************/
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <string_view>
#include <strings.h>
#include <memory>

#if __has_include("pcre/pcre.h")
#include <pcre/pcre.h>
#elif __has_include("pcre.h")
#include <pcre.h>
#else
#error "Unable to locate PCRE heeader"
#endif

#include "tsutil/ts_ip.h"

#include "iocore/eventsystem/ConfigProcessor.h"
#include "iocore/net/SNIActionItem.h"
#include "iocore/net/YamlSNIConfig.h"

#include <functional>

// Properties for the next hop server
struct NextHopProperty {
  std::string             client_cert_file;                                          // full path to client cert file for lookup
  std::string             client_key_file;                                           // full path to client key file for lookup
  YamlSNIConfig::Policy   verify_server_policy     = YamlSNIConfig::Policy::UNSET;   // whether to verify the next hop
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

  NamedElement(NamedElement const &other)            = delete;
  NamedElement &operator=(NamedElement const &other) = delete;
  NamedElement(NamedElement &&other);
  NamedElement &operator=(NamedElement &&other);
  ~NamedElement() = default;

  void set_glob_name(std::string name);
  void set_regex_name(const std::string &regex_name);

  std::vector<ts::port_range_t> inbound_port_ranges;

  std::unique_ptr<pcre, PcreFreer> match;

  uint32_t rank = 0; ///< order of the config. smaller is higher.
};

struct ActionElement : public NamedElement {
  ActionVector actions;
};

struct NextHopItem : public NamedElement {
  NextHopProperty prop;
};

class SNIConfigParams : public ConfigInfo
{
public:
  SNIConfigParams() = default;
  ~SNIConfigParams() override;

  const NextHopProperty *get_property_config(const std::string &servername) const;
  bool                   initialize();
  bool                   initialize(const std::string &sni_filename);
  /** Walk sni.yaml config and populate sni_action_list
      @return 0 for success, 1 is failure
   */
  bool                                                 load_sni_config();
  std::pair<const ActionVector *, ActionItem::Context> get(std::string_view servername, uint16_t dest_incoming_port) const;

  std::unordered_multimap<std::string, ActionElement> sni_action_map;  ///< for exact fqdn matching
  std::vector<ActionElement>                          sni_action_list; ///< for regex fqdn matching
  std::vector<NextHopItem>                            next_hop_list;
  YamlSNIConfig                                       yaml_sni;

private:
  bool set_next_hop_properties(YamlSNIConfig::Item const &item);
  bool load_certs_if_client_cert_specified(YamlSNIConfig::Item const &item, NextHopItem &nps);
};

class SNIConfig
{
public:
  using scoped_config = ConfigProcessor::scoped_config<SNIConfig, SNIConfigParams>;

  static void startup();
  /** Loads sni.yaml and swap into place if successful
      @return 0 for success, 1 is failure
   */
  static int              reconfigure();
  static SNIConfigParams *acquire();
  static void             release(SNIConfigParams *params);

  /**
   * Sets a callback to be invoked when the SNIConfig is reconfigured.
   *
   * This is used to reconfigure the pre-warm manager on SNI reload.
   */
  static void set_on_reconfigure_callback(std::function<void()> cb);

private:
  static int                   _configid;
  static std::function<void()> on_reconfigure;
};
