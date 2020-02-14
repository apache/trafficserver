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
//                    TSHttpAltInfoClientReqGet
//                    TSHttpAltInfoCachedReqGet
//                    TSHttpAltInfoCachedRespGet
//                    TSHttpAltInfoQualitySet
//
namespace AltInfoTest
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

  if (TS_EVENT_HTTP_SELECT_ALT == event) {
    auto info = static_cast<TSHttpAltInfo>(event_data);

    TSMBuffer client_req_bufp;
    TSMLoc client_req_mloc;

    if (TSHttpAltInfoClientReqGet(info, &client_req_bufp, &client_req_mloc) != TS_SUCCESS) {
      log("Unable to get handle to client request");

    } else {
      static char const Req_id_fld_name[] = "X-Request-ID";

      TSMLoc fld_loc = TSMimeHdrFieldFind(client_req_bufp, client_req_mloc, Req_id_fld_name, sizeof(Req_id_fld_name) - 1);
      if (TS_NULL_MLOC == fld_loc) {
        log("Unable to find %s field in client request", Req_id_fld_name);

      } else {
        if (TSMimeHdrFieldValuesCount(client_req_bufp, client_req_mloc, fld_loc) != 1) {
          log("Multiple values for %s field in client request", Req_id_fld_name);
        } else {
          int x_req_num = TSMimeHdrFieldValueIntGet(client_req_bufp, client_req_mloc, fld_loc, 0);

          if ((7 == x_req_num) || (8 == x_req_num)) {
            log("request id number = %d", x_req_num);

            auto data = static_cast<ContData *>(TSContDataGet(contp));
            data->test(checkHttpTxnReqOrResp(log, info, TSHttpAltInfoCachedReqGet, "alt info cached request", 6));
            data->test(
              checkHttpTxnReqOrResp(log, info, TSHttpAltInfoCachedRespGet, "alt info cached response", 6, TS_HTTP_STATUS_OK));

            if (7 == x_req_num) {
              // This function does not actually seem to do anything.
              TSHttpAltInfoQualitySet(info, 0.5);
              log("TSHttpAltInfoQualitySet(TSHttpAltInfo, 0.5)");
            }
          } else if ((6 == x_req_num) || (x_req_num < 0)) {
            log("bad request id number (%d)", x_req_num);
          }
        }
        TSReleaseAssert(TSHandleMLocRelease(client_req_bufp, client_req_mloc, fld_loc) == TS_SUCCESS);
      }
    }
    return 0;
  }

  auto txn = static_cast<TSHttpTxn>(event_data);

  auto txn_id = GetTxnID(txn).txn_id();
  if ((txn_id != "ALT_INFO1") && (txn_id != "ALT_INFO2") && (txn_id != "ALT_INFO3")) {
    TSHttpTxnReenable(txn, TS_EVENT_HTTP_CONTINUE);
    return 0;
  }

  TSReleaseAssert(contp == cont);

  auto data = static_cast<ContData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR: {
    TSHttpTxnHookAdd(txn, TS_HTTP_TXN_CLOSE_HOOK, cont);
    TSSkipRemappingSet(txn, 1);
  } break;

  case TS_EVENT_HTTP_TXN_CLOSE: {
    if ("ALT_INFO1" == txn_id) {
      TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, cont);
      log("Continuation added to TS_HTTP_SELECT_ALT_HOOK");
    }
    if ("ALT_INFO3" == txn_id) {
      log(data->good ? "Alt Info test ok" : "Alt Info test failed");
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
  log.open(run_dir_path + "/AltInfoTest.tlog");

  cont = TSContCreate(contFunc, nullptr);

  auto data = static_cast<ContData *>(TSmalloc(sizeof(ContData)));

  ::new (data) ContData;

  TSContDataSet(cont, data);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
}

void
cleanup()
{
  TSfree(TSContDataGet(cont));

  TSContDestroy(cont);

  log.close();
}

} // namespace AltInfoTest
