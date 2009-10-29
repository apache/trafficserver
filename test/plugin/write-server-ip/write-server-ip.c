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
#include "InkAPI.h"

#define TIMEOUT_VALUE 10
#define MY_HDR "MY_HDR"
#define DEBUG_TAG "write-server-ip-dbg"

/* Log macros */
#define PLUGIN_NAME "write-server-ip"
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


static const char *plugin_dir;
static INKMutex file_mutex;

/******************************************************************** 
 * When the client sends the same request the second time, get the
 * header field MY_HDR from the caches response header, print it out
 * and save it in a text file.
 ********************************************************************/
static int
handle_cache_hdr(INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_cache_hdr");

  INKMBuffer cache_bufp;
  INKMLoc cache_loc = NULL, field_loc = NULL;
  const char *my_hdr;
  int value_count, i, length;
  char output_file[1024], output_str[1024];
  INKFile file;

  output_str[0] = '\0';

  /* get the cached response header */
  if (!INKHttpTxnCachedRespGet(txnp, &cache_bufp, &cache_loc))
    LOG_ERROR_AND_RETURN("INKHttpTxnCachedRespGet");

  /* get the MY_HDR field in the header */
  if ((field_loc = INKMimeHdrFieldFind(cache_bufp, cache_loc, MY_HDR, strlen(MY_HDR))) == INK_ERROR_PTR ||
      field_loc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldFind");

  if ((value_count = INKMimeHdrFieldValuesCount(cache_bufp, cache_loc, field_loc)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValuesCount");
  for (i = 0; i <= value_count - 1; i++) {
    if (INKMimeHdrFieldValueStringGet(cache_bufp, cache_loc, field_loc, i, &my_hdr, &length) == INK_ERROR)
      LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueStringGet");

    /* concatenate the field to output_str */
    if (my_hdr) {
      snprintf(output_str, 1024, "%s MY_HDR(%d): %.*s \n", output_str, i, length, my_hdr);
      INKHandleStringRelease(cache_bufp, field_loc, my_hdr);
    }
  }

  snprintf(output_file, 1024, "%s/write_server_ip.txt", plugin_dir);
  INKDebug(DEBUG_TAG, "Writing record\n%s\nto file %s", output_str, output_file);

  /* append to the text file */
  if (INKMutexLock(file_mutex) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMutexLock");

  if ((file = INKfopen(output_file, "w")) == NULL)
    LOG_ERROR_AND_CLEANUP("INKfopen");
  INKfwrite(file, output_str, strlen(output_str));
  INKfflush(file);
  INKfclose(file);

  if (INKMutexUnlock(file_mutex) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMutexUnlock");

  /* cleanup */
Lcleanup:
  /* release mlocs */
  if (VALID_POINTER(field_loc))
    INKHandleMLocRelease(cache_bufp, cache_loc, field_loc);
  if (VALID_POINTER(cache_loc))
    INKHandleMLocRelease(cache_bufp, INK_NULL_MLOC, cache_loc);

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
handle_response_hdr(INKCont contp, INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_response_hdr");

  INKMBuffer resp_bufp;
  INKMLoc resp_loc = NULL;
  INKMLoc field_loc = NULL;
  unsigned int next_hop_ip = 0;
  unsigned int server_ip = 0;
  const char *request_method = NULL;
  char *r_method = NULL;
  int length;
  INKMBuffer req_bufp;
  INKMLoc req_loc = NULL;
  int incoming_port = 0, port = 0;
  char *hostname = NULL;
  int ret_value = -1;

  /* negative test */
#ifdef DEBUG
  if (INKHttpTxnServerIPGet(NULL) != 0)
    LOG_ERROR_NEG("INKHttpTxnServerIPGet");
  if (INKHttpTxnNextHopIPGet(NULL) != 0)
    LOG_ERROR_NEG("INKHttpTxnNextHopIPGet");
  if (INKHttpTxnParentProxyGet(NULL, &hostname, &port) != INK_ERROR)
    LOG_ERROR_NEG("INKHttpTxnParentProxyGet");
#endif

  /* get the server ip */
  if ((server_ip = INKHttpTxnServerIPGet(txnp)) == 0)
    LOG_ERROR_AND_RETURN("INKHttpTxnServerIPGet");

  /* get the request method */
  if (!INKHttpTxnServerReqGet(txnp, &req_bufp, &req_loc))
    LOG_ERROR_AND_RETURN("INKHttpTxnServerReqGet");

  if ((request_method = INKHttpHdrMethodGet(req_bufp, req_loc, &length)) == INK_ERROR_PTR || request_method == NULL)
    LOG_ERROR_AND_CLEANUP("INKHttpHdrMethodGet");

  r_method = INKstrndup(request_method, length);


  /* get the next hop ip */
  if ((next_hop_ip = INKHttpTxnNextHopIPGet(txnp)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpTxnNextHopIPGet");


  /* get the client incoming port */
  if ((incoming_port = INKHttpTxnClientIncomingPortGet(txnp)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpTxnClientIncomingPortGet");


  /* get the parent proxy */
  if (INKHttpTxnParentProxyGet(txnp, &hostname, &port) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKHttpTxnParentProxyGet");
  /* If no parent defined in records.config, set hostname to NULL and port to -1 */
  if (hostname == NULL) {
    hostname = "NULL";
    port = -1;
  }

  /* retrieve the server response header */
  if (!INKHttpTxnServerRespGet(txnp, &resp_bufp, &resp_loc))
    LOG_ERROR_AND_CLEANUP("INKHttpTxnServerRespGet");


  /* create and insert into hdr a new mime header field */
  if ((field_loc = INKMimeHdrFieldCreate(resp_bufp, resp_loc)) == INK_ERROR_PTR || field_loc == NULL)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldCreate");
  if (INKMimeHdrFieldAppend(resp_bufp, resp_loc, field_loc) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldAppend");
  if (INKMimeHdrFieldNameSet(resp_bufp, resp_loc, field_loc, MY_HDR, strlen(MY_HDR)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldNameSet");

  /* Add value to the new mime header field */
  if (INKMimeHdrFieldValueStringInsert(resp_bufp, resp_loc, field_loc, -1, r_method, strlen(r_method)) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueStringInsert");
  if (INKMimeHdrFieldValueUintInsert(resp_bufp, resp_loc, field_loc, -1, server_ip) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueUintInsert");
  if (INKMimeHdrFieldValueUintInsert(resp_bufp, resp_loc, field_loc, -1, next_hop_ip) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueUintInsert");
  if (INKMimeHdrFieldValueIntInsert(resp_bufp, resp_loc, field_loc, -1, incoming_port) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueIntInsert");
  if (INKMimeHdrFieldValueIntInsert(resp_bufp, resp_loc, field_loc, -1, port) == INK_ERROR)
    LOG_ERROR_AND_CLEANUP("INKMimeHdrFieldValueIntInsert");

  /* success */
  ret_value = 0;

Lcleanup:
  if (VALID_POINTER(r_method))
    INKfree(r_method);

  /* negative test for INKHandleStringRelease */
#ifdef DEBUG
  if (INKHandleStringRelease(NULL, req_loc, request_method) != INK_ERROR) {
    LOG_ERROR_NEG("INKHandleStringRelease");
  }
#endif

  /* release the buffer handles */
  if (VALID_POINTER(request_method))
    INKHandleStringRelease(req_bufp, req_loc, request_method);
  if (VALID_POINTER(req_loc))
    INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);

  /* free the handles and continuation data */
  if (VALID_POINTER(field_loc))
    INKHandleMLocRelease(resp_bufp, resp_loc, field_loc);
  if (VALID_POINTER(resp_loc))
    INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);

  return ret_value;
}

static int
handle_txn_start(INKCont contp, INKHttpTxn txnp)
{
  LOG_SET_FUNCTION_NAME("handle_txn_start");

  if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_CACHE_HDR_HOOK, contp) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpTxnHookAdd");
  if (INKHttpTxnHookAdd(txnp, INK_HTTP_READ_RESPONSE_HDR_HOOK, contp) == INK_ERROR)
    LOG_ERROR_AND_RETURN("INKHttpTxnHookAdd");

  return 0;
}

static int
process_plugin(INKCont contp, INKEvent event, void *edata)
{
  LOG_SET_FUNCTION_NAME("process_plugin");

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    handle_txn_start(contp, txnp);
    break;
  case INK_EVENT_HTTP_READ_CACHE_HDR:
    handle_cache_hdr(txnp);
    break;
  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    handle_response_hdr(contp, txnp);
    break;
  default:
    break;
  }

  if (INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE) == INK_ERROR)
    LOG_ERROR("INKHttpTxnReenable");

  return 0;
}


void
INKPluginInit(int argc, const char *argv[])
{
  LOG_SET_FUNCTION_NAME("INKPluginInit");

  INKCont contp;

  plugin_dir = INKPluginDirGet();
  if ((file_mutex = INKMutexCreate()) == INK_ERROR_PTR) {
    LOG_ERROR("INKMutexCreate");
    return;
  }

  if ((contp = INKContCreate(process_plugin, INKMutexCreate())) == INK_ERROR_PTR) {
    LOG_ERROR("INKContCreate");
    return;
  }

  if (INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, contp) == INK_ERROR)
    LOG_ERROR("INKHttpHookAdd");

}
