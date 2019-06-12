/** @file

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

////////////////////////////////////////////////////////////////////////////////
// collapsed_forwarding::
//
// ATS plugin to allow collapsed forwarding of concurrent requests for the same
// object. This plugin is based on open_write_fail_action feature, which detects
// cache open write failure on a cache miss and returns a 502 error along with a
// special @-header indicating the reason for 502 error. The plugin acts on the
// error by using an internal redirect follow back to itself, essentially blocking
// the request until a response arrives, at which point, relies on read-while-writer
// feature to start downloading the object to all waiting clients. The following
// config parameters are assumed to be set for this plugin to work:
////////////////////////////////////////////////////////////////////////////////////
// proxy.config.http.cache.open_write_fail_action        1 /////////////////////////
// proxy.config.cache.enable_read_while_writer           1 /////////////////////////
// proxy.config.http.number_of_redirections             10 /////////////////////////
// proxy.config.http.redirect_use_orig_cache_key         1 /////////////////////////
// proxy.config.http.background_fill_active_timeout      0 /////////////////////////
// proxy.config.http.background_fill_completed_threshold 0 /////////////////////////
////////////////////////////////////////////////////////////////////////////////////
// Additionally, given that collapsed forwarding works based on cache write
// lock failure detection, the plugin requires cache to be enabled and ready.
// On a restart, Traffic Server typically takes a few seconds to initialize
// the cache depending on the cache size and number of dirents. While the
// cache is not ready yet, collapsed forwarding can not detect the write lock
// contention and so can not work. The setting proxy.config.http.wait_for_cache
// may be enabled which allows blocking incoming connections from being
// accepted until cache is ready.
////////////////////////////////////////////////////////////////////////////////////
// This plugin currently supports only per-remap mode activation.
////////////////////////////////////////////////////////////////////////////////////

#include <sys/time.h>
#include <ts/ts.h>
#include <ts/remap.h>
#include <set>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <getopt.h>
#include <netdb.h>
#include <map>

static const char *DEBUG_TAG = (char *)"collapsed_forwarding";

static const char *LOCATION_HEADER      = "Location";
static const char *REDIRECT_REASON      = "See Other";
static const char *ATS_INTERNAL_MESSAGE = "@Ats-Internal";

static int OPEN_WRITE_FAIL_MAX_REQ_DELAY_RETRIES = 5;
static int OPEN_WRITE_FAIL_REQ_DELAY_TIMEOUT     = 500;

static bool global_init = false;

typedef struct _RequestData {
  TSHttpTxn txnp;
  int wl_retry; // write lock failure retry count
  std::string req_url;
} RequestData;

static int
add_redirect_header(TSMBuffer &bufp, TSMLoc &hdr_loc, const std::string &location)
{
  // This is needed in case the response already contains a Location header
  TSMLoc field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, LOCATION_HEADER, strlen(LOCATION_HEADER));

  if (field_loc == TS_NULL_MLOC) {
    TSMimeHdrFieldCreateNamed(bufp, hdr_loc, LOCATION_HEADER, strlen(LOCATION_HEADER), &field_loc);
  }

  if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, location.c_str(), location.size())) {
    TSDebug(DEBUG_TAG, "Adding Location header %s", LOCATION_HEADER);
    TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
  }

  TSHandleMLocRelease(bufp, hdr_loc, field_loc);

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_SEE_OTHER);
  TSHttpHdrReasonSet(bufp, hdr_loc, REDIRECT_REASON, strlen(REDIRECT_REASON));
  return TS_SUCCESS;
}

static bool
check_internal_message_hdr(TSHttpTxn &txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  bool found = false;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("check_internal_message_hdr: couldn't retrieve client response header");
    return false;
  }

  TSMLoc header_loc = TSMimeHdrFieldFind(bufp, hdr_loc, ATS_INTERNAL_MESSAGE, strlen(ATS_INTERNAL_MESSAGE));
  if (header_loc) {
    found = true;
    // found the header, remove it now..
    TSMimeHdrFieldDestroy(bufp, hdr_loc, header_loc);
    TSHandleMLocRelease(bufp, hdr_loc, header_loc);
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

  return found;
}

static int
on_OS_DNS(const RequestData *req, TSHttpTxn &txnp)
{
  if (req->wl_retry > 0) {
    TSDebug(DEBUG_TAG, "OS_DNS request delayed %d times, block origin req for url: %s", req->wl_retry, req->req_url.c_str());
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_SUCCESS;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
on_send_request_header(const RequestData *req, TSHttpTxn &txnp)
{
  if (req->wl_retry > 0) {
    TSDebug(DEBUG_TAG, "Send_Req request delayed %d times, block origin req for url: %s", req->wl_retry, req->req_url.c_str());
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return TS_SUCCESS;
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
on_read_response_header(TSHttpTxn &txnp)
{
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
on_immediate(RequestData *req, TSCont &contp)
{
  if (!req) {
    TSError("%s: invalid req_data", DEBUG_TAG);
    return TS_SUCCESS;
  }

  TSDebug(DEBUG_TAG, "continuation delayed, scheduling now..for url: %s", req->req_url.c_str());

  // add retry_done header to prevent looping
  std::string value;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientRespGet(req->txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("plugin=%s, level=error, error_code=could_not_retrieve_client_response_header for url %s", DEBUG_TAG,
            req->req_url.c_str());
    TSHttpTxnReenable(req->txnp, TS_EVENT_HTTP_ERROR);
    return TS_SUCCESS;
  }

  add_redirect_header(bufp, hdr_loc, req->req_url);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(req->txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
on_send_response_header(RequestData *req, TSHttpTxn &txnp, TSCont &contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("plugin=%s, level=error, error_code=could_not_retrieve_client_response_header", DEBUG_TAG);
    return TS_SUCCESS;
  }

  TSHttpStatus status = TSHttpHdrStatusGet(bufp, hdr_loc);
  TSDebug(DEBUG_TAG, "Response code: %d", status);

  if ((status == TS_HTTP_STATUS_BAD_GATEWAY) || (status == TS_HTTP_STATUS_SEE_OTHER) ||
      status == TS_HTTP_STATUS_INTERNAL_SERVER_ERROR) {
    bool is_internal_message_hdr = check_internal_message_hdr(txnp);
    bool delay_request =
      is_internal_message_hdr || ((req->wl_retry > 0) && (req->wl_retry < OPEN_WRITE_FAIL_MAX_REQ_DELAY_RETRIES));

    if (delay_request) {
      req->wl_retry++;
      TSDebug(DEBUG_TAG, "delaying request, url@%p: {{%s}} on retry: %d time", txnp, req->req_url.c_str(), req->wl_retry);
      TSContScheduleOnPool(contp, OPEN_WRITE_FAIL_REQ_DELAY_TIMEOUT, TS_THREAD_POOL_TASK);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      return TS_SUCCESS;
    }
  }

  if (req->wl_retry > 0) {
    TSDebug(DEBUG_TAG, "request delayed, but unsuccessful, url@%p: {{%s}} on retry: %d time", txnp, req->req_url.c_str(),
            req->wl_retry);
    req->wl_retry = 0;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int
on_txn_close(RequestData *req, TSHttpTxn &txnp, TSCont &contp)
{
  // done..cleanup
  delete req;
  TSContDestroy(contp);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int collapsed_cont(TSCont contp, TSEvent event, void *edata);

void
setup_transaction_cont(TSHttpTxn rh)
{
  TSCont cont = TSContCreate(collapsed_cont, TSMutexCreate());

  RequestData *req_data = new RequestData();

  req_data->txnp     = rh;
  req_data->wl_retry = 0;

  int url_len = 0;
  char *url   = TSHttpTxnEffectiveUrlStringGet(rh, &url_len);
  req_data->req_url.assign(url, url_len);

  TSfree(url);
  TSContDataSet(cont, req_data);

  TSHttpTxnHookAdd(rh, TS_HTTP_SEND_REQUEST_HDR_HOOK, cont);
  TSHttpTxnHookAdd(rh, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(rh, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(rh, TS_HTTP_OS_DNS_HOOK, cont);
  TSHttpTxnHookAdd(rh, TS_HTTP_TXN_CLOSE_HOOK, cont);
}

static int
collapsed_cont(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp      = static_cast<TSHttpTxn>(edata);
  RequestData *my_req = static_cast<RequestData *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    // Create per transaction state
    setup_transaction_cont(txnp);
    break;

  case TS_EVENT_HTTP_OS_DNS: {
    return on_OS_DNS(my_req, txnp);
  }

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    return on_send_request_header(my_req, txnp);
  }

  case TS_EVENT_HTTP_READ_RESPONSE_HDR: {
    return on_read_response_header(txnp);
  }
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_TIMEOUT: {
    return on_immediate(my_req, contp);
  }
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: {
    return on_send_response_header(my_req, txnp, contp);
  }
  case TS_EVENT_HTTP_TXN_CLOSE: {
    return on_txn_close(my_req, txnp, contp);
  }
  default: {
    TSDebug(DEBUG_TAG, "Unexpected event: %d", event);
    break;
  }
  }

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

void
process_args(int argc, const char **argv)
{
  // basic argv processing..
  for (int i = 1; i < argc; ++i) {
    if (strncmp(argv[i], "--delay=", 8) == 0) {
      OPEN_WRITE_FAIL_REQ_DELAY_TIMEOUT = atoi((char *)(argv[i] + 8));
    } else if (strncmp(argv[i], "--retries=", 10) == 0) {
      OPEN_WRITE_FAIL_MAX_REQ_DELAY_RETRIES = atoi((char *)(argv[i] + 10));
    }
  }
}

/*
 * Initialize globally
 */
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;

  info.plugin_name   = (char *)DEBUG_TAG;
  info.vendor_name   = (char *)"Apache Software Foundation";
  info.support_email = (char *)"dev@trafficserver.apache.org";

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("[%s] Plugin registration failed", DEBUG_TAG);
  }

  process_args(argc, argv);

  TSCont cont = TSContCreate(collapsed_cont, TSMutexCreate());

  TSDebug(DEBUG_TAG, "Global Initialized");
  // Set up the per transaction state in the READ_REQUEST event
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, cont);

  global_init = true;
}

TSReturnCode
TSRemapInit(TSRemapInterface * /* api_info */, char * /* errbuf */, int /* errbuf_size */)
{
  if (global_init) {
    TSError("Cannot initialize %s as both global and remap plugin", DEBUG_TAG);
    return TS_ERROR;
  } else {
    TSDebug(DEBUG_TAG, "plugin is successfully initialized for remap");
    return TS_SUCCESS;
  }
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void ** /* ih */, char * /* errbuf */, int /* errbuf_size */)
{
  process_args(argc - 1, const_cast<const char **>(argv + 1));
  return TS_SUCCESS;
}

TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn rh, TSRemapRequestInfo *rri)
{
  setup_transaction_cont(rh);

  return TSREMAP_NO_REMAP;
}

void
TSRemapDeleteInstance(void *ih)
{
  // To resolve run time error
}
