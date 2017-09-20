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
#include "ts/Diags.h"
#include "ts/SimpleTokenizer.h"
#include "P_SSLConfig.h"
#include "ts/ink_memory.h"
#define SNI_NAME_TAG "dest_host"
#define SNI_ACTION_TAG "action"
#define SNI_PARAM_TAG "param"

static ConfigUpdateHandler<SNIConfig> *sniConfigUpdate;
struct NetAccept;
extern std::vector<NetAccept *> naVec;
Map<int, SSLNextProtocolSet *> snpsMap;

NextHopProperty::NextHopProperty()
{
}
int
SNIConfigParams::getEnum(char *actionTag)
{
  if (0 == strncasecmp(actionTag, "TS_DISABLE_H2", sizeof("TS_DISABLE_H2")))
    return TS_DISABLE_H2;
  if (0 == strncasecmp(actionTag, "TS_VERIFY_CLIENT", sizeof("TS_VERIFY_CLIENT")))
    return TS_VERIFY_CLIENT;
  if (0 == strncasecmp(actionTag, "TS_TUNNEL_ROUTE", sizeof("TS_TUNNEL_ROUTE")))
    return TS_TUNNEL_ROUTE;
  if (0 == strncasecmp(actionTag, "TS_VERIFY_SERVER", sizeof("TS_VERIFY_SERVER")))
    return TS_VERIFY_SERVER;
  if (0 == strncasecmp(actionTag, "TS_CLIENT_CERT", sizeof("TS_CLIENT_CERT")))
    return TS_CLIENT_CERT;
  Warning("%s is not a valid action", actionTag);
  return -1;
}

void
SNIConfigParams::setPropertyConfig(PropertyActions id, char *servername, void *param)
{
  ats_wildcard_matcher w_Matcher;
  auto wildcard = w_Matcher.match(servername);
  auto property = next_hop_table.get(servername);
  if (!property)
    property = wild_next_hop_table.get(servername);
  if (id == TS_VERIFY_SERVER) {
    if (property) {
      property->verifyLevel = atoi(reinterpret_cast<char *>(param));
    } else {
      NextHopProperty *nps = new NextHopProperty();
      nps->name            = ats_strdup(servername);
      nps->verifyLevel     = atoi(reinterpret_cast<char *>(param));
      nps->ctx             = nullptr;
      if (wildcard)
        wild_next_hop_table.put(nps->name, nps);
      else
        next_hop_table.put(nps->name, nps);
    }
  } else if (id == TS_CLIENT_CERT) {
    // construct the absolute path
    char *certFile = reinterpret_cast<char *>(param);
    SSLConfig::scoped_config params;
    auto clientCTX = params->getCTX(servername);
    if (!clientCTX) {
      clientCTX = params->getNewCTX(certFile);
      params->InsertCTX(certFile, clientCTX);
    }
    if (property) {
      property->ctx = clientCTX;
    } else {
      NextHopProperty *nps = new NextHopProperty();
      nps->name            = ats_strdup(servername);
      nps->verifyLevel     = 0;
      nps->ctx             = clientCTX;
      if (wildcard)
        wild_next_hop_table.put(nps->name, nps);
      else
        next_hop_table.put(nps->name, nps);
    }
  }
}

NextHopProperty *
SNIConfigParams::getPropertyConfig(cchar *servername) const
{
  NextHopProperty *nps = nullptr;
  nps                  = next_hop_table.get(servername);
  if (!nps)
    nps = wild_next_hop_table.get(servername);
  return nps;
}

char *
SNIConfigParams::ParseSNIConfigLine(matcher_line const &line_info, ActionItem *&ai)
{
  char *sniname = nullptr;
  char *action  = nullptr;
  char *param   = nullptr;
  for (int i = 0; i < line_info.num_el; ++i) {
    char *label;
    char *value;

    label = line_info.line[0][i];
    value = line_info.line[1][i];
    if (0 == strncasecmp(label, SNI_NAME_TAG, sizeof(SNI_NAME_TAG))) {
      sniname = value;
    } else if (0 == strncasecmp(label, SNI_ACTION_TAG, sizeof(SNI_ACTION_TAG))) {
      action = value;
    } else if (0 == strncasecmp(label, SNI_PARAM_TAG, sizeof(SNI_PARAM_TAG))) {
      param = value;
    }
  }
  if (sniname && action) {
    int id = getEnum(action);
    switch (id) {
    case TS_DISABLE_H2:
      ai = new DisableH2();
      break;
    case TS_VERIFY_CLIENT:
      ai = new VerifyClient(param);
      break;
    default:;
    }

    if (id >= TS_VERIFY_SERVER && id <= TS_CLIENT_CERT && param) {
      setPropertyConfig(static_cast<PropertyActions>(id), sniname, param);
    }
    return sniname;
  }

  return nullptr;
}

int SNIConfig::configid = 0;
/*definition of member functions of SNIConfigParams*/
SNIConfigParams::SNIConfigParams()
{
}

actionVector *
SNIConfigParams::get(cchar *servername) const
{
  auto actionVec = sni_action_map.get(servername);
  if (!actionVec)
    actionVec = wild_sni_action_map.get(servername);
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
  // cleanup();
  ats_scoped_str fileBuffer;
  char *line;
  char *tok_state = nullptr;
  sni_filename    = ats_stringdup(RecConfigReadConfigPath("proxy.config.ssl.SNI.filename"));
  Debug("ssl", "Loading configuration from %s", sni_filename);
  const matcher_tags sniTags = {SNI_NAME_TAG, nullptr, nullptr, nullptr, nullptr, nullptr, false};
  matcher_line line_info;

  if (sni_filename) {
    fileBuffer = readIntoBuffer(sni_filename, __func__, nullptr);
  }
  if (!fileBuffer) {
    Warning("Couldn't load SNI config from %s", sni_filename);
  }
  line = tokLine(fileBuffer, &tok_state);
  while (line != nullptr) {
    ActionItem *ai = nullptr;
    // Skip all blank spaces at beginning of line.
    while (*line && isspace(*line)) {
      line++;
    }
    if (*line == '\0' || *line == '#') {
      line = tokLine(nullptr, &tok_state);
      continue;
    }
    const char *errPtr;

    errPtr = parseConfigLine(line, &line_info, &sniTags);
    if (errPtr) {
      Warning("Could not load configuration from %s : %s", sni_filename, errPtr);
      return -1;
    }
    char *sniname = ParseSNIConfigLine(line_info, ai);
    // check if wildcard
    // action item obj gets created only when the action is SNI based
    if (sniname && ai) {
      Debug("ssl_sni", "sniname: %s Action %s", sniname, typeid(ai).name());
      // check if sniname already present
      auto actionVec = sni_action_map.get(sniname);
      if (actionVec) {
        actionVec->emplace_back(ai);
      } else {
        actionVector *aiVec = new actionVector();
        aiVec->emplace_back(ai);
        sni_action_map.put(ats_strdup(sniname), aiVec);
      }
    }
    line = tokLine(nullptr, &tok_state);
  }
  return 0;
}

void
SNIConfigParams::cleanup()
{
  Vec<cchar *> keys;
  sni_action_map.get_keys(keys);
  for (int i = keys.length() - 1; i >= 0; i--) {
    auto actionVec = sni_action_map.get(keys.get(i));
    for (auto &ai : *actionVec)
      delete ai;

    actionVec->clear();
  }
  keys.free_and_clear();

  wild_sni_action_map.get_keys(keys);
  for (int i = keys.length() - 1; i >= 0; i--) {
    auto actionVec = wild_sni_action_map.get(keys.get(i));
    for (auto &ai : *actionVec)
      delete ai;

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
  sniConfigUpdate->attach("proxy.config.ssl.SNI.filename");
  reconfigure();
}

void
SNIConfig::cloneProtoSet()
{
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
