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

/**
 * @file crash_test.cc
 * @brief A plugin that intentionally crashes traffic_server for testing
 *        the crash log functionality.
 *
 * This plugin is for TESTING ONLY - do not use in production!
 *
 * When a request contains the header "X-Crash-Test: now", this plugin
 * will dereference a null pointer, causing a SIGSEGV.
 */

#include <ts/ts.h>
#include <cstring>
#include <cstdlib>

#define PLUGIN_NAME "crash_test"

namespace
{
DbgCtl dbg_ctl{PLUGIN_NAME};

int
handle_read_request(TSCont /* contp */, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);

  if (event != TS_EVENT_HTTP_READ_REQUEST_HDR) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMBuffer bufp;
  TSMLoc    hdr_loc;

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, "X-Crash-Test", -1);
  if (field_loc != TS_NULL_MLOC) {
    int         value_len = 0;
    char const *value     = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &value_len);

    if (value != nullptr && value_len == 3 && strncmp(value, "now", 3) == 0) {
      TSNote("Received crash trigger header - crashing now!");

      // Intentionally crash by dereferencing a null pointer.
      volatile int *null_ptr = nullptr;
      *null_ptr              = 42;
      TSNote("This should never be reached.");
    }

    TSHandleMLocRelease(bufp, hdr_loc, field_loc);
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

} // anonymous namespace

void
TSPluginInit(int /* argc */, char const ** /* argv */)
{
  Dbg(dbg_ctl, "initializing crash_test plugin");

  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(PLUGIN_NAME);
  info.vendor_name   = const_cast<char *>("Apache");
  info.support_email = const_cast<char *>("dev@trafficserver.apache.org");

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
    return;
  }

  TSCont contp = TSContCreate(handle_read_request, nullptr);
  if (contp == nullptr) {
    TSError("[%s] Failed to create continuation", PLUGIN_NAME);
    return;
  }

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
  Dbg(dbg_ctl, "crash_test plugin initialized - send 'X-Crash-Test: now' header to trigger crash");
}
