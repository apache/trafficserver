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

#include "configs.h"

// Read a config file, populare the linked list (chain the BgFetchRule's)
bool
BgFetchConfig::readConfig(const char *config_file)
{
  char file_path[4096];
  TSFile file;

  if (NULL == config_file) {
    TSError("[%s] invalid config file", PLUGIN_NAME);
    return false;
  }

  TSDebug(PLUGIN_NAME, "trying to open config file in this path: %s", config_file);

  file = TSfopen(config_file, "r");
  if (NULL == file) {
    TSDebug(PLUGIN_NAME, "Failed to open config file %s, trying rel path", config_file);
    snprintf(file_path, sizeof(file_path), "%s/%s", TSInstallDirGet(), config_file);
    file = TSfopen(file_path, "r");
    if (NULL == file) {
      TSError("[%s] invalid config file", PLUGIN_NAME);
      return false;
    }
  }

  BgFetchRule *cur = NULL;
  char buffer[8192];

  memset(buffer, 0, sizeof(buffer));
  while (TSfgets(file, buffer, sizeof(buffer) - 1) != NULL) {
    char *eol = 0;

    // make sure line was not bigger than buffer
    if (NULL == (eol = strchr(buffer, '\n')) && NULL == (eol = strstr(buffer, "\r\n"))) {
      TSError("[%s] exclusion line too long, did not get a good line in cfg, skipping, line: %s", PLUGIN_NAME, buffer);
      memset(buffer, 0, sizeof(buffer));
      continue;
    }
    // make sure line has something useful on it
    if (eol - buffer < 2 || buffer[0] == '#') {
      memset(buffer, 0, sizeof(buffer));
      continue;
    }

    char *savePtr = NULL;
    char *cfg     = strtok_r(buffer, "\n\r\n", &savePtr);

    if (NULL != cfg) {
      TSDebug(PLUGIN_NAME, "setting background_fetch exclusion criterion based on string: %s", cfg);
      char *cfg_type  = strtok_r(buffer, " ", &savePtr);
      char *cfg_name  = NULL;
      char *cfg_value = NULL;
      bool exclude    = false;

      if (cfg_type) {
        if (!strcmp(cfg_type, "exclude")) {
          exclude = true;
        } else if (strcmp(cfg_type, "include")) {
          TSError("[%s] invalid specifier %s, skipping config line", PLUGIN_NAME, cfg_type);
          memset(buffer, 0, sizeof(buffer));
          continue;
        }
        cfg_name = strtok_r(NULL, " ", &savePtr);
        if (cfg_name) {
          cfg_value = strtok_r(NULL, " ", &savePtr);
          if (cfg_value) {
            if (!strcmp(cfg_name, "Content-Length")) {
              if ((cfg_value[0] != '<') && (cfg_value[0] != '>')) {
                TSError("[%s] invalid content-len condition %s, skipping config value", PLUGIN_NAME, cfg_value);
                memset(buffer, 0, sizeof(buffer));
                continue;
              }
            }
            BgFetchRule *r = new BgFetchRule(exclude, cfg_name, cfg_value);

            if (NULL == cur) {
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
