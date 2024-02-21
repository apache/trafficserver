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

#include "PreWarmManager.h"

#include "tscore/Diags.h"
#include "tscore/SimpleTokenizer.h"
#include "tscore/ink_memory.h"
#include "tscore/I_Layout.h"

#include "tscpp/util/TextView.h"

#include <sstream>
#include <utility>
#include <pcre.h>

static constexpr int OVECSIZE{30};

////
// NamedElement
//
NamedElement::NamedElement(NamedElement &&other)
{
  *this = std::move(other);
}

NamedElement &
NamedElement::operator=(NamedElement &&other)
{
  if (this != &other) {
    match = std::move(other.match);
  }
  return *this;
}

void
NamedElement::set_glob_name(std::string name)
{
  std::string::size_type pos = 0;
  while ((pos = name.find('.', pos)) != std::string::npos) {
    name.replace(pos, 1, "\\.");
    pos += 2;
  }
  pos = 0;
  while ((pos = name.find('*', pos)) != std::string::npos) {
    name.replace(pos, 1, "(.{0,})");
  }
  Debug("ssl_sni", "Regexed fqdn=%s", name.c_str());
  set_regex_name(name);
}

void
NamedElement::set_regex_name(const std::string &regex_name)
{
  const char *err_ptr;
  int err_offset = 0;
  if (!regex_name.empty()) {
    match.reset(pcre_compile(regex_name.c_str(), PCRE_ANCHORED | PCRE_CASELESS, &err_ptr, &err_offset, nullptr));
  }
}

////
// SNIConfigParams
//
const NextHopProperty *
SNIConfigParams::get_property_config(const std::string &servername) const
{
  const NextHopProperty *nps = nullptr;
  for (auto &&item : next_hop_list) {
    if (pcre_exec(item.match.get(), nullptr, servername.c_str(), servername.length(), 0, 0, nullptr, 0) >= 0) {
      // Found a match
      nps = &item.prop;
      break;
    }
  }
  return nps;
}

int
SNIConfigParams::load_sni_config()
{
  for (auto &item : yaml_sni.items) {
    auto ai = sni_action_list.emplace(sni_action_list.end());
    ai->set_glob_name(item.fqdn);
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
      ai->actions.push_back(
        std::make_unique<TunnelDestination>(item.tunnel_destination, item.tunnel_type, item.tunnel_prewarm, item.tunnel_alpn));
    }
    if (!item.client_sni_policy.empty()) {
      ai->actions.push_back(std::make_unique<OutboundSNIPolicy>(item.client_sni_policy));
    }
    if (item.http2_buffer_water_mark.has_value()) {
      ai->actions.push_back(std::make_unique<HTTP2BufferWaterMark>(item.http2_buffer_water_mark.value()));
    }
    if (item.http2_max_settings_frames_per_minute.has_value()) {
      ai->actions.push_back(std::make_unique<HTTP2MaxSettingsFramesPerMinute>(item.http2_max_settings_frames_per_minute.value()));
    }
    if (item.http2_max_ping_frames_per_minute.has_value()) {
      ai->actions.push_back(std::make_unique<HTTP2MaxPingFramesPerMinute>(item.http2_max_ping_frames_per_minute.value()));
    }
    if (item.http2_max_priority_frames_per_minute.has_value()) {
      ai->actions.push_back(std::make_unique<HTTP2MaxPriorityFramesPerMinute>(item.http2_max_priority_frames_per_minute.value()));
    }
    if (item.http2_max_rst_stream_frames_per_minute.has_value()) {
      ai->actions.push_back(
        std::make_unique<HTTP2MaxRstStreamFramesPerMinute>(item.http2_max_rst_stream_frames_per_minute.value()));
    }
    if (item.http2_max_continuation_frames_per_minute.has_value()) {
      ai->actions.push_back(
        std::make_unique<HTTP2MaxContinuationFramesPerMinute>(item.http2_max_continuation_frames_per_minute.value()));
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

      auto ctx = params->getCTX(nps->prop.client_cert_file, nps->prop.client_key_file, params->clientCACertFilename,
                                params->clientCACertPath);
      if (ctx.get() == nullptr) {
        return 1;
      }
    }

    nps->set_glob_name(item.fqdn);
    nps->prop.verify_server_policy     = item.verify_server_policy;
    nps->prop.verify_server_properties = item.verify_server_properties;
  } // end for

  return 0;
}

std::pair<const ActionVector *, ActionItem::Context>
SNIConfigParams::get(std::string_view servername) const
{
  int ovector[OVECSIZE];

  for (const auto &retval : sni_action_list) {
    int length = servername.length();
    if (retval.match == nullptr && length == 0) {
      return {&retval.actions, {}};
    } else if (auto offset = pcre_exec(retval.match.get(), nullptr, servername.data(), length, 0, 0, ovector, OVECSIZE);
               offset >= 0) {
      if (offset == 1) {
        // first pair identify the portion of the subject string matched by the entire pattern
        if (ovector[0] == 0 && ovector[1] == length) {
          // full match
          return {&retval.actions, {}};
        } else {
          continue;
        }
      }
      // If contains groups
      if (offset == 0) {
        // reset to max if too many.
        offset = OVECSIZE / 3;
      }

      ActionItem::Context::CapturedGroupViewVec groups;
      groups.reserve(offset);
      for (int strnum = 1; strnum < offset; strnum++) {
        const std::size_t start  = ovector[2 * strnum];
        const std::size_t length = ovector[2 * strnum + 1] - start;

        groups.emplace_back(servername.data() + start, length);
      }
      return {&retval.actions, {std::move(groups)}};
    }
  }
  return {nullptr, {}};
}

int
SNIConfigParams::initialize()
{
  std::string sni_filename = RecConfigReadConfigPath("proxy.config.ssl.servername.filename");

  Note("%s loading ...", sni_filename.c_str());

  struct stat sbuf;
  if (stat(sni_filename.c_str(), &sbuf) == -1 && errno == ENOENT) {
    Note("%s failed to load", sni_filename.c_str());
    Warning("Loading SNI configuration - filename: %s doesn't exist", sni_filename.c_str());

    return 0;
  }

  YamlSNIConfig yaml_sni_tmp;
  ts::Errata zret = yaml_sni_tmp.loader(sni_filename);
  if (!zret.isOK()) {
    std::stringstream errMsg;
    errMsg << zret;
    if (TSSystemState::is_initializing()) {
      Emergency("%s failed to load: %s", sni_filename.c_str(), errMsg.str().c_str());
    } else {
      Error("%s failed to load: %s", sni_filename.c_str(), errMsg.str().c_str());
    }
    return 1;
  }
  yaml_sni = std::move(yaml_sni_tmp);

  return load_sni_config();
}

SNIConfigParams::~SNIConfigParams()
{
  // sni_action_list and next_hop_list should cleanup with the params object
}

////
// SNIConfig
//
int SNIConfig::_configid = 0;

void
SNIConfig::startup()
{
  if (!reconfigure()) {
    std::string sni_filename = RecConfigReadConfigPath("proxy.config.ssl.servername.filename");
    Fatal("failed to load %s", sni_filename.c_str());
  }
}

int
SNIConfig::reconfigure()
{
  Debug("ssl", "Reload SNI file");
  SNIConfigParams *params = new SNIConfigParams;

  int retStatus = params->initialize();
  if (!retStatus) {
    _configid = configProcessor.set(_configid, params);
    prewarmManager.reconfigure();
  } else {
    delete params;
  }

  std::string sni_filename = RecConfigReadConfigPath("proxy.config.ssl.servername.filename");
  if (!retStatus || TSSystemState::is_initializing()) {
    Note("%s finished loading", sni_filename.c_str());
  } else {
    Error("%s failed to load", sni_filename.c_str());
  }

  return !retStatus;
}

SNIConfigParams *
SNIConfig::acquire()
{
  return static_cast<SNIConfigParams *>(configProcessor.get(_configid));
}

void
SNIConfig::release(SNIConfigParams *params)
{
  configProcessor.release(_configid, params);
}

// See if any of the client-side actions would trigger for this combination of servername and client IP
// host_sni_policy is an in/out parameter.  It starts with the global policy from the records.config
// setting proxy.config.http.host_sni_policy and is possibly overridden if the sni policy
// contains a host_sni_policy entry
bool
SNIConfig::test_client_action(const char *servername, const IpEndpoint &ep, int &host_sni_policy)
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
