/** @file

  ats_ssl_plugin.cc - plugin setup

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

#include <cstdio>
#include <openssl/ssl.h>
#include <ts/ts.h>
#include <ts/apidefs.h>

#include "common.h"
#include "ssl_utils.h"

PluginThreads plugin_threads;

static int
shutdown_handler(TSCont contp, TSEvent event, void *edata)
{
  if (event == TS_EVENT_LIFECYCLE_SHUTDOWN) {
    plugin_threads.terminate();
  }
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)("ats_session_reuse");
  info.vendor_name   = (char *)("ats");
  info.support_email = (char *)("ats-devel@verizonmedia.com");

  TSLifecycleHookAdd(TS_LIFECYCLE_SHUTDOWN_HOOK, TSContCreate(shutdown_handler, nullptr));

#if (TS_VERSION_NUMBER >= 7000000)
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("Plugin registration failed.");
  }
#else
  if (TSPluginRegister(TS_SDK_VERSION_3_0, &info) != TS_SUCCESS) {
    TSError("Plugin registration failed.");
  }
#endif
  if (argc < 2) {
    TSError("Must specify config file.");
  } else if (!init_ssl_params(argv[1])) {
    init_subscriber();
    TSCont cont = TSContCreate(SSL_session_callback, nullptr);
    TSDebug(PLUGIN, "TSPluginInit adding TS_SSL_SESSION_HOOK.");
    TSHttpHookAdd(TS_SSL_SESSION_HOOK, cont);
  } else {
    TSError("init_ssl_params failed.");
  }
}
