/** @file

  A test plugin that rejects every TLS connection by reenabling a TLS hook with
  TS_EVENT_ERROR.

  Options:
    --hook=<vconn_start|client_hello|cert>  The hook to reject at (default vconn_start).
    --async                                 Reenable from a scheduled continuation rather
                                            than from within the hook.

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

#include <string_view>

#include <ts/ts.h>

namespace
{
constexpr char PLUGIN_NAME[] = "tls_hook_reject";

DbgCtl dbg_ctl{PLUGIN_NAME};

bool reject_async = false;

int
reject_later(TSCont cont, TSEvent /* event */, void * /* edata */)
{
  auto vconn = static_cast<TSVConn>(TSContDataGet(cont));

  Dbg(dbg_ctl, "Rejecting connection %p asynchronously", vconn);
  TSVConnReenableEx(vconn, TS_EVENT_ERROR);
  TSContDestroy(cont);
  return TS_SUCCESS;
}

int
handle_tls_hook(TSCont /* cont */, TSEvent /* event */, void *edata)
{
  auto vconn = static_cast<TSVConn>(edata);

  if (reject_async) {
    TSCont later = TSContCreate(reject_later, TSMutexCreate());

    TSContDataSet(later, vconn);
    TSContScheduleOnPool(later, 100, TS_THREAD_POOL_NET);
    return TS_SUCCESS;
  }

  Dbg(dbg_ctl, "Rejecting connection %p", vconn);
  TSVConnReenableEx(vconn, TS_EVENT_ERROR);
  return TS_SUCCESS;
}

} // namespace

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSHttpHookID hook = TS_VCONN_START_HOOK;

  for (int i = 1; i < argc; ++i) {
    std::string_view const arg{argv[i]};

    if (arg == "--async") {
      reject_async = true;
    } else if (arg == "--hook=vconn_start") {
      hook = TS_VCONN_START_HOOK;
    } else if (arg == "--hook=client_hello") {
      hook = TS_SSL_CLIENT_HELLO_HOOK;
    } else if (arg == "--hook=cert") {
      hook = TS_SSL_CERT_HOOK;
    } else {
      TSError("[%s] Unknown argument: %s", PLUGIN_NAME, argv[i]);
    }
  }

  TSHttpHookAdd(hook, TSContCreate(handle_tls_hook, nullptr));
}
