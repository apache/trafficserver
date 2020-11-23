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
#include "tscore/ink_memory.h"
#include "tscpp/util/TextView.h"
#include "tscore/I_Layout.h"
#include <sstream>
#include <pcre.h>

static constexpr int OVECSIZE{30};

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
    if (item.offer_h2.has_value()) {
      ai->actions.push_back(std::make_unique<ControlH2>(item.offer_h2.value()));
    }
    if (item.verify_client_level != 255) {
      ai->actions.push_back(
        std::make_unique<VerifyClient>(item.verify_client_level, item.verify_client_ca_file, item.verify_client_ca_dir));
    }
    if (item.host_sni_policy != 255) {
      ai->actions.push_back(std::make_unique<HostSniPolicy>(item.host_sni_policy));
    }
    if (!item.protocol_unset) {
      ai->actions.push_back(std::make_unique<TLSValidProtocols>(item.protocol_mask));
    }
    if (item.tunnel_destination.length() > 0) {
      ai->actions.push_back(std::make_unique<TunnelDestination>(item.tunnel_destination, item.tunnel_decrypt, item.tls_upstream));
    }

    ai->actions.push_back(std::make_unique<SNI_IpAllow>(item.ip_allow, item.fqdn));

    // set the next hop properties
    auto nps = next_hop_list.emplace(next_hop_list.end());

    SSLConfig::scoped_config params;
    // Load if we have at least specified the client certificate
    if (!item.client_cert.empty()) {
      nps->prop.client_cert_file = Layout::get()->relative_to(params->clientCertPathOnly, item.client_cert.data());
      if (!item.client_key.empty()) {
        nps->prop.client_key_file = Layout::get()->relative_to(params->clientKeyPathOnly, item.client_key.data());
      }

      params->getCTX(nps->prop.client_cert_file.c_str(),
                     nps->prop.client_key_file.empty() ? nullptr : nps->prop.client_key_file.c_str(), params->clientCACertFilename,
                     params->clientCACertPath);
    }

    nps->setGlobName(item.fqdn);
    nps->prop.verifyServerPolicy     = item.verify_server_policy;
    nps->prop.verifyServerProperties = item.verify_server_properties;
    nps->prop.tls_upstream           = item.tls_upstream;
  } // end for
}

int SNIConfig::configid = 0;
/*definition of member functions of SNIConfigParams*/
SNIConfigParams::SNIConfigParams() {}

std::pair<const actionVector *, ActionItem::Context>
SNIConfigParams::get(const std::string &servername) const
{
  int ovector[OVECSIZE];
  ActionItem::Context context;

  for (const auto &retval : sni_action_list) {
    int length = servername.length();
    if (retval.match == nullptr && length == 0) {
      return {&retval.actions, context};
    } else if (auto offset = pcre_exec(retval.match, nullptr, servername.c_str(), length, 0, 0, ovector, OVECSIZE); offset >= 0) {
      if (offset == 1) {
        // first pair identify the portion of the subject string matched by the entire pattern
        if (ovector[0] == 0 && ovector[1] == length) {
          // full match
          return {&retval.actions, context};
        } else {
          continue;
        }
      }
      // If contains groups
      if (offset == 0) {
        // reset to max if too many.
        offset = OVECSIZE / 3;
      }

      const char *psubStrMatchStr = nullptr;
      std::vector<std::string> groups;
      for (int strnum = 1; strnum < offset; strnum++) {
        pcre_get_substring(servername.c_str(), ovector, offset, strnum, &(psubStrMatchStr));
        groups.emplace_back(psubStrMatchStr);
      }
      context._fqdn_wildcard_captured_groups = std::move(groups);
      if (psubStrMatchStr) {
        pcre_free_substring(psubStrMatchStr);
      }

      return {&retval.actions, context};
    }
  }
  return {nullptr, context};
}

int
SNIConfigParams::Initialize()
{
  sni_filename = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.servername.filename"));

  Note("%s loading ...", sni_filename);

  struct stat sbuf;
  if (stat(sni_filename, &sbuf) == -1 && errno == ENOENT) {
    Note("%s failed to load", sni_filename);
    Warning("Loading SNI configuration - filename: %s doesn't exist", sni_filename);
    return 1;
  }

  ts::Errata zret = Y_sni.loader(sni_filename);
  if (!zret.isOK()) {
    std::stringstream errMsg;
    errMsg << zret;
    Error("%s failed to load: %s", sni_filename, errMsg.str().c_str());
    return 1;
  }

  loadSNIConfig();
  Note("%s finished loading", sni_filename);

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
  reconfigure();
}

void
SNIConfig::reconfigure()
{
  Debug("ssl", "Reload SNI file");
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

// See if any of the client-side actions would trigger for this combination of servername and
// client IP
// host_sni_policy is an in/out paramter.  It starts with the global policy from the records.config
// setting proxy.config.http.host_sni_policy and is possibly overridden if the sni policy
// contains a host_sni_policy entry
bool
SNIConfig::TestClientAction(const char *servername, const IpEndpoint &ep, int &host_sni_policy)
{
  bool retval = false;
  SNIConfig::scoped_config params;

  const auto &actions = params->get(servername);
  if (actions.first) {
    for (auto &&item : *actions.first) {
      retval |= item->TestClientSNIAction(servername, ep, host_sni_policy);
    }
  }
  return retval;
}
