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
 *   unless they are migrated to InkAPI.h  If you require stable APIs to 
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
#ifndef IDC
  inkapi void INKVConnCacheHttpInfoSet(INKVConn connp, INKCacheHttpInfo infop);
#else
  inkapi INKReturnCode INKVConnCacheHttpInfoSet(INKVConn connp, INKCacheHttpInfo infop);
#endif

/* NetVC API. Experimental, developed for RAFT. No longer used. */
  inkapi void INKVConnInactivityTimeoutSet(INKVConn connp, int timeout);
  inkapi void INKVConnInactivityTimeoutCancel(INKVConn connp);

/* ICP freshness functions */
  typedef int (*INKPluginFreshnessCalcFunc) (INKCont contp);
  inkapi void INKICPFreshnessFuncSet(INKPluginFreshnessCalcFunc funcp);

  inkapi int INKICPCachedReqGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj);
  inkapi int INKICPCachedRespGet(INKCont contp, INKMBuffer * bufp, INKMLoc * obj);

#ifdef IDC
// for the base_select_flag in HttpAltInfo
// Corresponding to enum BaseSelectFlags_t in HttpTransactCache.h
  enum ApiBaseSelectFlags
  {
    INK_API_BASE_SELECT_DONE = 0x0001,
    INK_API_BASE_SELECT_REPLACE = 0x0002
  };

  inkapi INKReturnCode INKHttpAltInfoBaseSelectFlagSet(INKHttpAltInfo infop, int flag);
  inkapi INKReturnCode INKCacheKeyBaseTxnSet(INKCacheKey key, INKHttpTxn txnp);
  inkapi int INKAtomicCAS(INK32 * ptr, INK32 old_value, INK32 new_value);
  inkapi int INKAtomicCAS64(INK64 * ptr, INK64 old_value, INK64 new_value);
  inkapi time_t INKHRTimeToSecond(INK64 hrtime);
  inkapi int INKHttpTxnIdGet(INKHttpTxn txnp, INKU64 * id);

/****************************************************************************
 * Api for writing HTTP objects directly into the cache.
 ***************************************************************************/
  inkapi INKReturnCode INKCacheHttpInfoCreate(INKCacheHttpInfo * new_info);
  inkapi INKReturnCode INKCacheHttpInfoClear(INKCacheHttpInfo infop);
  inkapi INKReturnCode INKCacheHttpInfoReqSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj);
  inkapi INKReturnCode INKCacheHttpInfoRespSet(INKCacheHttpInfo infop, INKMBuffer bufp, INKMLoc obj);
  inkapi INKReturnCode INKCacheHttpInfoReqSentTimeSet(INKCacheHttpInfo infop, time_t t);
  inkapi INKReturnCode INKCacheHttpInfoRespRecvTimeSet(INKCacheHttpInfo infop, time_t t);
  inkapi INKReturnCode INKCacheHttpInfoReqSentTimeGet(INKCacheHttpInfo infop, time_t * t);
  inkapi INKReturnCode INKCacheHttpInfoRespRecvTimeGet(INKCacheHttpInfo infop, time_t * t);

  inkapi INKReturnCode INKVConnCacheHttpInfoGet(INKVConn connp, INKCacheHttpInfo * infop_dest);

  inkapi INKReturnCode INKCacheKeyURLSet(INKCacheKey key, INKMBuffer bufp, INKMLoc obj);
  inkapi INKReturnCode INKCacheKeyReqSet(INKCacheKey key, INKMBuffer bufp, INKMLoc obj);
  inkapi INKReturnCode INKCacheKeyInfoSet(INKCacheKey key, INKCacheHttpInfo old_infop);

// added for BPMgmt plugin
  typedef void *INKIDCControlMatcher;
  typedef void *INKIDCControlResult;
  typedef void *INKIDCRequestData;
  typedef void *INKMD5;
  inkapi INKIDCControlMatcher INKIDCControlMatcherCreate(const char *file_var, const char *name, void *tags);
  inkapi INKIDCControlResult INKIDCControlResultCreate();
  inkapi INKIDCRequestData INKIDCRequestDataCreate(INKMBuffer url_bufp, INKMLoc url_offset);
  inkapi void INKIDCControlResultDestroy(INKIDCControlResult result);
  inkapi void INKIDCRequestDataDestroy(INKIDCRequestData rdata);
  inkapi void INKIDCControlGet(INKIDCControlMatcher matcher, INKIDCRequestData rdata, INKIDCControlResult result);
  inkapi int INKIsIDCEligible(INKIDCControlResult result);
  inkapi void INKMD5Create(char *buffer, int length, INKU64 * md5);

  typedef enum
  {
    INK_CONSUMER_CACHE,
    INK_CONSUMER_CLIENT
  } INKConsumerType;
  inkapi void INKConsumersOfTransformSetup(INKHttpTxn txnp, INKConsumerType type, int skip, int write);


/* Alarm */
  typedef enum
  {
    INK_SIGNAL_IDC_BPG_CONFIG_NOT_FOUND = 300,
    INK_SIGNAL_IDC_BPG_WENT_DOWN = 301
  } INKIDCAlarmType;

  inkapi int INKIDCSignalWarning(INKIDCAlarmType code, char *msg);

//inkapi int INKMD5Compare(INKMD5 md51, INKMD5 md52);
// added for BPMgmt Plugin ends
#endif

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* __INK_API_PRIVATE_H__ */
