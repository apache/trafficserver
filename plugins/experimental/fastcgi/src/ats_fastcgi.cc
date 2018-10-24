/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ats_fastcgi.h"

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/InterceptPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <iostream>
#include <netinet/in.h>
#include <string.h>

#include "ts/ink_defs.h"
#include "ts/ts.h"
#include "utils_internal.h"

#include "fcgi_config.h"
#include "server_intercept.h"
#include "server.h"
#include <regex>

#if ATS_FCGI_PROFILER
#include "Profiler.h"
#define CONFIGURU_IMPLEMENTATION 1
#include "configuru.hpp"
using namespace configuru;
#endif

using namespace atscppapi;

using std::cout;
using std::endl;
using std::string;

namespace InterceptGlobal
{
GlobalPlugin *plugin;
ats_plugin::InterceptPluginData *plugin_data;
ats_plugin::Server *gServer;
int reqBegId, reqEndId, respBegId, respEndId, threadCount, phpConnCount;
thread_local pthread_key_t threadKey = 0;
#if ATS_FCGI_PROFILER
ats_plugin::Profiler profiler;
#endif
} // namespace InterceptGlobal
// For experimental purpose to keep stats of plugin request/response
const char reqBegName[]  = "plugin." PLUGIN_NAME ".reqCountBeg";
const char reqEndName[]  = "plugin." PLUGIN_NAME ".reqCountEnd";
const char respBegName[] = "plugin." PLUGIN_NAME ".respCountBeg";
const char respEndName[] = "plugin." PLUGIN_NAME ".respCountEnd";
const char threadName[]  = "plugin." PLUGIN_NAME ".threadCount";
const char phpConnName[] = "plugin." PLUGIN_NAME ".phpConnCount";

using namespace InterceptGlobal;

class InterceptGlobalPlugin : public GlobalPlugin
{
public:
  InterceptGlobalPlugin() : GlobalPlugin(true)
  {
    // GlobalPlugin::registerHook(Plugin::HOOK_READ_REQUEST_HEADERS);
    GlobalPlugin::registerHook(Plugin::HOOK_CACHE_LOOKUP_COMPLETE);
  }

  void
  handleReadCacheLookupComplete(Transaction &transaction) override
  {
    if (transaction.getCacheStatus() == Transaction::CACHE_LOOKUP_HIT_FRESH) {
      transaction.resume();
      TSDebug(PLUGIN_NAME, " Cache hit.");
      return;
    }
    if (static_cast<TSHttpTxn>(transaction.getAtsHandle()) == nullptr) {
      TSDebug(PLUGIN_NAME, "Invalid Transaction.");
      return;
    }
    string path = transaction.getClientRequest().getUrl().getPath();
    // TODO: Regex based url selection
    std::smatch urlMatch;
    // std::regex e(".*.[wp*|php|js|css|scss|png|gif](p{2})?");
    std::regex e(".*");
    while (std::regex_search(path, urlMatch, e)) {
      // for (auto x : urlMatch)
      //   std::cout << x << " " << std::endl;
      // path = urlMatch.suffix().str();
      // std::cout << "Path:" << path << std::endl;
      break;
    }

    // if (path.find("php") != string::npos) {
    if (path.compare(urlMatch.str()) == 0) {
      TSStatIntIncrement(reqBegId, 1);
      auto intercept = new ats_plugin::ServerIntercept(transaction);

      if (threadKey == 0) {
        // setup thread local storage
        while (!gServer->setupThreadLocalStorage())
          ;
      }
      gServer->connect(intercept);

    } else {
      transaction.resume();
    }
  }
};

void
TSPluginInit(int argc, const char *argv[])
{
  RegisterGlobalPlugin(ATS_MODULE_FCGI_NAME, "apache", "dev@trafficserver.apache.org");
  plugin_data                          = new ats_plugin::InterceptPluginData();
  ats_plugin::FcgiPluginConfig *config = new ats_plugin::FcgiPluginConfig();
  if (argc > 1) {
    plugin_data->setGlobalConfigObj(config->initConfig(argv[1]));
  } else {
    plugin_data->setGlobalConfigObj(config->initConfig(nullptr));
  }
  ats_plugin::FcgiPluginConfig *gConfig = plugin_data->getGlobalConfigObj();
  if (gConfig->getFcgiEnabledStatus()) {
    plugin  = new InterceptGlobalPlugin();
    gServer = new ats_plugin::Server();

    if (TSStatFindName(reqBegName, &reqBegId) == TS_ERROR) {
      reqBegId = TSStatCreate(reqBegName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (reqBegId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, reqBegName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, reqBegName, reqBegId);

    if (TSStatFindName(reqEndName, &reqEndId) == TS_ERROR) {
      reqEndId = TSStatCreate(reqEndName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (reqEndId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, reqEndName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, reqEndName, reqEndId);

    if (TSStatFindName(respBegName, &respBegId) == TS_ERROR) {
      respBegId = TSStatCreate(respBegName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (respBegId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, respBegName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, respBegName, respBegId);

    if (TSStatFindName(respEndName, &respEndId) == TS_ERROR) {
      respEndId = TSStatCreate(respEndName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (respEndId == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, respEndName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, respEndName, respEndId);

    if (TSStatFindName(threadName, &threadCount) == TS_ERROR) {
      threadCount = TSStatCreate(threadName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (threadCount == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, threadName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, threadName, threadCount);

    if (TSStatFindName(phpConnName, &phpConnCount) == TS_ERROR) {
      phpConnCount = TSStatCreate(phpConnName, TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
      if (phpConnCount == TS_ERROR) {
        TSError("[%s] failed to register '%s'", PLUGIN_NAME, phpConnName);
        return;
      }
    }

    TSError("[%s] %s registered with id %d", PLUGIN_NAME, phpConnName, phpConnCount);

#if DEBUG
    TSReleaseAssert(reqBegId != TS_ERROR);
    TSReleaseAssert(reqEndId != TS_ERROR);
    TSReleaseAssert(respBegId != TS_ERROR);
    TSReleaseAssert(respEndId != TS_ERROR);
    TSReleaseAssert(threadCount != TS_ERROR);
    TSReleaseAssert(phpConnCount != TS_ERROR);
#endif
    // Set an initial value for our statistic.
    TSStatIntSet(reqBegId, 0);
    TSStatIntSet(reqEndId, 0);
    TSStatIntSet(respBegId, 0);
    TSStatIntSet(respEndId, 0);
    TSStatIntSet(threadCount, 0);
    TSStatIntSet(phpConnCount, 0);

  } else {
    TSDebug(PLUGIN_NAME, " plugin is disabled.");
  }
}
