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


#include "InkAPI.h"

#if 0
/* HTTP transactions */

/* Cached, get as soon as a transaction is available.
*/
inkapi int INKHttpTxnCachedReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
/* Cached, get as soon as a transaction is available.
*/
inkapi int INKHttpTxnCachedRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);

/* Client port */
inkapi int INKHttpTxnClientIncomingPortGet(INKHttpTxn txnp);

/* Client IP for a transaction (not incoming) */
inkapi unsigned int INKHttpTxnClientIPGet(INKHttpTxn txnp);

/* Non-cached, 
 * get client req after recieving INK_HTTP_READ_REQUEST_HDR_HOOK 
*/
inkapi int INKHttpTxnClientReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);
/* Non-cached, 
 * get "client" req after recieving INK_HTTP_READ_RESPONSE_HDR_HOOK 
*/
inkapi int INKHttpTxnClientRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);

/* This is a response to a client (req), which will be executed after reciept
 * of: 
 *   1. INK_HTTP_OS_DNS_HOOK fails for some reason to do the translation
 *   2. INK_HTTP_READ_RESPONSE_HDR_HOOK origin server replied with some 
 *      type of error. 
 *   3. An error is possible at any point in HTTP processing.
*/
inkapi void INKHttpTxnErrorBodySet(INKHttpTxn txnp, char *buf, int buflength, char *mimetype);

/* DONE */
inkapi void INKHttpTxnHookAdd(INKHttpTxn txnp, INKHttpHookID id, INKCont contp);

/* Origin Server (destination) or Parent IP */
inkapi unsigned int INKHttpTxnNextHopIPGet(INKHttpTxn txnp);

/* Results if parent proxy not enabled, results if parent proxy is enabled
*/
inkapi void INKHttpTxnParentProxyGet(INKHttpTxn txnp, char **hostname, int *port);

inkapi void INKHttpTxnParentProxySet(INKHttpTxn txnp, char *hostname, int port);

/*  */
inkapi void INKHttpTxnReenable(INKHttpTxn txnp, INKEvent event);

/* Origin Server IP */
inkapi unsigned int INKHttpTxnServerIPGet(INKHttpTxn txnp);

/* Need a transaction and a request: earliest point is 
 * not INK_HTTP_TXN_START_HOOK but INK_HTTP_READ_REQUEST_HDR_HOOK
 * process the request.
*/
inkapi int INKHttpTxnServerReqGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);

/* Need a transaction and a server response: earliest point is 
 * INK_HTTP_READ_RESPONSE_HDR_HOOK, then process the response. 
*/
inkapi int INKHttpTxnServerRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);


/* Call this as soon as a transaction has been created and retrieve the session
 * and do some processing.
*/
inkapi INKHttpSsn INKHttpTxnSsnGet(INKHttpTxn txnp);

/* Call this before a write to the cache is done, else too late for 
 * current txn:  
 * INK_HTTP_READ_RESPONSE_HDR_HOOK. 
 * default: transformed copy written to cache 
 * on == non-zero,	cache_transformed = true (default)
 * on == zero,		cache_transformed = false
*/
inkapi void INKHttpTxnTransformedRespCache(INKHttpTxn txnp, int on);

/* INK_HTTP_READ_RESPONSE_HOOK & INK_HTTP_RESPONSE_HDR_HOOK
 * Get the transform resp header from the HTTP transaction. 
 * re = 0 if dne, else 1.
*/
inkapi int INKHttpTxnTransformRespGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);

/* Call this before a write to the cache is done, else too late for 
 * current txn:  
 * INK_HTTP_READ_RESPONSE_HDR_HOOK. 
 * default: un-transformed copy not written to cache
 * on == non-zero,	cache_untransformed = true
 * on == zero,		cache_untransformed = false (default)
*/
inkapi void INKHttpTxnUntransformedRespCache(INKHttpTxn txnp, int on);

#endif

/* Run prototype code in this small plug-in. Then place this
 * code into it's own section.
*/
static int
INKProto(INKCont contp, INKEvent event, void *eData)
{

  INKHttpTxn txnp = (INKHttpTxn) eData;
  INKHttpSsn ssnp = (INKHttpSsn) eData;

  switch (event) {

  case INK_EVENT_HTTP_READ_RESPONSE_HDR:
    INKDebug("tag", "event %d received\n", event);
    /* INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE); */
    INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
    break;

  case INK_EVENT_HTTP_RESPONSE_TRANSFORM:
    INKDebug("tag", "event %d received\n", event);
    INKHttpSsnReenable(ssnp, INK_EVENT_HTTP_CONTINUE);
    break;
  default:
    INKDebug("tag", "Undefined event %d received\n");
    break;
  }

}

void
INKPluginInit(int argc, const char *argv[])
{
  INKCont contp = INKContCreate(INKProto, NULL);

  /* Context: INKHttpTxnTransformRespGet():
   * Q: are both of these received and if so, in what order? 
   */
  INKHttpHookAdd(INK_HTTP_READ_RESPONSE_HDR_HOOK, contp);
  INKHttpHookAdd(INK_HTTP_RESPONSE_TRANSFORM_HOOK, contp);
}
