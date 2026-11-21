/** @file

  A brief file description

  @par License

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
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "records/RecCore.h"
#include "tscore/Layout.h"
#include "proxy/http/remap/NextHopStrategyFactory.h"
#include "proxy/http/remap/NextHopConsistentHash.h"
#include "proxy/http/remap/NextHopRoundRobin.h"
#include <tsutil/YamlCfg.h>

namespace
{
// Returns the path an include target is opened as: an absolute target unchanged, a relative one joined to the configuration
// directory. The join is not normalized, so a '..' that follows a symlink is resolved through the symlink by the kernel.
std::filesystem::path
config_dir_include_path(std::string_view include_target)
{
  return std::filesystem::path{RecConfigReadConfigDir()} / include_target;
}

// Earlier releases opened a relative include from the traffic_server working directory, which is the install prefix.
// Returns that path, not normalized for the same reason as config_dir_include_path(), or an empty path for an absolute
// target. ToDo: Deprecated in 11.0.0, remove in 12.0.0
std::filesystem::path
legacy_include_path(std::string_view include_target)
{
  std::filesystem::path const target{include_target};

  if (target.is_absolute()) {
    return {};
  }
  return std::filesystem::path{Layout::get()->prefix} / target;
}

// Returns the elements of path without '.' and empty elements, which never change the file a path refers to. '..' elements
// are kept because they follow symlinks.
std::vector<std::filesystem::path>
path_elements(std::filesystem::path const &path)
{
  std::vector<std::filesystem::path> elements;

  for (auto const &element : path) {
    if (!element.empty() && element != ".") {
      elements.push_back(element);
    }
  }
  return elements;
}

// Returns an '#include' target that config_dir_include_path() opens as the same file as resolved_path: the path relative to
// the configuration directory when resolved_path lies under it, otherwise resolved_path itself.
std::string
recommended_include(std::filesystem::path const &resolved_path)
{
  auto const config_elements   = path_elements(RecConfigReadConfigDir());
  auto const resolved_elements = path_elements(resolved_path);

  if (resolved_elements.size() <= config_elements.size() ||
      !std::equal(config_elements.begin(), config_elements.end(), resolved_elements.begin())) {
    return resolved_path.string();
  }

  std::filesystem::path relative;

  for (auto element = resolved_elements.begin() + config_elements.size(); element != resolved_elements.end(); ++element) {
    relative /= *element;
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
  if (strategies_loaded) {
    // Precomputed so that strategyInstance() stays read-only.
    uint32_t idx = 0;
    for (auto &[name, strat] : _strategies) {
      strat->distance = idx++;
      _strategy_ptrs.insert(strat);
    }
  }
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
  // Called from transaction threads, must not mutate strategy state.
  if (!strategies_loaded) {
    if (!_not_loaded_logged.exchange(true, std::memory_order_relaxed)) {
      NH_Error("no strategy configurations were defined, see definitions in '%s' file", fn.c_str());
    }
    return nullptr;
  }

  if (name == nullptr) {
    return nullptr;
  }

  auto it = _strategies.find(name);
  return (it == _strategies.end()) ? nullptr : it->second;
}

bool
NextHopStrategyFactory::contains(NextHopSelectionStrategy const *strategy) const
{
  return _strategy_ptrs.contains(strategy);
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
            std::string       f                = config_dir_include_path(include_target).string();
            bool              used_legacy_path = false;
            std::string       legacy_path;

            // Retry an include that is absent from the configuration directory relative to the install prefix, the way earlier
            // releases resolved it. not_found covers a missing file and a path component that is not a directory. Other
            // failures, such as a permission error, are left to fail the load so they are reported rather than silently
            // replaced by a file from the install prefix.
            std::error_code ec;
            if (std::filesystem::status(f, ec).type() == std::filesystem::file_type::not_found) {
              legacy_path = legacy_include_path(include_target).string();
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
