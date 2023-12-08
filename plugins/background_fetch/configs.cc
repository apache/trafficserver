/** @file

    Plugin to perform background fetches of certain content that would
    otherwise not be cached. For example, Range: requests / responses.

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

#include <getopt.h>
#include <cstdio>
#include <memory.h>

#include "configs.h"

#include <swoc/TextView.h>
#include <swoc/swoc_file.h>
#include <tscpp/util/ts_bw_format.h>

using namespace swoc::literals;

// Parse the command line options. This got a little wonky, since we decided to have different
// syntax for remap vs global plugin initialization, and the global BG fetch state :-/. Clean up
// later...
bool
BgFetchConfig::parseOptions(int argc, const char *argv[])
{
  static const struct option longopt[] = {
    {const_cast<char *>("log"),       required_argument, nullptr, 'l' },
    {const_cast<char *>("config"),    required_argument, nullptr, 'c' },
    {const_cast<char *>("allow-304"), no_argument,       nullptr, 'a' },
    {nullptr,                         no_argument,       nullptr, '\0'}
  };

  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "lc", longopt, nullptr);

    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'l':
      Dbg(Bg_dbg_ctl, "option: log file specified: %s", optarg);
      _log_file = optarg;
      break;
    case 'c':
      Dbg(Bg_dbg_ctl, "option: config file '%s'", optarg);
      if (!readConfig(optarg)) {
        // Error messages are written in the parser
        return false;
      }
      break;
    case 'a':
      Dbg(Bg_dbg_ctl, "option: --allow-304 set");
      _allow_304 = true;
      break;
    default:
      TSError("[%s] invalid plugin option: %c", PLUGIN_NAME, opt);
      return false;
      break;
    }
  }

  return true;
}

// Read a config file, populate the linked list (chain the BgFetchRule's)
bool
BgFetchConfig::readConfig(const char *config_file)
{
  if (nullptr == config_file) {
    TSError("[%s] invalid config file", PLUGIN_NAME);
    return false;
  }

  swoc::file::path path(config_file);

  Dbg(Bg_dbg_ctl, "trying to open config file in this path: %s", config_file);

  if (!path.is_absolute()) {
    path = swoc::file::path(TSConfigDirGet()) / path;
  }
  Dbg(Bg_dbg_ctl, "chosen config file is at: %s", path.c_str());

  std::error_code ec;
  auto content = swoc::file::load(path, ec);
  if (ec) {
    swoc::bwprint(ts::bw_dbg, "[{}] invalid config file: {} {}", PLUGIN_NAME, path, ec);
    TSError("%s", ts::bw_dbg.c_str());
    Dbg(Bg_dbg_ctl, "%s", ts::bw_dbg.c_str());
    return false;
  }

  swoc::TextView text{content};
  while (text) {
    auto line = text.take_prefix_at('\n').ltrim_if(&isspace);

    if (line.empty() || line.front() == '#') {
      continue;
    }

    auto cfg_type = line.take_prefix_if(&isspace);
    if (cfg_type.empty()) {
      continue;
    }

    Dbg(Bg_dbg_ctl, "setting background_fetch exclusion criterion based on string: %.*s", int(cfg_type.size()), cfg_type.data());

    bool exclude = false;
    if (0 == strcasecmp(cfg_type, "exclude")) {
      exclude = true;
    } else if (0 != strcasecmp(cfg_type, "include")) {
      swoc::bwprint(ts::bw_dbg, "[{}] invalid specifier {}, skipping config line", PLUGIN_NAME, cfg_type);
      TSError("%s", ts::bw_dbg.c_str());
      continue;
    }

    if (auto cfg_name = line.take_prefix_if(&isspace); !cfg_name.empty()) {
      if (auto cfg_value = line.take_prefix_if(&isspace); !cfg_value.empty()) {
        if ("Client-IP"_tv == cfg_name) {
          swoc::IPRange r;
          // '*' is special - match any address. Signalled by empty range.
          if (cfg_value.size() != 1 || cfg_value.front() == '*') {
            if (!r.load(cfg_value)) { // assume if it loads, it's not empty.
              TSError("[%s] invalid IP address range %.*s, skipping config value", PLUGIN_NAME, int(cfg_value.size()),
                      cfg_value.data());
              continue;
            }
          }
          _rules.emplace_back(exclude, r);
          swoc::bwprint(ts::bw_dbg, "adding background_fetch address range rule {} for {}: {}", exclude, cfg_name, cfg_value);
          Dbg(Bg_dbg_ctl, "%s", ts::bw_dbg.c_str());
        } else if ("Content-Length"_tv == cfg_name) {
          BgFetchRule::size_cmp_type::OP op;
          if (cfg_value[0] == '<') {
            op = BgFetchRule::size_cmp_type::LESS_THAN_OR_EQUAL;
          } else if (cfg_value[0] == '>') {
            op = BgFetchRule::size_cmp_type::LESS_THAN_OR_EQUAL;
          } else {
            TSError("[%s] invalid Content-Length condition %.*s, skipping config value", PLUGIN_NAME, int(cfg_value.size()),
                    cfg_value.data());
            continue;
          }
          ++cfg_value; // Drop leading character.
          swoc::TextView parsed;
          auto n = swoc::svtou(cfg_value, &parsed);
          if (parsed.size() != cfg_value.size()) {
            TSError("[%s] invalid Content-Length size value %.*s, skipping config value", PLUGIN_NAME, int(cfg_value.size()),
                    cfg_value.data());
            continue;
          }
          _rules.emplace_back(exclude, op, size_t(n));

          swoc::bwprint(ts::bw_dbg, "adding background_fetch content length rule {} for {}: {}", exclude, cfg_name, cfg_value);
          Dbg(Bg_dbg_ctl, "%s", ts::bw_dbg.c_str());
        } else {
          _rules.emplace_back(exclude, cfg_name, cfg_value);
          swoc::bwprint(ts::bw_dbg, "adding background_fetch field compare rule {} for {}: {}", exclude, cfg_name, cfg_value);
          Dbg(Bg_dbg_ctl, "%s", ts::bw_dbg.c_str());
        }
      } else {
        TSError("[%s] invalid value %.*s, skipping config line", PLUGIN_NAME, int(cfg_name.size()), cfg_name.data());
      }
    }
  }

  Dbg(Bg_dbg_ctl, "Done parsing config");

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Check the configuration (either per remap, or global), and decide if
// this request is allowed to trigger a background fetch.
//
bool
BgFetchConfig::bgFetchAllowed(TSHttpTxn txnp) const
{
  Dbg(Bg_dbg_ctl, "Testing: request is internal?");
  if (TSHttpTxnIsInternal(txnp)) {
    return false;
  }

  bool allow_bg_fetch = true;

  // We could do this recursively, but following the linked list is probably more efficient.
  for (auto const &r : _rules) {
    if (r.check_field_configured(txnp)) {
      Dbg(Bg_dbg_ctl, "found %s rule match", r._exclude ? "exclude" : "include");
      allow_bg_fetch = !r._exclude;
      break;
    }
  }

  return allow_bg_fetch;
}
