/** @file

  Implements RFC 5861 (HTTP Cache-Control Extensions for Stale Content)

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

#include "stale_response.h"
#include "BodyData.h"
#include "CacheUpdate.h"
#include "DirectiveParser.h"
#include "MurmurHash3.h"
#include "ServerIntercept.h"

#include "swoc/TextView.h"
#include "ts/apidefs.h"
#include "ts/remap.h"
#include "ts/remap_version.h"
#include "ts_wrap.h"

#include <arpa/inet.h>
#include <cinttypes>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <getopt.h>
#include <string>

using namespace std;

const char PLUGIN_TAG[]     = "stale_response";
const char PLUGIN_TAG_BAD[] = "stale_response_bad";

DEF_DBG_CTL(PLUGIN_TAG)
DEF_DBG_CTL(PLUGIN_TAG_BAD)
DEF_DBG_CTL(PLUGIN_TAG_BODY)

static const char VENDOR_NAME[]   = "Apache Software Foundation";
static const char SUPPORT_EMAIL[] = "dev@trafficserver.apache.org";

static const char HTTP_VALUE_STALE_WARNING[]    = "110 Response is stale";
static const char SIE_SERVER_INTERCEPT_HEADER[] = "@X-CCExtensions-Sie-Intercept";
static const char HTTP_VALUE_SERVER_INTERCEPT[] = "1";

/*-----------------------------------------------------------------------------------------------*/
static ResponseInfo *
create_response_info(void)
{
  ResponseInfo *resp_info = static_cast<ResponseInfo *>(TSmalloc(sizeof(ResponseInfo)));

  resp_info->http_hdr_buf = TSMBufferCreate();
  resp_info->http_hdr_loc = TSHttpHdrCreate(resp_info->http_hdr_buf);
  resp_info->parser       = TSHttpParserCreate();
  resp_info->parsed       = false;

  return resp_info;
}

/*-----------------------------------------------------------------------------------------------*/
static void
free_response_info(ResponseInfo *resp_info)
{
  TSHandleMLocRelease(resp_info->http_hdr_buf, TS_NULL_MLOC, resp_info->http_hdr_loc);
  TSMBufferDestroy(resp_info->http_hdr_buf);
  TSHttpParserDestroy(resp_info->parser);
  TSfree(resp_info);
}

/*-----------------------------------------------------------------------------------------------*/
static RequestInfo *
create_request_info(TSHttpTxn txnp)
{
  RequestInfo *req_info = static_cast<RequestInfo *>(TSmalloc(sizeof(RequestInfo)));

  TSMBuffer hdr_url_buf;
  TSMLoc    hdr_url_loc;

  TSHttpTxnClientReqGet(txnp, &hdr_url_buf, &hdr_url_loc);

  // this only seems to be correct/consistent if done in http read request header state
  char *url               = TSHttpTxnEffectiveUrlStringGet(txnp, &req_info->effective_url_length);
  req_info->effective_url = TSstrndup(url, req_info->effective_url_length);
  TSfree(url);

  // copy the headers
  req_info->http_hdr_buf = TSMBufferCreate();
  TSHttpHdrClone(req_info->http_hdr_buf, hdr_url_buf, hdr_url_loc, &(req_info->http_hdr_loc));
  // release the client request
  TSHandleMLocRelease(hdr_url_buf, TS_NULL_MLOC, hdr_url_loc);

  // It turns out that the client_addr field isn't used if the request
  // is internal.  Cannot fetch a real client address in that case anyway
  if (!TSHttpTxnIsInternal(txnp)) {
    // copy client addr
    req_info->client_addr = reinterpret_cast<struct sockaddr *>(TSmalloc(sizeof(struct sockaddr)));
    memmove(req_info->client_addr, TSHttpTxnClientAddrGet(txnp), sizeof(struct sockaddr));
  } else {
    req_info->client_addr = nullptr;
  }

  // create the lookup key fron the effective url
  MurmurHash3_x86_32(req_info->effective_url, req_info->effective_url_length, c_hashSeed, &(req_info->key_hash));

  TSDebug(PLUGIN_TAG, "[%s] {%u} url=[%s]", __FUNCTION__, req_info->key_hash, req_info->effective_url);

  return req_info;
}

/*-----------------------------------------------------------------------------------------------*/
static void
free_request_info(RequestInfo *req_info)
{
  TSfree(req_info->effective_url);
  TSHandleMLocRelease(req_info->http_hdr_buf, TS_NULL_MLOC, req_info->http_hdr_loc);
  TSMBufferDestroy(req_info->http_hdr_buf);
  TSfree(req_info->client_addr);
  TSfree(req_info);
}

/*-----------------------------------------------------------------------------------------------*/
static StateInfo *
create_state_info(TSHttpTxn txnp, TSCont contp)
{
  StateInfo *state = new StateInfo(txnp, contp);
  state->req_info  = create_request_info(txnp);
  return state;
}

/*-----------------------------------------------------------------------------------------------*/
static void
free_state_info(StateInfo *state)
{
  // clean up states copy of url
  if (state->pristine_url) {
    TSfree(state->pristine_url);
  }
  state->pristine_url = nullptr;

  // bunch of buffers state has created
  if (state->req_io_buf_reader) {
    TSIOBufferReaderFree(state->req_io_buf_reader);
  }
  state->req_io_buf_reader = nullptr;
  if (state->req_io_buf) {
    TSIOBufferDestroy(state->req_io_buf);
  }
  state->req_io_buf = nullptr;
  if (state->resp_io_buf_reader) {
    TSIOBufferReaderFree(state->resp_io_buf_reader);
  }
  state->resp_io_buf_reader = nullptr;
  if (state->resp_io_buf) {
    TSIOBufferDestroy(state->resp_io_buf);
  }
  state->resp_io_buf = nullptr;

  // dont think these need cleanup
  // state->r_vio
  // state->w_vio
  // state->vconn

  // clean up request and response data state created
  if (state->req_info) {
    free_request_info(state->req_info);
  }
  state->req_info = nullptr;
  if (state->resp_info) {
    free_response_info(state->resp_info);
  }
  state->resp_info = nullptr;

  // this should be null but check and delete
  if (state->sie_body) {
    delete state->sie_body;
  }
  state->sie_body = nullptr;

  // this is a temp pointer do not delete
  // state->cur_save_body

  TSfree(state);
}

/*-----------------------------------------------------------------------------------------------*/
int64_t
aync_memory_total_add(ConfigInfo *plugin_config, int64_t change)
{
  int64_t total;
  TSMutexLock(plugin_config->body_data_mutex);
  plugin_config->body_data_memory_usage += change;
  total                                  = plugin_config->body_data_memory_usage;
  TSMutexUnlock(plugin_config->body_data_mutex);
  return total;
}
/*-----------------------------------------------------------------------------------------------*/
inline int64_t
aync_memory_total_get(ConfigInfo *plugin_config)
{
  return aync_memory_total_add(plugin_config, 0);
}

/*-----------------------------------------------------------------------------------------------*/
BodyData *
async_check_active(uint32_t key_hash, ConfigInfo *plugin_config)
{
  BodyData *pFound = nullptr;
  TSMutexLock(plugin_config->body_data_mutex);
  UintBodyMap::iterator pos = plugin_config->body_data->find(key_hash);
  if (pos != plugin_config->body_data->end()) {
    pFound = pos->second;
    ;
  }
  TSMutexUnlock(plugin_config->body_data_mutex);

  TSDebug(PLUGIN_TAG, "[%s] {%u} pFound=%p", __FUNCTION__, key_hash, pFound);
  return pFound;
}

/*-----------------------------------------------------------------------------------------------*/
bool
async_check_and_add_active(uint32_t key_hash, ConfigInfo *plugin_config)
{
  bool isNew = false;
  TSMutexLock(plugin_config->body_data_mutex);
  UintBodyMap::iterator pos = plugin_config->body_data->find(key_hash);
  if (pos == plugin_config->body_data->end()) {
    BodyData *pNew        = new BodyData();
    pNew->key_hash        = key_hash;
    pNew->key_hash_active = true;
    plugin_config->body_data->insert(make_pair(key_hash, pNew));
    isNew = true;
  }
  int tempSize = plugin_config->body_data->size();
  TSMutexUnlock(plugin_config->body_data_mutex);

  TSDebug(PLUGIN_TAG, "[%s] {%u} isNew=%d size=%d", __FUNCTION__, key_hash, isNew, tempSize);
  return isNew;
}

/*-----------------------------------------------------------------------------------------------*/
bool
add_header(TSMBuffer &reqp, TSMLoc &hdr_loc, string header, string value)
{
  bool bReturn = false;
  if (value.size() <= 0) {
    TSDebug(PLUGIN_TAG, "\tWould set header %s to an empty value, skipping", header.c_str());
  } else {
    TSMLoc new_field;

    if (TS_SUCCESS == TSMimeHdrFieldCreateNamed(reqp, hdr_loc, header.data(), header.size(), &new_field)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringInsert(reqp, hdr_loc, new_field, -1, value.data(), value.size())) {
        if (TS_SUCCESS == TSMimeHdrFieldAppend(reqp, hdr_loc, new_field)) {
          TSDebug(PLUGIN_TAG, "\tAdded header %s: %s", header.c_str(), value.c_str());
          bReturn = true;
        }
      } else {
        TSMimeHdrFieldDestroy(reqp, hdr_loc, new_field);
      }
      TSHandleMLocRelease(reqp, hdr_loc, new_field);
    }
  }
  return bReturn;
}
/*-----------------------------------------------------------------------------------------------*/
bool
async_remove_active(uint32_t key_hash, ConfigInfo *plugin_config)
{
  bool wasActive = false;
  TSMutexLock(plugin_config->body_data_mutex);
  UintBodyMap::iterator pos = plugin_config->body_data->find(key_hash);
  if (pos != plugin_config->body_data->end()) {
    plugin_config->body_data_memory_usage -= (pos->second)->getSize();
    delete pos->second;
    plugin_config->body_data->erase(pos);
    wasActive = true;
  }
  int tempSize = plugin_config->body_data->size();
  TSMutexUnlock(plugin_config->body_data_mutex);

  TSDebug(PLUGIN_TAG, "[%s] {%u} wasActive=%d size=%d", __FUNCTION__, key_hash, wasActive, tempSize);
  return wasActive;
}
/*-----------------------------------------------------------------------------------------------*/
bool
async_intercept_active(uint32_t key_hash, ConfigInfo *plugin_config)
{
  bool interceptActive = false;
  TSMutexLock(plugin_config->body_data_mutex);
  UintBodyMap::iterator pos = plugin_config->body_data->find(key_hash);
  if (pos != plugin_config->body_data->end()) {
    interceptActive = pos->second->intercept_active;
  }
  TSMutexUnlock(plugin_config->body_data_mutex);

  TSDebug(PLUGIN_TAG, "[%s] {%u} interceptActive=%d", __FUNCTION__, key_hash, interceptActive);
  return interceptActive;
}

/*-----------------------------------------------------------------------------------------------*/
void
send_stale_response(StateInfo *state)
{
  // force to use age header
  TSHttpTxnConfigIntSet(state->txnp, TS_CONFIG_HTTP_INSERT_AGE_IN_RESPONSE, 1);
  // add send response header hook for warning header
  TSHttpTxnHookAdd(state->txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, state->transaction_contp);
  // set cache as fresh
  TSHttpTxnCacheLookupStatusSet(state->txnp, TS_CACHE_LOOKUP_HIT_FRESH);
}

/*-----------------------------------------------------------------------------------------------*/
static CachedHeaderInfo *
get_cached_header_info(StateInfo *state)
{
  TSHttpTxn         txnp = state->txnp;
  CachedHeaderInfo *chi;
  TSMBuffer         cr_buf;
  TSMLoc            cr_hdr_loc, cr_date_loc, cr_cache_control_loc, cr_cache_control_dup_loc;
  int               cr_cache_control_count = 0;

  chi = static_cast<CachedHeaderInfo *>(TSmalloc(sizeof(CachedHeaderInfo)));

  chi->date    = 0;
  chi->max_age = 0;

  // -1 is used as a placeholder for the following two meaning that their
  // respective directives were not in the Cache-Control header.
  chi->stale_while_revalidate = -1;
  chi->stale_if_error         = -1;

  if (TSHttpTxnCachedRespGet(txnp, &cr_buf, &cr_hdr_loc) == TS_SUCCESS) {
    cr_date_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_DATE, TS_MIME_LEN_DATE);
    if (cr_date_loc != TS_NULL_MLOC) {
      chi->date = TSMimeHdrFieldValueDateGet(cr_buf, cr_hdr_loc, cr_date_loc);
      TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_date_loc);
    }

    cr_cache_control_loc = TSMimeHdrFieldFind(cr_buf, cr_hdr_loc, TS_MIME_FIELD_CACHE_CONTROL, TS_MIME_LEN_CACHE_CONTROL);

    while (cr_cache_control_loc != TS_NULL_MLOC) {
      cr_cache_control_count = TSMimeHdrFieldValuesCount(cr_buf, cr_hdr_loc, cr_cache_control_loc);

      DirectiveParser directives;
      for (int i = 0; i < cr_cache_control_count; ++i) {
        int         val_len = 0;
        char const *v       = TSMimeHdrFieldValueStringGet(cr_buf, cr_hdr_loc, cr_cache_control_loc, i, &val_len);
        TSDebug(PLUGIN_TAG, "Processing directives: %.*s", val_len, v);
        swoc::TextView cache_control_value{v, static_cast<size_t>(val_len)};
        directives.merge(DirectiveParser{cache_control_value});
      }
      TSDebug(PLUGIN_TAG, "max-age: %ld, stale-while-revalidate: %ld, stale-if-error: %ld", directives.get_max_age(),
              directives.get_stale_while_revalidate(), directives.get_stale_if_error());
      if (directives.get_max_age() >= 0) {
        chi->max_age = directives.get_max_age();
      }
      if (directives.get_stale_while_revalidate() >= 0) {
        chi->stale_while_revalidate = directives.get_stale_while_revalidate();
      }
      if (directives.get_stale_if_error() >= 0) {
        chi->stale_if_error = directives.get_stale_if_error();
      }

      cr_cache_control_dup_loc = TSMimeHdrFieldNextDup(cr_buf, cr_hdr_loc, cr_cache_control_loc);
      TSHandleMLocRelease(cr_buf, cr_hdr_loc, cr_cache_control_loc);
      cr_cache_control_loc = cr_cache_control_dup_loc;
    }
    TSHandleMLocRelease(cr_buf, TS_NULL_MLOC, cr_hdr_loc);
  }

  TSDebug(PLUGIN_TAG, "[%s] item_count=%d max_age=%ld swr=%ld sie=%ld", __FUNCTION__, cr_cache_control_count, chi->max_age,
          chi->stale_while_revalidate, chi->stale_if_error);

  // load the config mins/default
  if ((chi->stale_if_error == -1) && state->plugin_config->stale_if_error_default) {
    chi->stale_if_error = state->plugin_config->stale_if_error_default;
  }

  if (state->plugin_config->stale_if_error_override > chi->stale_if_error) {
    chi->stale_if_error = state->plugin_config->stale_if_error_override;
  }

  if ((chi->stale_while_revalidate == -1) && state->plugin_config->stale_while_revalidate_default) {
    chi->stale_while_revalidate = state->plugin_config->stale_while_revalidate_default;
  }

  if (state->plugin_config->stale_while_revalidate_override > chi->stale_while_revalidate) {
    chi->stale_while_revalidate = state->plugin_config->stale_while_revalidate_override;
  }

  // The callers use the stale-while-revalidate and stale-if-error values for
  // calulations and do not expect nor need -1 values for non-existent
  // directives as we did above. Now that we've handled the user configured
  // defaults, we can assume "not set" is a value of 0.
  chi->stale_while_revalidate = std::max(chi->stale_while_revalidate, 0l);
  chi->stale_if_error         = std::max(chi->stale_if_error, 0l);

  TSDebug(PLUGIN_TAG, "[%s] after defaults item_count=%d max_age=%ld swr=%ld sie=%ld", __FUNCTION__, cr_cache_control_count,
          chi->max_age, chi->stale_while_revalidate, chi->stale_if_error);

  return chi;
}

/*-----------------------------------------------------------------------------------------------*/
static void
fetch_save_response(StateInfo *state, BodyData *pBody)
{
  TSIOBufferBlock block;
  int64_t         avail;
  char const     *start;
  block = TSIOBufferReaderStart(state->resp_io_buf_reader);
  while (block != nullptr) {
    start = TSIOBufferBlockReadStart(block, state->resp_io_buf_reader, &avail);
    if (avail > 0) {
      pBody->addChunk(start, avail);
      // increase body_data_memory_usage only if content stored in plugin_config->body_data
      if (pBody->key_hash_active) {
        aync_memory_total_add(state->plugin_config, avail);
      }
    }
    block = TSIOBufferBlockNext(block);
  }
}

/*-----------------------------------------------------------------------------------------------*/
static void
fetch_parse_response(StateInfo *state)
{
  TSIOBufferBlock block;
  TSParseResult   pr = TS_PARSE_CONT;
  int64_t         avail;
  char const     *start;

  block = TSIOBufferReaderStart(state->resp_io_buf_reader);

  while ((pr == TS_PARSE_CONT) && (block != nullptr)) {
    start = TSIOBufferBlockReadStart(block, state->resp_io_buf_reader, &avail);
    if (avail > 0) {
      pr = TSHttpHdrParseResp(state->resp_info->parser, state->resp_info->http_hdr_buf, state->resp_info->http_hdr_loc, &start,
                              (start + avail));
    }
    block = TSIOBufferBlockNext(block);
  }

  if (pr != TS_PARSE_CONT) {
    state->resp_info->status = TSHttpHdrStatusGet(state->resp_info->http_hdr_buf, state->resp_info->http_hdr_loc);
    state->resp_info->parsed = true;
    TSDebug(PLUGIN_TAG, "[%s] {%u} HTTP Status: %d", __FUNCTION__, state->req_info->key_hash, state->resp_info->status);
  }
}

/*-----------------------------------------------------------------------------------------------*/
static void
fetch_read_the_data(StateInfo *state)
{
  // always save data
  if (state->cur_save_body) {
    fetch_save_response(state, state->cur_save_body);
  } else {
    TSDebug(PLUGIN_TAG_BAD, "[%s] no BodyData", __FUNCTION__);
  }
  // get the resp code
  if (!state->resp_info->parsed) {
    fetch_parse_response(state);
  }
  // Consume data
  int64_t avail = TSIOBufferReaderAvail(state->resp_io_buf_reader);
  TSIOBufferReaderConsume(state->resp_io_buf_reader, avail);
  TSVIONDoneSet(state->r_vio, TSVIONDoneGet(state->r_vio) + avail);
}

/*-----------------------------------------------------------------------------------------------*/
static void
fetch_finish(StateInfo *state)
{
  TSDebug(PLUGIN_TAG, "[%s] {%u} swr=%d sie=%d", __FUNCTION__, state->req_info->key_hash, state->swr_active, state->sie_active);
  if (state->swr_active) {
    TSDebug(PLUGIN_TAG, "[%s] {%u} SWR Unlock URL / Post request", __FUNCTION__, state->req_info->key_hash);
    if (state->sie_active && valid_sie_status(state->resp_info->status)) {
      TSDebug(PLUGIN_TAG, "[%s] {%u} SWR Bad Data skipping", __FUNCTION__, state->req_info->key_hash);
      if (!async_remove_active(state->req_info->key_hash, state->plugin_config)) {
        TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} didnt delete async active", __FUNCTION__, state->req_info->key_hash);
      }
    } else {
      // this will place the new data in cache by server intercept
      intercept_fetch_the_url(state);
    }
  } else // state->sie_active
  {
    TSDebug(PLUGIN_TAG, "[%s] {%u} SIE in sync path Reenable %d", __FUNCTION__, state->req_info->key_hash,
            state->resp_info->status);
    if (valid_sie_status(state->resp_info->status)) {
      TSDebug(PLUGIN_TAG, "[%s] {%u} SIE sending stale data", __FUNCTION__, state->req_info->key_hash);
      if (state->plugin_config->log_info.object &&
          (state->plugin_config->log_info.all || state->plugin_config->log_info.stale_if_error)) {
        CachedHeaderInfo *chi = get_cached_header_info(state);
        TSTextLogObjectWrite(state->plugin_config->log_info.object, "stale-if-error: %ld - %ld < %ld + %ld %s", state->txn_start,
                             chi->date, chi->max_age, chi->stale_if_error, state->req_info->effective_url);
        TSfree(chi);
      }
      // send out the stale data
      send_stale_response(state);
    } else {
      TSDebug(PLUGIN_TAG, "[%s] SIE {%u} sending new data", __FUNCTION__, state->req_info->key_hash);
      // load the data as if we are OS by ServerIntercept
      BodyData *pBody = state->sie_body;
      state->sie_body = nullptr;
      // ServerIntercept will delete the body and send the data to the client
      // Add sie_server_intercept header.
      TSMBuffer buf;
      TSMLoc    hdr_loc;
      TSHttpTxnClientReqGet(state->txnp, &buf, &hdr_loc);
      if (!add_header(buf, hdr_loc, SIE_SERVER_INTERCEPT_HEADER, HTTP_VALUE_SERVER_INTERCEPT)) {
        TSError("stale_response [%s] error inserting header %s", __FUNCTION__, SIE_SERVER_INTERCEPT_HEADER);
      }
      TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
      serverInterceptSetup(state->txnp, pBody, state->plugin_config);
      // This was TSHttpTxnNewCacheLookupDo -- now we just use the copy we have
    }
    TSHttpTxnReenable(state->txnp, TS_EVENT_HTTP_CONTINUE);
  }
}

/*-----------------------------------------------------------------------------------------------*/
static int
fetch_consume(TSCont contp, TSEvent event, void *edata)
{
  StateInfo *state;
  state = static_cast<StateInfo *>(TSContDataGet(contp));

  switch (event) {
  case TS_EVENT_VCONN_WRITE_READY:
    // We shouldn't get here because we specify the exact size of the buffer.
    TSDebug(PLUGIN_TAG, "[%s] {%u} Write Ready", __FUNCTION__, state->req_info->key_hash);
    // fallthrough
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(PLUGIN_TAG, "[%s] {%u} Write Complete", __FUNCTION__, state->req_info->key_hash);
    break;

  case TS_EVENT_VCONN_READ_READY:
    // save the data and parse header if needed
    fetch_read_the_data(state);
    TSVIOReenable(state->r_vio);
    break;

  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
  case TS_EVENT_ERROR:
    // Don't free the reference to the state object
    // The txnp object may already be freed at this point
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      TSDebug(PLUGIN_TAG, "[%s] {%u} Inactivity Timeout", __FUNCTION__, state->req_info->key_hash);
      TSVConnAbort(state->vconn, TS_VC_CLOSE_ABORT);
    } else {
      if (event == TS_EVENT_VCONN_READ_COMPLETE) {
        TSDebug(PLUGIN_TAG, "[%s] {%u} Vconn Read Complete", __FUNCTION__, state->req_info->key_hash);
      } else if (event == TS_EVENT_VCONN_EOS) {
        TSDebug(PLUGIN_TAG, "[%s] {%u} Vconn Eos", __FUNCTION__, state->req_info->key_hash);
      } else if (event == TS_EVENT_ERROR) {
        TSDebug(PLUGIN_TAG, "[%s] {%u} Error Event", __FUNCTION__, state->req_info->key_hash);
      }
      TSVConnClose(state->vconn);
    }

    // I dont think we need this here but it should not hurt
    fetch_read_the_data(state);
    // we are done
    fetch_finish(state);
    // free state
    free_state_info(state);
    TSContDestroy(contp);
    break;

  default:
    TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} Unknown event %d.", __FUNCTION__, state->req_info->key_hash, event);
    break;
  }

  return 0;
}

/*-----------------------------------------------------------------------------------------------*/
static int
fetch_resource(TSCont contp, TSEvent, void *)
{
  StateInfo *state;
  TSCont     consume_contp;
  state = static_cast<StateInfo *>(TSContDataGet(contp));

  TSDebug(PLUGIN_TAG, "[%s] {%u} Start swr=%d sie=%d ", __FUNCTION__, state->req_info->key_hash, state->swr_active,
          state->sie_active);
  consume_contp = TSContCreate(fetch_consume, TSMutexCreate());
  TSContDataSet(consume_contp, state);

  // create the response info swr may use this
  state->resp_info = create_response_info();
  // force a connection close header here seems to be needed
  fix_connection_close(state);
  // create some buffers
  state->req_io_buf         = TSIOBufferCreate();
  state->req_io_buf_reader  = TSIOBufferReaderAlloc(state->req_io_buf);
  state->resp_io_buf        = TSIOBufferCreate();
  state->resp_io_buf_reader = TSIOBufferReaderAlloc(state->resp_io_buf);
  // add in my trailing parameter -- stripped off post cache lookup
  add_trailing_parameter(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc);
  // copy all the headers into a buffer
  TSHttpHdrPrint(state->req_info->http_hdr_buf, state->req_info->http_hdr_loc, state->req_io_buf);
  TSIOBufferWrite(state->req_io_buf, "\r\n", 2);

  // setup place to store body data
  if (state->sie_body) {
    state->cur_save_body = state->sie_body;
  } else {
    state->cur_save_body = async_check_active(state->req_info->key_hash, state->plugin_config);
  }

  // connect , setup read , write
  assert(state->req_info->client_addr != nullptr);
  state->vconn = TSHttpConnect(state->req_info->client_addr);
  state->r_vio = TSVConnRead(state->vconn, consume_contp, state->resp_io_buf, INT64_MAX);
  state->w_vio =
    TSVConnWrite(state->vconn, consume_contp, state->req_io_buf_reader, TSIOBufferReaderAvail(state->req_io_buf_reader));

  TSContDestroy(contp);

  return 0;
}

/*-----------------------------------------------------------------------------------------------*/
static void
fetch_start(StateInfo *state, TSCont contp)
{
  TSCont fetch_contp = nullptr;
  TSDebug(PLUGIN_TAG, "[%s] {%u} Start swr=%d sie=%d ", __FUNCTION__, state->req_info->key_hash, state->swr_active,
          state->sie_active);

  ConfigInfo *plugin_config = static_cast<ConfigInfo *>(TSContDataGet(contp));

  if (state->swr_active) {
    bool isNew = async_check_and_add_active(state->req_info->key_hash, state->plugin_config);
    // If already doing async lookup lets just close shop and go home
    if (!isNew && !plugin_config->force_parallel_async) {
      TSDebug(PLUGIN_TAG, "[%s] {%u} async in progress skip", __FUNCTION__, state->req_info->key_hash);
      TSStatIntIncrement(state->plugin_config->rfc_stat_swr_hit_skip, 1);
      // free state
      TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
      free_state_info(state);
    } else {
      // get the pristine url for the server intercept
      get_pristine_url(state);
      fetch_contp = TSContCreate(fetch_resource, TSMutexCreate());
      TSContDataSet(fetch_contp, state);
      TSContScheduleOnPool(fetch_contp, 0, TS_THREAD_POOL_NET);
    }
  } else // state->sie_active
  {
    state->sie_body = new BodyData();
    fetch_contp     = TSContCreate(fetch_resource, TSMutexCreate());
    TSContDataSet(fetch_contp, state);
    TSContScheduleOnPool(fetch_contp, 0, TS_THREAD_POOL_NET);
  }
}

/*-----------------------------------------------------------------------------------------------*/
static int
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpStatus http_status = TS_HTTP_STATUS_NONE;
  int          status      = 0;
  StateInfo   *state       = nullptr;
  TSMBuffer    buf;
  TSMLoc       loc;

  TSHttpTxn const txnp          = static_cast<TSHttpTxn>(edata);
  ConfigInfo     *plugin_config = static_cast<ConfigInfo *>(TSContDataGet(contp));
  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:
    TSDebug(PLUGIN_TAG, "[%s] TS_EVENT_HTTP_READ_REQUEST_HDR", __FUNCTION__);
    assert(false);
    break;

  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    state = static_cast<StateInfo *>(TSUserArgGet(txnp, plugin_config->txn_slot));

    // If the state has already gone, just move on
    if (!state) {
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      break;
    }

    // get the cache status default to miss
    if (TSHttpTxnCacheLookupStatusGet(txnp, &status) != TS_SUCCESS) {
      status = TS_CACHE_LOOKUP_MISS;
      TSDebug(PLUGIN_TAG_BAD, "[%s] TSHttpTxnCacheLookupStatusGet failed", __FUNCTION__);
    }

    if (TSHttpTxnIsInternal(txnp)) {
      bool cache_fresh = (status == TS_CACHE_LOOKUP_HIT_FRESH);
      TSDebug(PLUGIN_TAG, "[%s] {%u} CacheLookupComplete Internal fresh=%d", __FUNCTION__, state->req_info->key_hash, cache_fresh);

      // We dont want our internal requests to ever hit cache
      if (cache_fresh && state->intercept_request) {
        TSDebug(PLUGIN_TAG, "[%s] {%u} Set Cache to miss", __FUNCTION__, state->req_info->key_hash);
        if (TSHttpTxnCacheLookupStatusSet(txnp, TS_CACHE_LOOKUP_MISS) != TS_SUCCESS) {
          TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} TSHttpTxnCacheLookupStatusSet failed", __FUNCTION__, state->req_info->key_hash);
        }
      } else if (cache_fresh) // I dont think this can happen
      {
        TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} cache fresh not in stripped or intercept", __FUNCTION__, state->req_info->key_hash);
      }

      TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
      free_state_info(state);
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
      TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    } else {
      if (status == TS_CACHE_LOOKUP_HIT_STALE) {
        // Get headers setup chi -- free chi when done
        CachedHeaderInfo *chi = get_cached_header_info(state);
        state->swr_active =
          ((((state->txn_start - chi->date) + 1) < (chi->max_age + chi->stale_while_revalidate)) && chi->stale_while_revalidate);
        state->sie_active = ((((state->txn_start - chi->date) + 1) < (chi->max_age + chi->stale_if_error)) && chi->stale_if_error);
        state->over_max_memory = (aync_memory_total_get(plugin_config) > plugin_config->max_body_data_memory_usage);

        TSDebug(PLUGIN_TAG, "[%s] {%u} CacheLookup Stale swr=%d sie=%d over=%d", __FUNCTION__, state->req_info->key_hash,
                state->swr_active, state->sie_active, state->over_max_memory);
        // see if we are using too much memory and if so do not swr/sie
        if (state->over_max_memory) {
          TSDebug(PLUGIN_TAG, "[%s] {%u} Over memory Usage %" PRId64, __FUNCTION__, state->req_info->key_hash,
                  aync_memory_total_get(plugin_config));
          TSStatIntIncrement(state->plugin_config->rfc_stat_memory_over, 1);
        }

        if (state->swr_active) {
          TSDebug(PLUGIN_TAG, "[%s] {%u} swr return stale - async refresh", __FUNCTION__, state->req_info->key_hash);
          TSStatIntIncrement(plugin_config->rfc_stat_swr_hit, 1);
          if (plugin_config->log_info.object && (plugin_config->log_info.all || plugin_config->log_info.stale_while_revalidate)) {
            TSTextLogObjectWrite(plugin_config->log_info.object, "stale-while-revalidate: %ld - %ld < %ld + %ld [%s]",
                                 state->txn_start, chi->date, chi->max_age, chi->stale_while_revalidate,
                                 state->req_info->effective_url);
          }
          // send the data to the client
          send_stale_response(state);
          // do async if we are not over max
          if (!state->over_max_memory) {
            fetch_start(state, contp);
          } else {
            // since no fetch clean up state
            TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
            free_state_info(state);
          }
          // reenable here
          TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
        } else if (state->sie_active) {
          TSDebug(PLUGIN_TAG, "[%s] {%u} sie wait response - return stale if 50x", __FUNCTION__, state->req_info->key_hash);
          TSStatIntIncrement(plugin_config->rfc_stat_sie_hit, 1);
          // lookup sync
          if (!state->over_max_memory) {
            fetch_start(state, contp);
          } else // over max just send stale data reenable
          {
            send_stale_response(state);
            // since no fetch clean up state and reenable
            TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
            free_state_info(state);
            TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
          }
          // dont reenable here we are doing a sync call
        } else {
          // free state - reenable - had check
          TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
          free_state_info(state);
          TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
        }
        TSfree(chi);
      } else if (status != TS_CACHE_LOOKUP_HIT_FRESH) {
        TSDebug(PLUGIN_TAG, "[%s] {%u} CacheLookup Miss/Skipped", __FUNCTION__, state->req_info->key_hash);

        // this is just for stats
        if (async_check_active(state->req_info->key_hash, plugin_config) != nullptr) {
          TSDebug(PLUGIN_TAG, "[%s] {%u} not_stale aync in progress", __FUNCTION__, state->req_info->key_hash);
          TSStatIntIncrement(plugin_config->rfc_stat_swr_miss_locked, 1);
        }

        // strip the async if we missed the internal fake cache lookup -- ats just misses ?
        if (plugin_config->intercept_reroute) {
          TSMBuffer buf;
          TSMLoc    hdr_loc;
          TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc);
          if (strip_trailing_parameter(buf, hdr_loc)) {
            TSDebug(PLUGIN_TAG_BAD, "[%s] {%u} missed fake internal cache lookup", __FUNCTION__, state->req_info->key_hash);
          }
          TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
        }

        // free state - reenable - had check
        TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
        free_state_info(state);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      } else // TS_CACHE_LOOKUP_HIT_FRESH
      {
        TSDebug(PLUGIN_TAG, "[%s] {%u} CacheLookup Fresh", __FUNCTION__, state->req_info->key_hash);

        // free state - reenable - had check
        TSUserArgSet(state->txnp, state->plugin_config->txn_slot, nullptr);
        free_state_info(state);
        TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
      }
    }
    break;

  case TS_EVENT_HTTP_SEND_REQUEST_HDR: {
    TSDebug(PLUGIN_TAG, "[%s]: strip_trailing_parameter", __FUNCTION__);
    TSHttpTxnServerReqGet(txnp, &buf, &loc);
    strip_trailing_parameter(buf, loc);
    TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);

    // reenable
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  }

  break;

  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    // this should be internal request dont cache if valid sie error code -- no state variable
    TSHttpTxnServerRespGet(txnp, &buf, &loc);
    http_status = TSHttpHdrStatusGet(buf, loc);
    if (valid_sie_status(http_status)) {
      TSDebug(PLUGIN_TAG, "[%s] Set non-cachable %d", __FUNCTION__, http_status);
      TSHttpTxnServerRespNoStoreSet(txnp, 1);
    }
    TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    // add in the stale warning header -- no state variable
    TSDebug(PLUGIN_TAG, "[%s] set warning header", __FUNCTION__);
    TSHttpTxnClientRespGet(txnp, &buf, &loc);
    if (!add_header(buf, loc, TS_MIME_FIELD_WARNING, HTTP_VALUE_STALE_WARNING)) {
      TSError("stale_response [%s] error inserting header %s", __FUNCTION__, TS_MIME_FIELD_WARNING);
    }
    TSHandleMLocRelease(buf, TS_NULL_MLOC, loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  default:
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;
  }

  return 0;
}

ConfigInfo *
parse_args(int argc, char const *argv[])
{
  if (argc <= 1) {
    return nullptr;
  }
  ConfigInfo *plugin_config = new ConfigInfo();
  int         c;
  optind                                = 1;
  static const struct option longopts[] = {
    {"log-all",                        no_argument,       nullptr, 'a'},
    {"log-stale-while-revalidate",     no_argument,       nullptr, 'b'},
    {"log-stale-if-error",             no_argument,       nullptr, 'c'},
    {"log-filename",                   required_argument, nullptr, 'd'},

    {"force-stale-if-error",           required_argument, nullptr, 'e'},
    {"force-stale-while-revalidate",   required_argument, nullptr, 'f'},
    {"stale-if-error-default",         required_argument, nullptr, 'g'},
    {"stale-while-revalidate-default", required_argument, nullptr, 'h'},

    {"intercept-reroute",              no_argument,       nullptr, 'i'},
    {"max-memory-usage",               required_argument, nullptr, 'j'},
    {"force-parallel-async",           no_argument,       nullptr, 'k'},

    //  released a version that used "_" by mistake
    {"force_stale_if_error",           required_argument, nullptr, 'E'},
    {"force_stale_while_revalidate",   required_argument, nullptr, 'F'},
    {"stale_if_error_default",         required_argument, nullptr, 'G'},
    {"stale_while_revalidate_default", required_argument, nullptr, 'H'},

    {nullptr,                          0,                 nullptr, 0  }
  };

  TSDebug(PLUGIN_TAG, "[%s] [%s]", __FUNCTION__, argv[1]);
  while ((c = getopt_long(argc, const_cast<char *const *>(argv), "akref:EFGH:", longopts, nullptr)) != -1) {
    switch (c) {
    case 'a':
      plugin_config->log_info.all = true;
      break;
    case 'b':
      plugin_config->log_info.stale_while_revalidate = true;
      break;
    case 'c':
      plugin_config->log_info.stale_if_error = true;
      break;
    case 'd':
      plugin_config->log_info.filename = strdup(optarg);
      break;

    case 'e':
    case 'E':
      plugin_config->stale_if_error_override = atoi(optarg);
      break;
    case 'f':
    case 'F':
      plugin_config->stale_while_revalidate_override = atoi(optarg);
      break;
    case 'g':
    case 'G':
      plugin_config->stale_if_error_default = atoi(optarg);
      break;
    case 'h':
    case 'H':
      plugin_config->stale_while_revalidate_default = atoi(optarg);
      break;

    case 'i':
      plugin_config->intercept_reroute = true;
      break;

    case 'j':
      plugin_config->max_body_data_memory_usage = atoi(optarg);
      break;

    case 'k':
      plugin_config->force_parallel_async = true;
      break;

    default:
      break;
    }
  }

  if (plugin_config->log_info.all || plugin_config->log_info.stale_while_revalidate || plugin_config->log_info.stale_if_error) {
    TSDebug(PLUGIN_TAG, "[%s] Logging to %s", __FUNCTION__, plugin_config->log_info.filename);
    TSTextLogObjectCreate(plugin_config->log_info.filename, TS_LOG_MODE_ADD_TIMESTAMP, &(plugin_config->log_info.object));
  }

  TSDebug(PLUGIN_TAG, "[%s] global stale if error override = %d", __FUNCTION__, (int)plugin_config->stale_if_error_override);
  TSDebug(PLUGIN_TAG, "[%s] global stale while revalidate override = %d", __FUNCTION__,
          (int)plugin_config->stale_while_revalidate_override);
  TSDebug(PLUGIN_TAG, "[%s] global stale if error default = %d", __FUNCTION__, (int)plugin_config->stale_if_error_default);
  TSDebug(PLUGIN_TAG, "[%s] global stale while revalidate default = %d", __FUNCTION__,
          (int)plugin_config->stale_while_revalidate_default);
  TSDebug(PLUGIN_TAG, "[%s] global intercept reroute = %d", __FUNCTION__, (int)plugin_config->intercept_reroute);
  TSDebug(PLUGIN_TAG, "[%s] global force parallel async = %d", __FUNCTION__, (int)plugin_config->force_parallel_async);
  TSDebug(PLUGIN_TAG, "[%s] global max memory usage = %" PRId64, __FUNCTION__, plugin_config->max_body_data_memory_usage);

  return plugin_config;
}

static void
read_request_header_handler(TSHttpTxn const txnp, ConfigInfo *plugin_config)
{
  TSCont transaction_contp = TSContCreate(transaction_handler, nullptr);
  TSContDataSet(transaction_contp, plugin_config);
  // todo: move state create to not always happen -- issue: effective url string seems to change in dif states
  StateInfo *state = create_state_info(txnp, transaction_contp);
  TSUserArgSet(txnp, plugin_config->txn_slot, state);

  if (TSHttpTxnIsInternal(txnp)) {
    // This is insufficient if there are other plugins using TSHttpConnect
    TSDebug(PLUGIN_TAG, "[%s] {%u} ReadRequestHdr Internal", __FUNCTION__, state->req_info->key_hash);
    BodyData *pBody = intercept_check_request(state);
    if (pBody) {
      TSDebug(PLUGIN_TAG, "[%s] {%u} ReadRequestHdr Intercept", __FUNCTION__, state->req_info->key_hash);
      // key hash will have whats in header here
      serverInterceptSetup(txnp, pBody, plugin_config);
      state->intercept_request = true;
    } else {
      // not sure this is needed since we wont serve intercept in this case
      TSDebug(PLUGIN_TAG, "[%s] {%u} ReadRequestHdr add response hook", __FUNCTION__, state->req_info->key_hash);
      // dont cache if valid sie status code to myself
      TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, transaction_contp);
    }
  } else {
    // should we use the data we just cached -- this doesnt seem to help
    if (plugin_config->intercept_reroute) {
      // see if we are in the middle of intercepting
      if (async_intercept_active(state->req_info->key_hash, plugin_config)) {
        // add the async to the end so we use the fake cached response
        TSMBuffer buf;
        TSMLoc    hdr_loc;
        TSHttpTxnClientReqGet(txnp, &buf, &hdr_loc);
        add_trailing_parameter(buf, hdr_loc);
        TSHandleMLocRelease(buf, TS_NULL_MLOC, hdr_loc);
        TSDebug(PLUGIN_TAG, "[%s] {%u} add async parm to get fake cached item", __FUNCTION__, state->req_info->key_hash);
      }
    }
  }

  // always cache lookup
  TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, transaction_contp);
}

/*-----------------------------------------------------------------------------------------------*/
static int
global_request_header_hook(TSCont contp, TSEvent event, void *edata)
{
  ConfigInfo     *plugin_config = static_cast<ConfigInfo *>(TSContDataGet(contp));
  TSHttpTxn const txnp          = static_cast<TSHttpTxn>(edata);
  read_request_header_handler(txnp, plugin_config);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static void
create_plugin_stats(ConfigInfo *plugin_config)
{
  plugin_config->rfc_stat_swr_hit =
    TSStatCreate("stale_response.swr.hit", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  plugin_config->rfc_stat_swr_hit_skip =
    TSStatCreate("stale_response.swr.hit.skip", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  plugin_config->rfc_stat_swr_miss_locked =
    TSStatCreate("stale_response.swr.miss.locked", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  plugin_config->rfc_stat_sie_hit =
    TSStatCreate("stale_response.sie.hit", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  plugin_config->rfc_stat_memory_over =
    TSStatCreate("stale_response.memory.over", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
}

/*-----------------------------------------------------------------------------------------------*/
void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  info.plugin_name   = const_cast<char *>(PLUGIN_TAG);
  info.vendor_name   = const_cast<char *>(VENDOR_NAME);
  info.support_email = const_cast<char *>(SUPPORT_EMAIL);

  if (TS_SUCCESS != TSPluginRegister(&info)) {
    TSError("Plugin registration failed.");
    return;
  }
  TSDebug(PLUGIN_TAG, "Plugin registration succeeded.");

  TSMgmtString value = nullptr;
  TSMgmtStringGet("proxy.config.http.server_session_sharing.pool", &value);
  if (nullptr == value || 0 != strcasecmp(value, "global")) {
    TSError("[stale-response] Server session pool must be set to 'global'");
    assert(false);
  }

  // create the default ConfigInfo
  ConfigInfo *plugin_config = parse_args(argc, argv);

  // proxy.config.http.insert_age_in_response
  if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_TAG, "reserve state info slot", &(plugin_config->txn_slot))) {
    TSError("stale_response [%s] failed to user argument data. Plugin registration failed.", PLUGIN_TAG);
    delete plugin_config;
    return;
  }
  TSCont main_contp = TSContCreate(global_request_header_hook, nullptr);
  TSContDataSet(main_contp, plugin_config);
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, main_contp);

  create_plugin_stats(plugin_config);

  TSDebug(PLUGIN_TAG, "[%s] Plugin Init Complete", __FUNCTION__);
}

/*-----------------------------------------------------------------------------------------------*/
// Remap support.

/**
 * Remap initialization.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  CHECK_REMAP_API_COMPATIBILITY(api_info, errbuf, errbuf_size);
  TSDebug(PLUGIN_TAG, "[%s] Plugin Remap Init Complete", __FUNCTION__);

  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /*errbuf */, int /* errbuf_size */)
{
  // second arg poses as the program name
  --argc;
  ++argv;
  ConfigInfo *const plugin_config = parse_args(argc, const_cast<char const **>(argv));
  *ih                             = static_cast<void *>(plugin_config);
  if (TS_SUCCESS != TSUserArgIndexReserve(TS_USER_ARGS_TXN, PLUGIN_TAG, "reserve state info slot", &(plugin_config->txn_slot))) {
    TSError("stale_response [%s] failed to user argument data. Plugin registration failed.", PLUGIN_TAG);
    return TS_ERROR;
  }
  create_plugin_stats(plugin_config);
  TSDebug(PLUGIN_TAG, "[%s] Plugin Remap New Instance Complete", __FUNCTION__);
  return TS_SUCCESS;
}

void
TSRemapDeleteInstance(void *ih)
{
  ConfigInfo *const plugin_config = static_cast<ConfigInfo *>(ih);
  delete plugin_config;
  TSDebug(PLUGIN_TAG, "[%s] Plugin Remap Delete Instance Complete", __FUNCTION__);
}

/**
 * Remap entry point.
 */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  ConfigInfo *plugin_config = static_cast<ConfigInfo *>(ih);
  read_request_header_handler(txnp, plugin_config);
  return TSREMAP_NO_REMAP;
}
/*-----------------------------------------------------------------------------------------------*/
