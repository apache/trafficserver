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

// Parse the command line options. This got a little wonky, since we decided to have different
// syntax for remap vs global plugin initialization, and the global BG fetch state :-/. Clean up
// later...
bool
BgFetchConfig::parseOptions(int argc, const char *argv[])
{
  static const struct option longopt[] = {{const_cast<char *>("log"), required_argument, nullptr, 'l'},
                                          {const_cast<char *>("config"), required_argument, nullptr, 'c'},
                                          {const_cast<char *>("allow-304"), no_argument, nullptr, 'a'},
                                          {nullptr, no_argument, nullptr, '\0'}};

  while (true) {
    int opt = getopt_long(argc, const_cast<char *const *>(argv), "lc", longopt, nullptr);

    if (opt == -1) {
      break;
    }

    switch (opt) {
    case 'l':
      TSDebug(PLUGIN_NAME, "option: log file specified: %s", optarg);
      _log_file = optarg;
      break;
    case 'c':
      TSDebug(PLUGIN_NAME, "option: config file '%s'", optarg);
      if (!readConfig(optarg)) {
        // Error messages are written in the parser
        return false;
      }
      break;
    case 'a':
      TSDebug(PLUGIN_NAME, "option: --allow-304 set");
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
  char file_path[4096];
  TSFile file;

  if (nullptr == config_file) {
    TSError("[%s] invalid config file", PLUGIN_NAME);
    return false;
  }

  TSDebug(PLUGIN_NAME, "trying to open config file in this path: %s", config_file);

  if (*config_file == '/') {
    snprintf(file_path, sizeof(file_path), "%s", config_file);
  } else {
    snprintf(file_path, sizeof(file_path), "%s/%s", TSConfigDirGet(), config_file);
  }
  TSDebug(PLUGIN_NAME, "chosen config file is at: %s", file_path);

  file = TSfopen(file_path, "r");
  if (nullptr == file) {
    TSError("[%s] invalid config file:  %s", PLUGIN_NAME, file_path);
    TSDebug(PLUGIN_NAME, "invalid config file: %s", file_path);
    return false;
  }

  BgFetchRule *cur = nullptr;
  char buffer[8192];

  memset(buffer, 0, sizeof(buffer));
  while (TSfgets(file, buffer, sizeof(buffer) - 1) != nullptr) {
    char *eol = nullptr;

    // make sure line was not bigger than buffer
    if (nullptr == (eol = strchr(buffer, '\n')) && nullptr == (eol = strstr(buffer, "\r\n"))) {
      TSError("[%s] exclusion line too long, did not get a good line in cfg, skipping, line: %s", PLUGIN_NAME, buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    }
    // make sure line has something useful on it
    if (eol - buffer < 2 || buffer[0] == '#') {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }

    char *savePtr = nullptr;
    char *cfg     = strtok_r(buffer, "\n\r\n", &savePtr);

    if (nullptr != cfg) {
      TSDebug(PLUGIN_NAME, "setting background_fetch exclusion criterion based on string: %s", cfg);
      char *cfg_type  = strtok_r(buffer, " ", &savePtr);
      char *cfg_name  = nullptr;
      char *cfg_value = nullptr;

      if (cfg_type) {
        bool exclude = false;
        if (!strcmp(cfg_type, "exclude")) {
          exclude = true;
        } else if (strcmp(cfg_type, "include")) {
          TSError("[%s] invalid specifier %s, skipping config line", PLUGIN_NAME, cfg_type);
          memset(buffer, 0, sizeof(buffer));
          continue;
        }
        cfg_name = strtok_r(nullptr, " ", &savePtr);
        if (cfg_name) {
          cfg_value = strtok_r(nullptr, " ", &savePtr);
          if (cfg_value) {
            if (!strcmp(cfg_name, "Content-Length")) {
              if ((cfg_value[0] != '<') && (cfg_value[0] != '>')) {
                TSError("[%s] invalid content-len condition %s, skipping config value", PLUGIN_NAME, cfg_value);
                memset(buffer, 0, sizeof(buffer));
                continue;
              }
            }
            BgFetchRule *r = new BgFetchRule(exclude, cfg_name, cfg_value);

            if (nullptr == cur) {
              _rules = r;
            } else {
              cur->chain(r);
            }
            cur = r;

            TSDebug(PLUGIN_NAME, "adding background_fetch exclusion rule %d for %s: %s", exclude, cfg_name, cfg_value);
          } else {
            TSError("[%s] invalid value %s, skipping config line", PLUGIN_NAME, cfg_name);
          }
        }
      }
      memset(buffer, 0, sizeof(buffer));
    }
  }

  TSfclose(file);
  TSDebug(PLUGIN_NAME, "Done parsing config");

  return true;
}

///////////////////////////////////////////////////////////////////////////
// Check the configuration (either per remap, or global), and decide if
// this request is allowed to trigger a background fetch.
//
bool
BgFetchConfig::bgFetchAllowed(TSHttpTxn txnp) const
{
  TSDebug(PLUGIN_NAME, "Testing: request is internal?");
  if (TSHttpTxnIsInternal(txnp)) {
    return false;
  }

  bool allow_bg_fetch = true;

  // We could do this recursively, but following the linked list is probably more efficient.
  for (const BgFetchRule *r = _rules; nullptr != r; r = r->_next) {
    if (r->check_field_configured(txnp)) {
      TSDebug(PLUGIN_NAME, "found field match %s, exclude %d", r->_field, static_cast<int>(r->_exclude));
      allow_bg_fetch = !r->_exclude;
      break;
    }
  }

  return allow_bg_fetch;
}
