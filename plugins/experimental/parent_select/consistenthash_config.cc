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
#include "time.h"

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
#include "ts/parentselectdefs.h"

#include "consistenthash_config.h"

namespace
{
std::mutex strategies_cache_mutex;
std::map<std::string, strategies_map> strategies_cache;
} // namespace

void
clearStrategiesCache(void)
{
  std::lock_guard<std::mutex> guard(strategies_cache_mutex);
  strategies_cache.clear();
}

void loadConfigFile(const std::string &fileName, std::stringstream &doc, std::unordered_set<std::string> &include_once);

// createStrategy creates and initializes a Consistent Hash strategy from the given YAML node.
// Caller takes ownership of the returned pointer, and must call delete on it.
TSNextHopSelectionStrategy *
createStrategy(const std::string &name, const YAML::Node &node)
{
  TSDebug(PLUGIN_NAME, "createStrategy %s calling.", name.c_str());
  try {
    PLNextHopConsistentHash *st = new PLNextHopConsistentHash(name, node);
    TSDebug(PLUGIN_NAME, "createStrategy %s succeeded, returning object", name.c_str());
    return st;
  } catch (std::exception &ex) {
    TSError("[%s] creating strategies '%s' threw '%s', returning nullptr", PLUGIN_NAME, name.c_str(), ex.what());
    return nullptr;
  }
}

// Caller takes ownership of the returned pointers in the map, and must call delete on them.
// TODO change to only call createStrategy for the one we need, for efficiency.
strategies_map
createStrategiesFromFile(const char *file)
{
  TSDebug(PLUGIN_NAME, "createStrategiesFromFile plugin createStrategiesFromFile file '%s'", file);

  {
    std::lock_guard<std::mutex> guard(strategies_cache_mutex);
    auto it = strategies_cache.find(file);
    if (it != strategies_cache.end()) {
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile file '%s' in cache from previous remap, using cache", file);
      return it->second;
    }
  }
  TSDebug(PLUGIN_NAME, "createStrategiesFromFile file '%s' not in cache, loading file", file);

  YAML::Node config;
  YAML::Node strategies;
  std::stringstream doc;
  std::unordered_set<std::string> include_once;
  std::string_view fn = file;

  // strategy policy
  constexpr std::string_view consistent_hash = "consistent_hash";

  const char *basename = fn.substr(fn.find_last_of('/') + 1).data();

  try {
    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s loading ...", basename);
    loadConfigFile(fn.data(), doc, include_once);
    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s loaded.", basename);

    config = YAML::Load(doc);

    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s yaml loaded.", basename);

    if (config.IsNull()) {
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile No NextHop strategy configs were loaded.");
      return strategies_map();
    }

    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s checked null.", basename);

    strategies = config["strategies"];
    if (strategies.Type() != YAML::NodeType::Sequence) {
      TSError("[%s] malformed %s file, expected a 'strategies' sequence", PLUGIN_NAME, basename);
      return strategies_map();
    }

    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s checked strategies member.", basename);

    // std::map<std::string, TSNextHopSelectionStrategy*, std::less<>>
    strategies_map strategiesMap;
    for (auto &&strategy : strategies) {
      auto name = strategy["strategy"].as<std::string>();
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s.", basename, name.c_str());
      auto policy = strategy["policy"];
      if (!policy) {
        TSError("[%s] no policy is defined for the strategy named '%s'.", PLUGIN_NAME, name.c_str());
        return strategies_map();
      }
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s checked policy.", basename, name.c_str());
      const auto &policy_value = policy.Scalar();
      if (policy_value != consistent_hash) {
        TSError("[%s] strategy named '%s' has unsupported policy '%s'.", PLUGIN_NAME, name.c_str(), policy_value.c_str());
        return strategies_map();
      }
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s creating strategy.", basename, name.c_str());
      TSNextHopSelectionStrategy *tsStrategy = createStrategy(name, strategy);
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s created strategy.", basename, name.c_str());
      if (tsStrategy == nullptr) {
        return strategies_map();
      }
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s checked strategy null.", basename, name.c_str());
      strategiesMap.emplace(name, std::unique_ptr<TSNextHopSelectionStrategy>(tsStrategy));
      TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s got strategy %s emplaced.", basename, name.c_str());
    }
    TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s returning strategies created.", basename);

    {
      std::lock_guard<std::mutex> guard(strategies_cache_mutex);
      strategies_cache[file] = strategiesMap;
    }

    return strategiesMap;
  } catch (std::exception &ex) {
    TSError("[%s] creating strategies from file %s threw '%s'.", PLUGIN_NAME, file, ex.what());
  }
  TSDebug(PLUGIN_NAME, "createStrategiesFromFile filename %s returning error.", basename);
  return strategies_map();
}

/*
 * loads the contents of a file into a std::stringstream document.  If the file has a '#include file'
 * directive, that 'file' is read into the document beginning at the point where the
 * '#include' was found. This allows the 'strategy' and 'hosts' yaml files to be separate.  The
 * 'strategy' yaml file would then normally have the '#include hosts.yml' in it's beginning.
 */
void
loadConfigFile(const std::string &fileName, std::stringstream &doc, std::unordered_set<std::string> &include_once)
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

    TSDebug(PLUGIN_NAME, "loading strategy YAML files from the directory %s", fileName.c_str());
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

      for (auto &f : files) {
        std::ifstream file(fileName + "/" + f.data());
        if (file.is_open()) {
          while (std::getline(file, line)) {
            if (line[0] == '#') {
              // continue;
            }
            doc << line << "\n";
          }
          file.close();
        } else {
          throw std::invalid_argument("Unable to open and read '" + fileName + "/" + f.data() + "'");
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
