/** @file

  A plugin to redo cache lookups with a fallback if cache lookups fail for specific urls.

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

#include <iostream>
#include <regex>
#include <cstring>
#include <set>
#include <sstream>
#include <getopt.h>

#include <ts/ts.h>
#include <ts/experimental.h>
#include "tscpp/api/GlobalPlugin.h"
#include "tscpp/api/utils.h"

#define PLUGIN_NAME "redo_cache_lookup"

using namespace atscppapi;

namespace
{
GlobalPlugin *plugin;
}

class RedoCacheLookupPlugin : public GlobalPlugin
{
public:
  RedoCacheLookupPlugin(const char *fallback) : fallback(fallback)
  {
    TSDebug(PLUGIN_NAME, "registering transaction hooks");
    RedoCacheLookupPlugin::registerHook(HOOK_CACHE_LOOKUP_COMPLETE);
  }

  void
  handleReadCacheLookupComplete(Transaction &transaction) override
  {
    Transaction::CacheStatus status = transaction.getCacheStatus();

    if (status == Transaction::CacheStatus::CACHE_LOOKUP_NONE || status == Transaction::CacheStatus::CACHE_LOOKUP_SKIPED ||
        status == Transaction::CacheStatus::CACHE_LOOKUP_MISS) {
      TSDebug(PLUGIN_NAME, "rewinding to check for fallback url: %s", fallback);
      TSHttpTxn txnp = static_cast<TSHttpTxn>(transaction.getAtsHandle());
      TSHttpTxnRedoCacheLookup(txnp, fallback, strlen(fallback));
    }

    transaction.resume();
  }

private:
  const char *fallback;
};

void
TSPluginInit(int argc, const char *argv[])
{
  TSDebug(PLUGIN_NAME, "Init");
  if (!RegisterGlobalPlugin("RedoCacheLookupPlugin", PLUGIN_NAME, "dev@trafficserver.apache.org")) {
    return;
  }

  const char *fallback = nullptr;

  // Read options from plugin.config
  static const struct option longopts[] = {{"fallback", required_argument, nullptr, 'f'}};

  int opt = 0;

  while (opt >= 0) {
    opt = getopt_long(argc, const_cast<char *const *>(argv), "f:", longopts, nullptr);
    switch (opt) {
    case 'f':
      fallback = optarg;
      break;
    case -1:
    case '?':
      break;
    default:
      TSDebug(PLUGIN_NAME, "Unexpected option: %i", opt);
      TSError("[%s] Unexpected options error.", PLUGIN_NAME);
      return;
    }
  }

  if (nullptr == fallback) {
    TSDebug(PLUGIN_NAME, "Missing fallback option.");
    TSError("[%s] Missing fallback option", PLUGIN_NAME);
    return;
  }
  TSDebug(PLUGIN_NAME, "Initialized with fallback: %s", fallback);

  plugin = new RedoCacheLookupPlugin(fallback);
}
