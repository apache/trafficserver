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

/*  InkAPIPrivate.h
 *
 *
 *   Interfaces in this header file are experimental, undocumented and 
 *   are subject to change even across minor releases of Traffic Server.
 *   None of the interfaces in this file are committed to be stable 
 *   unless they are migrated to ts/ts.h  If you require stable APIs to 
 *   Traffic Server, DO NOT USE anything in this file.
 *
 *   $Revision: 1.3 $ $Date: 2003-06-01 18:36:51 $
 */

#ifndef __INK_API_PRIVATE_H__
#define __INK_API_PRIVATE_H__

#include "InkAPIPrivateFrozen.h"

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */


/* Do not edit these apis, used internally */
  inkapi int INKMimeHdrFieldEqual(INKMBuffer bufp, INKMLoc hdr_obj, INKMLoc field1, INKMLoc field2);
  inkapi const char *INKMimeHdrFieldValueGetRaw(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, int *value_len_ptr);
  inkapi INKReturnCode INKMimeHdrFieldValueSetRaw(INKMBuffer bufp, INKMLoc hdr, INKMLoc field, const char *value,
                                                  int length);


  inkapi int INKHttpTxnHookRegisteredFor(INKHttpTxn txnp, INKHttpHookID id, INKEventFunc funcp);

/* IP Lookup */
#define                  INK_IP_LOOKUP_STATE_INVALID 0
  typedef void (*INKIPLookupPrintFunc) (void *data);
  inkapi void INKIPLookupPrint(INKIPLookup iplu, INKIPLookupPrintFunc pf);


/* api functions to access stats */
/* ClientResp APIs exist as well and are exposed in PrivateFrozen for DI */
  inkapi int INKHttpTxnClientReqHdrBytesGet(INKHttpTxn txnp, int *bytes);
  inkapi int INKHttpTxnClientReqBodyBytesGet(INKHttpTxn txnp, int *bytes);
  inkapi int INKHttpTxnServerReqHdrBytesGet(INKHttpTxn txnp, int *bytes);
  inkapi int INKHttpTxnServerReqBodyBytesGet(INKHttpTxn txnp, int *bytes);
  inkapi int INKHttpTxnPushedRespHdrBytesGet(INKHttpTxn txnp, int *bytes);
  inkapi int INKHttpTxnPushedRespBodyBytesGet(INKHttpTxn txnp, int *bytes);

/* used in internal sample plugin_as_origin */
  inkapi int INKHttpTxnNextHopPortGet(INKHttpTxn txnp);

/* for Media-IXT mms over http */
  typedef enum
  {
    INK_HTTP_CNTL_GET_LOGGING_MODE,
    INK_HTTP_CNTL_SET_LOGGING_MODE,
    INK_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE,
    INK_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE
  } INKHttpCntlType;

#define INK_HTTP_CNTL_OFF  (void*) 0
#define INK_HTTP_CNTL_ON   (void*) 1
/* usage: 
   void *onoff = 0;
   INKHttpTxnCntl(.., INK_HTTP_CNTL_GET_LOGGING_MODE, &onoff);
   if (onoff == INK_HTTP_CNTL_ON) ....
*/
  inkapi int INKHttpTxnCntl(INKHttpTxn txnp, INKHttpCntlType cntl, void *data);

/* Protocols APIs */
  inkapi void INKVConnCacheHttpInfoSet(INKVConn connp, INKCacheHttpInfo infop);

/* NetVC API. Experimental, developed for RAFT. No longer used. */
  inkapi void INKVConnInactivityTimeoutSet(INKVConn connp, int timeout);
  inkapi void INKVConnInactivityTimeoutCancel(INKVConn connp);

/* ICP freshness functions */
  typedef int (*INKPluginFreshnessCalcFunc) (INKCont contp);
  inkapi void INKICPFreshnessFuncSet(INKPluginFreshnessCalcFunc funcp);

  inkapi int INKICPCachedReqGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj);
  inkapi int INKICPCachedRespGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* __INK_API_PRIVATE_H__ */
