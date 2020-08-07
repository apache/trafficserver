/** @file

  Implementation of Parent Proxy routing

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

#include "SocksServer.h"

#include "ParentSelection.h"
#include "tscore/Filenames.h"
#include "tscore/ts_file.h"

#include <yaml-cpp/yaml.h>

static constexpr auto YAML_TAG_ROOT("socks");
static constexpr auto YAML_TAG_DESTINATIONS("destinations");

static constexpr auto modulePrefix      = "[Socks Server Selection]";
static constexpr auto filenameConfigVar = "proxy.config.socks.socks_config_file";

int SocksServerConfig::m_id = 0;

static Ptr<ProxyMutex> socks_server_reconfig_mutex;
void
SocksServerConfig::startup()
{
  socks_server_reconfig_mutex = new_ProxyMutex();

  // Load the initial configuration
  reconfigure();

  /* Handle update functions later. Socks does not yet support config update */
}

static int
setup_socks_servers(ParentRecord *rec_arr, int len)
{
  /* This changes hostnames into ip addresses and sets go_direct to false */
  for (int j = 0; j < len; j++) {
    rec_arr[j].go_direct = false;

    pRecord *pr   = rec_arr[j].parents;
    int n_parents = rec_arr[j].num_parents;

    for (int i = 0; i < n_parents; i++) {
      IpEndpoint ip4, ip6;
      if (0 == ats_ip_getbestaddrinfo(pr[i].hostname, &ip4, &ip6)) {
        IpEndpoint *ip = ats_is_ip6(&ip6) ? &ip6 : &ip4;
        ats_ip_ntop(ip, pr[i].hostname, MAXDNAME + 1);
      } else {
        Warning("Could not resolve socks server name \"%s\". "
                "Please correct it",
                pr[i].hostname);
        snprintf(pr[i].hostname, MAXDNAME + 1, "255.255.255.255");
      }
    }
  }

  return 0;
}

static P_table *
buildTable(const std::string &contents)
{
  Note("%s as YAML ...", ts::filename::SOCKS);
  YAML::Node config{YAML::Load(contents)};

  if (config.IsNull()) {
    Warning("malformed %s file; config is empty?", ts::filename::SOCKS);

    return nullptr;
  }

  if (!config.IsMap()) {
    Error("malformed %s file; expected a map", ts::filename::SOCKS);
    return nullptr;
  }

  if (!config[YAML_TAG_ROOT]) {
    Error("malformed %s file; expected a toplevel '%s' node", ts::filename::SOCKS, std::string(YAML_TAG_ROOT).c_str());
    return nullptr;
  }

  config = config[YAML_TAG_ROOT];

  if (!config[YAML_TAG_DESTINATIONS]) {
    Error("malformed %s file; expected '%s' node", ts::filename::SOCKS, std::string(YAML_TAG_DESTINATIONS).c_str());
    return nullptr;
  }

  config = config[YAML_TAG_DESTINATIONS];

  if (!config.IsSequence()) {
    Error("malformed %s file; expected a toplevel sequence/array", ts::filename::SOCKS);
    return nullptr;
  }

  return new P_table(filenameConfigVar, modulePrefix, config);
}

void
SocksServerConfig::reconfigure()
{
  Note("%s loading ...", ts::filename::SOCKS);

  char *default_val = nullptr;
  int retry_time    = 30;
  int fail_threshold;

  ats_scoped_str path(RecConfigReadConfigPath(filenameConfigVar, ts::filename::SOCKS));

  ParentConfigParams *params = nullptr;

  ts::file::path config_file{path};

  std::error_code ec;
  std::string content{ts::file::load(config_file, ec)};

  P_table *pTable = nullptr;
  if (ec.value() == 0) {
    // If it's a .yaml, treat as YAML
    if (ts::TextView{config_file.view()}.take_suffix_at('.') == "yaml") {
      pTable = buildTable(content);
    } else {
      pTable = new P_table(filenameConfigVar, modulePrefix, &socks_server_tags);
    }
  }

  params = new ParentConfigParams(pTable);
  ink_assert(params != nullptr);

  // Handle default parent
  REC_ReadConfigStringAlloc(default_val, "proxy.config.socks.default_servers");
  params->DefaultParent = createDefaultParent(default_val);
  ats_free(default_val);

  if (params->DefaultParent) {
    setup_socks_servers(params->DefaultParent, 1);
  }
  if (params->parent_table->ipMatch) {
    setup_socks_servers(params->parent_table->ipMatch->data_array, params->parent_table->ipMatch->array_len);
  }

  // Handle parent timeout
  REC_ReadConfigInteger(retry_time, "proxy.config.socks.server_retry_time");
  params->policy.ParentRetryTime = retry_time;

  // Handle the fail threshold
  REC_ReadConfigInteger(fail_threshold, "proxy.config.socks.server_fail_threshold");
  params->policy.FailThreshold = fail_threshold;

  m_id = configProcessor.set(m_id, params);

  if (is_debug_tag_set("Socks")) {
    SocksServerConfig::print();
  }

  Note("%s finished loading", ts::filename::SOCKS);
}

void
SocksServerConfig::print()
{
  ParentConfigParams *params = SocksServerConfig::acquire();

  printf("Parent Selection Config for Socks Server\n");
  printf("\tRetryTime %d\n", params->policy.ParentRetryTime);
  if (params->DefaultParent == nullptr) {
    printf("\tNo Default Parent\n");
  } else {
    printf("\tDefault Parent:\n");
    params->DefaultParent->Print();
  }
  printf("  ");
  params->parent_table->Print();

  SocksServerConfig::release(params);
}

ParentConfigParams *
SocksServerConfig::acquire()
{
  return static_cast<ParentConfigParams *>(configProcessor.get(SocksServerConfig::m_id));
}

void
SocksServerConfig::release(ParentConfigParams *params)
{
  configProcessor.release(SocksServerConfig::m_id, params);
}
