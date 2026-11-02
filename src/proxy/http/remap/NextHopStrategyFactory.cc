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

#include <sys/stat.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "records/RecCore.h"
#include "tscore/ink_platform.h"
#include "tscore/Layout.h"
#include "proxy/http/remap/NextHopStrategyFactory.h"
#include "proxy/http/remap/NextHopConsistentHash.h"
#include "proxy/http/remap/NextHopRoundRobin.h"
#include <tsutil/YamlCfg.h>

namespace
{
// Returns the include target resolved against the configuration directory. Layout::relative_to() calls ink_fatal() for a
// path too long to merge, which would take down a running traffic_server on a reload, so a target that would exceed the
// ink_filepath_merge() length bound is rejected as a configuration error instead.
std::string
config_dir_include_path(std::string const &include_target, std::string const &fileName)
{
  if (RecConfigReadConfigDir().size() + include_target.size() + 4 > PATH_NAME_MAX) {
    throw std::invalid_argument("'#include' path in '" + fileName + "' exceeds the " + std::to_string(PATH_NAME_MAX) +
                                " character path limit");
  }
  return RecConfigReadConfigPath(nullptr, include_target.c_str());
}

// Earlier releases opened a relative include from the traffic_server working directory, which is the install prefix.
// ToDo: Deprecated in 11.0.0, remove in 12.0.0
std::string
legacy_include_path(std::string_view include_path)
{
  std::filesystem::path const target{include_path};
  if (target.is_absolute()) {
    return {};
  }
  return (std::filesystem::path{Layout::get()->prefix} / target).lexically_normal().string();
}

std::string
recommended_include(std::string const &resolved_path)
{
  std::filesystem::path const relative =
    std::filesystem::path{resolved_path}.lexically_relative(std::filesystem::path{RecConfigReadConfigDir()}.lexically_normal());
  if (relative.empty() || relative == "." || *relative.begin() == "..") {
    return resolved_path;
  }
  return relative.string();
}
} // namespace

NextHopStrategyFactory::NextHopStrategyFactory(const char *file) : fn(file)
{
  YAML::Node                      config;
  YAML::Node                      strategies;
  std::stringstream               doc;
  std::unordered_set<std::string> include_once;

  // strategy policies.
  constexpr std::string_view consistent_hash = "consistent_hash";
  constexpr std::string_view first_live      = "first_live";
  constexpr std::string_view rr_strict       = "rr_strict";
  constexpr std::string_view rr_ip           = "rr_ip";
  constexpr std::string_view latched         = "latched";

  bool error_loading   = false;
  strategies_loaded    = true;
  const char *basename = std::string_view(fn).substr(fn.find_last_of('/') + 1).data();

  NH_Note("%s loading ...", basename);

  struct stat sbuf;
  if (stat(fn.c_str(), &sbuf) == -1 && errno == ENOENT) {
    // missing config file is an acceptable runtime state
    strategies_loaded = false;
    NH_Note("%s doesn't exist", fn.c_str());
    goto done;
  }

  // load the strategies yaml config file.
  try {
    loadConfigFile(fn.c_str(), doc, include_once);

    config = YAML::Load(doc);
    if (config.IsNull()) {
      NH_Note("No NextHop strategy configs were loaded.");
      strategies_loaded = false;
    } else {
      strategies = config["strategies"];
      if (strategies.Type() != YAML::NodeType::Sequence) {
        NH_Error("malformed %s file, expected a 'strategies' sequence", basename);
        strategies_loaded = false;
        error_loading     = true;
      }
    }
    // loop through the strategies document.
    for (auto &&strategie : strategies) {
      ts::Yaml::Map strategy{strategie};
      auto          name   = strategy["strategy"].as<std::string>();
      auto          policy = strategy["policy"];
      if (!policy) {
        NH_Error("No policy is defined for the strategy named '%s', this strategy will be ignored.", name.c_str());
        continue;
      }
      const auto  &policy_value = policy.Scalar();
      NHPolicyType policy_type  = NHPolicyType::UNDEFINED;

      if (policy_value == consistent_hash) {
        policy_type = NHPolicyType::CONSISTENT_HASH;
      } else if (policy_value == first_live) {
        policy_type = NHPolicyType::FIRST_LIVE;
      } else if (policy_value == rr_strict) {
        policy_type = NHPolicyType::RR_STRICT;
      } else if (policy_value == rr_ip) {
        policy_type = NHPolicyType::RR_IP;
      } else if (policy_value == latched) {
        policy_type = NHPolicyType::RR_LATCHED;
      }
      if (policy_type == NHPolicyType::UNDEFINED) {
        NH_Error("Invalid policy '%s' for the strategy named '%s', this strategy will be ignored.", policy_value.c_str(),
                 name.c_str());
      } else {
        NH_Dbg(NH_DBG_CTL, "createStragety '%s'", name.c_str());
        createStrategy(name, policy_type, strategy);
        strategy.done();
      }
    }
  } catch (std::exception &ex) {
    NH_Error("%s", ex.what());
    strategies_loaded = false;
    error_loading     = true;
  }

done:
  if (!error_loading) {
    NH_Note("%s finished loading", basename);
  } else {
    NH_Error("%s failed to load", basename);
  }
}

NextHopStrategyFactory::~NextHopStrategyFactory()
{
  NH_Dbg(NH_DBG_CTL, "destroying NextHopStrategyFactory");

  for (auto &[_, ptr] : _strategies) {
    delete ptr;
  }
}

void
NextHopStrategyFactory::createStrategy(const std::string &name, const NHPolicyType policy_type, ts::Yaml::Map &node)
{
  NextHopSelectionStrategy const *strat = strategyInstance(name.c_str());
  if (strat != nullptr) {
    NH_Note("A strategy named '%s' has already been loaded and another will not be created.", name.data());
    node.bad();
    return;
  }

  try {
    switch (policy_type) {
    case NHPolicyType::FIRST_LIVE:
    case NHPolicyType::RR_STRICT:
    case NHPolicyType::RR_IP:
    case NHPolicyType::RR_LATCHED: {
      NextHopRoundRobin *const strat_rr = new NextHopRoundRobin(name, policy_type, node);
      _strategies.emplace(std::make_pair(std::string(name), strat_rr));
      break;
    }
    case NHPolicyType::CONSISTENT_HASH: {
      NextHopConsistentHash *const strat_chash = new NextHopConsistentHash(name, policy_type, node);
      _strategies.emplace(std::make_pair(std::string(name), strat_chash));
      break;
    }
    default: // handles ParentRR_t::UNDEFINED, no strategy is added
      break;
    };
  } catch (std::exception &ex) {
    ;
  }
}

NextHopSelectionStrategy *
NextHopStrategyFactory::strategyInstance(const char *name) const
{
  NextHopSelectionStrategy *ps_strategy = nullptr;

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
 * directive, that 'file' is read into the document beginning at the point where the
 * '#include' was found. This allows the 'strategy' and 'hosts' yaml files to be separate.  The
 * 'strategy' yaml file would then normally have the '#include hosts.yml' in it's beginning.
 * Relative included paths are resolved against the configuration directory.
 */
void
NextHopStrategyFactory::loadConfigFile(const std::string &fileName, std::stringstream &doc,
                                       std::unordered_set<std::string> &include_once)
{
  constexpr std::string_view separators = " \t";
  struct stat                buf;
  std::string                line;

  if (stat(fileName.c_str(), &buf) == -1) {
    std::string err_msg = strerror(errno);
    throw std::invalid_argument("Unable to stat '" + fileName + "': " + err_msg);
  }

  // if fileName is a directory, concatenate all '.yaml' files alphanumerically
  // into a single document stream.  No #include is supported.
  if (S_ISDIR(buf.st_mode)) {
    DIR                          *dir     = nullptr;
    struct dirent                *dir_ent = nullptr;
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
        std::string_view line_view{line};
        if (line_view.starts_with('#')) {
          const auto directive_end = line_view.find_first_of(separators);
          if (line_view.substr(0, directive_end) == "#include") {
            const auto include_start = line_view.find_first_not_of(separators, directive_end);
            if (include_start == std::string_view::npos) {
              throw std::invalid_argument("'#include' without a file name in '" + fileName + "'");
            }
            const auto        include_line = line_view.substr(include_start);
            const auto        include_end  = include_line.find_first_of(separators);
            const std::string include_target{include_line.substr(0, include_end)};
            std::string       f                = config_dir_include_path(include_target, fileName);
            bool              used_legacy_path = false;
            std::string       legacy_path;

            // Retry an include that is absent from the configuration directory relative to the install prefix, the way earlier
            // releases resolved it. not_found covers a missing file and a path component that is not a directory. Other
            // failures, such as a permission error, are left to fail the load so they are reported rather than silently
            // replaced by a file from the install prefix.
            std::error_code ec;
            if (std::filesystem::status(f, ec).type() == std::filesystem::file_type::not_found) {
              legacy_path = legacy_include_path(include_target);
              if (!legacy_path.empty() && std::filesystem::exists(legacy_path, ec)) {
                f                = legacy_path;
                used_legacy_path = true;
              }
            }

            if (include_once.find(f) == include_once.end()) {
              include_once.insert(f);
              // try to load included file.
              try {
                loadConfigFile(f, doc, include_once);
              } catch (const std::exception &ex) {
                std::string const also_tried =
                  !used_legacy_path && !legacy_path.empty() ? " (also tried '" + legacy_path + "')" : "";
                throw std::invalid_argument("Unable to load '#include " + include_target + "' from '" + fileName + "' as '" + f +
                                            "'" + also_tried + ": " + ex.what());
              }
            }
            if (used_legacy_path) {
              NH_Warn("Deprecated '#include %s' in '%s'; use '#include %s' instead. This fallback will be removed in 12.0.0.",
                      include_target.c_str(), fileName.c_str(), recommended_include(f).c_str());
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
