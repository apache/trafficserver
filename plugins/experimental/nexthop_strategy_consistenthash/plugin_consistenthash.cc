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
#include "consistenthash_config.h"
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

void
TSRemapDeleteInstance(void *ih)
{
  std::string *config_file_path = static_cast<std::string *>(ih);
  delete config_file_path;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  return TSREMAP_NO_REMAP;
}

// TODO figure out why TSRemapInitStrategy needs this, but nothing else does.
#ifdef __cplusplus
extern "C" {
#endif

TSReturnCode
TSRemapInitStrategy(TSNextHopSelectionStrategy *&strategy, void *ih, char *errbuf, int errbuf_size)
{
  NH_Debug(NH_DEBUG_TAG, "%s TSRemapInitStrategy called.", PLUGIN_NAME);
  if (ih == nullptr) {
    NH_Debug(NH_DEBUG_TAG, "%s TSRemapInitStrategy called with nullptr, returning null strategy.", PLUGIN_NAME);
    strategy = nullptr;
    return TS_SUCCESS; // TODO determine if we should return TS_ERROR here?
  }
  strategy = static_cast<TSNextHopSelectionStrategy *>(ih);
  NH_Debug(NH_DEBUG_TAG, "%s is successfully initialized.", PLUGIN_NAME);
  return TS_SUCCESS;
}

#ifdef __cplusplus
}
#endif

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  // TODO add ATS API Version check here, to bail if ATS doesn't support the version necessary for strategy plugins

  if (!api_info) {
    strncpy(errbuf, "[tsstrategy_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSStrategyInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  NH_Debug(NH_DEBUG_TAG, "%s is successfully initialized.", PLUGIN_NAME);
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuff, int errbuff_size)
{
  NH_Debug(NH_DEBUG_TAG, "%s TSRemapNewInstance called.", PLUGIN_NAME);
  for (int i = 0; i < argc; ++i) {
    NH_Debug(NH_DEBUG_TAG, "%s TSRemapNewInstance arg %d '%s'", PLUGIN_NAME, i, argv[i]);
  }

  if (argc < 3) {
    NH_Error("insufficient number of arguments, %d, no config file argument.", argc);
    return TS_ERROR;
  }

  if (argc < 3) {
    NH_Error("too many arguments, %d, only expected config file argument. Ignoring the rest!", argc);
  }

  const char* config_file_path = argv[2];

  NH_Debug(NH_DEBUG_TAG, "%s TSRemapInitStrategy called with path '%s'", PLUGIN_NAME, config_file_path);

  TSNextHopSelectionStrategy* strategy = createStrategyFromFile(config_file_path, PLUGIN_NAME);
  if (strategy == nullptr) {
    NH_Debug(NH_DEBUG_TAG, "%s failed to create strategy.", PLUGIN_NAME);
    *ih = nullptr;
    return TS_ERROR;
  }
  NH_Debug(NH_DEBUG_TAG, "%s successfully created strategy.", PLUGIN_NAME);
  *ih = static_cast<void *>(strategy);
  return TS_SUCCESS;
}
