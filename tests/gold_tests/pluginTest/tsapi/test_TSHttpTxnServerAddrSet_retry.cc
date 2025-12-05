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
 * Test plugin to reproduce issue #12611
 *
 * This plugin sets server addresses via TSHttpTxnServerAddrSet() in the OS_DNS hook.
 * On first call, it sets a non-routable address that will fail to connect.
 * On retry (if OS_DNS is called again), it sets a working address.
 *
 * BUG: On master, the OS_DNS hook is NOT called again on retry, so the connection
 * keeps failing with the bad address.
 */

#include <ts/ts.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstdint>
#include <cstdlib>

namespace
{

DbgCtl dbg_ctl{"test_TSHttpTxnServerAddrSet_retry"};

// Transaction argument index for per-transaction call count
int g_txn_arg_idx = -1;

/** Get the OS_DNS call count for this transaction */
int
get_txn_call_count(TSHttpTxn txnp)
{
  return static_cast<int>(reinterpret_cast<intptr_t>(TSUserArgGet(txnp, g_txn_arg_idx)));
}

/** Set the OS_DNS call count for this transaction */
void
set_txn_call_count(TSHttpTxn txnp, int count)
{
  TSUserArgSet(txnp, g_txn_arg_idx, reinterpret_cast<void *>(static_cast<intptr_t>(count)));
}

/** Handler for OS_DNS hook */
int
handle_os_dns(TSCont /* cont */, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_OS_DNS) {
    TSError("[TSHttpTxnServerAddrSet_retry] Unexpected event in OS_DNS handler: %d", event);
    return TS_ERROR;
  }

  TSHttpTxn txnp = (TSHttpTxn)edata;

  // Increment per-transaction call count
  int call_count = get_txn_call_count(txnp) + 1;
  set_txn_call_count(txnp, call_count);

  Dbg(dbg_ctl, "OS_DNS hook called, count=%d", call_count);
  TSError("[TSHttpTxnServerAddrSet_retry] OS_DNS hook called, count=%d", call_count);

  // First call: set a bad address (192.0.2.1 is TEST-NET-1, non-routable)
  // Second call: set a good address (localhost)
  const char *addr_str;
  int         port;

  if (call_count == 1) {
    addr_str = "192.0.2.1"; // Non-routable - will fail
    port     = 80;
    TSError("[TSHttpTxnServerAddrSet_retry] Attempt 1: Setting BAD address %s:%d (will fail)", addr_str, port);
  } else {
    addr_str = "127.0.0.1"; // Localhost - should work
    port     = 8080;
    TSError("[TSHttpTxnServerAddrSet_retry] Attempt %d: Setting GOOD address %s:%d (should work)", call_count, addr_str, port);
  }

  // Create sockaddr
  struct sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port   = htons(port);
  if (inet_pton(AF_INET, addr_str, &sa.sin_addr) != 1) {
    TSError("[TSHttpTxnServerAddrSet_retry] Invalid address %s", addr_str);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }

  // Set the server address
  if (TSHttpTxnServerAddrSet(txnp, reinterpret_cast<struct sockaddr const *>(&sa)) != TS_SUCCESS) {
    TSError("[TSHttpTxnServerAddrSet_retry] Failed to set server address to %s:%d", addr_str, port);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_ERROR;
  }

  Dbg(dbg_ctl, "Set server address to %s:%d", addr_str, port);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

/** Handler for TXN_CLOSE hook - report results */
int
handle_txn_close(TSCont /* cont */, TSEvent event, void *edata)
{
  if (event != TS_EVENT_HTTP_TXN_CLOSE) {
    TSError("[TSHttpTxnServerAddrSet_retry] Unexpected event in TXN_CLOSE handler: %d", event);
    return TS_ERROR;
  }

  TSHttpTxn txnp = (TSHttpTxn)edata;

  // Get the per-transaction call count
  int call_count = get_txn_call_count(txnp);

  TSError("[TSHttpTxnServerAddrSet_retry] Transaction closing. OS_DNS was called %d time(s)", call_count);

  if (call_count == 1) {
    TSError("[TSHttpTxnServerAddrSet_retry] *** BUG CONFIRMED: OS_DNS hook was only called ONCE. "
            "Plugin could not retry with different address. This is issue #12611. ***");
  } else if (call_count >= 2) {
    TSError("[TSHttpTxnServerAddrSet_retry] SUCCESS: OS_DNS hook was called %d times. "
            "Plugin was able to retry with different address.",
            call_count);
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

} // anonymous namespace

void
TSPluginInit(int /* argc */, char const * /* argv */[])
{
  Dbg(dbg_ctl, "Initializing plugin to reproduce issue #12611");
  TSError("[TSHttpTxnServerAddrSet_retry] Plugin initialized - will test TSHttpTxnServerAddrSet retry behavior");

  TSPluginRegistrationInfo info;
  info.plugin_name   = "test_TSHttpTxnServerAddrSet_retry";
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";
  TSReleaseAssert(TSPluginRegister(&info) == TS_SUCCESS);

  // Reserve a transaction argument slot for per-transaction call count
  TSReleaseAssert(TSUserArgIndexReserve(TS_USER_ARGS_TXN, "test_TSHttpTxnServerAddrSet_retry", "OS_DNS call count",
                                        &g_txn_arg_idx) == TS_SUCCESS);

  TSCont os_dns_cont = TSContCreate(handle_os_dns, nullptr);
  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, os_dns_cont);

  TSCont close_cont = TSContCreate(handle_txn_close, nullptr);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, close_cont);
}
