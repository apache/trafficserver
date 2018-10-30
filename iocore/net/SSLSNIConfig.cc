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
Map<int, SSLNextProtocolSet *> snpsMap;
extern TunnelHashMap TunnelMap;
NextHopProperty::NextHopProperty() {}

NextHopProperty *
SNIConfigParams::getPropertyConfig(cchar *servername) const
{
  NextHopProperty *nps = nullptr;
  nps                  = next_hop_table.get(servername);
  if (!nps) {
    nps = wild_next_hop_table.get(servername);
  }
  return nps;
}

void
SNIConfigParams::loadSNIConfig()
{
  for (const auto &item : Y_sni.items) {
    actionVector *aiVec = new actionVector();
    Debug("ssl", "name: %s", item.fqdn.data());
    cchar *servername = item.fqdn.data();
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
        wild_sni_action_map.put(ats_stringdup(domain), aiVec);
      }
    } else {
      sni_action_map.put(ats_strdup(servername), aiVec);
    }

    if (item.tunnel_destination.length()) {
      TunnelMap.emplace(item.fqdn, item.tunnel_destination);
    }

    auto ai3 = new SNI_IpAllow(item.ip_allow, servername);
    aiVec->push_back(ai3);
    // set the next hop properties
    SSLConfig::scoped_config params;
    auto clientCTX  = params->getClientSSL_CTX();
    cchar *certFile = item.client_cert.data();
    cchar *keyFile  = item.client_key.data();
    if (certFile) {
      clientCTX = params->getNewCTX(certFile, keyFile);
    }
    NextHopProperty *nps        = new NextHopProperty();
    nps->name                   = ats_strdup(servername);
    nps->verifyServerPolicy     = item.verify_server_policy;
    nps->verifyServerProperties = item.verify_server_properties;
    nps->ctx                    = clientCTX;
    if (wildcard) {
      wild_next_hop_table.put(nps->name, nps);
    } else {
      next_hop_table.put(nps->name, nps);
    }
  } // end for
}

int SNIConfig::configid = 0;
/*definition of member functions of SNIConfigParams*/
SNIConfigParams::SNIConfigParams() {}

actionVector *
SNIConfigParams::get(cchar *servername) const
{
  auto actionVec = sni_action_map.get(servername);
  if (!actionVec) {
    Vec<cchar *> keys;
    wild_sni_action_map.get_keys(keys);
    for (int i = 0; i < static_cast<int>(keys.length()); i++) {
      std::string_view sv{servername, strlen(servername)};
      std::string_view key_sv{keys.get(i)};
      if (sv.size() >= key_sv.size() && sv.substr(sv.size() - key_sv.size()) == key_sv) {
        return wild_sni_action_map.get(key_sv.data());
      }
    }
  }
  return actionVec;
}

void
SNIConfigParams::printSNImap() const
{
  Vec<cchar *> keys;
  sni_action_map.get_keys(keys);
  for (size_t i = 0; i < keys.length(); i++) {
    Debug("ssl", "Domain name in the map %s: # of registered action items %lu", (char *)keys.get(i),
          sni_action_map.get(keys.get(i))->size());
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
  Vec<cchar *> keys;
  sni_action_map.get_keys(keys);
  for (int i = keys.length() - 1; i >= 0; i--) {
    auto actionVec = sni_action_map.get(keys.get(i));
    for (auto &ai : *actionVec) {
      delete ai;
    }

    actionVec->clear();
  }
  keys.free_and_clear();

  wild_sni_action_map.get_keys(keys);
  for (int i = keys.length() - 1; i >= 0; i--) {
    auto actionVec = wild_sni_action_map.get(keys.get(i));
    for (auto &ai : *actionVec) {
      delete ai;
    }

    actionVec->clear();
  }
  keys.free_and_clear();

  next_hop_table.get_keys(keys);
  for (int i = 0; i < static_cast<int>(keys.length()); i++) {
    auto *nps = next_hop_table.get(keys.get(i));
    delete (nps);
  }
  keys.free_and_clear();

  wild_next_hop_table.get_keys(keys);
  for (int i = 0; static_cast<int>(keys.length()); i++) {
    auto *nps = wild_next_hop_table.get(keys.get(i));
    delete (nps);
  }
  keys.free_and_clear();
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
      snpsMap.put(na->id, snps);
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
