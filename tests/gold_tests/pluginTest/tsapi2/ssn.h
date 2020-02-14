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

// Unit Test for API: TSHttpSsnHookAdd
//                    TSHttpSsnReenable
//                    TSHttpTxnHookAdd
//                    TSHttpTxnErrorBodySet
//                    TSHttpTxnParentProxyGet
//                    TSHttpTxnParentProxySet
//                    TSHttpTxnSsnGet
namespace SsnTest
{
Logger log;

TSCont cont{nullptr};

struct ContData {
  TSHttpSsn ssn{nullptr};
  int hooks_added{0};
  int hooks_triggered{0};
  bool good{true};

  void
  test(bool result)
  {
    good = good && result;
  }
};

bool
checkHttpTxnParentProxy(TSHttpTxn txn)
{
  char const *hostname    = "txnpp.example.com";
  char const *hostnameget = nullptr;
  int portget;

  TSHttpTxnParentProxySet(txn, const_cast<char *>(hostname), 0xdead);
  if (TSHttpTxnParentProxyGet(txn, &hostnameget, &portget) != TS_SUCCESS) {
    log("TSHttpTxnParentProxyGet doesn't return TS_SUCCESS");
  } else if ((std::strcmp(hostname, hostnameget) != 0) || (portget != 0xdead)) {
    log("TSHttpTxnParentProxyGet returns hostname=%s, port=0x%x should be %s, 0xdead", hostnameget, portget, hostname);
  } else {
    return true;
  }

  return false;
}

int
contFunc(TSCont contp, TSEvent event, void *event_data)
{
  TSReleaseAssert(event_data != nullptr);

  if (TS_EVENT_HTTP_SSN_START == event) {
    auto ssn = static_cast<TSHttpSsn>(event_data);
    if (GetTxnID(ssn).txn_id() == "SSN") {
      TSReleaseAssert(contp == cont);

      log("SSN_START hook trigger -- ok");

      auto data = static_cast<ContData *>(TSContDataGet(contp));
      ++data->hooks_triggered;
      data->ssn = ssn;
      TSHttpSsnHookAdd(ssn, TS_HTTP_TXN_START_HOOK, contp);
      ++data->hooks_added;
    }
    TSHttpSsnReenable(ssn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  auto txn = static_cast<TSHttpTxn>(event_data);

  if (GetTxnID(txn).txn_id() != "SSN") {
    log("Failure -- SSN test continuation is not global for event %d", static_cast<int>(event));
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSReleaseAssert(contp == cont);

  auto data = static_cast<ContData *>(TSContDataGet(contp));

  ++data->hooks_triggered;

  if (TSHttpTxnSsnGet(txn) != data->ssn) {
    log("TSHttpTxnSsnGet failed");
  }

  switch (event) {
  case TS_EVENT_HTTP_TXN_START: {
    log("TXN_START hook trigger -- ok");

    TSSkipRemappingSet(txn, 1);

    TSHttpTxnHookAdd(txn, TS_HTTP_OS_DNS_HOOK, contp);
    ++data->hooks_added;
  } break;

  case TS_EVENT_HTTP_OS_DNS: {
    log("OS_DNS hook trigger -- ok");

    TSHttpTxnHookAdd(txn, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    ++data->hooks_added;

    data->test(checkHttpTxnParentProxy(txn));

  } break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    log("SEND_RESPONSE_HDR hook trigger -- ok");

    TSHttpTxnStatusSet(txn, TS_HTTP_STATUS_INTERNAL_SERVER_ERROR);
    static char const Error_body[] = "TESTING ERROR PAGE";
    TSHttpTxnErrorBodySet(txn, TSstrdup(Error_body), sizeof(Error_body) - 1, nullptr);

    TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, contp);
    ++data->hooks_added;

    TSHttpTxnReenable(txn, TS_EVENT_HTTP_ERROR);
  }
    return 0;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    log("TXN_CLOSE hook trigger -- ok");

    if (data->hooks_triggered != data->hooks_added) {
      log("Failure : API hooks triggered (%d) not equal to API hooks added (%d)", data->hooks_triggered, data->hooks_added);
    }
    if (!data->good) {
      log("SSN test : failed");
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
  log.open(run_dir_path + "/SsnTest.tlog");

  cont = TSContCreate(contFunc, nullptr);

  auto data = static_cast<ContData *>(TSmalloc(sizeof(ContData)));

  ::new (data) ContData;

  TSContDataSet(cont, data);

  /* Register to HTTP hooks that are called in case of a cache MISS */
  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, cont);
  ++data->hooks_added;
}

void
cleanup()
{
  TSfree(TSContDataGet(cont));

  TSContDestroy(cont);

  log.close();
}

} // namespace SsnTest
