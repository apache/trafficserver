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
 * The plugins uses the functions in the INKHttpAltInfo* group. They are called back on the
 * INK_HTTP_SELECT_ALT_HOOK. It also calls some other functions in the INK_HTTP_OS_DNS_HOOK.
 **********************************************************************************************/

#include <stdio.h>
#include <string.h>
#include "InkAPI.h"

#define DEBUG_TAG "alt-info-dbg"

#define PLUGIN_NAME "alt-info"
#define VALID_POINTER(X) ((X != NULL) && (X != INK_ERROR_PTR))
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME
#define LOG_ERROR(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d", PLUGIN_NAME, API_NAME, "APIFAIL", \
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
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE); \
}
#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}


/*********************************************************************** 
 * play with the functions in the INKHttpAltInfo* group
 ***********************************************************************/
static void
handle_select_alt(INKHttpAltInfo infop)
{
  LOG_SET_FUNCTION_NAME("handle_select_alt");

  INKMBuffer client_req_buf = NULL;
  INKMBuffer cache_req_buf = NULL;
  INKMBuffer cache_resp_buf = NULL;

  INKMLoc client_req_hdr = NULL;
  INKMLoc cache_req_hdr = NULL;
  INKMLoc cache_resp_hdr = NULL;

  INKMLoc accept_language_field = NULL;
  INKMLoc content_language_field = NULL;

  int len;
  const char *accept_value = NULL;
  const char *content_value = NULL;
  int quality = 0;

  /* negative test for INKHttpAltInfo* functions */
#ifdef DEBUG
  if (INKHttpAltInfoClientReqGet(NULL, &client_req_buf, &client_req_hdr) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpAltInfoClientReqGet");

  if (INKHttpAltInfoCachedReqGet(NULL, &cache_req_buf, &cache_req_hdr) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpAltInfoCachedReqGet");

  if (INKHttpAltInfoCachedRespGet(NULL, &cache_resp_buf, &cache_resp_hdr) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpAltInfoCachedRespGet");

  if (INKHttpAltInfoQualitySet(NULL, quality) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpAltInfoQualitySet");
#endif

  /* get client request, cached request and cached response */
  if (INKHttpAltInfoClientReqGet(infop, &client_req_buf, &client_req_hdr) != INK_SUCCESS)
    LOG_ERROR_AND_CLEANUP("INKHttpAltInfoClientReqGet");

  if (INKHttpAltInfoCachedReqGet(infop, &cache_req_buf, &cache_req_hdr) != INK_SUCCESS)
    LOG_ERROR_AND_CLEANUP("INKHttpAltInfoCachedReqGet");

  if (INKHttpAltInfoCachedRespGet(infop, &cache_resp_buf, &cache_resp_hdr) != INK_SUCCESS)
    LOG_ERROR_AND_CLEANUP("INKHttpAltInfoCachedRespGet");

  /* 
   * get the Accept-Language field value from the client request
   * get the Content-language field value from the cached response
   * if these two values are equivalent, set the quality of this alternate to 1
   * otherwise set it to 0 
   */
  if ((accept_language_field = INKMimeHdrFieldRetrieve(client_req_buf, client_req_hdr,
                                                       INK_MIME_FIELD_ACCEPT_LANGUAGE)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");
  if (accept_language_field) {
    if (INKMimeHdrFieldValueStringGet(client_req_buf, client_req_hdr, accept_language_field,
                                      0, &accept_value, &len) == INK_ERROR)
      LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueStringGet");
  } else {
    /* If field Accept-language not found, set quality to 0 */
    quality = 0;
  }

  if ((content_language_field = INKMimeHdrFieldRetrieve(cache_resp_buf, cache_resp_hdr,
                                                        INK_MIME_FIELD_CONTENT_LANGUAGE)) == INK_ERROR_PTR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");
  if (content_language_field) {
    if (INKMimeHdrFieldValueStringGet(cache_resp_buf, cache_resp_hdr, content_language_field,
                                      0, &content_value, &len) == INK_ERROR)
      LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldRetrieve");
  } else {
    /* If field content_language_field not found, set quality to 0 */
    quality = 0;
  }

  if (accept_value && content_value && len > 0 && (strncmp(accept_value, content_value, len) == 0)) {
    quality = 1;
  }

  if (INKHttpAltInfoQualitySet(infop, quality) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpAltInfoQualitySet");

  INKDebug(DEBUG_TAG, "Accept-Language: %s, Content-Language: %s, alternate quality set to %d",
           accept_value, content_value, quality);


  /* cleanup */
Lcleanup:
  if (VALID_POINTER(accept_value))
    INKHandleStringRelease(client_req_buf, accept_language_field, accept_value);
  if (VALID_POINTER(accept_language_field))
    INKHandleMLocRelease(client_req_buf, client_req_hdr, accept_language_field);
  if (VALID_POINTER(client_req_hdr))
    INKHandleMLocRelease(client_req_buf, INK_NULL_MLOC, client_req_hdr);

  if (VALID_POINTER(content_value))
    INKHandleStringRelease(cache_resp_buf, content_language_field, content_value);
  if (VALID_POINTER(content_language_field))
    INKHandleMLocRelease(cache_resp_buf, cache_resp_hdr, content_language_field);
  if (VALID_POINTER(cache_resp_hdr))
    INKHandleMLocRelease(cache_resp_buf, INK_NULL_MLOC, cache_resp_hdr);

  if (VALID_POINTER(cache_req_hdr))
    INKHandleMLocRelease(cache_req_buf, INK_NULL_MLOC, cache_req_hdr);

}

/**********************************************************************
 * Call the following functions on the INK_HTTP_OS_DNS_HOOK:
 * -- INKHttpTxnCachedReqGet
 * -- INKHttpTxnSsnGet
 * -- INKHttpTxnParentProxySet
 * -- INKrealloc
 **********************************************************************/

static void
handle_os_dns(INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_os_dns");

  INKMBuffer bufp = NULL;
  INKMLoc hdr_loc = NULL;
  INKHttpSsn ssnp = NULL;

  void *m = NULL;
  void *r = NULL;
  const unsigned int size1 = 100;
  const unsigned int size2 = 200;

  char *hostname = "npdev.inktomi.com";
  int port = 10180;

  /* get the cached request header */
  if (INKHttpTxnCachedReqGet(txnp, &bufp, &hdr_loc) == 0) {
    INKDebug(DEBUG_TAG, "Cannot get cached request header");
  } else {
    INKDebug(DEBUG_TAG, "Successfully get cached request header");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
  }

  /* get the session of this transaction */
  if ((ssnp = INKHttpTxnSsnGet(txnp)) == INK_ERROR_PTR || ssnp == NULL)
    LOG_ERROR_AND_REENABLE("INKHttpTxnSsnGet");

  /* negative test for INKHttpTxnSsnGet */
#ifdef DEBUG
  if (INKHttpTxnSsnGet(NULL) != INK_ERROR_PTR)
    LOG_ERROR_NEG("INKHttpTxnSsnGet");
#endif

  /* set the parent proxy */
  if (INKHttpTxnParentProxySet(txnp, hostname, port) == INK_ERROR)
    LOG_ERROR_AND_REENABLE("INKHttpTxnParentProxySet");

  /* negative test for INKHttpTxnParentProxySet */
#ifdef DEBUG
  if (INKHttpTxnParentProxySet(NULL, hostname, port) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpTxnParentProxySet");

  if (INKHttpTxnParentProxySet(txnp, NULL, port) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpTxnParentProxySet");
#endif

  /* try INKrealloc */
  if ((m = INKmalloc(size1)) == NULL)
    LOG_ERROR_AND_REENABLE("INKmalloc");
  if ((r = INKrealloc(m, size2)) == NULL)
    LOG_ERROR_AND_REENABLE("INKrealloc");

  INKfree(r);

  /* reenable the transaction */
  if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR)
    LOG_ERROR("INKHttpTxnReenable");

}

static int
alt_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpAltInfo infop;
  INKHttpTxn txnp;

  switch (event) {

  case INK_EVENT_HTTP_SELECT_ALT:
    infop = (INKHttpAltInfo) edata;
    handle_select_alt(infop);
    break;

  case INK_EVENT_HTTP_OS_DNS:
    txnp = (INKHttpTxn) edata;
    handle_os_dns(txnp);
    break;

  default:
    break;
  }

  return 0;
}

void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");
  INKCont contp;

  if ((contp = INKContCreate(alt_plugin, NULL)) == INK_ERROR_PTR)
    LOG_ERROR("INKContCreate")
      else {
    if (INKHttpHookAdd(INK_HTTP_SELECT_ALT_HOOK, contp) == INK_ERROR)
      LOG_ERROR("INKHttpHookAdd");
    if (INKHttpHookAdd(INK_HTTP_OS_DNS_HOOK, contp) == INK_ERROR)
      LOG_ERROR("INKHttpHookAdd");
    }
}
