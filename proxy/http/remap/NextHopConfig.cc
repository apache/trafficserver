/** @file

  Nexhop parent selection strategy yaml parser

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

#include "NextHopConfig.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <string.h>

#include "ts/Diags.h"

ts::Errata
NextHopConfig::loadConfig(const char *fileName)
{
  std::stringstream doc;
  std::unordered_set<std::string> include_once;

  try {
    loadFile(fileName, doc, include_once);
    YAML::Node node = YAML::Load(doc);
    config          = node.as<NextHopStrategyConfig>();

  } catch (std::exception &ex) {
    errata.push(ts::Errata::Message(1, 1, ex.what()));
    return errata;
  }
  return errata;
}

void
NextHopConfig::loadFile(const std::string fileName, std::stringstream &doc, std::unordered_set<std::string> &include_once)
{
  const char *sep = " \t";
  char *tok, *last;
  std::string line;

  std::ifstream file(fileName);
  if (file.is_open()) {
    while (std::getline(file, line)) {
      if (line[0] == '#') {
        tok = strtok_r(const_cast<char *>(line.c_str()), sep, &last);
        if (tok != nullptr && strcmp(tok, "#include") == 0) {
          std::string f = strtok_r(nullptr, sep, &last);
          if (include_once.find(f) == include_once.end()) {
            include_once.insert(f);
            loadFile(f, doc, include_once);
          }
        }
      } else {
        doc << line << "\n";
      }
    }
    file.close();
  } else {
    throw std::runtime_error("unable to load '" + fileName + "'");
  }
}

namespace YAML
{
// decodes a 'NextHopStrategyConfig' type.
template <> struct convert<NextHopStrategyConfig> {
  static bool
  decode(const Node &node, NextHopStrategyConfig &cfg)
  {
    YAML::Node strategy;
    try {
      strategy = node[NH_strategy];
    } catch (std::exception &ex) {
      throw std::runtime_error("the required 'strategy' node does not exist in the yaml document.");
    }
    if (strategy.IsMap()) {
      // verify keys
      for (auto const &item : strategy) {
        if (std::none_of(valid_strategy_keys.begin(), valid_strategy_keys.end(),
                         [item](std::string s) { return s == item.first.Scalar(); })) {
          throw std::invalid_argument("unsupported strategy key: " + item.first.Scalar());
        }
      }
      std::string policy;
      try {
        policy = strategy[NH_policy].Scalar();
      } catch (std::exception &ex) {
        throw std::invalid_argument("required 'policy' field was not found");
      }
      cfg.policy = static_cast<NextHopSelectionPolicy>(POLICY_DESCRIPTOR.get(policy));
      if (cfg.policy < 0) {
        throw std::invalid_argument("unknown policy value '" + policy + "'");
      }

      // parse the groups list.
      YAML::Node groups;
      try {
        groups = strategy[NH_groups];
      } catch (std::exception &ex) {
        throw std::invalid_argument("the required 'groups' node is not defined in the strategy.");
      }
      if (!groups.IsSequence()) {
        throw std::invalid_argument("the 'groups' node is not a sequence.");
      } else {
        cfg.groups.reserve(groups.size());
        for (unsigned int i = 0; i < groups.size(); i++) {
          YAML::Node hostList = groups[i];
          if (!hostList.IsSequence()) {
            throw std::invalid_argument("the 'hostsList' node in the group list is not a sequence.");
          }
          // process the hostList
          std::vector<NextHopHost> v;
          for (unsigned int j = 0; j < hostList.size(); j++) {
            NextHopHost h = hostList[j].as<NextHopHost>();
            v.push_back(h);
          }
          cfg.groups.push_back(v);
        }
      }

      // hash_key is optional.
      std::string hashKey;
      try {
        hashKey = strategy[NH_hashKey].Scalar();
      } catch (std::exception &ex) {
        ;
      }
      if (!hashKey.empty()) {
        cfg.hashKey = static_cast<NextHopHashKey>(HASH_KEY_DESCRIPTOR.get(hashKey));
        if (cfg.hashKey < 0) {
          throw std::invalid_argument("unknown hash_key value '" + hashKey + "'");
        }
      }

      // protocol is optional.
      std::string protocol;
      try {
        protocol = strategy[NH_protocol].Scalar();
      } catch (std::exception &ex) {
        ;
      }
      if (!protocol.empty()) {
        cfg.protocol = static_cast<NextHopProtocol>(PROTOCOL_DESCRIPTOR.get(protocol));
        if (cfg.protocol < 0) {
          throw std::invalid_argument("unknown protocol value '" + protocol + "'");
        }
      }

      // failover is optional
      YAML::Node failover;
      try {
        failover = strategy[NH_failover];
      } catch (std::exception &ex) {
        ;
      }
      if (!failover.IsNull()) {
        if (failover.IsMap()) {
          // verify the keys
          for (auto const &item : failover) {
            if (std::none_of(valid_failover_keys.begin(), valid_failover_keys.end(),
                             [item](std::string s) { return s == item.first.Scalar(); })) {
              throw std::invalid_argument("unsupported failover key: " + item.first.Scalar());
            }
          }
          cfg.failover = failover.as<NextHopFailOver>();
        } else {
          throw std::invalid_argument("'failover' is not a map in thhis strategy");
        }
      }
    } else {
      throw std::invalid_argument("the required 'strategy' node does not exist in the yaml document.");
    }

    return true;
  }
};

// decodes a 'NextHopFailOver' type.
template <> struct convert<NextHopFailOver> {
  static bool
  decode(const Node &node, NextHopFailOver &fo)
  {
    // parse the ring mode
    std::string ringMode;
    try {
      ringMode = node[NH_ringMode].Scalar();
    } catch (std::exception &ex) {
      throw std::invalid_argument("the required 'ring_mode' setting is not present in the 'failover' map.");
    }
    if (!ringMode.empty()) {
      fo.ringMode = static_cast<NextHopRingMode>(RING_MODE_DESCRIPTOR.get(ringMode));
      if (fo.ringMode < 0) {
        throw std::invalid_argument("unknown ring_mode value '" + ringMode + "'");
      }
    }

    // parse optional response_codes.
    YAML::Node responseCodes;
    try {
      responseCodes = node[NH_responseCodes];
    } catch (std::exception &ex) {
      ;
    }
    if (!responseCodes.IsNull()) {
      if (responseCodes.IsSequence()) {
        for (unsigned int i = 0; i < responseCodes.size(); i++) {
          int code;
          try {
            code = responseCodes[i].as<int>();
          } catch (std::exception &ex) {
            throw std::invalid_argument("invalid response code value, not an 'int'");
          }
          fo.responseCodes.push_back(code);
        }
      } else {
        throw std::invalid_argument("the 'response_codes' node is not a sequence.");
      }
    }

    // parse the health check list.
    YAML::Node healthCheck;
    try {
      healthCheck = node[NH_health_Check];
    } catch (std::exception &ex) {
      throw std::invalid_argument("the required 'health_check' node is not defined in 'failover.");
    }
    if (!healthCheck.IsNull()) {
      if (healthCheck.IsSequence()) {
        for (unsigned int i = 0; i < healthCheck.size(); i++) {
          std::string value     = healthCheck[i].Scalar();
          NextHopHealthCheck hc = static_cast<NextHopHealthCheck>(HEALTH_CHECK_DESCRIPTOR.get(value));
          if (hc < 0) {
            throw std::invalid_argument("invalid health check  value '" + value + "'");
          }
          fo.healthChecks.push_back(hc);
        }
      } else {
        throw std::invalid_argument("the 'health_check' node is not a sequence.");
      }
    }
    return true;
  }
};

// decodes a 'NextHopHost' type
template <> struct convert<NextHopHost> {
  static bool
  decode(const Node &node, NextHopHost &nh)
  {
    bool ext_found = false;
    YAML::Node nd;

    if (!node.IsMap()) {
      throw std::invalid_argument("the 'host' node is not a map");
    } else {
      // check for the use of an alias extension.
      try {
        nd        = node[NH_alias_extension];
        ext_found = true;
      } catch (std::exception &ex) {
        nd = node;
      }

      // verify the keys
      for (auto const &item : nd) {
        if (std::none_of(valid_host_keys.begin(), valid_host_keys.end(),
                         [item](std::string s) { return s == item.first.Scalar(); })) {
          throw std::invalid_argument("unsupported host key: " + item.first.Scalar());
        }
      }
    }

    // parse the host field
    try {
      std::string host = nd[NH_host].Scalar();
      nh.host          = host;
    } catch (std::exception &ex) {
      throw std::invalid_argument("the required 'host' field is missing in the 'hosts' list.");
    }
    // parse the health check url field.
    std::string healthCheckUrl;
    try {
      healthCheckUrl    = nd[NH_healthCheck][NH_url].Scalar();
      nh.healthCheckUrl = healthCheckUrl;
    } catch (std::exception &ex) {
      throw std::invalid_argument("the required 'healthcheck' 'url' field is missing for a host in the 'hosts' list.");
    }

    // parse the host protocols sequence
    YAML::Node protocols;
    try {
      protocols = nd[NH_protocol];
    } catch (std::exception &ex) {
      throw std::invalid_argument("the required 'protocol' sequence field is missing for a host in the 'hosts' list.");
    }
    if (protocols.IsSequence()) {
      for (unsigned int i = 0; i < protocols.size(); i++) {
        NextHopHostProtocols np = protocols[i].as<NextHopHostProtocols>();
        nh.protocols.push_back(np);
      }
    } else {
      throw std::invalid_argument("the 'protocol' field is not a sequence  for a host in the 'hosts' list.");
    }

    // if an alias extension is found, look for the weight field, otherwise defaults are used.
    if (ext_found) {
      double weight = 0;
      try {
        weight    = node[NH_weight].as<double>();
        nh.weight = weight;
      } catch (std::exception &ex) {
        throw std::invalid_argument("the required 'weight' field is missing for a host in the  'hosts' list.");
      }
    }

    return true;
  }
};

// decodes a 'NextHopHostProtocols' type.
template <> struct convert<NextHopHostProtocols> {
  static bool
  decode(const Node &node, NextHopHostProtocols &np)
  {
    bool port_found = true;

    // look for http port.
    unsigned int http_port  = 0;
    unsigned int https_port = 0;

    // look for http port
    try {
      http_port   = node[NH_http].as<int>();
      port_found  = true;
      np.protocol = NH_http;
      np.port     = http_port;
    } catch (std::exception &ex) {
      port_found = false;
    }

    // if not an http port, look for https port
    if (!port_found) {
      try {
        https_port  = node[NH_https].as<int>();
        np.protocol = NH_https;
        np.port     = https_port;
        port_found  = true;
      } catch (std::exception &ex) {
        port_found = false;
      }
    }

    if (!port_found) {
      throw std::invalid_argument("no protocol or port found for a 'host' in the host list.");
    }

    return true;
  }
};

} // namespace YAML
