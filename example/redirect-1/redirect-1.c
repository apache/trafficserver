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
 * 	  redirect-1.so block_ip url_redirect
 *
 *
 */

#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ts/ts.h>

static in_addr_t ip_deny;

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

void update_redirected_method_stats(TSMBuffer bufp, TSMLoc hdr_loc);

static char *url_redirect;
static char *uri_redirect;
static char *block_ip;

static void
handle_client_lookup(TSHttpTxn txnp, TSCont contp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc, url_loc;
  int host_length;

  in_addr_t clientip = 0;

  const char *host;

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
  local_requests_all       = INKStatCoupledLocalAdd(local_request_outcomes, "requests.all.local", INKSTAT_TYPE_FLOAT);
  local_requests_redirects = INKStatCoupledLocalAdd(local_request_outcomes, "requests.redirects.local", INKSTAT_TYPE_INT64);
  local_requests_unchanged = INKStatCoupledLocalAdd(local_request_outcomes, "requests.unchanged.local", INKSTAT_TYPE_INT64);

  /*
   *   Increment the count of total requests:
   *     (it is more natural to treat the number of requests as an
   *      integer, but we declare this a FLOAT in order to demonstrate
   *      how to increment coupled FLOAT stats)
   */
  INKStatFloatAddTo(local_requests_all, 1.0);

  if (TSIsDebugTagSet("redirect")) {
    struct sockaddr const *addr = TSHttpTxnClientAddrGet(txnp);

    if (addr) {
      socklen_t addr_size = 0;

      if (addr->sa_family == AF_INET)
        addr_size = sizeof(struct sockaddr_in);
      else if (addr->sa_family == AF_INET6)
        addr_size = sizeof(struct sockaddr_in6);
      if (addr_size > 0) {
        char clientstring[INET6_ADDRSTRLEN];

        if (NULL != inet_ntop(addr->sa_family, addr, clientstring, addr_size))
          TSDebug("redirect", "clientip is %s and block_ip is %s", clientstring, block_ip);
      }
    }
  }

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[redirect-1] Couldn't retrieve client request header");
    goto done;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[redirect-1] Couldn't retrieve request url");
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = TSUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    TSError("[redirect-1] Couldn't retrieve request hostname");
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

  /* TODO: This is odd, clientip is never set ... */
  if (ip_deny == clientip) {
    TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

    update_redirected_method_stats(bufp, hdr_loc);

    TSHandleMLocRelease(bufp, hdr_loc, url_loc);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);

    /*
     *   Increment the local redirect stat and do global update:
     */
    INKStatIncrement(local_requests_redirects);
    INKStatsCoupledUpdate(local_request_outcomes);
    INKStatCoupledLocalCopyDestroy(local_request_outcomes);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return;
  }

done:
  /*
   * Increment the local number unchanged stat and do global update:
   */
  INKStatIncrement(local_requests_unchanged);
  INKStatsCoupledUpdate(local_request_outcomes);
  INKStatCoupledLocalCopyDestroy(local_request_outcomes);

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
}

static void
handle_response(TSHttpTxn txnp)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc, newfield_loc;
  char *errormsg_body = "All requests from this IP address are redirected.\n";
  char *tmp_body;

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[redirect-1] Couldn't retrieve client response header");
    goto done;
  }

  TSHttpHdrStatusSet(bufp, hdr_loc, TS_HTTP_STATUS_MOVED_PERMANENTLY);
  TSHttpHdrReasonSet(bufp, hdr_loc, TSHttpHdrReasonLookup(TS_HTTP_STATUS_MOVED_PERMANENTLY),
                     strlen(TSHttpHdrReasonLookup(TS_HTTP_STATUS_MOVED_PERMANENTLY)));

  TSMimeHdrFieldCreate(bufp, hdr_loc, &newfield_loc); /* Probably should check for errors ... */
  TSMimeHdrFieldNameSet(bufp, hdr_loc, newfield_loc, TS_MIME_FIELD_LOCATION, TS_MIME_LEN_LOCATION);
  TSMimeHdrFieldValueStringInsert(bufp, hdr_loc, newfield_loc, -1, uri_redirect, strlen(uri_redirect));
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
  TSHttpTxn txnp = (TSHttpTxn)edata;

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
  /* noncoupled: */
  method_count_redirected_connect   = INKStatCreate("method.count.redirected.connect", INKSTAT_TYPE_INT64);
  method_count_redirected_delete    = INKStatCreate("method.count.redirected.delete", INKSTAT_TYPE_INT64);
  method_count_redirected_get       = INKStatCreate("method.count.redirected.get", INKSTAT_TYPE_INT64);
  method_count_redirected_head      = INKStatCreate("method.count.redirected.head", INKSTAT_TYPE_FLOAT);
  method_count_redirected_icp_query = INKStatCreate("method.count.redirected.icp_query", INKSTAT_TYPE_FLOAT);
  method_count_redirected_options   = INKStatCreate("method.count.redirected.options", INKSTAT_TYPE_INT64);
  method_count_redirected_post      = INKStatCreate("method.count.redirected.post", INKSTAT_TYPE_INT64);
  method_count_redirected_purge     = INKStatCreate("method.count.redirected.purge", INKSTAT_TYPE_INT64);
  method_count_redirected_put       = INKStatCreate("method.count.redirected.put", INKSTAT_TYPE_INT64);
  method_count_redirected_trace     = INKStatCreate("method.count.redirected.trace", INKSTAT_TYPE_INT64);
  method_count_redirected_unknown   = INKStatCreate("method.count.redirected.unknown", INKSTAT_TYPE_INT64);

  /* coupled: */
  request_outcomes   = INKStatCoupledGlobalCategoryCreate("request_outcomes");
  requests_all       = INKStatCoupledGlobalAdd(request_outcomes, "requests.all", INKSTAT_TYPE_FLOAT);
  requests_redirects = INKStatCoupledGlobalAdd(request_outcomes, "requests.redirects", INKSTAT_TYPE_INT64);
  requests_unchanged = INKStatCoupledGlobalAdd(request_outcomes, "requests.unchanged", INKSTAT_TYPE_INT64);
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
  int64_t tempint;

  txn_method = TSHttpHdrMethodGet(bufp, hdr_loc, &length);

  if (NULL != txn_method) {
    if (0 == strncmp(txn_method, TS_HTTP_METHOD_CONNECT, length))
      INKStatIncrement(method_count_redirected_connect);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_DELETE, length))
      INKStatIncrement(method_count_redirected_delete);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_GET, length))
      INKStatIncrement(method_count_redirected_get);

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_HEAD, length))
      INKStatFloatAddTo(method_count_redirected_head, 1);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_ICP_QUERY, length))
      INKStatFloatAddTo(method_count_redirected_icp_query, 1);

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_OPTIONS, length)) {
      tempint = INKStatIntGet(method_count_redirected_options);
      tempint++;
      INKStatIntSet(method_count_redirected_options, tempint);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_POST, length)) {
      INKStatDecrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
      INKStatIncrement(method_count_redirected_post);
    }

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PURGE, length))
      INKStatIncrement(method_count_redirected_purge);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PUT, length))
      INKStatIncrement(method_count_redirected_put);
    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_TRACE, length))
      INKStatIncrement(method_count_redirected_trace);
    else
      INKStatIncrement(method_count_redirected_unknown);
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  const char prefix[] = "http://";
  int uri_len;
  TSPluginRegistrationInfo info;

  info.plugin_name   = "redirect-1";
  info.vendor_name   = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[redirect-1] Plugin registration failed.");
  }

  if (argc == 3) {
    block_ip = TSstrdup(argv[1]);

    /*
     *   The Location header must contain an absolute URI:
     */

    url_redirect = TSstrdup(argv[2]);
    uri_len      = strlen(prefix) + strlen(url_redirect) + 1;
    uri_redirect = TSmalloc(uri_len);
    TSstrlcpy(uri_redirect, prefix, uri_len);
    TSstrlcat(uri_redirect, url_redirect, uri_len);

  } else {
    TSError("[redirect-1] Incorrect syntax in plugin.conf:  correct usage is"
            "redirect-1.so ip_deny url_redirect");
    return;
  }

  ip_deny = inet_addr(block_ip);

  TSDebug("redirect_init", "initializing stats...");
  init_stats();
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(redirect_plugin, NULL));

  TSDebug("redirect_init", "block_ip is %s, url_redirect is %s, and uri_redirect is %s", block_ip, url_redirect, uri_redirect);
  // ToDo: Should figure out how to print IPs which are IPv4 / v6.
  // TSDebug("redirect_init", "ip_deny is %ld\n", ip_deny);

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
