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
 *   redirect_1.c:
 *	an example program which redirects clients based on the source IP
 *
 *
 *	Usage:
 * 	  redirect_1.so block_ip url_redirect
 *
 *
 */

#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ts/ts.h>

#define PLUGIN_NAME "redirect_1"
#define STAT_PREFIX "plugin." PLUGIN_NAME "."

static in_addr_t ip_deny;

// The created stat indices will be held in these variables.
static int redirect_count_connect;
static int redirect_count_delete;
static int redirect_count_get;
static int redirect_count_head;
static int redirect_count_options;
static int redirect_count_post;
static int redirect_count_purge;
static int redirect_count_put;
static int redirect_count_trace;
static int redirect_count_unknown;

static int requests_redirects;
static int requests_unchanged;

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

  if (TSIsDebugTagSet("redirect")) {
    struct sockaddr const *addr = TSHttpTxnClientAddrGet(txnp);

    if (addr) {
      socklen_t addr_size = 0;

      if (addr->sa_family == AF_INET) {
        addr_size = sizeof(struct sockaddr_in);
      } else if (addr->sa_family == AF_INET6) {
        addr_size = sizeof(struct sockaddr_in6);
      }
      if (addr_size > 0) {
        char clientstring[INET6_ADDRSTRLEN];

        if (NULL != inet_ntop(addr->sa_family, addr, clientstring, addr_size)) {
          TSDebug(PLUGIN_NAME, "clientip is %s and block_ip is %s", clientstring, block_ip);
        }
      }
    }
  }

  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    goto done;
  }

  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = TSUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    TSError("[%s] Couldn't retrieve request hostname", PLUGIN_NAME);
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
    TSStatIntIncrement(requests_redirects, 1);

    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return;
  }

done:
  // Increment the local number unchanged stat and do global update:
  TSStatIntIncrement(requests_unchanged, 1);

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
    TSError("[%s] Couldn't retrieve client response header", PLUGIN_NAME);
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
  redirect_count_connect =
    TSStatCreate(STAT_PREFIX "count.connect", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_delete = TSStatCreate(STAT_PREFIX "count.delete", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_get    = TSStatCreate(STAT_PREFIX "count.get", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_head   = TSStatCreate(STAT_PREFIX "count.head", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_options =
    TSStatCreate(STAT_PREFIX "count.options", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_post  = TSStatCreate(STAT_PREFIX "count.post", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_purge = TSStatCreate(STAT_PREFIX "count.purge", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_put   = TSStatCreate(STAT_PREFIX "count.put", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_trace = TSStatCreate(STAT_PREFIX "count.trace", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  redirect_count_unknown =
    TSStatCreate(STAT_PREFIX "count.unknown", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);

  requests_redirects = TSStatCreate(STAT_PREFIX "total.redirects", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
  requests_unchanged = TSStatCreate(STAT_PREFIX "total.unchanged", TS_RECORDDATATYPE_INT, TS_STAT_NON_PERSISTENT, TS_STAT_SYNC_SUM);
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
    if (0 == strncmp(txn_method, TS_HTTP_METHOD_CONNECT, length)) {
      TSStatIntIncrement(redirect_count_connect, 1);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_DELETE, length)) {
      TSStatIntIncrement(redirect_count_delete, 1);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_GET, length)) {
      TSStatIntIncrement(redirect_count_get, 1);

    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_HEAD, length)) {
      TSStatIntIncrement(redirect_count_head, 1);

    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_OPTIONS, length)) {
      // This is a bad idea in a real plugin because it causes a race condition
      // with other transactions, but is here for illustrative purposes.
      tempint = TSStatIntGet(redirect_count_options);
      ++tempint;
      TSStatIntSet(redirect_count_options, tempint);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_POST, length)) {
      // Illustrative only.
      TSStatIntDecrement(redirect_count_post, 1);
      TSStatIntIncrement(redirect_count_post, 2);
    }

    else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PURGE, length)) {
      TSStatIntIncrement(redirect_count_purge, 1);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_PUT, length)) {
      TSStatIntIncrement(redirect_count_put, 1);
    } else if (0 == strncmp(txn_method, TS_HTTP_METHOD_TRACE, length)) {
      TSStatIntIncrement(redirect_count_trace, 1);
    } else {
      TSStatIntIncrement(redirect_count_unknown, 1);
    }
  }
}

void
TSPluginInit(int argc, const char *argv[])
{
  const char prefix[] = "http://";
  int uri_len;
  TSPluginRegistrationInfo info;

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "Apache Software Foundation";
  info.support_email = "dev@trafficserver.apache.org";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);
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
    TSError("[%s] Incorrect syntax in plugin.conf:  correct usage is", PLUGIN_NAME "redirect_1.so ip_deny url_redirect");
    return;
  }

  ip_deny = inet_addr(block_ip);

  TSDebug(PLUGIN_NAME, "initializing stats");
  init_stats();
  TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, TSContCreate(redirect_plugin, NULL));

  TSDebug(PLUGIN_NAME ".init", "block_ip is %s, url_redirect is %s, and uri_redirect is %s", block_ip, url_redirect, uri_redirect);

  /*
   *  Demonstrate another tracing function.  This can be used to
   *  enable debug calculations and other work that should only
   *  be done in debug mode.
   */

  if (TSIsDebugTagSet(PLUGIN_NAME ".demo")) {
    TSDebug(PLUGIN_NAME ".init", "The redirect_demo tag is set");
  } else {
    TSDebug(PLUGIN_NAME ".init", "The redirect_demo tag is not set");
  }
}
