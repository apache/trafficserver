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
#include "tscore/I_Layout.h"
#include <sstream>
#include <pcre.h>

static ConfigUpdateHandler<SNIConfig> *sniConfigUpdate;
struct NetAccept;
std::unordered_map<int, SSLNextProtocolSet *> snpsMap;

const NextHopProperty *
SNIConfigParams::getPropertyConfig(const std::string &servername) const
{
  const NextHopProperty *nps = nullptr;
  for (auto &&item : next_hop_list) {
    if (pcre_exec(item.match, nullptr, servername.c_str(), servername.length(), 0, 0, nullptr, 0) >= 0) {
      // Found a match
      nps = &item.prop;
      break;
    }
  }
  return nps;
}

void
SNIConfigParams::loadSNIConfig()
{
  for (auto &item : Y_sni.items) {
    auto ai = sni_action_list.emplace(sni_action_list.end());
    ai->setGlobName(item.fqdn);
    Debug("ssl", "name: %s", item.fqdn.data());

    // set SNI based actions to be called in the ssl_servername_only callback
    if (item.disable_h2) {
      ai->actions.push_back(std::make_unique<DisableH2>());
    }
    if (item.verify_client_level != 255) {
      ai->actions.push_back(std::make_unique<VerifyClient>(item.verify_client_level));
    }
    if (!item.protocol_unset) {
      ai->actions.push_back(std::make_unique<TLSValidProtocols>(item.protocol_mask));
    }
    if (item.tunnel_destination.length() > 0) {
      ai->actions.push_back(std::make_unique<TunnelDestination>(item.tunnel_destination, item.tunnel_decrypt));
    }

    ai->actions.push_back(std::make_unique<SNI_IpAllow>(item.ip_allow, item.fqdn));

    // set the next hop properties
    SSLConfig::scoped_config params;
    auto clientCTX = params->getClientSSL_CTX();
    // Load if we have at least specified the client certificate
    if (!item.client_cert.empty()) {
      std::string certFilePath = Layout::get()->relative_to(params->clientCertPathOnly, item.client_cert.data());
      std::string keyFilePath;
      if (!item.client_key.empty()) {
        keyFilePath = Layout::get()->relative_to(params->clientKeyPathOnly, item.client_key.data());
      }
      clientCTX = params->getCTX(certFilePath.c_str(), keyFilePath.c_str(), params->clientCACertFilename, params->clientCACertPath);
    }

    auto nps = next_hop_list.emplace(next_hop_list.end());
    nps->setGlobName(item.fqdn);
    nps->prop.verifyServerPolicy     = item.verify_server_policy;
    nps->prop.verifyServerProperties = item.verify_server_properties;
    nps->prop.ctx                    = clientCTX;
  } // end for
}

int SNIConfig::configid = 0;
/*definition of member functions of SNIConfigParams*/
SNIConfigParams::SNIConfigParams() {}

const actionVector *
SNIConfigParams::get(const std::string &servername) const
{
  for (const auto &retval : sni_action_list) {
    if (retval.match == nullptr && servername.length() == 0) {
      return &retval.actions;
    } else if (pcre_exec(retval.match, nullptr, servername.c_str(), servername.length(), 0, 0, nullptr, 0) >= 0) {
      return &retval.actions;
    }
  }
  return nullptr;
}

int
SNIConfigParams::Initialize()
{
  sni_filename = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.servername.filename"));

  Note("ssl_server_name.yaml loading ...");

  struct stat sbuf;
  if (stat(sni_filename, &sbuf) == -1 && errno == ENOENT) {
    Note("ssl_server_name.yaml failed to load");
    Warning("Loading SNI configuration - filename: %s doesn't exist", sni_filename);
    return 1;
  }

  ts::Errata zret = Y_sni.loader(sni_filename);
  if (!zret.isOK()) {
    std::stringstream errMsg;
    errMsg << zret;
    Error("ssl_server_name.yaml failed to load: %s", errMsg.str().c_str());
    return 1;
  }

  loadSNIConfig();
  Note("ssl_server_name.yaml finished loading");

  return 0;
}

SNIConfigParams::~SNIConfigParams()
{
  // sni_action_list and next_hop_list should cleanup with the params object
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
