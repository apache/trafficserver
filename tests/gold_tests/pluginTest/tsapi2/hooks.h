/* Licensed to the Apache Software Foundation (ASF) under one
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

#include <cstring>

// Regression Test for API: TSHttpHookAdd
//                          TSHttpTxnReenable
//                          TSHttpTxnClientReqGet
//                          TSHttpTxnClientRespGet
//                          TSHttpTxnServerReqGet
//                          TSHttpTxnServerRespGet
//                          TSHttpTxnClientProtocolStackGet
//                          TSHttpTxnClientProtocolStackContains
//                          TSHttpTxnClientAddrGet
//                          TSHttpTxnIncomingAddrGet
//                          TSHttpTxnOutgoingAddrGet
//                          TSHttpTxnServerAddrGet
//                          TSHttpTxnNextHopAddrGet
//
namespace HooksTest
{
Logger log;

TSCont cont{nullptr};

struct ContData {
  int hook_mask{0};
  bool good{true};
  void
  test(bool result)
  {
    good = good && result;
  }
};

using _GetSockAddrFunc = sockaddr const *(*)(TSHttpTxn);

// If port parameter is 0, no port check.
//
bool
checkLoopbackSockAddr(TSHttpTxn txn, _GetSockAddrFunc func, char const *func_str, std::uint16_t port = 0)
{
  auto ptr = reinterpret_cast<sockaddr_in const *>(func(txn));
  if (!ptr) {
    log("%s : returns null", func_str);
    return false;
  }

  static in_addr_t const Loopback_ip = htonl(INADDR_LOOPBACK); /* 127.0.0.1 is expected */

  if ((AF_INET == ptr->sin_family) && (ptr->sin_addr.s_addr == Loopback_ip)) {
    log("%s : address ok", func_str);
  } else {
    log("%s : address values mismatch [expected %.8x got %.8x]", func_str, Loopback_ip, ptr->sin_addr.s_addr);
    return false;
  }
  if (port) {
    if (port == ntohs(ptr->sin_port)) {
      log("%s : port ok", func_str);
    } else {
      log("%s : port values mismatch [expected %u got %u]", func_str, port, ntohs(ptr->sin_port));
      return false;
    }
  }
  return true;
}

// This func is called by us from contFunc to check for TSHttpTxnClientProtocolStackGet
//
bool
checkHttpTxnClientProtocolStackGet(TSHttpTxn txn)
{
  const char *results[10];
  int count = 0;
  TSHttpTxnClientProtocolStackGet(txn, 10, results, &count);
  // Should return results[0] = "http/1.0", results[1] = "tcp", results[2] = "ipv4"
  if (count != 3) {
    log("TSHttpTxnClientProtocolStackGet : count should be 3 is %d", count);
  } else if (std::strcmp(results[0], "http/1.0") != 0) {
    log("TSHttpTxnClientProtocolStackGet : results[0] should be http/1.0 is %s", results[0]);
  } else if (std::strcmp(results[1], "tcp") != 0) {
    log("TSHttpTxnClientProtocolStackGet : results[1] should be tcp is %s", results[1]);
  } else if (std::strcmp(results[2], "ipv4") != 0) {
    log("TSHttpTxnClientProtocolStackGet : results[2] should be ipv4 is %s", results[2]);
  } else {
    log("TSHttpTxnClientProtocolStackGet : ok stack_size=%d", count);
    return true;
  }
  return false;
}

// This func is called by us from contFunc to check for TSHttpTxnClientProtocolStackContains
//
bool
checkHttpTxnClientProtocolStackContains(TSHttpTxn txn)
{
  bool result{true};
  const char *ret_tag = TSHttpTxnClientProtocolStackContains(txn, "tcp");
  if (ret_tag) {
    const char *normalized_tag = TSNormalizedProtocolTag("tcp");
    if (normalized_tag != ret_tag) {
      log("TSHttpTxnClientProtocolStackContains : contains tcp, but normalized tag is wrong");
      result = false;
    } else {
      log("TSHttpTxnClientProtocolStackContains : ok tcp");
    }
  } else {
    log("TSHttpTxnClientProtocolStackContains : missing tcp");
    result = false;
  }
  ret_tag = TSHttpTxnClientProtocolStackContains(txn, "udp");
  if (!ret_tag) {
    log("TSHttpTxnClientProtocolStackContains : ok no udp");
  } else {
    log("TSHttpTxnClientProtocolStackContains : faulty udp report");
    result = false;
  }
  return result;
}

// Depending on the timing of the DNS response, OS_DNS can happen before or after CACHE_LOOKUP.
//
int
contFunc(TSCont contp, TSEvent event, void *event_data)
{
  TSReleaseAssert(event_data != nullptr);

  auto txn = static_cast<TSHttpTxn>(event_data);

  if (GetTxnID(txn).txn_id() != "HOOKS") {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSReleaseAssert(contp == cont);

  auto data = static_cast<ContData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    if (data->hook_mask == 0) {
      data->hook_mask |= 1;
    }
  } break;

  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSSkipRemappingSet(txn, 1);
    if (data->hook_mask == 1) {
      data->hook_mask |= 2;
    }
    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnClientReqGet, "client request", 1));
  } break;

  case TS_EVENT_HTTP_OS_DNS: {
    if (data->hook_mask == 3 || data->hook_mask == 7) {
      data->hook_mask |= 8;
    }
    std::uint16_t src_port, proxy_port;

    yaml_data(src_port, "HOOKS_src_port");
    yaml_data(proxy_port, "proxy_port", "HOOKS", "txns");

    data->test(checkLoopbackSockAddr(txn, TSHttpTxnClientAddrGet, "TSHttpTxnClientAddrGet", src_port));
    data->test(checkLoopbackSockAddr(txn, TSHttpTxnIncomingAddrGet, "TSHttpTxnIncomingAddrGet", proxy_port));
    data->test(checkLoopbackSockAddr(txn, TSHttpTxnServerAddrGet, "TSHttpTxnServerAddrGet", server_port));
  } break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    if (data->hook_mask == 3 || data->hook_mask == 11) {
      data->hook_mask |= 4;
    }
  } break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    if (data->hook_mask == 15) {
      data->hook_mask |= 16;
    }
    data->test(checkLoopbackSockAddr(txn, TSHttpTxnOutgoingAddrGet, "TSHttpTxnOutgoingAddrGet"));

    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnServerReqGet, "request to server", 1));
    data->test(checkLoopbackSockAddr(txn, TSHttpTxnNextHopAddrGet, "TSHttpTxnNextHopAddrGet", server_port));
    data->test(checkHttpTxnClientProtocolStackContains(txn));
    data->test(checkHttpTxnClientProtocolStackGet(txn));
  } break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    if (data->hook_mask == 31) {
      data->hook_mask |= 32;
    }
    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnServerRespGet, "server response", 1, TS_HTTP_STATUS_OK));
  } break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    if (data->hook_mask == 63) {
      data->hook_mask |= 64;
    }

    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnClientRespGet, "response to client", 1, TS_HTTP_STATUS_OK));
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    if (data->hook_mask == 127) {
      log("TSHttpHookAdd : ok");

    } else {
      log("TSHttpHookAdd : Hooks not called or request failure. Hook mask = 0x%x", data->hook_mask);
    }

    if (!data->good) {
      log("TSHttpHookAdd : failed");
    }
    log.flush();
  } break;

  default:
    TSError("Unexpected event %d", event);
    TSReleaseAssert(false);
    break;
  }

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
init()
{
  log.open(run_dir_path + "/HooksTest.tlog");

  cont = TSContCreate(contFunc, nullptr);

  auto data = static_cast<ContData *>(TSmalloc(sizeof(ContData)));

  ::new (data) ContData;

  TSContDataSet(cont, data);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, cont);
}

void
cleanup()
{
  TSfree(TSContDataGet(cont));

  TSContDestroy(cont);

  log.close();
}

} // namespace HooksTest
