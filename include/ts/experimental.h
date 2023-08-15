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

#pragma once

namespace tsapi
{
namespace c
{

  enum TSFetchEventExt {
    TS_FETCH_EVENT_EXT_HEAD_READY = -1,
    TS_FETCH_EVENT_EXT_HEAD_DONE  = -2,
    TS_FETCH_EVENT_EXT_BODY_READY = -3,
    TS_FETCH_EVENT_EXT_BODY_DONE  = -4
  };

  enum TSFetchFlags {
    TS_FETCH_FLAGS_NONE                 = 0,      // do nothing
    TS_FETCH_FLAGS_STREAM               = 1 << 1, // enable stream IO
    TS_FETCH_FLAGS_DECHUNK              = 1 << 2, // dechunk body content
    TS_FETCH_FLAGS_NEWLOCK              = 1 << 3, // allocate new lock for fetch sm
    TS_FETCH_FLAGS_NOT_INTERNAL_REQUEST = 1 << 4, // Allow this fetch to be created as a non-internal request.
    TS_FETCH_FLAGS_SKIP_REMAP           = 1 << 5, // Skip remapping and allow requesting arbitary URL
  };

  /* Forward declaration of in_addr, any user of these APIs should probably
     include net/netinet.h or whatever is appropriate on the platform. */
  struct in_addr;

  /* Cache APIs that are not yet fully supported and/or frozen nor complete. */
  TSReturnCode TSCacheBufferInfoGet(TSCacheTxn txnp, uint64_t *length, uint64_t *offset);

  TSCacheHttpInfo TSCacheHttpInfoCreate();
  void TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
  void TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *obj);
  void TSCacheHttpInfoReqSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
  void TSCacheHttpInfoRespSet(TSCacheHttpInfo infop, TSMBuffer bufp, TSMLoc obj);
  void TSCacheHttpInfoKeySet(TSCacheHttpInfo infop, TSCacheKey key);
  void TSCacheHttpInfoSizeSet(TSCacheHttpInfo infop, int64_t size);
  int TSCacheHttpInfoVector(TSCacheHttpInfo infop, void *data, int length);
  time_t TSCacheHttpInfoReqSentTimeGet(TSCacheHttpInfo infop);
  time_t TSCacheHttpInfoRespReceivedTimeGet(TSCacheHttpInfo infop);
  int64_t TSCacheHttpInfoSizeGet(TSCacheHttpInfo infop);

  /* Do not edit these apis, used internally */
  int TSMimeHdrFieldEqual(TSMBuffer bufp, TSMLoc hdr_obj, TSMLoc field1, TSMLoc field2);
  TSReturnCode TSHttpTxnHookRegisteredFor(TSHttpTxn txnp, TSHttpHookID id, TSEventFunc funcp);

  /* Protocols APIs */
  void TSVConnCacheHttpInfoSet(TSVConn connp, TSCacheHttpInfo infop);

  /* The rest is from the old "froze" private API include, we should consider
     moving some of these over to ts/ts.h as well. TODO */

  /****************************************************************************
   *  Test if cache ready to accept request for a specific type of data
   ****************************************************************************/
  TSReturnCode TSCacheDataTypeReady(TSCacheDataType type, int *is_ready);

  /****************************************************************************
   *  When reenabling a txn in error, keep the connection open in case
   *  of keepalive.
   ****************************************************************************/
  void TSHttpTxnClientKeepaliveSet(TSHttpTxn txnp, int set);

  /****************************************************************************
   *  Allow to set the body of a POST request.
   ****************************************************************************/
  void TSHttpTxnServerRequestBodySet(TSHttpTxn txnp, char *buf, int64_t buflength);

  TSReturnCode TSHttpTxnCachedRespTimeGet(TSHttpTxn txnp, time_t *resp_time);

  /* ===== Cache ===== */
  TSReturnCode TSCacheKeyDataTypeSet(TSCacheKey key, TSCacheDataType type);

  /* ===== Utility ===== */
  /****************************************************************************
   *  Create a random number
   *  Return random integer between <X> and <Y>
   ****************************************************************************/
  unsigned int TSrandom(void);

  /****************************************************************************
   *  Create a random double
   *  Return random double between <X> and <Y>
   ****************************************************************************/
  double TSdrandom(void);

  /* =====  CacheHttpInfo =====  */

  TSCacheHttpInfo TSCacheHttpInfoCopy(TSCacheHttpInfo infop);
  void TSCacheHttpInfoReqGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
  void TSCacheHttpInfoRespGet(TSCacheHttpInfo infop, TSMBuffer *bufp, TSMLoc *offset);
  void TSCacheHttpInfoDestroy(TSCacheHttpInfo infop);

  /* Get Arbitrary Txn info such as cache lookup details etc as defined in TSHttpTxnInfoKey */
  /**
     Return the particular txn info requested.

     @param txnp the transaction pointer
     @param key the requested txn info.
     @param TSMgmtInt a pointer to a integer where the return value is stored

     @return @c TS_SUCCESS if the requested info is supported, TS_ERROR otherwise

  */
  TSReturnCode TSHttpTxnInfoIntGet(TSHttpTxn txnp, TSHttpTxnInfoKey key, TSMgmtInt *value);

  /****************************************************************************
   *  TSHttpTxnCacheLookupCountGet
   *  Return: TS_SUCCESS/TS_ERROR
   ****************************************************************************/
  TSReturnCode TSHttpTxnCacheLookupCountGet(TSHttpTxn txnp, int *lookup_count);
  TSReturnCode TSHttpTxnServerRespIgnore(TSHttpTxn txnp);
  TSReturnCode TSHttpTxnShutDown(TSHttpTxn txnp, TSEvent event);
  TSReturnCode TSHttpTxnCloseAfterResponse(TSHttpTxn txnp, int should_close);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  int TSHttpTxnClientReqIsServerStyle(TSHttpTxn txnp);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  void TSHttpTxnOverwriteExpireTime(TSHttpTxn txnp, time_t expire_time);

  /****************************************************************************
   *  ??
   *  Return ??
   ****************************************************************************/
  TSReturnCode TSHttpTxnUpdateCachedObject(TSHttpTxn txnp);

  /****************************************************************************
   *  ??
   *  TODO: This returns a LookingUp_t value, we need to SDK'ify it.
   ****************************************************************************/
  int TSHttpTxnLookingUpTypeGet(TSHttpTxn txnp);

  /**
     Attempt to attach the contp continuation to sockets that have already been
     opened by the traffic Server and defined as belonging to plugins (based on
     records.yaml configuration). If a connection is successfully accepted,
     the TS_EVENT_NET_ACCEPT is delivered to the continuation. The event
     data will be a valid TSVConn bound to the accepted connection.
     In order to configure such a socket, add the "plugin" keyword to a port
     in proxy.config.http.server_ports like "8082:plugin"
     Transparency/IP settings can also be defined, but a port cannot have
     both the "ssl" or "plugin" keywords configured.

     Need to update records.yaml comments on proxy.config.http.server_ports
     when this option is promoted from experimental.
   */
  TSReturnCode TSPluginDescriptorAccept(TSCont contp);

  /**
      Opens a network connection to the host specified by the 'to' sockaddr
      spoofing the client addr to equal the 'from' sockaddr.
      If the connection is successfully opened, contp
      is called back with the event TS_EVENT_NET_CONNECT and the new
      network vconnection will be passed in the event data parameter.
      If the connection is not successful, contp is called back with
      the event TS_EVENT_NET_CONNECT_FAILED.

      Note: It is possible to receive TS_EVENT_NET_CONNECT
      even if the connection failed, because of the implementation of
      network sockets in the underlying operating system. There is an
      exception: if a plugin tries to open a connection to a port on
      its own host machine, then TS_EVENT_NET_CONNECT is sent only
      if the connection is successfully opened. In general, however,
      your plugin needs to look for an TS_EVENT_VCONN_WRITE_READY to
      be sure that the connection is successfully opened.

      @return TSAction which allows you to check if the connection is complete,
        or cancel the attempt to connect.

   */
  TSAction TSNetConnectTransparent(
    TSCont contp, /**< continuation that is called back when the attempted net connection either succeeds or fails. */
    struct sockaddr const *from, /**< Address to spoof as connection origin */
    struct sockaddr const *to    /**< Address to which to connect. */
  );

/* =====  Matcher Utils =====  */
#define TS_MATCHER_LINE_INVALID 0
  using TSMatcherLine = struct tsapi_matcheline *;

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  char *TSMatcherReadIntoBuffer(char *file_name, int *file_len);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  char *TSMatcherTokLine(char *buffer, char **last);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  char *TSMatcherExtractIPRange(char *match_str, uint32_t *addr1, uint32_t *addr2);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  TSMatcherLine TSMatcherLineCreate();

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  void TSMatcherLineDestroy(TSMatcherLine ml);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  const char *TSMatcherParseSrcIPConfigLine(char *line, TSMatcherLine ml);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  char *TSMatcherLineName(TSMatcherLine ml, int element);

  /****************************************************************************
   *  ??
   *  Return
   ****************************************************************************/
  char *TSMatcherLineValue(TSMatcherLine ml, int element);

  TSReturnCode TSMgmtConfigFileAdd(const char *parent, const char *fileName);
  /* ----------------------------------------------------------------------
   * Interfaces used by Wireless group
   * ---------------------------------------------------------------------- */

  TSEvent const TS_NET_EVENT_DATAGRAM_READ_COMPLETE  = TS_EVENT_INTERNAL_206;
  TSEvent const TS_NET_EVENT_DATAGRAM_READ_ERROR     = TS_EVENT_INTERNAL_207;
  TSEvent const TS_NET_EVENT_DATAGRAM_WRITE_COMPLETE = TS_EVENT_INTERNAL_208;
  TSEvent const TS_NET_EVENT_DATAGRAM_WRITE_ERROR    = TS_EVENT_INTERNAL_209;
  TSEvent const TS_NET_EVENT_DATAGRAM_READ_READY     = TS_EVENT_INTERNAL_210;
  TSEvent const TS_NET_EVENT_DATAGRAM_OPEN           = TS_EVENT_INTERNAL_211;
  TSEvent const TS_NET_EVENT_DATAGRAM_ERROR          = TS_EVENT_INTERNAL_212;

  /**
   * Extended FetchSM's AIPs
   */

  /*
   * Create FetchSM, this API will enable stream IO automatically.
   *
   * @param contp: continuation to be callbacked.
   * @param method: request method.
   * @param url: scheme://host[:port]/path.
   * @param version: client http version, eg: "HTTP/1.1".
   * @param client_addr: client addr sent to log.
   * @param flags: can be bitwise OR of several TSFetchFlags.
   *
   * return TSFetchSM which should be destroyed by TSFetchDestroy().
   */
  TSFetchSM TSFetchCreate(TSCont contp, const char *method, const char *url, const char *version,
                          struct sockaddr const *client_addr, int flags);

  /*
   * Set fetch flags to FetchSM Context
   *
   * @param fetch_sm: returned value of TSFetchCreate().
   * @param flags: can be bitwise OR of several TSFetchFlags.
   *
   * return void
   */
  void TSFetchFlagSet(TSFetchSM fetch_sm, int flags);

  /*
   * Create FetchSM, this API will enable stream IO automatically.
   *
   * @param fetch_sm: returned value of TSFetchCreate().
   * @param name: name of header.
   * @param name_len: len of name.
   * @param value: value of header.
   * @param name_len: len of value.
   *
   * return TSFetchSM which should be destroyed by TSFetchDestroy().
   */
  void TSFetchHeaderAdd(TSFetchSM fetch_sm, const char *name, int name_len, const char *value, int value_len);

  /*
   * Write data to FetchSM
   *
   * @param fetch_sm: returned value of TSFetchCreate().
   * @param data/len: data to be written to fetch sm.
   */
  void TSFetchWriteData(TSFetchSM fetch_sm, const void *data, size_t len);

  /*
   * Read up to *len* bytes from FetchSM into *buf*.
   *
   * @param fetch_sm: returned value of TSFetchCreate().
   * @param buf/len: buffer to contain data from fetch sm.
   */
  ssize_t TSFetchReadData(TSFetchSM fetch_sm, void *buf, size_t len);

  /*
   * Launch FetchSM to do http request, before calling this API,
   * you should append http request header into fetch sm through
   * TSFetchWriteData() API
   *
   * @param fetch_sm: comes from returned value of TSFetchCreate().
   */
  void TSFetchLaunch(TSFetchSM fetch_sm);

  /*
   * Destroy FetchSM
   *
   * @param fetch_sm: returned value of TSFetchCreate().
   */
  void TSFetchDestroy(TSFetchSM fetch_sm);

  /*
   * Set user-defined data in FetchSM
   */
  void TSFetchUserDataSet(TSFetchSM fetch_sm, void *data);

  /*
   * Get user-defined data in FetchSM
   */
  void *TSFetchUserDataGet(TSFetchSM fetch_sm);

  /*
   * Get client response hdr mbuffer
   */
  TSMBuffer TSFetchRespHdrMBufGet(TSFetchSM fetch_sm);

  /*
   * Get client response hdr mloc
   */
  TSMLoc TSFetchRespHdrMLocGet(TSFetchSM fetch_sm);

  /*
   * Print as a MIME header date string.
   */
  TSReturnCode TSMimeFormatDate(time_t const value_time, char *const value_str, int *const value_len);

} // end namespace c
} // end namespace tsapi

using namespace ::tsapi::c;
