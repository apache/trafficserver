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
 *   unless they are migrated to InkAPI.h  If you require stable APIs to 
 *   Traffic Server, DO NOT USE anything in this file.
 */

#ifndef __INK_API_PRIVATE_FROZEN_H__
#define __INK_API_PRIVATE_FROZEN_H__

#include "InkAPI.h"
#include "InkAPIPrivateIOCore.h"
#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

/* ----------------------------------------------------------------------
 * Interfaces for MIXT plugin
 * ---------------------------------------------------------------------- */

/****************************************************************************
 *  Create a new field and assign it a name 
 *  contact: MIXT
 ****************************************************************************/
  inkapi INKMLoc INKMimeHdrFieldCreateNamed(INKMBuffer bufp, INKMLoc mh_mloc, const char *name, int name_len);


/****************************************************************************
 *  Test if cache ready to accept request for a specific type of data
 *  contact: DI, MIXT
 ****************************************************************************/
  inkapi INKReturnCode INKCacheDataTypeReady(INKCacheDataType type, int *is_ready);

/* ----------------------------------------------------------------------
 * Interfaces for F5
 * ---------------------------------------------------------------------- */

/****************************************************************************
 *  When reenabling a txn in error, keep the connection open in case
 *  of keepalive.
 *  contact: F5
 ****************************************************************************/
  inkapi int INKHttpTxnClientKeepaliveSet(INKHttpTxn txnp);

/****************************************************************************
 *  Allow to set the body of a POST request.
 *  contact: F5
 ****************************************************************************/
  inkapi void INKHttpTxnServerRequestBodySet(INKHttpTxn txnp, char *buf, int buflength);

/* ===== High Resolution Time ===== */
#define INK_HRTIME_FOREVER  HRTIME_FOREVER
#define INK_HRTIME_DECADE   HRTIME_DECADE
#define INK_HRTIME_YEAR     HRTIME_YEAR
#define INK_HRTIME_WEEK     HRTIME_WEEK
#define INK_HRTIME_DAY      HRTIME_DAY
#define INK_HRTIME_HOUR     HRTIME_HOUR
#define INK_HRTIME_MINUTE   HRTIME_MINUTE
#define INK_HRTIME_SECOND   HRTIME_SECOND
#define INK_HRTIME_MSECOND  HRTIME_MSECOND
#define INK_HRTIME_USECOND  HRTIME_USECOND
#define INK_HRTIME_NSECOND  HRTIME_NSECOND

#define INK_HRTIME_APPROX_SECONDS(_x) HRTIME_APPROX_SECONDS(_x)
#define INK_HRTIME_APPROX_FACTOR      HRTIME_APPROX_FACTOR

/*
////////////////////////////////////////////////////////////////////
//
//	Map from units to ink_hrtime values
//
////////////////////////////////////////////////////////////////////
*/
#define INK_HRTIME_YEARS(_x)    HRTIME_YEARS(_x)
#define INK_HRTIME_WEEKS(_x)    HRTIME_WEEKS(_x)
#define INK_HRTIME_DAYS(_x)     HRTIME_DAYS(_x)
#define INK_HRTIME_HOURS(_x)    HRTIME_HOURS(_x)
#define INK_HRTIME_MINUTES(_x)  HRTIME_MINUTES(_x)
#define INK_HRTIME_SECONDS(_x)  HRTIME_SECONDS(_x)
#define INK_HRTIME_MSECONDS(_x) HRTIME_MSECONDS(_x)
#define INK_HRTIME_USECONDS(_x) HRTIME_USECONDS(_x)
#define INK_HRTIME_NSECONDS(_x) HRTIME_NSECONDS(_x)

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi unsigned int INKBasedTimeGet();

/* ===== Time ===== */
/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi double INKBasedTimeGetD();

/****************************************************************************
 *  Get time when Http TXN started
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnStartTimeGet(INKHttpTxn txnp, INK64 * start_time);

/****************************************************************************
 *  Get time when Http TXN ended
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnEndTimeGet(INKHttpTxn txnp, INK64 * end_time);

/****************************************************************************
 *  Get time when Http TXN started
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnStartTimeGetD(INKHttpTxn txnp, double *start_time);

/****************************************************************************
 *  Get time when Http TXN ended
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnEndTimeGetD(INKHttpTxn txnp, double *end_time);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCachedRespTimeGet(INKHttpTxn txnp, long *resp_time);

/* ===== Cache ===== */

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi INKReturnCode INKCacheKeyDataTypeSet(INKCacheKey key, INKCacheDataType type);


/* ===== Utility ===== */
/****************************************************************************
 *  Create a random number
 *  Return random integer between <X> and <Y>
 *  contact: DI
 ****************************************************************************/
  inkapi unsigned int INKrandom(void);

/****************************************************************************
 *  Create a random double
 *  Return random double between <X> and <Y>
 *  contact: DI
 ****************************************************************************/
  inkapi double INKdrandom(void);

/****************************************************************************
 *  Return Hi-resolution current time. (ink64)
 *  contact: DI
 ****************************************************************************/
  inkapi INK64 INKhrtime(void);

/* ===== global http stats ===== */
/****************************************************************************
 *  Get number of current client http connections
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpCurrentClientConnectionsGet(int *num_connections);

/****************************************************************************
 *  Get number of current active client http connections
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpCurrentActiveClientConnectionsGet(int *num_connections);

/****************************************************************************
 *  Get number of current idle client http connections
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpCurrentIdleClientConnectionsGet(int *num_connections);

/****************************************************************************
 *  Get number of current http connections to cache
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpCurrentCacheConnectionsGet(int *num_connections);

/****************************************************************************
 *  Get number of current http server connections
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpCurrentServerConnectionsGet(int *num_connections);

/* http transaction status -- more in InkAPIPrivate.h */
/****************************************************************************
 *  Get size of response header
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnServerRespHdrBytesGet(INKHttpTxn txnp, int *bytes);

/****************************************************************************
 *  Get size of response body
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnServerRespBodyBytesGet(INKHttpTxn txnp, int *bytes);

/* =====  CacheHttpInfo =====  */

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
#ifdef IDC
  inkapi INKReturnCode INKCacheHttpInfoCopy(INKCacheHttpInfo infop_src, INKCacheHttpInfo * infop_dest);
#else
  inkapi INKCacheHttpInfo INKCacheHttpInfoCopy(INKCacheHttpInfo * infop);
#endif

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
#ifdef IDC
  inkapi INKReturnCode INKCacheHttpInfoReqGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj);
#else
  inkapi void INKCacheHttpInfoReqGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * offset);
#endif

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
#ifdef IDC
  inkapi INKReturnCode INKCacheHttpInfoRespGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * obj);
#else
  inkapi void INKCacheHttpInfoRespGet(INKCacheHttpInfo infop, INKMBuffer * bufp, INKMLoc * offset);
#endif

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
#ifdef IDC
  inkapi INKReturnCode INKCacheHttpInfoDestroy(INKCacheHttpInfo infop);
#else
  inkapi void INKCacheHttpInfoDestroy(INKCacheHttpInfo * infop);
#endif

/* =====  ICP =====  */
  inkapi void INKHttpIcpDynamicSet(int value);

/* =====  Http Transactions =====  */
/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCachedRespModifiableGet(INKHttpTxn txnp, INKMBuffer * bufp, INKMLoc * offset);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCacheLookupStatusSet(INKHttpTxn txnp, int cachelookup);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCacheLookupUrlGet(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc obj);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCachedUrlSet(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc obj);

/****************************************************************************
 *  INKHttpTxnCacheLookupCountGet
 *  Return: INK_SUCESS/INK_ERROR
 *  contact: DI
 ****************************************************************************/
  INKReturnCode INKHttpTxnCacheLookupCountGet(INKHttpTxn txnp, int *lookup_count);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnNewCacheLookupDo(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc url_loc);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnSecondUrlTryLock(INKHttpTxn txnp);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnRedirectRequest(INKHttpTxn txnp, INKMBuffer bufp, INKMLoc url_loc);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCacheLookupSkip(INKHttpTxn txnp);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnServerRespNoStore(INKHttpTxn txnp);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnServerRespIgnore(INKHttpTxn txnp);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnShutDown(INKHttpTxn txnp, INKEvent event);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnAborted(INKHttpTxn txnp);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnClientReqIsServerStyle(INKHttpTxn txnp);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnOverwriteExpireTime(INKHttpTxn txnp, time_t expire_time);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnUpdateCachedObject(INKHttpTxn txnp);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnCachedRespTimeGet(INKHttpTxn txnp, long *resp_time);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnLookingUpTypeGet(INKHttpTxn txnp);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnClientRespHdrBytesGet(INKHttpTxn txnp, int *bytes);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: DI
 ****************************************************************************/
  inkapi int INKHttpTxnClientRespBodyBytesGet(INKHttpTxn txnp, int *bytes);


/* =====  Matcher Utils =====  */
#define               INK_MATCHER_LINE_INVALID 0
  typedef void *INKMatcherLine;

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherReadIntoBuffer(char *file_name, int *file_len);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherTokLine(char *buffer, char **last);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherExtractIPRange(char *match_str, INKU32 * addr1, INKU32 * addr2);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi INKMatcherLine INKMatcherLineCreate();

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi void INKMatcherLineDestroy(INKMatcherLine ml);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherParseSrcIPConfigLine(char *line, INKMatcherLine ml);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherLineName(INKMatcherLine ml, int element);

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
  inkapi char *INKMatcherLineValue(INKMatcherLine ml, int element);

/* =====  IP Lookup =====  */
#define               INK_IP_LOOKUP_INVALID 0
  typedef void *INKIPLookup;
  typedef void *INKIPLookupState;

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
#if 0                           // Not used.
  inkapi INKIPLookup INKIPLookupCreate();
#endif

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
#if 0                           // Not used.
  inkapi void INKIPLookupDestroy(INKIPLookup iplu);
#endif

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
#if 0                           // Not used.
  inkapi INKIPLookupState INKIPLookupStateCreate();
#endif

/****************************************************************************
 *  ??
 *  Return
 *  contact: DI
 ****************************************************************************/
#if 0                           // Not used.
  inkapi void INKIPLookupStateDestroy(INKIPLookupState iplus);
#endif

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi void INKIPLookupNewEntry(INKIPLookup iplu, INKU32 addr1, INKU32 addr2, void *data);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKIPLookupMatchFirst(INKIPLookup iplu, INKU32 addr, INKIPLookupState iplus, void **data);

/****************************************************************************
 *  contact: DI
 ****************************************************************************/
  inkapi int INKIPLookupMatchNext(INKIPLookup iplu, INKIPLookupState iplus, void **data);

/* ===== Configuration Setting ===== */

/****************************************************************************
 *  Set a records.config integer variable
 *  contact: DI
 ****************************************************************************/
  inkapi int INKMgmtConfigIntSet(const char *var_name, INKMgmtInt value);

/* ----------------------------------------------------------------------
 * Interfaces used by Wireless group
 * ---------------------------------------------------------------------- */

#define INK_NET_EVENT_DATAGRAM_READ_COMPLETE  INK_EVENT_INTERNAL_206
#define INK_NET_EVENT_DATAGRAM_READ_ERROR     INK_EVENT_INTERNAL_207
#define INK_NET_EVENT_DATAGRAM_WRITE_COMPLETE INK_EVENT_INTERNAL_208
#define INK_NET_EVENT_DATAGRAM_WRITE_ERROR    INK_EVENT_INTERNAL_209
#define INK_NET_EVENT_DATAGRAM_READ_READY     INK_EVENT_INTERNAL_210
#define INK_NET_EVENT_DATAGRAM_OPEN	      INK_EVENT_INTERNAL_211
#define INK_NET_EVENT_DATAGRAM_ERROR          INK_EVENT_INTERNAL_212

  typedef enum
  {
    INK_SIGNAL_WDA_BILLING_CONNECTION_DIED = 100,
    INK_SIGNAL_WDA_BILLING_CORRUPTED_DATA = 101,
    INK_SIGNAL_WDA_XF_ENGINE_DOWN = 102,
    INK_SIGNAL_WDA_RADIUS_CORRUPTED_PACKETS = 103
  } INKAlarmType;

/* ===== Alarm ===== */
/****************************************************************************
 *  ??
 *  Return ??
 *  contact: OXYGEN
 ****************************************************************************/
  inkapi int INKSignalWarning(INKAlarmType code, char *msg);

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
 *	 INKAddClusterStatusFunction() and INKAddClusterRPCFunction() must 
 *       be non-blocking (i.e. usage of INKMutexLock() and file i/o is 
 *       not allowed).
 *    6) INKSendClusterRPC() can only process INKClusterRPCMsg_t generated
 *	 by INKAllocClusterRPCMsg().  Failure to adhere to this rule will
 *	 result in heap corruption.
 *    7) Messages sent via INKSendClusterRPC() must be at least 4 bytes in
 * 	 length.
 *    8) The user is not provided with any alignment guarantees on the 
 *	 'm_data' field in the INKClusterRPCMsg_t returned via 
 *	 INKAllocClusterRPCMsg().  Assume byte alignment.
 *    9) INKSendClusterRPC() interface owns the memory and is responsible
 *       for freeing the memory.
 *   10) RPC functions defined via INKAddClusterRPCFunction() own the 
 *       memory when invoked and are responsible for freeing it via
 *	 INKFreeRPCMsg().
 */
#define MAX_CLUSTER_NODES 256

  typedef struct INKClusterRPCHandle
  {
    int opaque[2];
  } INKClusterRPCHandle_t;

  typedef int INKClusterStatusHandle_t;
  typedef int INKNodeHandle_t;

  typedef struct INKClusterRPCMsg
  {
    INKClusterRPCHandle_t m_handle;
    char m_data[4];
  } INKClusterRPCMsg_t;

  typedef enum
  {
    NODE_ONLINE = 1,
    NODE_OFFLINE
  } INKNodeStatus_t;

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
  } INKClusterRPCKey_t;

  typedef void (*INKClusterRPCFunction) (INKNodeHandle_t * node, INKClusterRPCMsg_t * msg, int msg_data_len);
  typedef void (*INKClusterStatusFunction) (INKNodeHandle_t * node, INKNodeStatus_t s);

/****************************************************************************
 *  Subscribe to node up/down status notification.     			    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 * contact: OXY, DY 
 ****************************************************************************/
  inkapi int
    INKAddClusterStatusFunction(INKClusterStatusFunction Status_Function, INKMutex m, INKClusterStatusHandle_t * h);
/****************************************************************************
 *  Cancel subscription to node up/down status notification. 		    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 * contact: OXY, DY 
 ****************************************************************************/
  inkapi int INKDeleteClusterStatusFunction(INKClusterStatusHandle_t * h);

/****************************************************************************
 *  Get the struct in_addr associated with the INKNodeHandle_t.	    	    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 * contact: OXY, DY 
 ****************************************************************************/
  inkapi int INKNodeHandleToIPAddr(INKNodeHandle_t * h, struct in_addr *in);

/****************************************************************************
 *  Get the INKNodeHandle_t for the local node.	    	    		    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi void INKGetMyNodeHandle(INKNodeHandle_t * h);

/****************************************************************************
 *  Enable node up/down notification for subscription added via 	    *
 *  INKAddClusterStatusFunction().					    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi void INKEnableClusterStatusCallout(INKClusterStatusHandle_t * h);

/****************************************************************************
 *  Associate the given key with the given RPC function.		    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi int
    INKAddClusterRPCFunction(INKClusterRPCKey_t k, INKClusterRPCFunction RPC_Function, INKClusterRPCHandle_t * h);

/****************************************************************************
 *  Delete the key to function association created via 			    *
 *  INKAddClusterRPCFunction().						    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi int INKDeleteClusterRPCFunction(INKClusterRPCHandle_t * h);

/****************************************************************************
 *  Free INKClusterRPCMsg_t received via RPC function		    	    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi void INKFreeRPCMsg(INKClusterRPCMsg_t * msg, int msg_data_len);

/****************************************************************************
 *  Allocate INKClusterRPCMsg_t for use in INKSendClusterRPC() 		    *
 *	Return != 0 Success						    *
 *	Return == 0 Allocation failed					    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi INKClusterRPCMsg_t *INKAllocClusterRPCMsg(INKClusterRPCHandle_t * h, int data_size);

/****************************************************************************
 *  Send the RPC message to the specified node.			    	    *
 *    Cluster frees the given memory on send.				    *
 *    RPC function frees memory on receive.				    *
 *	Return == 0 Success						    *
 *	Return != 0 Failure						    *
 *  contact: OXY, DY
 ****************************************************************************/
  inkapi int INKSendClusterRPC(INKNodeHandle_t * nh, INKClusterRPCMsg_t * msg);

/* ---------------------------------------------------------------------- 
 * Interfaces used for the AAA project
 * ---------------------------------------------------------------------- */

/* ===== IP to User Name Cache ===== */
/****************************************************************************
 *  Insert a name into the user-name cache
 *  Return
 *  contact: AAA, CPOINT
 ****************************************************************************/
  inkapi int INKUserNameCacheInsert(INKCont contp, unsigned long ip, const char *userName);

/****************************************************************************
 *  Lookup a name in the user-name cache
 *  Return
 *  contact: AAA, CPOINT
 ****************************************************************************/
  inkapi int INKUserNameCacheLookup(INKCont contp, unsigned long ip, char *userName);

/****************************************************************************
 *  Remove a name from the user-name cache
 *  Return
 *  contact: AAA, CPOINT
 ****************************************************************************/
  inkapi int INKUserNameCacheDelete(INKCont contp, unsigned long ip);

#define INK_EVENT_POLICY_LOOKUP INK_EVENT_INTERNAL_1200

/****************************************************************************
 *  ??
 *  Return
 *  contact: AAA
 ****************************************************************************/
  inkapi INKReturnCode INKUserPolicyLookup(INKHttpTxn txnp, void **user_info);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: AAA
 ****************************************************************************/
  inkapi INKReturnCode INKHttpTxnBillable(INKHttpTxn txnp, int bill, const char *eventName);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: AAA
 ****************************************************************************/
  inkapi void INKPolicyContSet(INKCont p);

/****************************************************************************
 *  ??
 *  Return ??
 *  contact: AAA
 ****************************************************************************/
  inkapi INKReturnCode INKUserPolicyFetch(INKU32 ip, char *name);

/* ---------------------------------------------------------------------- 
 *
 * Aerocast, MIXT SDK
 * contact: MIXT
 *
 * ----------------------------------------------------------------------  */
#define INK_EVENT_MIXT_READ_REQUEST_HDR INK_EVENT_INTERNAL_60201
#if 0
#define INK_EVENT_MIXT_CONTINUE         INK_EVENT_INTERNAL_60200
#define INK_EVENT_MIXT_ERROR            INK_EVENT_INTERNAL_60202
#endif
/* ---------------------------------------------------------------------- 
 * Prefetch APIs
 * ----------------------------------------------------------------------  */

#include "InkAPIHughes.h"

#ifdef __cplusplus
}
#endif                          /* __cplusplus */
#endif                          /* __INK_API_PRIVATE_FROZEN_H__ */
