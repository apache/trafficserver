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

#include "ts.h"

#if !defined (_WIN32)
static in_addr_t ip_deny;
#else
static unsigned int ip_deny;
#endif

/*
 * uncoupled statistics variables:
 */
static TSStat method_count_redirected_connect;
static TSStat method_count_redirected_delete;
static TSStat method_count_redirected_get;
static TSStat method_count_redirected_head;
static TSStat method_count_redirected_icp_query;
static TSStat method_count_redirected_options;
static TSStat method_count_redirected_post;
static TSStat method_count_redirected_purge;
static TSStat method_count_redirected_put;
static TSStat method_count_redirected_trace;
static TSStat method_count_redirected_unknown;


/*
 *	coupled statistics variables:
 *		coupled stat category for the following stats
 *              is request_outcomes. The relationship among the stats is:
 *		requests_all = requests_redirects + requests_unchanged
 */
static TSCoupledStat request_outcomes;
static TSStat requests_all;
static TSStat requests_redirects;
static TSStat requests_unchanged;


void update_redirected_method_stats(TSMBuffer bufp, TSMLoc hdr_loc);

static char *url_redirect;
static char *uri_redirect;
static char *block_ip;

#define PLUGIN_NAME "redirect-1-neg"
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_ERROR_NEG(API_NAME) { \
    TSDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

static void
handle_client_lookup(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc, url_loc;
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
  TSCoupledStat local_request_outcomes;
  TSStat local_requests_all;
  TSStat local_requests_redirects;
  TSStat local_requests_unchanged;

  LOG_SET_FUNCTION_NAME("handle_client_lookup");

  /*
   *  Create local copy of the global coupled stat category:
   */
  local_request_outcomes = TSStatCoupledLocalCopyCreate("local_request_outcomes", request_outcomes);


  /*
   * Create the local copies of the global coupled stats:
   */
  local_requests_all = TSStatCoupledLocalAdd(local_request_outcomes, "requests.all.local", TSSTAT_TYPE_FLOAT);
  local_requests_redirects = TSStatCoupledLocalAdd(local_request_outcomes,
                                                    "requests.redirects.local", TSSTAT_TYPE_INT64);
  local_requests_unchanged = TSStatCoupledLocalAdd(local_request_outcomes,
                                                    "requests.unchanged.local", TSSTAT_TYPE_INT64);


  /*
   *   Increment the count of total requests:
   *     (it is more natural to treat the number of requests as an
   *      integer, but we declare this a FLOAT in order to demonstrate
   *      how to increment coupled FLOAT stats)
   */
  TSStatFloatAddTo(local_requests_all, 1.0);

  /* negative test */
#ifdef DEBUG
  if (TSStatCoupledLocalCopyCreate(NULL, request_outcomes) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledLocalCopyCreate");
  }
  if (TSStatCoupledLocalCopyCreate("my_local_copy", NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledLocalCopyCreate");
  }

  if (TSStatCoupledLocalAdd(NULL, "requests.negtest", TSSTAT_TYPE_INT64) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledLocalAdd");
  }
  if (TSStatCoupledLocalAdd(local_request_outcomes, NULL, TSSTAT_TYPE_INT64) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledLocalAdd");
  }

  if (TSStatFloatAddTo(NULL, 1.0) != TS_ERROR) {
    LOG_ERROR_NEG("TSStatFloatAddTo");
  }

  if (TSHttpTxnClientIPGet(NULL) != 0) {
    LOG_ERROR_NEG("TSHttpTxnClientIPGet");
  }
#endif

#if !defined (_WIN32)
  clientip = (in_addr_t) TSHttpTxnClientIPGet(txnp);
#else
  clientip = TSHttpTxnClientIPGet(txnp);
#endif

  tempstruct.s_addr = clientip;
  clientstring = inet_ntoa(tempstruct);
  TSDebug("redirect", "clientip is %s and block_ip is %s", clientstring, block_ip);

  if (!TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    TSError("couldn't retrieve client request header\n");
    goto done;
  }

  url_loc = TSHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    TSError("couldn't retrieve request url\n");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = TSUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    TSError("couldn't retrieve request hostname\n");
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  /*
   *   Check to see if the client is already headed to the redirect site.
   */
  if (strncmp(host, url_redirect, host_length) == 0) {
    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  if (ip_deny == clientip) {

    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

    update_redirected_method_stats(bufp, hdr_loc);

    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    /*
     *   Increment the local redirect stat and do global update:
     */
    TSStatIncrement(local_requests_redirects);
    TSStatsCoupledUpdate(local_request_outcomes);
    TSStatCoupledLocalCopyDestroy(local_request_outcomes);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return;
  }

done:
  /*
   * Increment the local number unchanged stat and do global update:
   */
  TSStatIncrement(local_requests_unchanged);
  TSStatsCoupledUpdate(local_request_outcomes);
  TSStatCoupledLocalCopyDestroy(local_request_outcomes);

  /* negative test */
#ifdef DEBUG
  if (TSStatsCoupledUpdate(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSStatsCoupledUpdate");
  }
  if (TSStatCoupledLocalCopyDestroy(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSStatCoupledLocalCopyDestroy");
  }
#endif

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}



static void
handle_response(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc, newfield_loc;
  TSMLoc url_loc;
  char *url_str;
  char *buf;
  char *errormsg_body = "All requests from this IP address are redirected.\n";
  char *tmp_body;

  if (!TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    TSError("couldn't retrieve client response header\n");
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_MOVED_PERMANENTLY);
  TSHttpHdrReasonSet(bufp, hdr_loc,
                      TSHttpHdrReasonLookup(TS_HTTP_STATUS_MOVED_PERMANENTLY),
                      strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_MOVED_PERMANENTLY)));

  newfield_loc = TSMimeHdrFieldCreate(bufp, hdr_loc);
  TSMimeHdrFieldNameSet(bufp, hdr_loc, newfield_loc, TS_MIME_FIELD_LOCATION, TS_MIME_LEN_LOCATION);
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, newfield_loc, uri_redirect, strlen(uri_redirect), -1);
  TSMimeHdrFieldAppend(bufp, hdr_loc, newfield_loc);


  /*
   *  Note that we can't directly use errormsg_body, as TSHttpTxnErrorBodySet()
   *  will try to free the passed buffer with TSfree().
   */
  tmp_body = TSstrdup(errormsg_body);
  TSHttpTxnErrorBodySet(txnp, tmp_body, strlen(tmp_body), NULL);
  TSHandleMLocRelease(bufp, hdr_loc, newfield_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);


done:
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}



static int
redirect_plugin(TSCont contp, TSEvent event, void *edata)
{

  TSHttpTxn txnp = (TSHttpTxn) edata;

  switch (event) {
  case TS_EVENT_HTTP_READ_REQUEST_HDR:

    handle_client_lookup(txnp, contp);
    return 0;

  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:

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
  method_count_redirected_connect = TSStatCreate("method.count.redirected.connect", TSSTAT_TYPE_INT64);
  method_count_redirected_delete = TSStatCreate("method.count.redirected.delete", TSSTAT_TYPE_INT64);
  method_count_redirected_get = TSStatCreate("method.count.redirected.get", TSSTAT_TYPE_INT64);
  method_count_redirected_head = TSStatCreate("method.count.redirected.head", TSSTAT_TYPE_FLOAT);
  method_count_redirected_icp_query = TSStatCreate("method.count.redirected.icp_query", TSSTAT_TYPE_FLOAT);
  method_count_redirected_options = TSStatCreate("method.count.redirected.options", TSSTAT_TYPE_INT64);
  method_count_redirected_post = TSStatCreate("method.count.redirected.post", TSSTAT_TYPE_INT64);
  method_count_redirected_purge = TSStatCreate("method.count.redirected.purge", TSSTAT_TYPE_INT64);
  method_count_redirected_put = TSStatCreate("method.count.redirected.put", TSSTAT_TYPE_INT64);
  method_count_redirected_trace = TSStatCreate("method.count.redirected.trace", TSSTAT_TYPE_INT64);
  method_count_redirected_unknown = TSStatCreate("method.count.redirected.unknown", TSSTAT_TYPE_INT64);

  /* coupled: */
  request_outcomes = TSStatCoupledGlobalCategoryCreate("request_outcomes");
  requests_all = TSStatCoupledGlobalAdd(request_outcomes, "requests.all", TSSTAT_TYPE_FLOAT);
  requests_redirects = TSStatCoupledGlobalAdd(request_outcomes, "requests.redirects", TSSTAT_TYPE_INT64);
  requests_unchanged = TSStatCoupledGlobalAdd(request_outcomes, "requests.unchanged", TSSTAT_TYPE_INT64);

  /* negative test */
#ifdef DEBUG
  if (TSStatCoupledGlobalCategoryCreate(NULL) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledGlobalCategoryCreate");
  }

  if (TSStatCoupledGlobalAdd(NULL, "requests.mytest", TSSTAT_TYPE_INT64) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledGlobalAdd");
  }
  if (TSStatCoupledGlobalAdd(request_outcomes, NULL, TSSTAT_TYPE_INT64) != TS_ERROR_PTR) {
    LOG_ERROR_NEG("TSStatCoupledGlobalAdd");
  }
#endif
}

/*
 *	This function is only called for redirected requests.  It illustrates
 *	several different ways of updating INT64 stats.  Some may consider
 *	the particular use of TSDecrementStat() shown below somewhat contrived.
 */
void
update_redirected_method_stats(TSMBuffer bufp, TSMLoc hdr_loc)
{
  const char *txn_method;
  int length;
  TS64 tempint;

  LOG_SET_FUNCTION_NAME("update_redirected_method_stats");

  txn_method = TSHttpHdrMethodGet(bufp, hdr_loc, &length);

  if (NULL != txn_method) {
    if (0 == strncmp(txn_method, TS_HTTP_METHOD_CONNECT, length))
      TSStatIncrement(method_count_redirected_connect);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_DELETE, length))
      TSStatIncrement(method_count_redirected_delete);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_GET, length))
      TSStatIncrement(method_count_redirected_get);

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_HEAD, length))
      TSStatFloatAddTo(method_count_redirected_head, 1);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_ICP_QUERY, length))
      TSStatFloatAddTo(method_count_redirected_icp_query, 1);

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_OPTIONS, length)) {
      TSStatIntGet(method_count_redirected_options, tempint);
      tempint++;
      TSStatIntSet(method_count_redirected_options, tempint);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_POST, length)) {
      TSStatDecrement(method_count_redirected_post);
      TSStatIncrement(method_count_redirected_post);
      TSStatIncrement(method_count_redirected_post);
    }

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PURGE, length))
      TSStatIncrement(method_count_redirected_purge);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PUT, length))
      TSStatIncrement(method_count_redirected_put);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_TRACE, length))
      TSStatIncrement(method_count_redirected_trace);
    else
      TSStatIncrement(method_count_redirected_unknown);
  }

  /* negative test */
#ifdef DEBUG
  if (TSStatIntSet(NULL, 0) != TS_ERROR) {
    LOG_ERROR_NEG("TSStatIntSet");
  }

  if (TSStatDecrement(NULL) != TS_ERROR) {
    LOG_ERROR_NEG("TSStatDecrement");
  }
#endif

}

int
check_ts_version()
{

  const char *ts_version = TSTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 2.0 to run */
    if (major_ts_version >= 2) {
      result = 1;
    }
  }

  return result;
}

void
TSPluginInit(int argc, const char *argv[])
{
  const char prefix[] = "http://";
  int uri_len;
  TSPluginRegistrationInfo info;

  info.plugin_name = "redirect-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!TSPluginRegister(TS_SDK_VERSION_2_0, &info)) {
    TSError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    TSError("Plugin requires Traffic Server 2.0 or later\n");
    return;
  }

  if (argc == 3) {
    block_ip = TSstrdup(argv[1]);

    /*
     *   The Location header must contain an absolute URI:
     */

    url_redirect = TSstrdup(argv[2]);
    uri_len = strlen(prefix) + strlen(url_redirect) + 1;
    uri_redirect = TSmalloc(uri_len);
    strcpy(uri_redirect, prefix);
    strcat(uri_redirect, url_redirect);

  } else {
    TSError("Incorrect syntax in plugin.conf:  correct usage is" "redirect-1.so ip_deny url_redirect");
    return;
  }

  ip_deny = inet_addr(block_ip);

  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(redirect_plugin, NULL));

  TSDebug("redirect_init", "block_ip is %s, url_redirect is %s, and uri_redirect is %s",
           block_ip, url_redirect, uri_redirect);
  TSDebug("redirect_init", "ip_deny is %ld\n", ip_deny);

  TSDebug("redirect_init", "initializing stats...");
  init_stats();


  /*
   *  Demonstrate another tracing function.  This can be used to
   *  enable debug calculations and other work that should only
   *  be done in debug mode.
   */

  if (TSIsDebugTagSet("redirect_demo"))
    TSDebug("redirect_init", "The redirect_demo tag is set");
  else
    TSDebug("redirect_init", "The redirect_demo tag is not set");
}
