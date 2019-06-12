/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include "ts/ts.h"
#include "ts/remap.h"

#include "money_trace.h"

/**
 * Allocate transaction data structure.
 */
static struct txndata *
allocTransactionData()
{
  LOG_DEBUG("allocating transaction state data.");
  struct txndata *txn_data           = (struct txndata *)TSmalloc(sizeof(struct txndata));
  txn_data->client_request_mt_header = nullptr;
  txn_data->new_span_mt_header       = nullptr;
  return txn_data;
}

/**
 * free any previously allocated transaction data.
 */
static void
freeTransactionData(struct txndata *txn_data)
{
  LOG_DEBUG("de-allocating transaction state data.");

  if (txn_data != nullptr) {
    LOG_DEBUG("freeing transaction data.");
    TSfree(txn_data->client_request_mt_header);
    TSfree(txn_data->new_span_mt_header);
    TSfree(txn_data);
  }
}

/**
 * The TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE event callback.
 *
 * If there is a cache hit only schedule a TS_HTTP_SEND_RESPONSE_HDR_HOOK
 * continuation to send back the money trace header in the response to the
 * client.
 *
 * If there is a cache miss, a new money trace header is created and a
 * TS_HTTP_SEND_REQUES_HDR_HOOK continuation is scheduled to add the
 * new money trace header to the parent request.
 */
static void
mt_cache_lookup_check(TSCont contp, TSHttpTxn txnp, struct txndata *txn_data)
{
  MT generator;
  int cache_result = 0;
  char *new_mt_header;

  if (TS_SUCCESS != TSHttpTxnCacheLookupStatusGet(txnp, &cache_result)) {
    LOG_ERROR("Unable to get cache status.");
  } else {
    switch (cache_result) {
    case TS_CACHE_LOOKUP_MISS:
    case TS_CACHE_LOOKUP_SKIPPED:
      new_mt_header = (char *)generator.moneyTraceHdr(txn_data->client_request_mt_header);
      if (new_mt_header != nullptr) {
        LOG_DEBUG("cache miss, built a new money trace header: %s.", new_mt_header);
        txn_data->new_span_mt_header = new_mt_header;
      } else {
        LOG_DEBUG("failed to build a new money trace header.");
      }
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);

      // fall through to the default as we always need to send the original
      // money trace header received from the client back to the client in the
      // response
      // fallthrough

    default:
      TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      break;
    }
  }
}

/**
 * remap entry point, called to check for the existence of a money trace
 * header.  If there is one, schedule the continuation to call back and
 * process on TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK and TS_HTTP_TXN_CLOSE_HOOK.
 */
static void
mt_check_request_header(TSHttpTxn txnp)
{
  int length               = 0;
  struct txndata *txn_data = nullptr;
  TSMBuffer bufp;
  TSMLoc hdr_loc = nullptr, field_loc = nullptr;
  TSCont contp;

  // check for a money trace header.  If there is one, schedule appropriate continuations.
  if (TS_SUCCESS == TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, MIME_FIELD_MONEY_TRACE, MIME_LEN_MONEY_TRACE);
    if (TS_NULL_MLOC != field_loc) {
      const char *hdr_value = TSMimeHdrFieldValueStringGet(bufp, hdr_loc, field_loc, 0, &length);
      if (!hdr_value || length <= 0) {
        LOG_DEBUG("ignoring, corrupt money trace header.");
      } else {
        if (nullptr == (contp = TSContCreate(transaction_handler, nullptr))) {
          LOG_ERROR("failed to create the transaction handler continuation");
        } else {
          txn_data                                   = allocTransactionData();
          txn_data->client_request_mt_header         = TSstrndup(hdr_value, length);
          txn_data->client_request_mt_header[length] = '\0'; // workaround for bug in core.
          LOG_DEBUG("found money trace header: %s, length: %d", txn_data->client_request_mt_header, length);
          TSContDataSet(contp, txn_data);
          TSHttpTxnHookAdd(txnp, TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
          TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, contp);
        }
      }
    } else {
      LOG_DEBUG("no money trace header was found in the request.");
    }
  } else {
    LOG_DEBUG("failed to retrieve the client request.");
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);
}

/**
 * The TS_EVENT_HTTP_SEND_RESPONSE_HDR callback.
 *
 * Adds the money trace header received in the client request to the
 * client response headers.
 */
static void
mt_send_client_response(TSHttpTxn txnp, struct txndata *txn_data)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc = nullptr, field_loc = nullptr;

  if (txn_data->client_request_mt_header == nullptr) {
    LOG_DEBUG("no client request header to return.");
    return;
  }

  // send back the money trace header received in the request
  // back in the response to the client.
  if (TS_SUCCESS != TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    LOG_DEBUG("could not get the server response headers.");
    return;
  } else {
    if (TS_SUCCESS ==
        TSMimeHdrFieldCreateNamed(bufp, hdr_loc, MIME_FIELD_MONEY_TRACE, strlen(MIME_FIELD_MONEY_TRACE), &field_loc)) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, txn_data->client_request_mt_header,
                                                     strlen(txn_data->client_request_mt_header))) {
        LOG_DEBUG("response header added: %s: %s", MIME_FIELD_MONEY_TRACE, txn_data->client_request_mt_header);
        TSMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
      }
    } else {
      LOG_DEBUG("failed to create money trace response header.");
    }
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);

  return;
}

/**
 * The TS_EVENT_HTTP_SEND_REQUEST_HDR callback.
 *
 * When a parent request is made, this function adds the new
 * money trace header to the parent request headers.
 */
static void
mt_send_server_request(TSHttpTxn txnp, struct txndata *txn_data)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc = nullptr, field_loc = nullptr;

  if (txn_data->new_span_mt_header == nullptr) {
    LOG_DEBUG("there is no new mt request header to send.");
    return;
  }

  if (TS_SUCCESS == TSHttpTxnServerReqGet(txnp, &bufp, &hdr_loc)) {
    field_loc = TSMimeHdrFieldFind(bufp, hdr_loc, MIME_FIELD_MONEY_TRACE, MIME_LEN_MONEY_TRACE);
    if (TS_NULL_MLOC != field_loc) {
      if (TS_SUCCESS == TSMimeHdrFieldValueStringSet(bufp, hdr_loc, field_loc, -1, txn_data->new_span_mt_header,
                                                     strlen(txn_data->new_span_mt_header))) {
        LOG_DEBUG("server request header updated: %s: %s", MIME_FIELD_MONEY_TRACE, txn_data->new_span_mt_header);
      }
    } else {
      LOG_DEBUG("unable to retrieve the money trace header location from the server request headers.");
      return;
    }
  }
  TSHandleMLocRelease(bufp, hdr_loc, field_loc);

  return;
}

/**
 * Remap initialization.
 */
TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  if (!api_info) {
    strncpy(errbuf, "[tsremap_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSRemapInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  LOG_DEBUG("cache_range_requests remap is successfully initialized.");

  return TS_SUCCESS;
}

/**
 * not used, one instance per remap and no parameters are used.
 */
TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char * /*errbuf */, int /* errbuf_size */)
{
  return TS_SUCCESS;
}

/**
 * not used, one instance per remap
 */
void
TSRemapDeleteInstance(void *ih)
{
  LOG_DEBUG("no op");
}

/**
 * Remap entry point.
 */
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo * /* rri */)
{
  mt_check_request_header(txnp);
  return TSREMAP_NO_REMAP;
}

/**
 * Transaction event handler.
 */
static int
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp           = static_cast<TSHttpTxn>(edata);
  struct txndata *txn_data = (struct txndata *)TSContDataGet(contp);

  switch (event) {
  case TS_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    LOG_DEBUG("transaction cache lookup complete.");
    mt_cache_lookup_check(contp, txnp, txn_data);
    break;
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    LOG_DEBUG("updating send request headers.");
    mt_send_server_request(txnp, txn_data);
    break;
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    LOG_DEBUG("updating send response headers.");
    mt_send_client_response(txnp, txn_data);
    break;
  case TS_EVENT_HTTP_TXN_CLOSE:
    LOG_DEBUG("handling transaction close.");
    freeTransactionData(txn_data);
    TSContDestroy(contp);
    break;
  default:
    TSAssert(!"Unexpected event");
    break;
  }
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);

  return TS_SUCCESS;
}

const char *
MT::moneyTraceHdr(const char *mt_request_hdr)
{
  char copy[8192] = {'\0'};
  char *toks[3], *p = nullptr, *saveptr = nullptr;
  std::ostringstream temp_str;
  std::string mt_header_str;
  int numtoks = 0;

  if (mt_request_hdr == nullptr) {
    LOG_DEBUG("an empty header was passed in.");
    return nullptr;
  } else {
    strncpy(copy, mt_request_hdr, 8191);
  }

  // parse the money header.
  p = strtok_r(copy, ";", &saveptr);
  if (p != nullptr) {
    toks[numtoks++] = p;
    // copy the traceid
  } else {
    LOG_DEBUG("failed to parse the money_trace_header: %s", mt_request_hdr);
    return nullptr;
  }
  do {
    p = strtok_r(nullptr, ";", &saveptr);
    if (p != nullptr) {
      toks[numtoks++] = p;
    }
  } while (p != nullptr && numtoks < 3);

  if (numtoks != 3 || toks[0] == nullptr || toks[1] == nullptr || toks[2] == nullptr) {
    LOG_DEBUG("failed to parse the money_trace_header: %s", mt_request_hdr);
    return nullptr;
  }

  if (strncmp(toks[0], "trace-id", strlen("trace-id")) == 0 && strncmp(toks[2], "span-id", strlen("span-id")) == 0 &&
      (p = strchr(toks[2], '=')) != nullptr) {
    p++;
    if (strncmp("0x", p, 2) == 0) {
      temp_str << toks[0] << ";parent-id=" << p << ";span-id=0x" << std::hex << spanId() << std::ends;
    } else {
      temp_str << toks[0] << ";parent-id=" << p << ";span-id=" << spanId() << std::ends;
    }
  } else {
    LOG_DEBUG("invalid money_trace_header: %s", mt_request_hdr);
    return nullptr;
  }

  mt_header_str = temp_str.str();

  return TSstrndup(mt_header_str.c_str(), mt_header_str.length());
}
