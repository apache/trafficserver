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

/* 
 *   redirect-1.c:  
 *	an example program which redirects clients based on the source IP 
 *
 *
 *	Usage:	
 * 	(NT): Redirect.dll block_ip url_redirect 
 * 	(Solaris): redirect-1.so block_ip url_redirect 
 *
 *
 */

#include <string.h>

#if !defined (_WIN32)
#  include <unistd.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#else
#  include <windows.h>
#endif

#include "InkAPI.h"

#if !defined (_WIN32)
static in_addr_t ip_deny;
#else
static unsigned int ip_deny;
#endif

/* 
 * uncoupled statistics variables: 
 */
static INKStat method_count_redirected_connect;
static INKStat method_count_redirected_delete;
static INKStat method_count_redirected_get;
static INKStat method_count_redirected_head;
static INKStat method_count_redirected_icp_query;
static INKStat method_count_redirected_options;
static INKStat method_count_redirected_post;
static INKStat method_count_redirected_purge;
static INKStat method_count_redirected_put;
static INKStat method_count_redirected_trace;
static INKStat method_count_redirected_unknown;


/* 
 *	coupled statistics variables: 
 *		coupled stat category for the following stats
 *              is request_outcomes. The relationship among the stats is:
 *		requests_all = requests_redirects + requests_unchanged
 */
static INKCoupledStat request_outcomes;
static INKStat requests_all;
static INKStat requests_redirects;
static INKStat requests_unchanged;


void update_redirected_method_stats(INKMBuffer bufp, INKMLoc hdr_loc);

static char *url_redirect;
static char *uri_redirect;
static char *block_ip;

#define PLUGIN_NAME "redirect-1-neg"
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

static void
handle_client_lookup(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc, url_loc;
  int host_length;

#if !defined (_WIN32)
  in_addr_t clientip;
#else
  unsigned int clientip;
#endif

  const char *host;
  char *clientstring;
  struct in_addr tempstruct;

  /* 
   * Here we declare local coupled statistics variables: 
   */
  INKCoupledStat local_request_outcomes;
  INKStat local_requests_all;
  INKStat local_requests_redirects;
  INKStat local_requests_unchanged;

  LOG_SET_FUNCTION_NAME("handle_client_lookup");

  /*
   *  Create local copy of the global coupled stat category: 
   */
  local_request_outcomes = INKStatCoupledLocalCopyCreate("local_request_outcomes", request_outcomes);


  /* 
   * Create the local copies of the global coupled stats: 
   */
  local_requests_all = INKStatCoupledLocalAdd(local_request_outcomes, "requests.all.local", INKSTAT_TYPE_FLOAT);
  local_requests_redirects = INKStatCoupledLocalAdd(local_request_outcomes,
                                                    "requests.redirects.local", INKSTAT_TYPE_INT64);
  local_requests_unchanged = INKStatCoupledLocalAdd(local_request_outcomes,
                                                    "requests.unchanged.local", INKSTAT_TYPE_INT64);


  /* 
   *   Increment the count of total requests: 
   *     (it is more natural to treat the number of requests as an
   *      integer, but we declare this a FLOAT in order to demonstrate
   *      how to increment coupled FLOAT stats)
   */
  INKStatFloatAddTo(local_requests_all, 1.0);

  /* negative test */
#ifdef DEBUG
  if (INKStatCoupledLocalCopyCreate(NULL, request_outcomes) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledLocalCopyCreate");
  }
  if (INKStatCoupledLocalCopyCreate("my_local_copy", NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledLocalCopyCreate");
  }

  if (INKStatCoupledLocalAdd(NULL, "requests.negtest", INKSTAT_TYPE_INT64) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledLocalAdd");
  }
  if (INKStatCoupledLocalAdd(local_request_outcomes, NULL, INKSTAT_TYPE_INT64) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledLocalAdd");
  }

  if (INKStatFloatAddTo(NULL, 1.0) != INK_ERROR) {
    LOG_ERROR_NEG("INKStatFloatAddTo");
  }

  if (INKHttpTxnClientIPGet(NULL) != 0) {
    LOG_ERROR_NEG("INKHttpTxnClientIPGet");
  }
#endif

#if !defined (_WIN32)
  clientip = (in_addr_t) INKHttpTxnClientIPGet(txnp);
#else
  clientip = INKHttpTxnClientIPGet(txnp);
#endif

  tempstruct.s_addr = clientip;
  clientstring = inet_ntoa(tempstruct);
  INKDebug("redirect", "clientip is %s and block_ip is %s", clientstring, block_ip);

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = INKUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    INKError("couldn't retrieve request hostname\n");
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  /* 
   *   Check to see if the client is already headed to the redirect site.
   */
  if (strncmp(host, url_redirect, host_length) == 0) {
    INKHandleStringRelease(bufp, url_loc, host);
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  if (ip_deny == clientip) {

    INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    INKHandleStringRelease(bufp, url_loc, host);

    update_redirected_method_stats(bufp, hdr_loc);

    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

    /* 
     *   Increment the local redirect stat and do global update: 
     */
    INKStatIncrement(local_requests_redirects);
    INKStatsCoupledUpdate(local_request_outcomes);
    INKStatCoupledLocalCopyDestroy(local_request_outcomes);

    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);
    return;
  }

done:
  /* 
   * Increment the local number unchanged stat and do global update: 
   */
  INKStatIncrement(local_requests_unchanged);
  INKStatsCoupledUpdate(local_request_outcomes);
  INKStatCoupledLocalCopyDestroy(local_request_outcomes);

  /* negative test */
#ifdef DEBUG
  if (INKStatsCoupledUpdate(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKStatsCoupledUpdate");
  }
  if (INKStatCoupledLocalCopyDestroy(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKStatCoupledLocalCopyDestroy");
  }
#endif

  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}



static void
handle_response(INKHttpTxn txnp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc, newfield_loc;
  INKMLoc url_loc;
  char *url_str;
  char *buf;
  char *errormsg_body = "All requests from this IP address are redirected.\n";
  char *tmp_body;

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client response header\n");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_MOVED_PERMANENTLY);
  INKHttpHdrReasonSet(bufp, hdr_loc,
                      INKHttpHdrReasonLookup(INK_HTTP_STATUS_MOVED_PERMANENTLY),
                      strlen(INKHttpHdrReasonLookup(INK_HTTP_STATUS_MOVED_PERMANENTLY)));

  newfield_loc = INKMimeHdrFieldCreate(bufp, hdr_loc);
  INKMimeHdrFieldNameSet(bufp, hdr_loc, newfield_loc, INK_MIME_FIELD_LOCATION, INK_MIME_LEN_LOCATION);
  INKMimeHdrFieldValueInsert(bufp, hdr_loc, newfield_loc, uri_redirect, strlen(uri_redirect), -1);
  INKMimeHdrFieldInsert(bufp, hdr_loc, newfield_loc, -1);


  /* 
   *  Note that we can't directly use errormsg_body, as INKHttpTxnErrorBodySet()
   *  will try to free the passed buffer with INKfree().
   */
  tmp_body = INKstrdup(errormsg_body);
  INKHttpTxnErrorBodySet(txnp, tmp_body, strlen(tmp_body), NULL);
  INKHandleMLocRelease(bufp, hdr_loc, newfield_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);


done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
}



static int
redirect_plugin(INKCont contp, INKEvent event, void *edata)
{

  INKHttpTxn txnp = (INKHttpTxn) edata;

  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:

    handle_client_lookup(txnp, contp);
    return 0;

  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:

    handle_response(txnp);
    return 0;

  default:
    break;
  }

  return 0;
}



/* 
 *  Global statistics functions: 
 */

void
init_stats(void)
{
  LOG_SET_FUNCTION_NAME("init_stats");

  /* noncoupled: */
  method_count_redirected_connect = INKStatCreate("method.count.redirected.connect", INKSTAT_TYPE_INT64);
  method_count_redirected_delete = INKStatCreate("method.count.redirected.delete", INKSTAT_TYPE_INT64);
  method_count_redirected_get = INKStatCreate("method.count.redirected.get", INKSTAT_TYPE_INT64);
  method_count_redirected_head = INKStatCreate("method.count.redirected.head", INKSTAT_TYPE_FLOAT);
  method_count_redirected_icp_query = INKStatCreate("method.count.redirected.icp_query", INKSTAT_TYPE_FLOAT);
  method_count_redirected_options = INKStatCreate("method.count.redirected.options", INKSTAT_TYPE_INT64);
  method_count_redirected_post = INKStatCreate("method.count.redirected.post", INKSTAT_TYPE_INT64);
  method_count_redirected_purge = INKStatCreate("method.count.redirected.purge", INKSTAT_TYPE_INT64);
  method_count_redirected_put = INKStatCreate("method.count.redirected.put", INKSTAT_TYPE_INT64);
  method_count_redirected_trace = INKStatCreate("method.count.redirected.trace", INKSTAT_TYPE_INT64);
  method_count_redirected_unknown = INKStatCreate("method.count.redirected.unknown", INKSTAT_TYPE_INT64);

  /* coupled: */
  request_outcomes = INKStatCoupledGlobalCategoryCreate("request_outcomes");
  requests_all = INKStatCoupledGlobalAdd(request_outcomes, "requests.all", INKSTAT_TYPE_FLOAT);
  requests_redirects = INKStatCoupledGlobalAdd(request_outcomes, "requests.redirects", INKSTAT_TYPE_INT64);
  requests_unchanged = INKStatCoupledGlobalAdd(request_outcomes, "requests.unchanged", INKSTAT_TYPE_INT64);

  /* negative test */
#ifdef DEBUG
  if (INKStatCoupledGlobalCategoryCreate(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledGlobalCategoryCreate");
  }

  if (INKStatCoupledGlobalAdd(NULL, "requests.mytest", INKSTAT_TYPE_INT64) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledGlobalAdd");
  }
  if (INKStatCoupledGlobalAdd(request_outcomes, NULL, INKSTAT_TYPE_INT64) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKStatCoupledGlobalAdd");
  }
#endif
}

/*
 *	This function is only called for redirected requests.  It illustrates
 *	several different ways of updating INT64 stats.  Some may consider
 *	the particular use of INKDecrementStat() shown below somewhat contrived.
 */
void
update_redirected_method_stats(INKMBuffer bufp, INKMLoc hdr_loc)
{
  const char *txn_method;
  int length;
  INK64 tempint;

  LOG_SET_FUNCTION_NAME("update_redirected_method_stats");

  txn_method = INKHttpHdrMethodGet(bufp, hdr_loc, &length);

  if (NULL != txn_method) {
    if (0 == strncmp(txn_method, INK_HTTP_METHOD_CONNECT, length))
      INKStatIncrement(method_count_redirected_connect);
    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_DELETE, length))
      INKStatIncrement(method_count_redirected_delete);
    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_GET, length))
      INKStatIncrement(method_count_redirected_get);

    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_HEAD, length))
      INKStatFloatAddTo(method_count_redirected_head, 1);
    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_ICP_QUERY, length))
      INKStatFloatAddTo(method_count_redirected_icp_query, 1);

    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_OPTIONS, length)) {
      tempint = INKStatIntRead(method_count_redirected_options);
      tempint++;
      INKStatIntSet(method_count_redirected_options, tempint);
    } else if (0 == strncmp(txn_method, INK_HTTP_METHOD_POST, length)) {
      INKStatDecrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
    }

    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_PURGE, length))
      INKStatIncrement(method_count_redirected_purge);
    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_PUT, length))
      INKStatIncrement(method_count_redirected_put);
    else if (0 == strncmp(txn_method, INK_HTTP_METHOD_TRACE, length))
      INKStatIncrement(method_count_redirected_trace);
    else
      INKStatIncrement(method_count_redirected_unknown);
  }
  INKHandleStringRelease(bufp, hdr_loc, txn_method);

  /* negative test */
#ifdef DEBUG
  if (INKStatIntSet(NULL, 0) != INK_ERROR) {
    LOG_ERROR_NEG("INKStatIntSet");
  }

  if (INKStatDecrement(NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKStatDecrement");
  }
#endif

}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 3.5.2 to run */
    if (major_ts_version > 3) {
      result = 1;
    } else if (major_ts_version == 3) {
      if (minor_ts_version > 5) {
        result = 1;
      } else if (minor_ts_version == 5) {
        if (patch_ts_version >= 2) {
          result = 1;
        }
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  const char prefix[] = "http://";
  int uri_len;
  INKPluginRegistrationInfo info;

  info.plugin_name = "redirect-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }

  if (argc == 3) {
    block_ip = INKstrdup(argv[1]);

    /*
     *   The Location header must contain an absolute URI: 
     */

    url_redirect = INKstrdup(argv[2]);
    uri_len = strlen(prefix) + strlen(url_redirect) + 1;
    uri_redirect = INKmalloc(uri_len);
    strcpy(uri_redirect, prefix);
    strcat(uri_redirect, url_redirect);

  } else {
    INKError("Incorrect syntax in plugin.conf:  correct usage is" "redirect-1.so ip_deny url_redirect");
    return;
  }

  ip_deny = inet_addr(block_ip);

  INKHttpHookAdd(INK_HTTP_READ_REQUEST_HDR_HOOK, INKContCreate(redirect_plugin, NULL));

  INKDebug("redirect_init", "block_ip is %s, url_redirect is %s, and uri_redirect is %s",
           block_ip, url_redirect, uri_redirect);
  INKDebug("redirect_init", "ip_deny is %ld\n", ip_deny);

  INKDebug("redirect_init", "initializing stats...");
  init_stats();


  /*
   *  Demonstrate another tracing function.  This can be used to
   *  enable debug calculations and other work that should only
   *  be done in debug mode.
   */

  if (INKIsDebugTagSet("redirect_demo"))
    INKDebug("redirect_init", "The redirect_demo tag is set");
  else
    INKDebug("redirect_init", "The redirect_demo tag is not set");
}
