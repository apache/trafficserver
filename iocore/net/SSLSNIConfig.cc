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
  SSLSNIConfig.cc
   Created On      : 05/02/2017

   Description:
   SNI based Configuration in ATS
 ****************************************************************************/

#include "P_SSLSNI.h"
#include "tscore/Diags.h"
#include "tscore/SimpleTokenizer.h"
#include "P_SSLConfig.h"
#include "tscore/ink_memory.h"
#include "tscpp/util/TextView.h"

static ConfigUpdateHandler<SNIConfig> *sniConfigUpdate;
struct NetAccept;
std::unordered_map<int, SSLNextProtocolSet *> snpsMap;
extern TunnelHashMap TunnelMap;
NextHopProperty::NextHopProperty() {}

NextHopProperty *
SNIConfigParams::getPropertyConfig(const char *servername) const
{
  if (auto it = next_hop_table.find(servername); it != next_hop_table.end()) {
    return it->second;
  }
  if (auto it = wild_next_hop_table.find(servername); it != wild_next_hop_table.end()) {
    return it->second;
  }
  return nullptr;
}

void
SNIConfigParams::loadSNIConfig()
{
  for (const auto &item : Y_sni.items) {
    actionVector *aiVec = new actionVector();
    Debug("ssl", "name: %s", item.fqdn.data());
    const char *servername = item.fqdn.data();
    ats_wildcard_matcher w_Matcher;
    auto wildcard = w_Matcher.match(servername);

    // set SNI based actions to be called in the ssl_servername_only callback
    auto ai1 = new DisableH2();
    aiVec->push_back(ai1);
    auto ai2 = new VerifyClient(item.verify_client_level);
    aiVec->push_back(ai2);
    if (wildcard) {
      ts::TextView domain{servername, strlen(servername)};
      domain.take_prefix_at('.');
      if (!domain.empty()) {
        wild_sni_action_map.emplace(domain, aiVec);
      }
    } else {
      sni_action_map.emplace(servername, aiVec);
    }

    if (item.tunnel_destination.length()) {
      TunnelMap.emplace(item.fqdn, item.tunnel_destination);
    }

    auto ai3 = new SNI_IpAllow(item.ip_allow, servername);
    aiVec->push_back(ai3);
    // set the next hop properties
    SSLConfig::scoped_config params;
    auto clientCTX       = params->getClientSSL_CTX();
    const char *certFile = item.client_cert.data();
    const char *keyFile  = item.client_key.data();
    if (certFile) {
      clientCTX = params->getNewCTX(certFile, keyFile);
    }
    if (servername) { // a safety check
      NextHopProperty *nps = new NextHopProperty();

      nps->name                   = ats_strdup(servername);
      nps->verifyServerPolicy     = item.verify_server_policy;
      nps->verifyServerProperties = item.verify_server_properties;
      nps->ctx                    = clientCTX;
      if (wildcard) {
        wild_next_hop_table.emplace(nps->name, nps);
      } else {
        next_hop_table.emplace(nps->name, nps);
      }
    }
  } // end for
}

int SNIConfig::configid = 0;
/*definition of member functions of SNIConfigParams*/
SNIConfigParams::SNIConfigParams() {}

actionVector *
SNIConfigParams::get(const char *servername) const
{
  auto action_it = sni_action_map.find(servername);
  if (action_it != sni_action_map.end()) {
    for (const auto &it : wild_sni_action_map) {
      std::string_view sv{servername};
      std::string_view key_sv{it.first};
      if (sv.size() >= key_sv.size() && sv.substr(sv.size() - key_sv.size()) == key_sv) {
        auto wild_action_it = wild_sni_action_map.find(key_sv.data());
        return wild_action_it != wild_sni_action_map.end() ? wild_action_it->second : nullptr;
      }
    }
  }
  return action_it != sni_action_map.end() ? action_it->second : nullptr;
}

void
SNIConfigParams::printSNImap() const
{
  for (const auto &it : sni_action_map) {
    Debug("ssl", "Domain name in the map %s: # of registered action items %lu", it.first.c_str(), it.second->size());
  }
}

int
SNIConfigParams::Initialize()
{
  sni_filename = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.servername.filename"));

  Note("loading %s", sni_filename);

  struct stat sbuf;
  if (stat(sni_filename, &sbuf) == -1 && errno == ENOENT) {
    Note("failed to reload ssl_server_name.yaml");
    Warning("Loading SNI configuration - filename: %s doesn't exist", sni_filename);
    return 1;
  }

  ts::Errata zret = Y_sni.loader(sni_filename);
  if (!zret.isOK()) {
    std::stringstream errMsg;
    errMsg << zret;
    Error("failed to load ssl_server_name.yaml: %s", errMsg.str().c_str());
    return 1;
  }

  loadSNIConfig();
  Note("ssl_server_name.yaml done reloading!");

  return 0;
}

void
SNIConfigParams::cleanup()
{
  for (const auto &it : sni_action_map) {
    auto actionVec = it.second;
    for (const auto &ai : *actionVec) {
      delete ai;
    }
    delete actionVec;
  }
  for (const auto &it : wild_sni_action_map) {
    auto actionVec = it.second;
    for (const auto &ai : *actionVec) {
      delete ai;
    }
    delete actionVec;
  }
  for (const auto &it : next_hop_table) {
    delete it.second;
  }
  for (const auto &it : wild_next_hop_table) {
    delete it.second;
  }
}

SNIConfigParams::~SNIConfigParams()
{
  cleanup();
}

/*definition of member functions of SNIConfig*/
void
SNIConfig::startup()
{
  sniConfigUpdate = new ConfigUpdateHandler<SNIConfig>();
  sniConfigUpdate->attach("proxy.config.ssl.servername.filename");
  reconfigure();
}

void
SNIConfig::cloneProtoSet()
{
  SCOPED_MUTEX_LOCK(lock, naVecMutex, this_ethread());
  for (auto na : naVec) {
    if (na->snpa) {
      auto snps = na->snpa->cloneProtoSet();
      snps->unregisterEndpoint(TS_ALPN_PROTOCOL_HTTP_2_0, nullptr);
      snpsMap.emplace(na->id, snps);
    }
  }
}

void
SNIConfig::reconfigure()
{
  SNIConfigParams *params = new SNIConfigParams;

  params->Initialize();
  configid = configProcessor.set(configid, params);
}

SNIConfigParams *
SNIConfig::acquire()
{
  return (SNIConfigParams *)configProcessor.get(configid);
}

void
SNIConfig::release(SNIConfigParams *params)
{
  configProcessor.release(configid, params);
}
