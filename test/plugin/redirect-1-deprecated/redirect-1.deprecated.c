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
static in_addr_t clientip;
#else
static unsigned int ip_deny;
static unsigned int clientip;
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


static void
handle_client_lookup(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc, url_loc;

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
    goto done;
  }

  host = INKUrlHostGet(bufp, url_loc, NULL);
  if (!host) {
    INKError("couldn't retrieve request hostname\n");
    goto done;
  }

  /* 
   *   Check to see if the client is already headed to the redirect site.
   */
  if (strcmp(host, url_redirect) == 0) {
    goto done;
  }

  if (ip_deny == clientip) {

    INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);

    update_redirected_method_stats(bufp, hdr_loc);

    /* 
     *   Increment the local redirect stat and do global update: 
     */
    INKStatIncrement(local_requests_redirects);
    INKStatsCoupledUpdate(local_request_outcomes);

    return;
  }

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);

  /* 
   * Increment the local number unchanged stat and do global update: 
   */
  INKStatIncrement(local_requests_unchanged);
  INKStatsCoupledUpdate(local_request_outcomes);
  INKStatCoupledLocalCopyDestroy(local_request_outcomes);
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
  INKHttpHdrReasonSet(bufp, hdr_loc, INKHttpHdrReasonLookup(INK_HTTP_STATUS_MOVED_PERMANENTLY), -1);

  newfield_loc = INKMimeFieldCreate(bufp);
  INKMimeFieldNameSet(bufp, newfield_loc, INK_MIME_FIELD_LOCATION, -1);
  INKMimeFieldValueInsert(bufp, newfield_loc, uri_redirect, -1, -1);
  INKMimeHdrFieldInsert(bufp, hdr_loc, newfield_loc, -1);


  /* 
   *  Note that we can't directly use errormsg_body, as INKHttpTxnBodySet()
   *  will try to free the passed buffer with INKfree().
   */
  tmp_body = INKstrdup(errormsg_body);
  INKHttpTxnErrorBodySet(txnp, tmp_body, strlen(tmp_body), NULL);


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

  txn_method = INKHttpHdrMethodGet(bufp, hdr_loc, &length);

  if (NULL != txn_method) {
    if (0 == strcmp(txn_method, INK_HTTP_METHOD_CONNECT))
      INKStatIncrement(method_count_redirected_connect);
    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_DELETE))
      INKStatIncrement(method_count_redirected_delete);
    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_GET))
      INKStatIncrement(method_count_redirected_get);

    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_HEAD))
      INKStatFloatAddTo(method_count_redirected_head, 1);
    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_ICP_QUERY))
      INKStatFloatAddTo(method_count_redirected_icp_query, 1);

    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_OPTIONS)) {
      tempint = INKStatIntRead(method_count_redirected_options);
      tempint++;
      INKStatIntSet(method_count_redirected_options, tempint);
    } else if (0 == strcmp(txn_method, INK_HTTP_METHOD_POST)) {
      INKStatDecrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
    }

    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_PURGE))
      INKStatIncrement(method_count_redirected_purge);
    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_PUT))
      INKStatIncrement(method_count_redirected_put);
    else if (0 == strcmp(txn_method, INK_HTTP_METHOD_TRACE))
      INKStatIncrement(method_count_redirected_trace);
    else
      INKStatIncrement(method_count_redirected_unknown);
  }

}



void
INKPluginInit(int argc, const char *argv[])
{
  const char prefix[] = "http://";
  int uri_len;

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
