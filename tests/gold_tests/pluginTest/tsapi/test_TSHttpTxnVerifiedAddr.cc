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

#include <fstream>
#include <cstdlib>
#include <arpa/inet.h>

#include <ts/ts.h>

namespace
{
#define PINAME "test_TSHttpTxnVerifiedAddr"
char PIName[] = PINAME;

DbgCtl dbg_ctl{PIName};

void
handle_txn_start(TSHttpTxn txn)
{
  struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port   = 0,
    .sin_addr   = {.s_addr = 0x01010101} // 1.1.1.1
  };
  TSHttpTxnVerifiedAddrSet(txn, reinterpret_cast<struct sockaddr *>(&addr));
  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
}

int
globalContFunc(TSCont, TSEvent event, void *eventData)
{
  Dbg(dbg_ctl, "Global: event=%s(%d) eventData=%p", TSHttpEventNameLookup(event), event, eventData);

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    handle_txn_start(static_cast<TSHttpTxn>(eventData));
    break;
  default:
    break;
  } // end switch

  return 0;
}

TSCont gCont;

} // end anonymous namespace

void
TSPluginInit(int /* argc ATS_UNUSED */, const char ** /* argv ATS_UNUSED */)
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = PIName;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError(PINAME ": Plugin registration failed");

    return;
  }

  TSMutex mtx = TSMutexCreate();
  gCont       = TSContCreate(globalContFunc, mtx);
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, gCont);
}
