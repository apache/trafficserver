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

#include "P_SSLUtils.h"
#include "P_SSLConfig.h"
#include "iocore/net/SSLSNIConfig.h"
#include "iocore/net/SNIActionItem.h"
#include "tscore/Diags.h"
#include "tscore/Layout.h"
#include "tscore/TSSystemState.h"
#include "tsutil/ts_ip.h"
#include "tsutil/Convert.h"

#include <netinet/in.h>
#include <sstream>
#include <utility>
#include <pcre.h>
#include <algorithm>
#include <functional>
#include <utility>

namespace
{
constexpr int OVECSIZE{30};

DbgCtl dbg_ctl_ssl{"ssl"};
DbgCtl dbg_ctl_ssl_sni{"ssl_sni"};
DbgCtl dbg_ctl_sni{"sni"};

bool
is_port_in_the_ranges(const std::vector<ts::port_range_t> &port_ranges, in_port_t port)
{
  return std::any_of(port_ranges.begin(), port_ranges.end(),
                     [port](ts::port_range_t const &port_range) { return port_range.contains(port); });
}
} // namespace

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
    match               = std::move(other.match);
    inbound_port_ranges = std::move(other.inbound_port_ranges);
    rank                = other.rank;
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
  Dbg(dbg_ctl_ssl_sni, "Regexed fqdn=%s", name.c_str());
  set_regex_name(name);
}

void
NamedElement::set_regex_name(const std::string &regex_name)
{
  const char *err_ptr;
  int         err_offset = 0;
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

bool
SNIConfigParams::load_sni_config()
{
  uint32_t             count = 0;
  ats_wildcard_matcher wildcard;

  for (auto &item : yaml_sni.items) {
    Dbg(dbg_ctl_ssl, "name: %s", item.fqdn.data());

    ActionElement *element = nullptr;

    // servername is case-insensitive, store & find it in lower case
    char lower_case_name[TS_MAX_HOST_NAME_LEN + 1];
    ts::transform_lower(item.fqdn, lower_case_name);

    if (wildcard.match(lower_case_name)) {
      auto &ai = sni_action_list.emplace_back();
      ai.set_glob_name(lower_case_name);
      element = &ai;
    } else {
      auto it = sni_action_map.emplace(std::make_pair(lower_case_name, ActionElement()));
      if (it == sni_action_map.end()) {
        Error("error on loading sni yaml - fqdn=%s", item.fqdn.c_str());
        return false;
      }

      element = &it->second;
    }

    element->inbound_port_ranges = item.inbound_port_ranges;
    element->rank                = count++;

    item.populate_sni_actions(element->actions);
    if (!set_next_hop_properties(item)) {
      return false;
    }
  }

  return true;
}

bool
SNIConfigParams::set_next_hop_properties(YamlSNIConfig::Item const &item)
{
  auto &nps = next_hop_list.emplace_back();
  if (!load_certs_if_client_cert_specified(item, nps)) {
    return false;
  };

  nps.set_glob_name(item.fqdn);
  nps.prop.verify_server_policy     = item.verify_server_policy;
  nps.prop.verify_server_properties = item.verify_server_properties;

  return true;
}

bool
SNIConfigParams::load_certs_if_client_cert_specified(YamlSNIConfig::Item const &item, NextHopItem &nps)
{
  if (!item.client_cert.empty()) {
    SSLConfig::scoped_config params;
    nps.prop.client_cert_file = Layout::get()->relative_to(params->clientCertPathOnly, item.client_cert.data());
    if (!item.client_key.empty()) {
      nps.prop.client_key_file = Layout::get()->relative_to(params->clientKeyPathOnly, item.client_key.data());
    }

    auto ctx =
      params->getCTX(nps.prop.client_cert_file, nps.prop.client_key_file, params->clientCACertFilename, params->clientCACertPath);
    if (ctx.get() == nullptr) {
      return false;
    }
  }

  return true;
}

/**
  CAVEAT: the "fqdn" field in the sni.yaml accepts wildcards (*), but it has a negative performance impact.
  */
std::pair<const ActionVector *, ActionItem::Context>
SNIConfigParams::get(std::string_view servername, in_port_t dest_incoming_port) const
{
  const ActionElement *element = nullptr;

  // Check for exact matches
  char lower_case_name[TS_MAX_HOST_NAME_LEN + 1];
  ts::transform_lower(servername, lower_case_name);

  Dbg(dbg_ctl_sni, "lower_case_name=%s", lower_case_name);

  auto range = sni_action_map.equal_range(lower_case_name);
  for (auto it = range.first; it != range.second; ++it) {
    Dbg(dbg_ctl_sni, "match with %s", it->first.c_str());

    if (!is_port_in_the_ranges(it->second.inbound_port_ranges, dest_incoming_port)) {
      continue;
    }

    const ActionElement *candidate = &it->second;
    if (element == nullptr) {
      element = candidate;
    } else if (candidate->rank < element->rank) {
      element = &it->second;
    }
  }

  // Check for wildcard matches
  int ovector[OVECSIZE];

  for (auto const &retval : sni_action_list) {
    if (element != nullptr && element->rank < retval.rank) {
      break;
    }

    int length = servername.length();
    if (retval.match == nullptr && length == 0) {
      return {&retval.actions, {}};
    } else if (auto offset = pcre_exec(retval.match.get(), nullptr, servername.data(), length, 0, 0, ovector, OVECSIZE);
               offset >= 0) {
      if (!is_port_in_the_ranges(retval.inbound_port_ranges, dest_incoming_port)) {
        continue;
      }
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

  if (element != nullptr) {
    return {&element->actions, {}};
  } else {
    return {nullptr, {}};
  }
}

bool
SNIConfigParams::initialize()
{
  std::string sni_filename = RecConfigReadConfigPath("proxy.config.ssl.servername.filename");
  return initialize(sni_filename);
}

bool
SNIConfigParams::initialize(std::string const &sni_filename)
{
  Note("%s loading ...", sni_filename.c_str());

  struct stat sbuf;
  if (stat(sni_filename.c_str(), &sbuf) == -1 && errno == ENOENT) {
    Note("%s failed to load", sni_filename.c_str());
    Warning("Loading SNI configuration - filename: %s doesn't exist", sni_filename.c_str());

    return true;
  }

  YamlSNIConfig yaml_sni_tmp;
  auto          zret = yaml_sni_tmp.loader(sni_filename);
  if (!zret.is_ok()) {
    std::stringstream errMsg;
    errMsg << zret;
    if (TSSystemState::is_initializing()) {
      Emergency("%s failed to load: %s", sni_filename.c_str(), errMsg.str().c_str());
    } else {
      Error("%s failed to load: %s", sni_filename.c_str(), errMsg.str().c_str());
    }
    return false;
  }
  yaml_sni = std::move(yaml_sni_tmp);

  return load_sni_config();
}

SNIConfigParams::~SNIConfigParams()
{
  // sni_action_map, sni_action_list and next_hop_list should cleanup with the params object
}

////
// SNIConfig
//
int                   SNIConfig::_configid      = 0;
std::function<void()> SNIConfig::on_reconfigure = nullptr;

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
  Dbg(dbg_ctl_ssl, "Reload SNI file");
  SNIConfigParams *params = new SNIConfigParams;

  bool retStatus = params->initialize();
  if (retStatus) {
    _configid = configProcessor.set(_configid, params);
    if (SNIConfig::on_reconfigure) {
      SNIConfig::on_reconfigure();
    }
  } else {
    delete params;
  }

  std::string sni_filename = RecConfigReadConfigPath("proxy.config.ssl.servername.filename");
  if (retStatus || TSSystemState::is_initializing()) {
    Note("%s finished loading", sni_filename.c_str());
  } else {
    Error("%s failed to load", sni_filename.c_str());
  }

  return retStatus ? 1 : 0;
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

void
SNIConfig::set_on_reconfigure_callback(std::function<void()> cb)
{
  SNIConfig::on_reconfigure = std::move(cb);
}
