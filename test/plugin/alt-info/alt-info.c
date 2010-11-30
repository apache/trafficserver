/** @file

  A brief file description

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

/**********************************************************************************************
 * The plugins uses the functions in the TSHttpAltInfo* group. They are called back on the
 * TS_HTTP_SELECT_ALT_HOOK. It also calls some other functions in the TS_HTTP_OS_DNS_HOOK.
 **********************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "ts.h"

#define DEBUG_TAG "alt-info-dbg"

#define PLUGIN_NAME "alt-info"
#define VALID_POINTER(X) ((X != NULL) && (X != TS_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
	     FUNCTION_NAME, __FILE__, __LINE__); \
}
#define LOG_ERROR_AND_RETURN(API_NAME) { \
    LOG_ERROR(API_NAME); \
    return -1; \
}
#define LOG_ERROR_AND_CLEANUP(API_NAME) { \
  LOG_ERROR(API_NAME); \
  goto Lcleanup; \
}
#define LOG_ERROR_AND_REENABLE(API_NAME) { \
  LOG_ERROR(API_NAME); \
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


/***********************************************************************
 * play with the functions in the TSHttpAltInfo* group
 ***********************************************************************/
static void
handle_select_alt(TSHttpAltInfo infop)
{
  LOG_SET_FUNCTION_NAME("handle_select_alt");

  TSMBuffer client_req_buf = NULL;
  TSMBuffer cache_req_buf = NULL;
  TSMBuffer cache_resp_buf = NULL;

  TSMLoc client_req_hdr = NULL;
  TSMLoc cache_req_hdr = NULL;
  TSMLoc cache_resp_hdr = NULL;

  TSMLoc accept_language_field = NULL;
  TSMLoc content_language_field = NULL;

  int len;
  const char *accept_value = NULL;
  const char *content_value = NULL;
  int quality = 0;

  /* negative test for TSHttpAltInfo* functions */
#ifdef DEBUG
  if (TSHttpAltInfoClientReqGet(NULL, &client_req_buf, &client_req_hdr) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpAltInfoClientReqGet");

  if (TSHttpAltInfoCachedReqGet(NULL, &cache_req_buf, &cache_req_hdr) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpAltInfoCachedReqGet");

  if (TSHttpAltInfoCachedRespGet(NULL, &cache_resp_buf, &cache_resp_hdr) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpAltInfoCachedRespGet");

  if (TSHttpAltInfoQualitySet(NULL, quality) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpAltInfoQualitySet");
#endif

  /* get client request, cached request and cached response */
  if (TSHttpAltInfoClientReqGet(infop, &client_req_buf, &client_req_hdr) != TS_SUCCESS)
    LOG_ERROR_AND_CLEANUP("TSHttpAltInfoClientReqGet");

  if (TSHttpAltInfoCachedReqGet(infop, &cache_req_buf, &cache_req_hdr) != TS_SUCCESS)
    LOG_ERROR_AND_CLEANUP("TSHttpAltInfoCachedReqGet");

  if (TSHttpAltInfoCachedRespGet(infop, &cache_resp_buf, &cache_resp_hdr) != TS_SUCCESS)
    LOG_ERROR_AND_CLEANUP("TSHttpAltInfoCachedRespGet");

  /*
   * get the Accept-Language field value from the client request
   * get the Content-language field value from the cached response
   * if these two values are equivalent, set the quality of this alternate to 1
   * otherwise set it to 0
   */
  if ((accept_language_field = TSMimeHdrFieldFind(client_req_buf, client_req_hdr, TS_MIME_FIELD_ACCEPT_LANGUAGE,
                                                   TS_MIME_LEN_ACCEPT_LANGUAGE)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");
  if (accept_language_field) {
    if (TSMimeHdrFieldValueStringGet(client_req_buf, client_req_hdr, accept_language_field,
                                      0, &accept_value, &len) == TS_ERROR)
      LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueStringGet");
  } else {
    /* If field Accept-language not found, set quality to 0 */
    quality = 0;
  }

  if ((content_language_field = TSMimeHdrFieldFind(cache_resp_buf, cache_resp_hdr, TS_MIME_FIELD_CONTENT_LANGUAGE,
                                                    TS_MIME_LEN_CONTENT_LANGUAGE)) == TS_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");
  if (content_language_field) {
    if (TSMimeHdrFieldValueStringGet(cache_resp_buf, cache_resp_hdr, content_language_field,
                                      0, &content_value, &len) == TS_ERROR)
      LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");
  } else {
    /* If field content_language_field not found, set quality to 0 */
    quality = 0;
  }

  if (accept_value && content_value && len > 0 && (strncmp(accept_value, content_value, len) == 0)) {
    quality = 1;
  }

  if (TSHttpAltInfoQualitySet(infop, quality) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpAltInfoQualitySet");

  TSDebug(DEBUG_TAG, "Accept-Language: %s, Content-Language: %s, alternate quality set to %d",
           accept_value, content_value, quality);


  /* cleanup */
Lcleanup:
  if (VALID_POINTER(accept_value))
    TSHandleStringRelease(client_req_buf, accept_language_field, accept_value);
  if (VALID_POINTER(accept_language_field))
    TSHandleMLocRelease(client_req_buf, client_req_hdr, accept_language_field);
  if (VALID_POINTER(client_req_hdr))
    TSHandleMLocRelease(client_req_buf, TS_NULL_MLOC, client_req_hdr);

  if (VALID_POINTER(content_value))
    TSHandleStringRelease(cache_resp_buf, content_language_field, content_value);
  if (VALID_POINTER(content_language_field))
    TSHandleMLocRelease(cache_resp_buf, cache_resp_hdr, content_language_field);
  if (VALID_POINTER(cache_resp_hdr))
    TSHandleMLocRelease(cache_resp_buf, TS_NULL_MLOC, cache_resp_hdr);

  if (VALID_POINTER(cache_req_hdr))
    TSHandleMLocRelease(cache_req_buf, TS_NULL_MLOC, cache_req_hdr);

}

/**********************************************************************
 * Call the following functions on the TS_HTTP_OS_DNS_HOOK:
 * -- TSHttpTxnCachedReqGet
 * -- TSHttpTxnSsnGet
 * -- TSHttpTxnParentProxySet
 * -- TSrealloc
 **********************************************************************/

static void
handle_os_dns(TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_os_dns");

  TSMBuffer bufp = NULL;
  TSMLoc hdr_loc = NULL;
  TSHttpSsn ssnp = NULL;

  void *m = NULL;
  void *r = NULL;
  const unsigned int size1 = 100;
  const unsigned int size2 = 200;

  char *hostname = "npdev.inktomi.com";
  int port = 10180;

  /* get the cached request header */
  if (TSHttpTxnCachedReqGet(txnp, &bufp, &hdr_loc) == 0) {
    TSDebug(DEBUG_TAG, "Cannot get cached request header");
  } else {
    TSDebug(DEBUG_TAG, "Successfully get cached request header");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  }

  /* get the session of this transaction */
  if ((ssnp = TSHttpTxnSsnGet(txnp)) == TS_ERROR_PTR || ssnp == NULL)
    LOG_ERROR_AND_REENABLE("TSHttpTxnSsnGet");

  /* negative test for TSHttpTxnSsnGet */
#ifdef DEBUG
  if (TSHttpTxnSsnGet(NULL) != TS_ERROR_PTR)
    LOG_ERROR_NEG("TSHttpTxnSsnGet");
#endif

  /* set the parent proxy */
  if (TSHttpTxnParentProxySet(txnp, hostname, port) == TS_ERROR)
    LOG_ERROR_AND_REENABLE("TSHttpTxnParentProxySet");

  /* negative test for TSHttpTxnParentProxySet */
#ifdef DEBUG
  if (TSHttpTxnParentProxySet(NULL, hostname, port) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpTxnParentProxySet");

  if (TSHttpTxnParentProxySet(txnp, NULL, port) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpTxnParentProxySet");
#endif

  /* try TSrealloc */
  if ((m = TSmalloc(size1)) == NULL)
    LOG_ERROR_AND_REENABLE("TSmalloc");
  if ((r = TSrealloc(m, size2)) == NULL)
    LOG_ERROR_AND_REENABLE("TSrealloc");

  TSfree(r);

  /* reenable the transaction */
  if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR)
    LOG_ERROR("TSHttpTxnReenable");

}

static int
alt_plugin(TSCont contp, TSEvent event, void *edata)
{
  TSHttpAltInfo infop;
  TSHttpTxn txnp;

  switch (event) {

  case TS_EVENT_HTTP_SELECT_ALT:
    infop = (TSHttpAltInfo) edata;
    handle_select_alt(infop);
    break;

  case TS_EVENT_HTTP_OS_DNS:
    txnp = (TSHttpTxn) edata;
    handle_os_dns(txnp);
    break;

  default:
    break;
  }

  return 0;
}

void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");
  TSCont contp;

  if ((contp = TSContCreate(alt_plugin, NULL)) == TS_ERROR_PTR)
    LOG_ERROR("TSContCreate")
      else {
    if (TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, contp) == TS_ERROR)
      LOG_ERROR("TSHttpHookAdd");
    if (TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, contp) == TS_ERROR)
      LOG_ERROR("TSHttpHookAdd");
    }
}
