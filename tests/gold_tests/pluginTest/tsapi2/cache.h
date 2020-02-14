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

// Unit Test for API: TSHttpTxnCachedReqGet
//                    TSHttpTxnCachedRespGet
//                    TSHttpTxnCacheLookupStatusGet
//
namespace CacheTest
{
Logger log;

TSCont cont{nullptr};

struct ContData {
  bool good{true};
  void
  test(bool result)
  {
    good = good && result;
  }
};

int
contFunc(TSCont contp, TSEvent event, void *event_data)
{
  TSReleaseAssert(event_data != nullptr);

  auto txn = static_cast<TSHttpTxn>(event_data);

  auto txn_id = GetTxnID(txn).txn_id();
  if ((txn_id != "CACHE") && (txn_id != "CACHE_DUP")) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSReleaseAssert(contp == cont);

  auto data = static_cast<ContData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSSkipRemappingSet(txn, 1);
  } break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE: {
    int lookup_status;
    if (TSHttpTxnCacheLookupStatusGet(txn, &lookup_status) != TS_SUCCESS) {
      data->good = false;
      log("TSHttpTxnCacheLookupStatusGet() doesn't return TS_SUCCESS");

    } else if ("CACHE" == txn_id) {
      if (lookup_status == TS_CACHE_LOOKUP_MISS) {
        log("TSHttpTxnCacheLookupStatusGet() ok (miss)");
      } else {
        data->good = false;
        log("TSHttpTxnCacheLookupStatusGet() did not return miss -- error");
      }
    } else {
      if (lookup_status == TS_CACHE_LOOKUP_HIT_FRESH) {
        log("TSHttpTxnCacheLookupStatusGet() ok (hit)");
      } else {
        data->good = false;
        log("TSHttpTxnCacheLookupStatusGet() did not return fresh hit -- error");
      }
    }
  } break;

  case TS_EVENT_HTTP_READ_CACHE_HDR: {
    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnCachedReqGet, "cached request", 2));
    data->test(checkHttpTxnReqOrResp(log, txn, TSHttpTxnCachedRespGet, "cached response", 2, TS_HTTP_STATUS_OK));
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    if ("CACHE_DUP" == txn_id) {
      log(data->good ? "cache test ok" : "cache test failed");
    }
    log.flush();
  } break;

  default: {
    TSError("Unexpected event %d", event);
    TSReleaseAssert(false);
  } break;
  } // end switch

  TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
init()
{
  log.open(run_dir_path + "/CacheTest.tlog");

  cont = TSContCreate(contFunc, nullptr);

  auto data = static_cast<ContData *>(TSmalloc(sizeof(ContData)));

  ::new (data) ContData;

  TSContDataSet(cont, data);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
  /* Register to HTTP hooks that are called in case of a cache MISS */
  TSHttpHookAdd(TS_HTTP_READ_CACHE_HDR_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, cont);
  TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, cont);
}

void
cleanup()
{
  TSfree(TSContDataGet(cont));

  TSContDestroy(cont);

  log.close();
}

} // namespace CacheTest
