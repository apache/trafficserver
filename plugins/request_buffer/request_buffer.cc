/* request_buffer.cc - Plugin to enable request buffer for the given transaction.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <string>

#include "ts/ts.h"
#include "ts/ink_defs.h"

#define PLUGIN_NAME "request_buffer"

static const int MIN_BYTE_PER_SEC = 1000;
static int TXN_INDEX_ARG_TIME;
struct TimeRecord {
  timespec start_time;
  TimeRecord() { clock_gettime(CLOCK_MONOTONIC, &start_time); }
};
bool
is_post_request(TSHttpTxn txnp)
{
  const char *method;
  int method_len;
  TSMLoc req_loc;
  TSMBuffer req_bufp;
  if (TSHttpTxnClientReqGet(txnp, &req_bufp, &req_loc) == TS_ERROR) {
    TSError("Error while retrieving client request header\n");
    return false;
  }
  method = TSHttpHdrMethodGet(req_bufp, req_loc, &method_len);
  if (static_cast<size_t>(method_len) != strlen(TS_HTTP_METHOD_POST) || strncasecmp(method, TS_HTTP_METHOD_POST, method_len) != 0) {
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
    return false;
  }
  TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);
  return true;
}
bool
reached_min_speed(TSHttpTxn txnp, int body_len)
{
  TimeRecord *timeRecord = (TimeRecord *)TSHttpTxnArgGet(txnp, TXN_INDEX_ARG_TIME);
  timespec now_time;
  clock_gettime(CLOCK_MONOTONIC, &now_time);
  double time_diff_in_sec =
    (now_time.tv_sec - timeRecord->start_time.tv_sec) + 1e-9 * (now_time.tv_nsec - timeRecord->start_time.tv_nsec);
  TSDebug("http", "time_diff_in_sec = %f, body_len = %d, date_rate = %f\n", time_diff_in_sec, body_len,
          body_len / time_diff_in_sec);
  return body_len / time_diff_in_sec >= MIN_BYTE_PER_SEC;
}
static int
hook_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)(edata);
  if (event == TS_EVENT_HTTP_READ_REQUEST_HDR && is_post_request(txnp)) {
    // enable the request body buffering
    TSHttpTxnConfigIntSet(txnp, TS_CONFIG_HTTP_REQUEST_BUFFER_ENABLED, 1);

    // save the start time for calculating the data rate
    TimeRecord *timeRecord = new TimeRecord();
    TSHttpTxnArgSet(txnp, TXN_INDEX_ARG_TIME, static_cast<void *>(timeRecord));

    TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_BUFFER_READ_HOOK, TSContCreate(hook_handler, TSMutexCreate()));
    TSHttpTxnHookAdd(txnp, TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK, TSContCreate(hook_handler, TSMutexCreate()));
    TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, TSContCreate(hook_handler, TSMutexCreate()));
  } else if (event == TS_EVENT_HTTP_REQUEST_BUFFER_READ || event == TS_EVENT_HTTP_REQUEST_BUFFER_COMPLETE) {
    int64_t ret_len = TSHttpTxnClientReqBodyBytesGet(txnp);
    if (event == TS_EVENT_HTTP_REQUEST_BUFFER_READ && !reached_min_speed(txnp, ret_len)) {
      TSError("[hook_handler] Error : reached_min_speed checking failed\n");
      TSHttpTxnReenable(txnp, TS_EVENT_ERROR);
      return 0;
    }

    // get the received request body
    TSIOBufferReader buffer_reader = TSHttpTxnGetClientRequestBufferReader(txnp);
    int64_t read_avail = TSIOBufferReaderAvail(buffer_reader);
    if (read_avail) {
      char *body = (char *)TSmalloc(sizeof(char) * read_avail);
      int64_t consumed = 0;
      int64_t data_len = 0;
      const char *char_data = NULL;
      TSIOBufferBlock block = TSIOBufferReaderStart(buffer_reader);
      while (block != NULL) {
        char_data = TSIOBufferBlockReadStart(block, buffer_reader, &data_len);
        memcpy(body + consumed, char_data, data_len);
        consumed += data_len;
        block = TSIOBufferBlockNext(block);
      }
      // play with the body
      // ...
      TSfree(body);
    }
  } else if (event == TS_EVENT_HTTP_TXN_CLOSE) {
    TimeRecord *timeRecord = (TimeRecord *)TSHttpTxnArgGet(txnp, TXN_INDEX_ARG_TIME);
    delete timeRecord;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name = (char *)PLUGIN_NAME;
  ;
  info.vendor_name = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";
  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[PluginInit] Plugin registration failed.\n");
  } else {
    if (TSHttpArgIndexReserve(PLUGIN_NAME, "Stores the transaction context", &TXN_INDEX_ARG_TIME) != TS_SUCCESS) {
      TSError("[PluginInit] failed to reserve an argument index");
    }
    TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(hook_handler, TSMutexCreate()));
  }
}
