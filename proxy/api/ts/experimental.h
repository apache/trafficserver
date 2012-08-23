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
 *   Interfaces in this header file are experimental, undocumented and
 *   are subject to change even across minor releases of Traffic Server.
 *   None of the interfaces in this file are committed to be stable
 *   unless they are migrated to ts/ts.h  If you require stable APIs to
 *   Traffic Server, DO NOT USE anything in this file.
 */

#ifndef __TS_API_EXPERIMENTAL_H__
#define __TS_API_EXPERIMENTAL_H__

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

  /* Cache APIs that are not yet fully supported and/or frozen nor complete. */
  tsapi TSReturnCode TSCacheBufferInfoGet(TSCacheTxn txnp, uint64_t *length, uint64_t *offset);

  tsapi TSCacheHttpInfo TSCacheHttpInfoCreate();
  tsapi void TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
  tsapi void TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
  tsapi void TSCacheHttpInfoReqSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
  tsapi void TSCacheHttpInfoRespSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
  tsapi void TSCacheHttpInfoKeySet(TSCacheHttpInfo infop, TSCacheKey key);
  tsapi void TSCacheHttpInfoSizeSet(TSCacheHttpInfo infop, int64_t size);
  tsapi int TSCacheHttpInfoVector(TSCacheHttpInfo infop, void *data, int length);
  tsapi time_t TSCacheHttpInfoReqSentTimeGet(TSCacheHttpInfo infop);
  tsapi time_t TSCacheHttpInfoRespReceivedTimeGet(TSCacheHttpInfo infop);
  int64_t TSCacheHttpInfoSizeGet(TSCacheHttpInfo infop);

  /* Do not edit these apis, used internally */
  tsapi int TSMimeHdrFieldEqual(TSMBuffer bufp, TSMLoc hdr_obj, TSMLoc field1, TSMLoc field2);
  tsapi TSReturnCode TSHttpTxnHookRegisteredFor(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp);

  /* for Media-IXT mms over http */
  typedef enum
    {
      TS_HTTP_CNTL_GET_LOGGING_MODE,
      TS_HTTP_CNTL_SET_LOGGING_MODE,
      TS_HTTP_CNTL_GET_INTERCEPT_RETRY_MODE,
      TS_HTTP_CNTL_SET_INTERCEPT_RETRY_MODE
    } TSHttpCntlType;

#define TS_HTTP_CNTL_OFF  (void*) 0
#define TS_HTTP_CNTL_ON   (void*) 1
  /* usage:
     void *onoff = 0;
     TSHttpTxnCntl(.., TS_HTTP_CNTL_GET_LOGGING_MODE, &onoff);
     if (onoff == TS_HTTP_CNTL_ON) ....
  */
  tsapi TSReturnCode TSHttpTxnCntl(TSHttpTxn txnp, TSHttpCntlType cntl, void *data);


  /* Protocols APIs */
  tsapi void TSVConnCacheHttpInfoSet(TSVConn connp, TSCacheHttpInfo infop);

  /* ICP freshness functions */
  typedef int (*TSPluginFreshnessCalcFunc) (TSCont contp);
  tsapi void TSICPFreshnessFuncSet(TSPluginFreshnessCalcFunc funcp);

  tsapi TSReturnCode TSICPCachedReqGet(TSCont contp, TSMBuffer *bufp, TSMLoc *obj);
  tsapi TSReturnCode TSICPCachedRespGet(TSCont contp, TSMBuffer *bufp, TSMLoc *obj);


  /* The rest is from the old "froze" private API include, we should consider
     moving some of these over to ts/ts.h as well. TODO */

  /****************************************************************************
   *  Test if cache ready to accept request for a specific type of data
   ****************************************************************************/
  tsapi TSReturnCode TSCacheDataTypeReady(TSCacheDataType type, int *is_ready);

  /****************************************************************************
   *  When reenabling a txn in error, keep the connection open in case
   *  of keepalive.
   ****************************************************************************/
  tsapi void TSHttpTxnClientKeepaliveSet(TSHttpTxn txnp, int set);

  /****************************************************************************
   *  Allow to set the body of a POST request.
   ****************************************************************************/
  tsapi void TSHttpTxnServerRequestBodySet(TSHttpTxn txnp, char *buf, int64_t buflength);

  /* ===== High Resolution Time ===== */
#define TS_HRTIME_FOREVER  HRTIME_FOREVER
#define TS_HRTIME_DECADE   HRTIME_DECADE
#define TS_HRTIME_YEAR     HRTIME_YEAR
#define TS_HRTIME_WEEK     HRTIME_WEEK
#define TS_HRTIME_DAY      HRTIME_DAY
#define TS_HRTIME_HOUR     HRTIME_HOUR
#define TS_HRTIME_MINUTE   HRTIME_MINUTE
#define TS_HRTIME_SECOND   HRTIME_SECOND
#define TS_HRTIME_MSECOND  HRTIME_MSECOND
#define TS_HRTIME_USECOND  HRTIME_USECOND
#define TS_HRTIME_NSECOND  HRTIME_NSECOND

#define TS_HRTIME_APPROX_SECONDS(_x) HRTIME_APPROX_SECONDS(_x)
#define TS_HRTIME_APPROX_FACTOR      HRTIME_APPROX_FACTOR

  /*
////////////////////////////////////////////////////////////////////
//
//	Map from units to ts_hrtime values
//
////////////////////////////////////////////////////////////////////
*/
#define TS_HRTIME_YEARS(_x)    HRTIME_YEARS(_x)
#define TS_HRTIME_WEEKS(_x)    HRTIME_WEEKS(_x)
#define TS_HRTIME_DAYS(_x)     HRTIME_DAYS(_x)
#define TS_HRTIME_HOURS(_x)    HRTIME_HOURS(_x)
#define TS_HRTIME_MINUTES(_x)  HRTIME_MINUTES(_x)
#define TS_HRTIME_SECONDS(_x)  HRTIME_SECONDS(_x)
#define TS_HRTIME_MSECONDS(_x) HRTIME_MSECONDS(_x)
#define TS_HRTIME_USECONDS(_x) HRTIME_USECONDS(_x)
#define TS_HRTIME_NSECONDS(_x) HRTIME_NSECONDS(_x)

  tsapi TSReturnCode TSHttpTxnCachedRespTimeGet(TSHttpTxn txnp, time_t *resp_time);

  /* ===== Cache ===== */
  tsapi TSReturnCode TSCacheKeyDataTypeSet(TSCacheKey key, TSCacheDataType type);


  /* ===== Utility ===== */
  /****************************************************************************
   *  Create a random number
   *  Return random integer between <X> and <Y>
   ****************************************************************************/
  tsapi unsigned int TSrandom(void);

  /****************************************************************************
   *  Create a random double
   *  Return random double between <X> and <Y>
   ****************************************************************************/
  tsapi double TSdrandom(void);

  /****************************************************************************
   *  Return Hi-resolution current time. (int64_t)
   ****************************************************************************/
  tsapi TSHRTime TShrtime(void);

  /* =====  CacheHttpInfo =====  */

  tsapi TSCacheHttpInfo TSCacheHttpInfoCopy(TSCacheHttpInfo infop);
  tsapi void TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
  tsapi void TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
  tsapi void TSCacheHttpInfoDestroy(TSCacheHttpInfo infop);


  /* =====  ICP =====  */
  tsapi void TSHttpIcpDynamicSet(int value);

  /****************************************************************************
   *  TSHttpTxnCacheLookupCountGet
   *  Return: TS_SUCESS/TS_ERROR
   ****************************************************************************/
  tsapi TSReturnCode TSHttpTxnCacheLookupCountGet(TSHttpTxn txnp, int *lookup_count);
  tsapi TSReturnCode TSHttpTxnNewCacheLookupDo(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc url_loc);
  tsapi TSReturnCode TSHttpTxnSecondUrlTryLock(TSHttpTxn txnp);
  tsapi TSReturnCode TSHttpTxnRedirectRequest(TSHttpTxn txnp, TSMBuffer bufp, TSMLoc url_loc);
  tsapi TSReturnCode TSHttpTxnCacheLookupSkip(TSHttpTxn txnp);
  tsapi TSReturnCode TSHttpTxnServerRespIgnore(TSHttpTxn txnp);
  tsapi TSReturnCode TSHttpTxnShutDown(TSHttpTxn txnp, TSEvent event);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  tsapi int TSHttpTxnClientReqIsServerStyle(TSHttpTxn txnp);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  tsapi void TSHttpTxnOverwriteExpireTime(TSHttpTxn txnp, time_t expire_time);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  tsapi TSReturnCode TSHttpTxnUpdateCachedObject(TSHttpTxn txnp);

  /****************************************************************************
   *  ??
   *  TODO: This returns a LookingUp_t value, we need to SDK'ify it.
   ****************************************************************************/
  tsapi int TSHttpTxnLookingUpTypeGet(TSHttpTxn txnp);


  /* =====  Matcher Utils =====  */
#define               TS_MATCHER_LINE_INVALID 0
  typedef struct tsapi_matcheline* TSMatcherLine;

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi char *TSMatcherReadIntoBuffer(char *file_name, int *file_len);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi char *TSMatcherTokLine(char *buffer, char **last);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi char *TSMatcherExtractIPRange(char *match_str, uint32_t *addr1, uint32_t *addr2);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi TSMatcherLine TSMatcherLineCreate();

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi void TSMatcherLineDestroy(TSMatcherLine ml);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi const char *TSMatcherParseSrcIPConfigLine(char *line, TSMatcherLine ml);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi char *TSMatcherLineName(TSMatcherLine ml, int element);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  tsapi char *TSMatcherLineValue(TSMatcherLine ml, int element);

  /****************************************************************************
   *  Set a records.config integer variable
   ****************************************************************************/
  tsapi TSReturnCode TSMgmtConfigIntSet(const char *var_name, TSMgmtInt value);

  /* ----------------------------------------------------------------------
   * Interfaces used by Wireless group
   * ---------------------------------------------------------------------- */

#define TS_NET_EVENT_DATAGRAM_READ_COMPLETE  TS_EVENT_INTERNAL_206
#define TS_NET_EVENT_DATAGRAM_READ_ERROR     TS_EVENT_INTERNAL_207
#define TS_NET_EVENT_DATAGRAM_WRITE_COMPLETE TS_EVENT_INTERNAL_208
#define TS_NET_EVENT_DATAGRAM_WRITE_ERROR    TS_EVENT_INTERNAL_209
#define TS_NET_EVENT_DATAGRAM_READ_READY     TS_EVENT_INTERNAL_210
#define TS_NET_EVENT_DATAGRAM_OPEN	      TS_EVENT_INTERNAL_211
#define TS_NET_EVENT_DATAGRAM_ERROR          TS_EVENT_INTERNAL_212

  typedef enum
    {
      TS_SIGNAL_WDA_BILLING_CONNECTION_DIED = 100,
      TS_SIGNAL_WDA_BILLING_CORRUPTED_DATA = 101,
      TS_SIGNAL_WDA_XF_ENGINE_DOWN = 102,
      TS_SIGNAL_WDA_RADIUS_CORRUPTED_PACKETS = 103
    } TSAlarmType;

  /* ===== Alarm ===== */
  /****************************************************************************
   *  ??
   *  contact: OXYGEN
   ****************************************************************************/
  tsapi void TSSignalWarning(TSAlarmType code, char *msg);

  /*****************************************************************************
   * 			Cluster RPC API support 			     *
   *****************************************************************************/
  /*
   *  Usage notes:
   *    1) User is responsible for marshalling and unmarshaling data.
   *    2) RPC message incompatiblities due to different plugin versions
   *	 must be dealt with by the user.
   *    3) Upon receipt of a machine offline, no guarantees are made about
   *	 messages sent prior to the machine offline.
   *    4) A node transitioning to the online state in an active cluster,
   *	 is assumed to have no prior knowledge of messages processed in
   *	 the past.
   *    5) Key point to reiterate, actions taken in the functions specified in
   *	 TSAddClusterStatusFunction() and TSAddClusterRPCFunction() must
   *       be non-blocking (i.e. usage of TSMutexLock() and file i/o is
   *       not allowed).
   *    6) TSSendClusterRPC() can only process TSClusterRPCMsg_t generated
   *	 by TSAllocClusterRPCMsg().  Failure to adhere to this rule will
   *	 result in heap corruption.
   *    7) Messages sent via TSSendClusterRPC() must be at least 4 bytes in
   * 	 length.
   *    8) The user is not provided with any alignment guarantees on the
   *	 'm_data' field in the TSClusterRPCMsg_t returned via
   *	 TSAllocClusterRPCMsg().  Assume byte alignment.
   *    9) TSSendClusterRPC() interface owns the memory and is responsible
   *       for freeing the memory.
   *   10) RPC functions defined via TSAddClusterRPCFunction() own the
   *       memory when invoked and are responsible for freeing it via
   *	 TSFreeRPCMsg().
   */
#define MAX_CLUSTER_NODES 256

  typedef struct TSClusterRPCHandle
  {
    int opaque[2];
  } TSClusterRPCHandle_t;

  typedef int TSClusterStatusHandle_t;
  typedef int TSNodeHandle_t;

  typedef struct TSClusterRPCMsg
  {
    TSClusterRPCHandle_t m_handle;
    char m_data[4];
  } TSClusterRPCMsg_t;

  typedef enum
    {
      NODE_ONLINE = 1,
      NODE_OFFLINE
    } TSNodeStatus_t;

  typedef enum
    {
      RPC_API_WIRELESS_F01 = 51,
      RPC_API_WIRELESS_F02,
      RPC_API_WIRELESS_F03,
      RPC_API_WIRELESS_F04,
      RPC_API_WIRELESS_F05,
      RPC_API_WIRELESS_F06,
      RPC_API_WIRELESS_F07,
      RPC_API_WIRELESS_F08,
      RPC_API_WIRELESS_F09,
      RPC_API_WIRELESS_F10
    } TSClusterRPCKey_t;

  typedef void (*TSClusterRPCFunction) (TSNodeHandle_t *node, TSClusterRPCMsg_t *msg, int msg_data_len);
  typedef void (*TSClusterStatusFunction) (TSNodeHandle_t *node, TSNodeStatus_t s);

  /****************************************************************************
   *  Subscribe to node up/down status notification.     		    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   * contact: OXY, DY
   ****************************************************************************/
  tsapi int TSAddClusterStatusFunction(TSClusterStatusFunction Status_Function, TSMutex m, TSClusterStatusHandle_t *h);
  /****************************************************************************
   *  Cancel subscription to node up/down status notification. 		    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   * contact: OXY, DY
   ****************************************************************************/
  tsapi int TSDeleteClusterStatusFunction(TSClusterStatusHandle_t *h);

  /****************************************************************************
   *  Get the struct in_addr associated with the TSNodeHandle_t.	    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   * contact: OXY, DY
   ****************************************************************************/
  tsapi int TSNodeHandleToIPAddr(TSNodeHandle_t *h, struct in_addr *in);

  /****************************************************************************
   *  Get the TSNodeHandle_t for the local node.	    	    	    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi void TSGetMyNodeHandle(TSNodeHandle_t *h);

  /****************************************************************************
   *  Enable node up/down notification for subscription added via 	    *
   *  TSAddClusterStatusFunction().					    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi void TSEnableClusterStatusCallout(TSClusterStatusHandle_t *h);

  /****************************************************************************
   *  Associate the given key with the given RPC function.		    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi int TSAddClusterRPCFunction(TSClusterRPCKey_t k, TSClusterRPCFunction RPC_Function, TSClusterRPCHandle_t *h);

  /****************************************************************************
   *  Delete the key to function association created via 		    *
   *  TSAddClusterRPCFunction().					    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi int TSDeleteClusterRPCFunction(TSClusterRPCHandle_t *h);

  /****************************************************************************
   *  Free TSClusterRPCMsg_t received via RPC function		    	    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi void TSFreeRPCMsg(TSClusterRPCMsg_t *msg, int msg_data_len);

  /****************************************************************************
   *  Allocate TSClusterRPCMsg_t for use in TSSendClusterRPC() 		    *
   *	Return != 0 Success						    *
   *	Return == 0 Allocation failed					    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi TSClusterRPCMsg_t *TSAllocClusterRPCMsg(TSClusterRPCHandle_t *h, int data_size);

  /****************************************************************************
   *  Send the RPC message to the specified node.			    *
   *    Cluster frees the given memory on send.				    *
   *    RPC function frees memory on receive.				    *
   *	Return == 0 Success						    *
   *	Return != 0 Failure						    *
   *  contact: OXY, DY
   ****************************************************************************/
  tsapi int TSSendClusterRPC(TSNodeHandle_t *nh, TSClusterRPCMsg_t *msg);

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* __TS_API_EXPERIMENTAL_H__ */
