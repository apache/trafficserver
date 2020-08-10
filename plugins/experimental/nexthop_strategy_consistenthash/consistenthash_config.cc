/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include "strategy.h"
#include "consistenthash.h"
#include "util.h"

#include <cinttypes>
#include <string>
#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <fstream>
#include <cstring>

#include <sys/stat.h>
#include <dirent.h>

#include <yaml-cpp/yaml.h>

#include "tscore/HashSip.h"
#include "tscore/ConsistentHash.h"
#include "tscore/ink_assert.h"
#include "ts/ts.h"
#include "ts/remap.h"
#include "ts/nexthop.h"
#include "ts/parentresult.h"

void loadConfigFile(const std::string fileName, std::stringstream &doc, std::unordered_set<std::string> &include_once);

// createStrategy creates and initializes a Consistent Hash strategy from the given YAML node.
// Caller takes ownership of the returned pointer, and must call delete on it.
TSNextHopSelectionStrategy *
createStrategy(const std::string &name, const YAML::Node &node)
{
  NextHopConsistentHash *st = new NextHopConsistentHash(name);
  if (!st->Init(node)) {
    return nullptr;
  }
  return st;
}

// createStrategyFromFile creates a Consistent Hash strategy from the given config file.
// Caller takes ownership of the returned pointer, and must call delete on it.
TSNextHopSelectionStrategy *
createStrategyFromFile(const char *file, const char *strategyName)
{
  NH_Debug(NH_DEBUG_TAG, "plugin createStrategyFromFile file '%s' strategy '%s'", file, strategyName);

  YAML::Node config;
  YAML::Node strategies;
  std::stringstream doc;
  std::unordered_set<std::string> include_once;
  std::string_view fn = file;

  // strategy policy
  constexpr std::string_view consistent_hash = "consistent_hash";

  const char *basename = fn.substr(fn.find_last_of('/') + 1).data();

  try {
    NH_Note("%s loading ...", basename);
    loadConfigFile(fn.data(), doc, include_once);

    config = YAML::Load(doc);
    if (config.IsNull()) {
      NH_Note("No NextHop strategy configs were loaded.");
      return nullptr;
    }

    strategies = config["strategies"];
    if (strategies.Type() != YAML::NodeType::Sequence) {
      NH_Error("malformed %s file, expected a 'strategies' sequence", basename);
      return nullptr;
    }

    for (unsigned int i = 0; i < strategies.size(); ++i) {
      YAML::Node strategy = strategies[i];
      auto name           = strategy["strategy"].as<std::string>();
      if (name != strategyName) {
        continue;
      }
      auto policy = strategy["policy"];
      if (!policy) {
        NH_Error("No policy is defined for the strategy named '%s', this strategy will be ignored.", name.c_str());
        continue;
      }
      auto policy_value = policy.Scalar();
      if (policy_value != consistent_hash) {
        NH_Error("Strategy named '%s' has unsupported policy '%s', this strategy will be ignored.", strategyName,
                 policy_value.c_str());
        return nullptr;
      }
      return createStrategy(name, strategy);
    }
    NH_Error("no strategy named '%s' found", strategyName);
  } catch (std::exception &ex) {
    NH_Note("%s", ex.what());
  }
  return nullptr;
}

/*
 * loads the contents of a file into a std::stringstream document.  If the file has a '#include file'
 * directive, that 'file' is read into the document beginning at the the point where the
 * '#include' was found. This allows the 'strategy' and 'hosts' yaml files to be separate.  The
 * 'strategy' yaml file would then normally have the '#include hosts.yml' in it's begining.
 */
void
loadConfigFile(const std::string fileName, std::stringstream &doc, std::unordered_set<std::string> &include_once)
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

      for (uint32_t i = 0; i < files.size(); i++) {
        std::ifstream file(fileName + "/" + files[i].data());
        if (file.is_open()) {
          while (std::getline(file, line)) {
            if (line[0] == '#') {
              // continue;
            }
            doc << line << "\n";
          }
          file.close();
        } else {
          throw std::invalid_argument("Unable to open and read '" + fileName + "/" + files[i].data() + "'");
        }
      }
    }
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
