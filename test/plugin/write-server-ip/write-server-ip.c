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

  @section details Details

  This plugin does the following things:
    -# Get the server ip and request method in the client request. Get
      the next hop ip from the server response.
    -# Create a new http header field MY_HDR in the server response and
      insert server ip, request method and next hop ip into the field as
      field values.
    -# When the client sends the same request the second time, get the
      header field MY_HDR from the caches response header, print it out
      and save it in a text file.

*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ts.h"

#define TIMEOUT_VALUE 10
#define MY_HDR "MY_HDR"
#define DEBUG_TAG "write-server-ip-dbg"

/* Log macros */
#define PLUGIN_NAME "write-server-ip"
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


static const char *plugin_dir;
static TSMutex file_mutex;

/********************************************************************
 * When the client sends the same request the second time, get the
 * header field MY_HDR from the caches response header, print it out
 * and save it in a text file.
 ********************************************************************/
static int
handle_cache_hdr(TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_cache_hdr");

  TSMBuffer cache_bufp;
  TSMLoc cache_loc = NULL, field_loc = NULL;
  const char *my_hdr;
  int value_count, i, length;
  char output_file[1024], output_str[1024];
  TSFile file;

  output_str[0] = '\0';

  /* get the cached response header */
  if (!TSHttpTxnCachedRespGet(txnp, &cache_bufp, &cache_loc))
    LOG_ERROR_AND_RETURN("TSHttpTxnCachedRespGet");

  /* get the MY_HDR field in the header */
  if ((field_loc = TSMimeHdrFieldFind(cache_bufp, cache_loc, MY_HDR, strlen(MY_HDR))) == TS_ERROR_PTR ||
      field_loc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldFind");

  if ((value_count = TSMimeHdrFieldValuesCount(cache_bufp, cache_loc, field_loc)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValuesCount");
  for (i = 0; i <= value_count - 1; i++) {
    if (TSMimeHdrFieldValueStringGet(cache_bufp, cache_loc, field_loc, i, &my_hdr, &length) == TS_ERROR)
      LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueStringGet");

    /* concatenate the field to output_str */
    if (my_hdr) {
      snprintf(output_str, 1024, "%s MY_HDR(%d): %.*s \n", output_str, i, length, my_hdr);
    }
  }

  snprintf(output_file, 1024, "%s/write_server_ip.txt", plugin_dir);
  TSDebug(DEBUG_TAG, "Writing record\n%s\nto file %s", output_str, output_file);

  /* append to the text file */
  if (TSMutexLock(file_mutex) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMutexLock");

  if ((file = TSfopen(output_file, "w")) == NULL)
    LOG_ERROR_AND_CLEANUP("TSfopen");
  TSfwrite(file, output_str, strlen(output_str));
  TSfflush(file);
  TSfclose(file);

  if (TSMutexUnlock(file_mutex) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMutexUnlock");

  /* cleanup */
Lcleanup:
  /* release mlocs */
  if (VALID_POINTER(field_loc))
    TSHandleMLocRelease(cache_bufp, cache_loc, field_loc);
  if (VALID_POINTER(cache_loc))
    TSHandleMLocRelease(cache_bufp, TS_NULL_MLOC, cache_loc);

  return 0;
}


/***********************************************************************
 * Get the server ip and request method in the client request. Get the
 * next hop ip from the server response.
 * Create a new http header field MY_HDR in the server response and
 * insert server ip, request method and next hop ip into the field as
 * field values.
 ***********************************************************************/
static int
handle_response_hdr(TSCont contp, TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_response_hdr");

  TSMBuffer resp_bufp;
  TSMLoc resp_loc = NULL;
  TSMLoc field_loc = NULL;
  unsigned int next_hop_ip = 0;
  unsigned int server_ip = 0;
  const char *request_method = NULL;
  char *r_method = NULL;
  int length;
  TSMBuffer req_bufp;
  TSMLoc req_loc = NULL;
  int incoming_port = 0, port = 0;
  char *hostname = NULL;
  int ret_value = -1;

  /* negative test */
#ifdef DEBUG
  if (TSHttpTxnServerIPGet(NULL) != 0)
    LOG_ERROR_NEG("TSHttpTxnServerIPGet");
  if (TSHttpTxnNextHopIPGet(NULL) != 0)
    LOG_ERROR_NEG("TSHttpTxnNextHopIPGet");
  if (TSHttpTxnParentProxyGet(NULL, &hostname, &port) != TS_ERROR)
    LOG_ERROR_NEG("TSHttpTxnParentProxyGet");
#endif

  /* get the server ip */
  if ((server_ip = TSHttpTxnServerIPGet(txnp)) == 0)
    LOG_ERROR_AND_RETURN("TSHttpTxnServerIPGet");

  /* get the request method */
  if (!TSHttpTxnServerReqGet(txnp, &req_bufp, &req_loc))
    LOG_ERROR_AND_RETURN("TSHttpTxnServerReqGet");

  if ((request_method = TSHttpHdrMethodGet(req_bufp, req_loc, &length)) == TS_ERROR_PTR || request_method == NULL)
    LOG_ERROR_AND_CLEANUP("TSHttpHdrMethodGet");

  r_method = TSstrndup(request_method, length);


  /* get the next hop ip */
  if ((next_hop_ip = TSHttpTxnNextHopIPGet(txnp)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpTxnNextHopIPGet");


  /* get the client incoming port */
  if ((incoming_port = TSHttpTxnClientIncomingPortGet(txnp)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpTxnClientIncomingPortGet");


  /* get the parent proxy */
  if (TSHttpTxnParentProxyGet(txnp, &hostname, &port) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSHttpTxnParentProxyGet");
  /* If no parent defined in records.config, set hostname to NULL and port to -1 */
  if (hostname == NULL) {
    hostname = "NULL";
    port = -1;
  }

  /* retrieve the server response header */
  if (!TSHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc))
    LOG_ERROR_AND_CLEANUP("TSHttpTxnServerRespGet");


  /* create and insert into hdr a new mime header field */
  if ((field_loc = TSMimeHdrFieldCreate(resp_bufp, resp_loc)) == TS_ERROR_PTR || field_loc == NULL)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldCreate");
  if (TSMimeHdrFieldAppend(resp_bufp, resp_loc, field_loc) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldAppend");
  if (TSMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, MY_HDR, strlen(MY_HDR)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldNameSet");

  /* Add value to the new mime header field */
  if (TSMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, field_loc, -1, r_method, strlen(r_method)) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueStringInsert");
  if (TSMimeHdrFieldValueUintInsert(resp_bufp, resp_loc, field_loc, -1, server_ip) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueUintInsert");
  if (TSMimeHdrFieldValueUintInsert(resp_bufp, resp_loc, field_loc, -1, next_hop_ip) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueUintInsert");
  if (TSMimeHdrFieldValueIntInsert(resp_bufp, resp_loc, field_loc, -1, incoming_port) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueIntInsert");
  if (TSMimeHdrFieldValueIntInsert(resp_bufp, resp_loc, field_loc, -1, port) == TS_ERROR)
    LOG_ERROR_AND_CLEANUP("TSMimeHdrFieldValueIntInsert");

  /* success */
  ret_value = 0;

Lcleanup:
  if (VALID_POINTER(r_method))
    TSfree(r_method);

  /* release the buffer handles */
  if (VALID_POINTER(req_loc))
    TSHandleMLocRelease(req_bufp, TS_NULL_MLOC, req_loc);

  /* free the handles and continuation data */
  if (VALID_POINTER(field_loc))
    TSHandleMLocRelease(resp_bufp, resp_loc, field_loc);
  if (VALID_POINTER(resp_loc))
    TSHandleMLocRelease(resp_bufp, TS_NULL_MLOC, resp_loc);

  return ret_value;
}

static int
handle_txn_start(TSCont contp, TSHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_start");

  if (TSHttpTxnHookAdd(txnp, TS_HTTP_READ_CACHE_HDR_HOOK, contp) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpTxnHookAdd");
  if (TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, contp) == TS_ERROR)
    LOG_ERROR_AND_RETURN("TSHttpTxnHookAdd");

  return 0;
}

static int
process_plugin(TSCont contp, TSEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("process_plugin");

  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:
    handle_txn_start(contp, txnp);
    break;
  case TS_EVENT_HTTP_READ_CACHE_HDR:
    handle_cache_hdr(txnp);
    break;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_response_hdr(contp, txnp);
    break;
  default:
    break;
  }

  if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR)
    LOG_ERROR("TSHttpTxnReenable");

  return 0;
}


void
TSPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("TSPluginInit");

  TSCont contp;

  plugin_dir = TSPluginDirGet();
  if ((file_mutex = TSMutexCreate()) == TS_ERROR_PTR) {
    LOG_ERROR("TSMutexCreate");
    return;
  }

  if ((contp = TSContCreate(process_plugin, TSMutexCreate())) == TS_ERROR_PTR) {
    LOG_ERROR("TSContCreate");
    return;
  }

  if (TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, contp) == TS_ERROR)
    LOG_ERROR("TSHttpHookAdd");

}
