/**
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
#include "../proxy/api/ts/ts.h"
#include "../lib/ts/apidefs.h"

#include <atscppapi/GlobalPlugin.h>
#include <atscppapi/Logger.h>
#include <atscppapi/PluginInit.h>
#include <atscppapi/utils.h>

using namespace atscppapi;

using std::cout;
using std::endl;
using std::list;
using std::string;

namespace
{
Logger log;
GlobalPlugin *plugin;
}

class SslCloseHookPlugin : public GlobalPlugin
{
public:
  SslCloseHookPlugin()
  {
    registerHook(HOOK_READ_REQUEST_HEADERS_PRE_REMAP);
  }

  void
  handleReadRequestHeadersPreRemap(Transaction &transaction) override
  {
    TSHttpTxn txn = static_cast<TSHttpTxn>(transaction.getAtsHandle());
    TSHttpSsn ssn = TSHttpTxnSsnGet(txn);
    TSVConn connp = TSHttpSsnClientVConnGet(ssn);
    char *user_data = (char *) TSVConnGetUserData(connp, "ssl-close-hook");

    log.logInfo("connection user data: %s", user_data);
    transaction.resume();
  }
};

int
SslCloseHookCallback(TSCont contp, TSEvent event, void *edata)
  // Clears app data stored in the
{
  TSVConn ssl_vc      = reinterpret_cast<TSVConn>(edata);
  char* user_data      = (char *) TSVConnGetUserData(ssl_vc, "ssl-close-hook");

  if (user_data) {
    log.logInfo("Freeing user data. %s", user_data);
    TSfree(user_data);
  }

  return 0;   // re-enable not needed.
}

int
SslPreAccepCallback(TSCont contp, TSEvent /*event*/, void *edata)
{
  const int kBufSize = 64;
  char* user_data = (char *) TSmalloc(kBufSize);
  TSVConn ssl_vc = reinterpret_cast<TSVConn>(edata);

  snprintf(user_data, 64, "!!!! Test user data !!!!");
  if (!TSVConnSetUserData(ssl_vc, "ssl-close-hook", user_data)) {
    log.logError("Failed to set user data");
    TSfree(user_data);
  }

  log.logInfo("Successfully set user data for vconn");
  TSVConnReenable(ssl_vc);
  return 0;
}

void
TSPluginInit(int argc ATSCPPAPI_UNUSED, const char *argv[] ATSCPPAPI_UNUSED)
{
  RegisterGlobalPlugin("CPP_Example_SslCloseHook", "apache", "dev@trafficserver.apache.org");
  log.init("ssl_close_hook", true, true, Logger::LOG_LEVEL_DEBUG, true, 3600);

  plugin = new SslCloseHookPlugin();
  TSHttpHookAdd(TS_VCONN_PRE_ACCEPT_HOOK, TSContCreate(&SslPreAccepCallback, TSMutexCreate()));
  TSHttpHookAdd(TS_SSL_CLOSE_HOOK, TSContCreate(&SslCloseHookCallback, TSMutexCreate()));
  log.logInfo("CPP_Example_SslCloseHook initialized");
}
