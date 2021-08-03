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

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <cstring>

#include "NextHopStrategyFactory.h"
#include "NextHopConsistentHash.h"
#include "NextHopRoundRobin.h"

NextHopStrategyFactory::NextHopStrategyFactory(const char *file)
{
  YAML::Node config;
  YAML::Node strategies;
  std::stringstream doc;
  std::unordered_set<std::string> include_once;
  std::string_view fn = file;

  // strategy policies.
  constexpr std::string_view consistent_hash = "consistent_hash";
  constexpr std::string_view first_live      = "first_live";
  constexpr std::string_view rr_strict       = "rr_strict";
  constexpr std::string_view rr_ip           = "rr_ip";
  constexpr std::string_view latched         = "latched";

  strategies_loaded    = true;
  const char *basename = fn.substr(fn.find_last_of('/') + 1).data();

  // load the strategies yaml config file.
  try {
    NH_Note("%s loading ...", basename);
    loadConfigFile(fn.data(), doc, include_once);

    config = YAML::Load(doc);
    if (config.IsNull()) {
      NH_Note("No NextHop strategy configs were loaded.");
      strategies_loaded = false;
    } else {
      strategies = config["strategies"];
      if (strategies.Type() != YAML::NodeType::Sequence) {
        NH_Error("malformed %s file, expected a 'strategies' sequence", basename);
        strategies_loaded = false;
      }
    }
    // loop through the strategies document.
    for (auto &&strategie : strategies) {
      YAML::Node strategy = strategie;
      auto name           = strategy["strategy"].as<std::string>();
      auto policy         = strategy["policy"];
      if (!policy) {
        NH_Error("No policy is defined for the strategy named '%s', this strategy will be ignored.", name.c_str());
        continue;
      }
      const auto &policy_value = policy.Scalar();
      NHPolicyType policy_type = NH_UNDEFINED;

      if (policy_value == consistent_hash) {
        policy_type = NH_CONSISTENT_HASH;
      } else if (policy_value == first_live) {
        policy_type = NH_FIRST_LIVE;
      } else if (policy_value == rr_strict) {
        policy_type = NH_RR_STRICT;
      } else if (policy_value == rr_ip) {
        policy_type = NH_RR_IP;
      } else if (policy_value == latched) {
        policy_type = NH_RR_LATCHED;
      }
      if (policy_type == NH_UNDEFINED) {
        NH_Error("Invalid policy '%s' for the strategy named '%s', this strategy will be ignored.", policy_value.c_str(),
                 name.c_str());
      } else {
        createStrategy(name, policy_type, strategy);
      }
    }
  } catch (std::exception &ex) {
    NH_Note("%s", ex.what());
    strategies_loaded = false;
  }
  if (strategies_loaded) {
    NH_Note("%s finished loading", basename);
  }
}

NextHopStrategyFactory::~NextHopStrategyFactory()
{
  NH_Debug(NH_DEBUG_TAG, "destroying NextHopStrategyFactory");
}

void
NextHopStrategyFactory::createStrategy(const std::string &name, const NHPolicyType policy_type, const YAML::Node &node)
{
  std::shared_ptr<NextHopSelectionStrategy> strat;
  std::shared_ptr<NextHopRoundRobin> strat_rr;
  std::shared_ptr<NextHopConsistentHash> strat_chash;

  strat = strategyInstance(name.c_str());
  if (strat != nullptr) {
    NH_Note("A strategy named '%s' has already been loaded and another will not be created.", name.data());
    return;
  }

  switch (policy_type) {
  case NH_FIRST_LIVE:
  case NH_RR_STRICT:
  case NH_RR_IP:
  case NH_RR_LATCHED:
    strat_rr = std::make_shared<NextHopRoundRobin>(name, policy_type);
    if (strat_rr->Init(node)) {
      _strategies.emplace(std::make_pair(std::string(name), strat_rr));
    } else {
      strat.reset();
    }
    break;
  case NH_CONSISTENT_HASH:
    strat_chash = std::make_shared<NextHopConsistentHash>(name, policy_type);
    if (strat_chash->Init(node)) {
      _strategies.emplace(std::make_pair(std::string(name), strat_chash));
    } else {
      strat_chash.reset();
    }
    break;
  default: // handles P_UNDEFINED, no strategy is added
    break;
  };
}

std::shared_ptr<NextHopSelectionStrategy>
NextHopStrategyFactory::strategyInstance(const char *name)
{
  std::shared_ptr<NextHopSelectionStrategy> ps_strategy;

  if (!strategies_loaded) {
    NH_Error("no strategy configurations were defined, see definitions in '%s' file", fn.c_str());
    return nullptr;
  } else {
    auto it = _strategies.find(name);
    if (it == _strategies.end()) {
      // NH_Error("no strategy found for name: %s", name);
      return nullptr;
    } else {
      ps_strategy           = it->second;
      ps_strategy->distance = std::distance(_strategies.begin(), it);
    }
  }

  return ps_strategy;
}

/*
 * loads the contents of a file into a std::stringstream document.  If the file has a '#include file'
 * directive, that 'file' is read into the document beginning at the the point where the
 * '#include' was found. This allows the 'strategy' and 'hosts' yaml files to be separate.  The
 * 'strategy' yaml file would then normally have the '#include hosts.yml' in it's beginning.
 */
void
NextHopStrategyFactory::loadConfigFile(const std::string &fileName, std::stringstream &doc,
                                       std::unordered_set<std::string> &include_once)
{
  const char *sep = " \t";
  char *tok, *last;
  struct stat buf;
  std::string line;

  if (stat(fileName.c_str(), &buf) == -1) {
    std::string err_msg = strerror(errno);
    throw std::invalid_argument("Unable to stat '" + fileName + "': " + err_msg);
  }

  // if fileName is a directory, concatenate all '.yaml' files alphanumerically
  // into a single document stream.  No #include is supported.
  if (S_ISDIR(buf.st_mode)) {
    DIR *dir               = nullptr;
    struct dirent *dir_ent = nullptr;
    std::vector<std::string_view> files;

    NH_Note("loading strategy YAML files from the directory %s", fileName.c_str());
    if ((dir = opendir(fileName.c_str())) == nullptr) {
      std::string err_msg = strerror(errno);
      throw std::invalid_argument("Unable to open the directory '" + fileName + "': " + err_msg);
    } else {
      while ((dir_ent = readdir(dir)) != nullptr) {
        // filename should be greater that 6 characters to have a '.yaml' suffix.
        if (strlen(dir_ent->d_name) < 6) {
          continue;
        }
        std::string_view sv = dir_ent->d_name;
        if (sv.find(".yaml", sv.size() - 5) == sv.size() - 5) {
          files.push_back(sv);
        }
      }
      // sort the files alphanumerically
      std::sort(files.begin(), files.end(),
                [](const std::string_view lhs, const std::string_view rhs) { return lhs.compare(rhs) < 0; });

      for (auto &i : files) {
        std::ifstream file(fileName + "/" + i.data());
        if (file.is_open()) {
          while (std::getline(file, line)) {
            if (line[0] == '#') {
              // continue;
            }
            doc << line << "\n";
          }
          file.close();
        } else {
          throw std::invalid_argument("Unable to open and read '" + fileName + "/" + i.data() + "'");
        }
      }
    }
    closedir(dir);
  } else {
    std::ifstream file(fileName);
    if (file.is_open()) {
      while (std::getline(file, line)) {
        if (line[0] == '#') {
          tok = strtok_r(const_cast<char *>(line.c_str()), sep, &last);
          if (tok != nullptr && strcmp(tok, "#include") == 0) {
            std::string f = strtok_r(nullptr, sep, &last);
            if (include_once.find(f) == include_once.end()) {
              include_once.insert(f);
              // try to load included file.
              try {
                loadConfigFile(f, doc, include_once);
              } catch (std::exception &ex) {
                throw std::invalid_argument("Unable to open included file '" + f + "' from '" + fileName + "'");
              }
            }
          }
        } else {
          doc << line << "\n";
        }
      }
      file.close();
    } else {
      throw std::invalid_argument("Unable to open and read '" + fileName + "'");
    }
  }
}
