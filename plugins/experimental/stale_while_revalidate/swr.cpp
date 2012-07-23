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

/**
 *   swr.cpp
 *
 *   If the cache data has expired but falls within the stale while revalidate window, 
 *   serve the cached data and make an async request for new data.
 *
*/

#include <stdio.h>
#include <set>
#include <string>
#include <sstream>
#include <iostream>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "InkAPI.h"
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#define DEBUG 0

static const char SWR_LOG_TAG[] = "http_swr_plugin";

/// Field in Cache control header which defines the SWR window.
static const char HTTP_VALUE_STALE_WHILE_REVALIDATE[] = "stale-while-revalidate";

/// Field in Cache control header which defines the time to wait for SWR response
static const char HTTP_VALUE_TIME_TO_WAIT[] = "time-to-wait";

/// Field in Cache control header for background-fetch
static const char HTTP_VALUE_BACKGROUND_FETCH[] = "background-fetch";

/// This header is added when a SWR request is made.
/// This is used by the plugin to distinguish between a regular request and a SWR request
static const char SWR_FETCH_HEADER[] = "X-TS-SWR: 1\r\n\r\n";

/// Can be set with stale_while_revalidate_window config param.
/// This is overridden by the server's Cache control header.
static time_t STALE_WHILE_REVALIDATE_WINDOW = 0;

/// Can be set with stale_while_revalidate_window config param.
/// This is overridden by the server's Cache control header.
static long STALE_WHILE_REVALIDATE_WINDOW_INFINITE = -1;

/// In milli seconds. Can be set with time_to_wait config param.
/// Controls the time to wait for asynchronous request to complete before returning stale data.
static unsigned int TIME_TO_WAIT = 0;

/// Can be set with max_age config param.
/// This is overridden by the server's Cache control header.
/// This is needed because some origin servers do not advertise either max-age or mime-field-expires
static time_t MAX_AGE = 0;

static
  std::set <
  std::string >
  swr_sites_requested;
static INKMutex swr_mutex = INKMutexCreate();

static const char *SWR_WARNING_HEADER = "110 \"Response is stale\"";

typedef struct
{

  INKVIO writeVIO;
  INKVIO readVIO;
  INKIOBuffer reqBuff;
  INKIOBufferReader reqReader;
  INKIOBuffer respBuff;
  INKIOBufferReader respReader;
  INKIOBuffer dumpBuff;
  int iDumpLen;

} FetchData;

struct request_header_values_t
{
  bool swr_can_run;
  bool only_if_cached;
  bool background_fetch;
    request_header_values_t();
};

request_header_values_t::request_header_values_t()
:
swr_can_run(true), only_if_cached(false), background_fetch(false)
{

}

struct response_header_values_t
{
  time_t mime_field_expires;
  long stale_while_revalidate_window;
  time_t max_age;
  time_t date;
  time_t time_to_wait;
  bool must_revalidate;

  response_header_values_t();
  void init();
  time_t get_expiration();
  time_t get_max_stale_time();
  time_t get_time_to_wait();
  long get_swr_window();

};

response_header_values_t::response_header_values_t()
:
mime_field_expires(0),
stale_while_revalidate_window(STALE_WHILE_REVALIDATE_WINDOW), max_age(MAX_AGE), date(0), time_to_wait(TIME_TO_WAIT),
must_revalidate(false)
{

}

void
response_header_values_t::init()
{
  mime_field_expires = 0;
  stale_while_revalidate_window = STALE_WHILE_REVALIDATE_WINDOW;
  max_age = MAX_AGE;
  date = 0;
  time_to_wait = TIME_TO_WAIT;
}

/**
* Get expiration time for the page from cache_control header
* expiration time = date + max_age
* @param[out] time_t : expiration time for the URL
*/
time_t
response_header_values_t::get_expiration()
{
  if (max_age)
    return date + max_age;
  else if(mime_field_expires)
    return mime_field_expires;

  // If max_age and mime_field_expires are 0, expire NOW!
  return date;
}

/**
* Get time until which the page is considered valid.
* max_stale_time = exp_time + stale_white_revalidate_window
* @param[out] time_t : time till which the page can be served even if it is past expiration time
*/
time_t
response_header_values_t::get_max_stale_time()
{
  time_t expiration_time = get_expiration();
  if (expiration_time == 0) {
    return 0;
  }
  return (expiration_time + stale_while_revalidate_window);
}

time_t
response_header_values_t::get_time_to_wait()
{
  return time_to_wait;
}

long
response_header_values_t::get_swr_window()
{
  return stale_while_revalidate_window;
}

/**
* This will be called only in debug mode
* Dumps out response from origin server
*/
static void
dump_response(INKCont contp)
{

  FetchData *pData = (FetchData *) INKContDataGet(contp);
  INKIOBufferReader reader = INKIOBufferReaderAlloc(pData->dumpBuff);

  int iReadTotal = 0;
  const int BUFF_SIZE = (pData->iDumpLen + 1);
  char dump[BUFF_SIZE];
  char *dumpPtr = &dump[0];
  memset((void *) dumpPtr, 0, BUFF_SIZE);

  int iAvail = INKIOBufferReaderAvail(reader);

  if (iAvail <= 0) {
    INKDebug(SWR_LOG_TAG, "[swr] nothing to read... ");
    return;
  }


  INKIOBufferBlock startBlock = INKIOBufferReaderStart(reader);

  while (iAvail > 0 && startBlock != INK_ERROR_PTR) {
    const char *startPtr = INKIOBufferBlockReadStart(startBlock, reader, &iAvail);

    if (startPtr == INK_ERROR_PTR || iAvail == INK_ERROR) {
      INKError("[swr] dump: could not get block read starting point \n");
      break;
    }

    iReadTotal += iAvail;
    memcpy((void *) dumpPtr, (void *) startPtr, iAvail);
    dumpPtr += iAvail;
    INKIOBufferReaderConsume(reader, iAvail);
    startBlock = INKIOBufferBlockNext(startBlock);
    iAvail = INKIOBufferReaderAvail(reader);

    iReadTotal += iAvail;
    if (iReadTotal > pData->iDumpLen) {
      INKError
        ("[swr] dump: read was bigger than expected, aborting. total resp len: %d  wanted to read: %d \n",
         pData->iDumpLen, iReadTotal);
      break;
    }
  }

  dumpPtr = &dump[0];
  if (dumpPtr != NULL && strlen(dumpPtr) > 0) {
    INKDebug(SWR_LOG_TAG, "[swr] dump: successful copy: %s \n", dumpPtr);
  }

  INKIOBufferReaderFree(reader);
  return;
}

/**
* Read data from VIO
* Reenable  if there is more data to be read
*/
static void
read_response(INKCont contp)
{
  FetchData *pData = (FetchData *) INKContDataGet(contp);
  if (pData == INK_ERROR_PTR) {
    INKError("[swr] ERROR could not get data from contp to write fetch");
    return;
  }

  int iTodo = INKVIONTodoGet(pData->readVIO);
  if (iTodo > 0) {
    int iAvail = INKIOBufferReaderAvail(pData->respReader);
    if (iAvail == INK_ERROR) {
      INKError("[swr] could not get avail bytes from read vio, returning");
      INKVIOReenable(pData->readVIO);
      return;
    }

    if (iTodo > iAvail) {
      iTodo = iAvail;
    }

    INKDebug(SWR_LOG_TAG, "[swr] going to read in: %d \n", iTodo);

    if (iTodo > 0) {
      INKIOBufferCopy(pData->dumpBuff, pData->respReader, iTodo, 0);

      // just move pointer in reader, don't actually get data
      if (INKIOBufferReaderConsume(pData->respReader, iTodo) == INK_ERROR) {
        INKDebug(SWR_LOG_TAG, "[swr] could not tell resp reader to consume, returning");
        INKVIOReenable(pData->readVIO);
        return;
      }
      pData->iDumpLen += iTodo;
      INKDebug(SWR_LOG_TAG, "[swr] bytes to be dumped: %d \n", pData->iDumpLen);
    }

    iTodo = INKVIONTodoGet(pData->readVIO);

    if (iTodo > 0) {
      // still have some left to read
      INKVIOReenable(pData->readVIO);
      INKDebug(SWR_LOG_TAG, "[swr] more data to read... reenable read vio \n");
    }
  }

  return;
}

/**
* Write request to server
*/
static void
write_fetch_request(INKCont contp)
{
  FetchData *pData = (FetchData *) INKContDataGet(contp);

  if (pData == INK_ERROR_PTR) {
    INKError("[swr] ERROR could not get data from contp to write fetch");
    return;
  }

  int iTodo = INKVIONTodoGet(pData->writeVIO);
  INKError("[swr] write todo ret: %d", iTodo);

  int iWriteVIONBytes = INKVIONBytesGet(pData->writeVIO);
  INKDebug(SWR_LOG_TAG, "[swr] writeVIO NBytes ret: %d", iWriteVIONBytes);


  if (INKVIOReenable(pData->writeVIO) == INK_ERROR) {
    INKError("[swr] could not re-enable write vio");
  }

  return;
}

static int
fetch_handler(INKCont contp, INKEvent event, void *edata)
{

  switch (event) {
  case INK_EVENT_VCONN_WRITE_READY:
    {
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::INK_EVENT_VCONN_WRITE_READY calling write_fetch_request");
      write_fetch_request(contp);
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::INK_EVENT_VCONN_WRITE_READY write_fetch_request done");
      break;
    }

  case INK_EVENT_VCONN_WRITE_COMPLETE:
    {
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::INK_EVENT_VCONN_WRITE_COMPLETE");
      break;
    }

  case INK_EVENT_VCONN_READ_READY:
    {
      // - there is new data in the read buffer; when we're done reading, re-enable VIO
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::EVENT_VCONN_READ_READY calling read_response");
      read_response(contp);
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::EVENT_VCONN_READ_READY read_response done");
      break;
    }

  case INK_EVENT_VCONN_READ_COMPLETE:
    {
      // - the VIO has read all the bytes specified by INKVConnRead, vconn can be re-used or tossed
      INKDebug(SWR_LOG_TAG, "[swr]  FETCH_HANDLER::EVENT_VCONN_READ_COMPLETE");
      break;
    }

  case INK_EVENT_VCONN_EOS:
    {
      // - occurs when read goes past end of byte stream b/c # bytes specified in VConnRead was bigger
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::EVENT_VCONN_EOS");
#if DEBUG
      dump_response(contp);
#endif

      FetchData *pData = (FetchData *) INKContDataGet(contp);
      INKIOBufferDestroy(pData->reqBuff);
      INKIOBufferDestroy(pData->respBuff);
      INKIOBufferDestroy(pData->dumpBuff);
      INKVConn fetchConn = INKVIOVConnGet(pData->writeVIO);
      INKVConnShutdown(fetchConn, 1, 1);
      INKVConnClose(fetchConn);
      delete pData;
      INKContDestroy(contp);

      break;
    }
  case INK_EVENT_ERROR:
    {
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::EVENT_ERROR");
      break;
    }

  default:
    {
      INKDebug(SWR_LOG_TAG, "[swr] FETCH_HANDLER::DEFAULT");
      break;
    }
  }

  return 0;
}

/**
* get the request URL
*/
static char *
getURLFromReqHeader(INKHttpTxn & txnp)
{
  // MAKE SURE YOU FREE THE RETURNED STRING!

  INKMBuffer req_bufp;
  INKMLoc hdr_loc;
  char *url_str;
  int url_length;

  if (!INKHttpTxnClientReqGet(txnp, &req_bufp, &hdr_loc)) {
    INKError("[swr] getURLFromReqHeader : couldn't retrieve client response header\n");
    return NULL;
  }

  INKMLoc url_loc = INKHttpHdrUrlGet(req_bufp, hdr_loc);
  if (!url_loc) {
    INKError("[swr] getURLFromReqHeader : couldn't retrieve request url\n");
    INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, hdr_loc);
    return NULL;
  }

  url_str = INKUrlStringGet(req_bufp, url_loc, &url_length);
  INKHandleMLocRelease(req_bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, hdr_loc);
  return url_str;
}

/**
* Check if the req is a Stale while revalidate reques
* @param[out] bool true if the request is a stale while revalidate request. false otherwise
*/
static bool
isSWR(INKHttpTxn & txnp)
{
  INKMBuffer req_bufp;
  INKMLoc req_loc;
  bool bRet = false;

  INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc);
  INKMLoc swr_loc = NULL;
  swr_loc = INKMimeHdrFieldFind(req_bufp, req_loc, "X-TS-SWR", strlen("X-TS-SWR"));

  if (swr_loc != INK_ERROR_PTR && swr_loc != NULL) {
    bRet = true;
    INKDebug(SWR_LOG_TAG, "[swr] Request is Stale while revalidate");
  } else {
    INKDebug(SWR_LOG_TAG, "[swr] Request NOT Stale while revalidate");
  }

  INKHandleMLocRelease(req_bufp, req_loc, swr_loc);
  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);

  return bRet;
}


/**
   
*/
static void
deleteFromHeader(INKMBuffer & req_bufp, INKMLoc & req_loc, const char *header, const char *field, int field_len)
{
  INKDebug(SWR_LOG_TAG, "[swr] deleteFromHeader trying to remove from %s : %s ", header, field);
  INKMLoc header_loc, dup_header_loc;
  const char *value;

  header_loc = INKMimeHdrFieldFind(req_bufp, req_loc, header, -1);
  while (header_loc != INK_ERROR_PTR && header_loc != 0) {
    int nvalues = INKMimeFieldValuesCount(req_bufp, header_loc);
    for (int i = 0; i < nvalues; i++) {
      int value_len;
      value = INKMimeFieldValueGet(req_bufp, header_loc, i, &value_len);
      if (value_len == field_len && strncasecmp(value, field, field_len) == 0) {
        INKDebug(SWR_LOG_TAG, "[swr] deleteFromHeader : %s, %s ", header, field);
        INKMimeHdrFieldValueDelete(req_bufp, req_loc, header_loc, i);
      }

      INKHandleStringRelease(req_bufp, header_loc, value);
    }

    // Get next header
    dup_header_loc = INKMimeHdrFieldNextDup(req_bufp, req_loc, header_loc);
    INKHandleMLocRelease(req_bufp, req_loc, header_loc);
    header_loc = dup_header_loc;
  }

  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);
}

/**
* Send request to self
* Add special SWR_FETCH_HEADER so that this can be differntiated from other requests
*/
static bool
sendRequestToSelf(INKHttpTxn &txnp, response_header_values_t &my_state)
{
  INKDebug(SWR_LOG_TAG, "[swr] sendRequestToSelf called");
  INKVConn fetchOnDemandVC;
  bool ret = true;

  unsigned int client_ip = INKHttpTxnClientIPGet(txnp);
  if (INKHttpConnect(htonl(client_ip), 9999, &fetchOnDemandVC) != INK_ERROR) {
    // writer needs: request to write, 
    INKCont fetchCont = INKContCreate(fetch_handler, INKMutexCreate());
    FetchData *pData = new FetchData();
    if (pData) {
      INKContDataSet(fetchCont, pData);

      // Create req and resp buffers for background fetch
      pData->iDumpLen = 0;
      pData->reqBuff = INKIOBufferCreate();
      pData->reqReader = INKIOBufferReaderAlloc(pData->reqBuff);

      pData->respBuff = INKIOBufferCreate();
      pData->respReader = INKIOBufferReaderAlloc(pData->respBuff);
      pData->dumpBuff = INKIOBufferCreate();

      // get the original request with headers and copy to the background fetch request
      INKMBuffer req_bufp;
      INKMLoc req_loc;
      INKIOBuffer reqBuff;
      INKIOBufferReader reqReader;
      int block_avail;

      reqBuff = INKIOBufferCreate();
      reqReader = INKIOBufferReaderAlloc(reqBuff);
      INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc);

      // Get pristine URL
      INKMLoc pristine_url_loc;
 
      if (INKHttpTxnPristineUrlGet(txnp, &req_bufp, &pristine_url_loc) != INK_ERROR) {
        // Set pristine URL in request
        INKDebug(SWR_LOG_TAG, "[swr] setting pristine URL in request");
        INKHttpHdrUrlSet(req_bufp, req_loc, pristine_url_loc);
      }
      // write original header to background fetch request
      if (INKHttpHdrPrint(req_bufp, req_loc, reqBuff) == INK_ERROR) {
        INKDebug(SWR_LOG_TAG, "[swr] INKHttpHdrPrint failed");
        ret = false;
      } else {
        INKDebug(SWR_LOG_TAG, "[swr] INKHttpHdrPrint succeeded");
        if (INKIOBufferReaderAvail(reqReader)) {
          INKIOBufferBlock block = INKIOBufferReaderStart(reqReader);
          while (1) {
            const char *block_start;
            block_start = INKIOBufferBlockReadStart(block, reqReader, &block_avail);
            if ((block = INKIOBufferBlockNext(block)) != NULL) {
              INKIOBufferWrite(pData->reqBuff, block_start, block_avail);
            } else {
              // need to truncate the very last newline character as we need to add 
              // a couple of more headers. (the last newline is treated as terminator)
              if (block_start[block_avail - 1] == '\n' && block_start[block_avail - 2] == '\r')
                INKIOBufferWrite(pData->reqBuff, block_start, block_avail - 2);
              else
                INKIOBufferWrite(pData->reqBuff, block_start, block_avail - 1);
              break;
            }
          }

          // add a If-Modified-Since header so traffic server will update the cache instead of replacing the entry
          INKMLoc ims = INKMimeHdrFieldCreate(req_bufp, req_loc);
          INKMimeFieldNameSet(req_bufp, ims, INK_MIME_FIELD_IF_MODIFIED_SINCE, INK_MIME_LEN_IF_MODIFIED_SINCE);
          INKMimeHdrFieldValueDateSet(req_bufp, req_loc, ims, my_state.date);

          if (INKIOBufferWrite(pData->reqBuff, SWR_FETCH_HEADER, strlen(SWR_FETCH_HEADER)) == INK_ERROR) {
            ret = false;
            INKDebug(SWR_LOG_TAG, "[swr] could not write req to buffer");
          }
#if 1
          block = INKIOBufferReaderStart(pData->reqReader);
          const char *block_start = INKIOBufferBlockReadStart(block, pData->reqReader,
                                                              &block_avail);
          int size = INKIOBufferBlockReadAvail(block, pData->reqReader);
          INKDebug(SWR_LOG_TAG, "[swr] request string: %.*s", size, block_start);
#endif
        }
      }

      pData->writeVIO = INKVConnWrite(fetchOnDemandVC,
                                      fetchCont, pData->reqReader, INKIOBufferReaderAvail(pData->reqReader));
      pData->readVIO = INKVConnRead(fetchOnDemandVC, fetchCont, pData->respBuff, INT_MAX);

      // Release stuff
      INKIOBufferReaderFree(reqReader);
      INKIOBufferDestroy(reqBuff);
      INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);
    } else {
      ret = false;
      INKDebug(SWR_LOG_TAG, "[swr] problem creating continuation");
      INKContDestroy(fetchCont);
      INKVConnShutdown(fetchOnDemandVC, 0, 0);
    }
  } else {
    ret = false;
    INKError("[swr] problem doing http connect");
  }
  INKDebug(SWR_LOG_TAG, "[swr] sendRequestToSelf ends");

  return ret;
}

/**
* Set cache lookup status to whatever is passed in
*/
static void
setCacheStatus(INKHttpTxn & txnp, int lookupStatus)
{
  // Set cache status to FRESH
  INKDebug(SWR_LOG_TAG, "[swr] setCacheStatusFresh : setting cache hit status to %d", lookupStatus);
  INKHttpTxnCacheLookupStatusSet(txnp, lookupStatus);
}

/**
* Add warning header to indicate that response is stale
*/
static bool
addSWRWarningHeader(INKHttpTxn & txnp)
{
  INKMBuffer bufp = NULL;
  INKMLoc hdr_loc = NULL;
  INKMLoc field_loc = NULL;
  bool new_field = false;
  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    INKDebug(SWR_LOG_TAG, "addSWRWarningHeader : Could not get server response");
    return false;
  }
  INKDebug(SWR_LOG_TAG, "addSWRWarningHeader : trying to add header");

  // look for the field first
  if ((field_loc = INKMimeHdrFieldFind(bufp, hdr_loc, INK_MIME_FIELD_WARNING, INK_MIME_LEN_WARNING)) == NULL) {
    // "set" or "append", need to create the field first
    field_loc = INKMimeHdrFieldCreate(bufp, hdr_loc);
    INKMimeHdrFieldNameSet(bufp, hdr_loc, field_loc, INK_MIME_FIELD_WARNING, INK_MIME_LEN_WARNING);
    new_field = true;
  }
  // append the value at the end
  INKMimeHdrFieldValueStringInsert(bufp, hdr_loc, field_loc, -1, SWR_WARNING_HEADER, strlen(SWR_WARNING_HEADER));

  if (new_field) {
    // append the new field
    INKMimeHdrFieldAppend(bufp, hdr_loc, field_loc);
  }

  INKHandleMLocRelease(bufp, hdr_loc, field_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
  INKDebug(SWR_LOG_TAG, "addSWRWarningHeader : done");
  return true;
}

/**
* looks for no-cache directive from the client
*/
static void
parseRequestHeaders(INKHttpTxn & txnp, request_header_values_t & my_state)
{
  INKDebug(SWR_LOG_TAG, "[swr] parseRequestHeaders called");
  INKMBuffer req_bufp;
  INKMLoc req_loc;
  const char *value;

  if (!INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc)) {
    INKError("[swr] parseRequestHeaders : couldn't retrieve client request header.");
    return;
  }

  INKMLoc cache_control_loc, dup_cache_control_loc;
  cache_control_loc = INKMimeHdrFieldFind(req_bufp, req_loc, "Cache-Control", -1);
  while (cache_control_loc != INK_ERROR_PTR && cache_control_loc != 0) {
    int nvalues = INKMimeFieldValuesCount(req_bufp, cache_control_loc);
    for (int i = 0; i < nvalues; i++) {
      int value_len;
      value = INKMimeFieldValueGet(req_bufp, cache_control_loc, i, &value_len);
      if (value_len == INK_HTTP_LEN_NO_CACHE && strncasecmp(value, INK_HTTP_VALUE_NO_CACHE, INK_HTTP_LEN_NO_CACHE) == 0) {
        INKDebug(SWR_LOG_TAG, "[swr] parseRequestHeader : set swr_can_run to false");
        my_state.swr_can_run = false;
      } else if (value_len == INK_HTTP_LEN_ONLY_IF_CACHED &&
                 strncasecmp(value, INK_HTTP_VALUE_ONLY_IF_CACHED, INK_HTTP_LEN_ONLY_IF_CACHED) == 0) {
        INKDebug(SWR_LOG_TAG, "[swr] parseRequestHeader : set only_if_cached to true");
        my_state.only_if_cached = true;
      } else if (value_len == (int) strlen(HTTP_VALUE_BACKGROUND_FETCH) &&
                 strncasecmp(value, HTTP_VALUE_BACKGROUND_FETCH, strlen(HTTP_VALUE_BACKGROUND_FETCH)) == 0) {
        INKDebug(SWR_LOG_TAG, "[swr] parseRequestHeader : set background_fetch to true");
        my_state.background_fetch = true;
      }

      INKHandleStringRelease(req_bufp, cache_control_loc, value);
    }

    // Get next Cache-Control header
    dup_cache_control_loc = INKMimeHdrFieldNextDup(req_bufp, req_loc, cache_control_loc);
    INKHandleMLocRelease(req_bufp, req_loc, cache_control_loc);
    cache_control_loc = dup_cache_control_loc;
  }

  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);
  INKDebug(SWR_LOG_TAG, "[swr] parseRequestHeaders ends");
}

/**
* looks for max-age, stale-while-revalidate, time-to-wait, must-revalidate 
*/
static void
parseResponseHeaders(INKHttpTxn & txnp, response_header_values_t & my_state)
{
  INKMBuffer resp_bufp;
  INKMLoc resp_loc;
  const char *value;

  if (!INKHttpTxnCachedRespGet(txnp, &resp_bufp, &resp_loc)) {
    INKError("[swr] parseResponseHeaders : couldn't retrieve server response header.");
    return;
  }

  INKMLoc date_loc;
  date_loc = INKMimeHdrFieldFind(resp_bufp, resp_loc, INK_MIME_FIELD_DATE, INK_MIME_LEN_DATE);
  if (date_loc != INK_ERROR_PTR && date_loc != 0) {
    INKMimeHdrFieldValueDateGet(resp_bufp, resp_loc, date_loc, &my_state.date);
    INKHandleMLocRelease(resp_bufp, resp_loc, date_loc);
  }

  INKMLoc cache_control_loc, dup_cache_control_loc;
  cache_control_loc =
    INKMimeHdrFieldFind(resp_bufp, resp_loc, INK_MIME_FIELD_CACHE_CONTROL, INK_MIME_LEN_CACHE_CONTROL);
  while (cache_control_loc != INK_ERROR_PTR && cache_control_loc != 0) {
    int nvalues = INKMimeFieldValuesCount(resp_bufp, cache_control_loc);
    for (int i = 0; i < nvalues; i++) {
      int value_len;
      value = INKMimeFieldValueGet(resp_bufp, cache_control_loc, i, &value_len);
      if (value_len >= INK_HTTP_LEN_MAX_AGE + 2) {      // +2 - one for =, atleast another for a number
        const char *ptr;
        if (ptr = strcasestr(value, INK_HTTP_VALUE_MAX_AGE)) {
          ptr += INK_HTTP_LEN_MAX_AGE;
          if (*ptr == '=') {
            ptr++;
            my_state.max_age = atol(ptr);
          }
        }
      }

      if (value_len >= (int) strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE) + 2) {   // +2 - one for =, atleast another for a number
        const char *ptr;
        if (ptr = strcasestr(value, HTTP_VALUE_STALE_WHILE_REVALIDATE)) {
          ptr += strlen(HTTP_VALUE_STALE_WHILE_REVALIDATE);
          if (*ptr == '=') {
            ptr++;
            my_state.stale_while_revalidate_window = atol(ptr);
          }
        }
      }

      if (value_len >= (int) strlen(HTTP_VALUE_TIME_TO_WAIT) + 2) {     //  +2 - one for =, atleast another for a number
        const char *ptr;
        if (ptr = strcasestr(value, HTTP_VALUE_TIME_TO_WAIT)) {
          ptr += strlen(HTTP_VALUE_TIME_TO_WAIT);
          if (*ptr == '=') {
            ptr++;
            my_state.time_to_wait = atol(ptr);
          }
        }
      }

      if (value_len == INK_HTTP_LEN_MUST_REVALIDATE &&
          strncasecmp(value, INK_HTTP_VALUE_MUST_REVALIDATE, INK_HTTP_LEN_MUST_REVALIDATE) == 0) {
        my_state.must_revalidate = true;
      }

      if (value_len == INK_HTTP_LEN_PROXY_REVALIDATE &&
          strncasecmp(value, INK_HTTP_VALUE_PROXY_REVALIDATE, INK_HTTP_LEN_PROXY_REVALIDATE) == 0) {
        my_state.must_revalidate = true;
      }

      INKHandleStringRelease(resp_bufp, cache_control_loc, value);
    }

    // Get next Cache-Control header
    dup_cache_control_loc = INKMimeHdrFieldNextDup(resp_bufp, resp_loc, cache_control_loc);
    INKHandleMLocRelease(resp_bufp, resp_loc, cache_control_loc);
    cache_control_loc = dup_cache_control_loc;
  }

  INKMLoc mime_field_expires_loc;
  mime_field_expires_loc = INKMimeHdrFieldFind(resp_bufp, resp_loc, INK_MIME_FIELD_EXPIRES, INK_MIME_LEN_EXPIRES);
  if (mime_field_expires_loc != INK_ERROR_PTR && mime_field_expires_loc != 0) {
    INKMimeHdrFieldValueDateGet(resp_bufp, resp_loc, mime_field_expires_loc, &my_state.mime_field_expires);
    INKHandleMLocRelease(resp_bufp, resp_loc, mime_field_expires_loc);
  }

  INKHandleMLocRelease(resp_bufp, INK_NULL_MLOC, resp_loc);
  INKDebug(SWR_LOG_TAG,
           "[swr] parseResponseHeaders : mime_field_expires=%ld, stale_while_revalidate_window=%ld, max_age=%ld, date=%ld, time_to_wait=%ld",
           my_state.mime_field_expires, my_state.stale_while_revalidate_window, my_state.max_age, my_state.date,
           my_state.time_to_wait);
}

/**
* This function gets called only if (curr_time > exp_time)
* Logic
*     if ((curr_time<=max stale time)) && (req is not a SWR req))
*     {
*         if(no one else has made async req for URL)
*         {
*             make async req to self for the URL;
*         }    
*         return stale data from cache;
*     }
*     else
*     {
*         nothing special needs to be done
*     }
*  @param[out] int. 0 : Do not serve stale data, as SWR was off or time is past max_stale_time
*  @param[out] int. 1 : Serve stale data as SWR was done
*/
static int
doStaleWhileRevalidate(INKCont & contp, void *edata, response_header_values_t & my_state)
{
  INKHttpTxn txnp = (INKHttpTxn) edata;
  INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : Started");

  long swr_window = my_state.get_swr_window();
  if (swr_window == 0) {
    INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : turned OFF");
    return 0;
  }
  int retval = 0;
  time_t curr_time = INKhrtime() / 1000000000;
  time_t max_stale_time = my_state.get_max_stale_time();

  time_t diff = max_stale_time - curr_time;
  INKDebug(SWR_LOG_TAG,
           "[swr] doStaleWhileRevalidate : curr_time=%ld, max_stale_time=%ld, diff=%ld",
           curr_time, max_stale_time, diff);
  if (diff > 0 || swr_window == STALE_WHILE_REVALIDATE_WINDOW_INFINITE) {
    if (!isSWR(txnp)) {
      // If no one else has made the request or is making the request,
      // Send request to self after adding special header

      char *url = getURLFromReqHeader(txnp);
      if (url == NULL) {
        INKError("[swr] doStaleWhileRevalidate : url is NULL");
        return 0;
      }
      // retval to indicate that stale data must be served
      retval = 1;
      bool useTimeToWait = true;
      INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : url=%s", url);
      if (INKMutexLock(swr_mutex) == INK_SUCCESS) {
        // Check again after getting the mutex lock as someone else might have requested the URL in the interim
        if (swr_sites_requested.find(url) == swr_sites_requested.end()) {
          INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : sending req to self. inserting URL=%s", url);

          // insert URL into sites requested and unlock mutex
          swr_sites_requested.insert(url);
          INKMutexUnlock(swr_mutex);

          if (!sendRequestToSelf(txnp, my_state)) {
            // If sending request to self failed, do not use time for wait
            // remove URL from sites requested  
            useTimeToWait = false;
            if (INKMutexLock(swr_mutex) == INK_SUCCESS) {
              INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : removing url=%s", url);
              swr_sites_requested.erase(url);
              INKMutexUnlock(swr_mutex);
            }
          }
        } else {
          INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : some one else is requesting URL=%s", url);
          INKMutexUnlock(swr_mutex);
          useTimeToWait = false;
        }
      } else {
        useTimeToWait = false;
      }
      INKfree((void *) url);

      // wait for time-to-wait milli secs and then set cache lookup status to fresh.
      // this will give the async request some time to complete.
      //FIXME 
      //Commenting out time to wait code as this cannot be implemented easily now
      /*
         if (useTimeToWait) {
         time_t time_to_wait = my_state.get_time_to_wait();
         INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate scheduling continuation after %ld ms", time_to_wait);
         INKHttpSchedule(contp, txnp, time_to_wait);
         retval = 2;
         }
       */

    } else {
      INKDebug(SWR_LOG_TAG,
               "[swr] doStaleWhileRevalidate : swr request received. nothing to do.");
    }
  } else {
    INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : Not doing SWR, as cache data has expired");
  }
  INKDebug(SWR_LOG_TAG, "[swr] doStaleWhileRevalidate : Ends");
  return retval;
}

/**
* Remove URL from set of URLs being requested asynchronously
*/
static void
removeURLFromSitesRequested(INKHttpTxn & txnp)
{
  char *url = getURLFromReqHeader(txnp);
  if (url == NULL) {
    INKError("[swr] removeURLFromSitesRequested : url is NULL");
    return;
  }
  if (INKMutexLock(swr_mutex) != INK_ERROR) {
    INKDebug(SWR_LOG_TAG, "[swr] removeURLFromSitesRequested : removing url=%s", url);
    swr_sites_requested.erase(url);
    INKMutexUnlock(swr_mutex);
  }
  INKfree((void *) url);
}


static bool
isRequestFromLocalhost(INKHttpTxn & txnp)
{
    INKDebug(SWR_LOG_TAG, "[swr] isRequestFromLocalhost returning : %d", INKHttpIsInternalRequest(txnp));
    return INKHttpIsInternalRequest(txnp);
}

static void
ignoreOnlyIfCached(INKHttpTxn & txnp)
{
  // Delete only-if-cached from Cache-Control
  INKMBuffer req_bufp;
  INKMLoc req_loc;
  INKHttpTxnClientReqGet(txnp, &req_bufp, &req_loc);
  deleteFromHeader(req_bufp, req_loc, "Cache-Control", INK_HTTP_VALUE_ONLY_IF_CACHED, INK_HTTP_LEN_ONLY_IF_CACHED);
  INKHandleMLocRelease(req_bufp, INK_NULL_MLOC, req_loc);
}

static int
plugin_worker_handler(INKCont contp, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_HTTP_READ_REQUEST_HDR:
    {
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_READ_REQUEST_HDR_HOOK");
      INKHttpTxn txnp = (INKHttpTxn) edata;
      request_header_values_t pstate;

      parseRequestHeaders(txnp, pstate);
      // skip RWW if request is SWR and is from localhost
      if (isSWR(txnp) && isRequestFromLocalhost(txnp)) {
        INKDebug(SWR_LOG_TAG, "[swr] Disable RWW as this is a SWR request from localhost");
        INKHttpTxnSkipRww(txnp);
      }

      if (pstate.swr_can_run) {
        // Add hook only if SWR can run
        INKHttpTxnHookAdd(txnp, INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK, contp);
      }

      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      break;
    }
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    {
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_SEND_RESPONSE_HDR_HOOK");
      INKHttpTxn txnp = (INKHttpTxn) edata;
      addSWRWarningHeader(txnp);
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_SEND_RESPONSE_HDR ends");
      break;
    }
  case INK_EVENT_HTTP_CACHE_LOOKUP_COMPLETE:
    {
      INKHttpTxn txnp = (INKHttpTxn) edata;
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK");

      int lookupStatus = 0;
      if (INKHttpTxnCacheLookupStatusGet(txnp, &lookupStatus) != INK_SUCCESS) {
        INKDebug(SWR_LOG_TAG, "[swr] cache status get failure");
      } else {
        request_header_values_t pstate;
        bool is_swr_request = false;
        if (lookupStatus == INK_CACHE_LOOKUP_MISS || lookupStatus == INK_CACHE_LOOKUP_HIT_STALE) {
          parseRequestHeaders(txnp, pstate);
          is_swr_request = isSWR(txnp);
        }

        if (lookupStatus == INK_CACHE_LOOKUP_MISS) {
          INKDebug(SWR_LOG_TAG, "[swr] cache status MISS");
          // Code for background fetch on miss if only_if_cached and background_fetch are set by the client
          if (!is_swr_request) {
            if (pstate.only_if_cached && pstate.background_fetch) {
              // Do background fetch only if client Cache-Control has both only-if-cached and background-fetch
              response_header_values_t resp_values;
              resp_values.init();
              resp_values.stale_while_revalidate_window = STALE_WHILE_REVALIDATE_WINDOW_INFINITE;
              INKDebug(SWR_LOG_TAG, "[swr] doing background fetch");
              // Ignore return value in this case as we do not want a warning header
              doStaleWhileRevalidate(contp, edata, resp_values);
            }
          } else {
            // SWR request. Handle background fetch request
            if (pstate.only_if_cached && pstate.background_fetch) {
              // Delete only-if-cached from Cache-Control
              // This will force the core to make a req to the OS
              INKDebug(SWR_LOG_TAG, "[swr] ignoreOnlyIfCached");
              ignoreOnlyIfCached(txnp);
            }
          }
        } else if (lookupStatus == INK_CACHE_LOOKUP_HIT_STALE) {
          INKDebug(SWR_LOG_TAG, "[swr] cache status HIT STALE");

          if (!is_swr_request) {

            response_header_values_t my_state;
            my_state.init();
            parseResponseHeaders(txnp, my_state);
            bool forced_background_fetch = false;
            if (pstate.only_if_cached && pstate.background_fetch) {
                // Force background fetch only if document age is within max_stale_time
                time_t curr_time = INKhrtime() / 1000000000;
                time_t max_stale_time = my_state.get_max_stale_time();
                time_t diff = max_stale_time - curr_time;
                if(diff < 0) {
                    forced_background_fetch = true;
                }
                my_state.stale_while_revalidate_window = STALE_WHILE_REVALIDATE_WINDOW_INFINITE;
            }

            if (my_state.must_revalidate == false && doStaleWhileRevalidate(contp, edata, my_state) == 1 && !forced_background_fetch) {
              setCacheStatus(txnp, INK_CACHE_LOOKUP_HIT_FRESH);
              INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
            } else {
              INKDebug(SWR_LOG_TAG, "[swr] Not serving stale data");
              if (pstate.only_if_cached) {
                // Set cache status to miss
                // must return an error
                setCacheStatus(txnp, INK_CACHE_LOOKUP_MISS);
              }
            }
          } else {
            // SWR request. Handle background fetch request
            if (pstate.only_if_cached && pstate.background_fetch) {
              // Delete only-if-cached from Cache-Control
              // This will force the core to make a req to the OS
              INKDebug(SWR_LOG_TAG, "[swr] ignoreOnlyIfCached");
              ignoreOnlyIfCached(txnp);
            }
          }
          //FIXME
          //The code below should be used when time to wait can be implemented correctly
          /*
             if (doStaleWhileRevalidate(contp, event, edata)) {
             // If doStaleWhileRevalidate returns true, there is an async request happening to OS.
             // Do not reenable continuation.
             // Wait for timeout
             break;
             }
           */
        } else if (lookupStatus == INK_CACHE_LOOKUP_HIT_FRESH) {
          INKDebug(SWR_LOG_TAG, "[swr] cache status HIT FRESH");
        } else if (lookupStatus == INK_CACHE_LOOKUP_SKIPPED) {
          INKDebug(SWR_LOG_TAG, "[swr] cache status SKIPPED");
        }
      }
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_CACHE_LOOKUP_COMPLETE_HOOK ends");
      break;
    }
  case INK_EVENT_HTTP_TXN_CLOSE:
    {
      INKHttpTxn txnp = (INKHttpTxn) edata;
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_TXN_CLOSE_HOOK");
      if (isSWR(txnp)) {
        removeURLFromSitesRequested(txnp);
      }
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      INKContDestroy(contp);
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_TXN_CLOSE_HOOK ends");
      break;
    }
    //FIXME
    //The code below should be used when time to wait can be implemented correctly
    /*
       case INK_EVENT_IMMEDIATE:
       {
       // TIME_TO_WAIT was 0
       INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_EVENT_IMMEDIATE_HOOK TIME_TO_WAIT was 0");
       INKHttpTxn txnp = (INKHttpTxn) edata;
       setCacheStatus(txnp, INK_CACHE_LOOKUP_HIT_FRESH);
       INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
       INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_EVENT_IMMEDIATE_HOOK ends");
       break;
       }
       case INK_EVENT_TIMEOUT:
       {
       // We have waited long enough for the async request to complete
       INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_EVENT_TIMEOUT_HOOK done waiting for response");
       INKHttpTxn txnp = (INKHttpTxn) edata;
       setCacheStatus(txnp, INK_CACHE_LOOKUP_HIT_FRESH);
       INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_EVENT_TIMEOUT_HOOK txnp = %u", txnp);
       INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
       INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_EVENT_TIMEOUT_HOOK ends");
       break;
       }
     */
  default:
    {
      INKHttpTxn txnp = (INKHttpTxn) edata;
      INKDebug(SWR_LOG_TAG, "[swr] default event");
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      break;
    }
  }

  return 1;
}

static int
plugin_main_handler(INKCont contp, INKEvent event, void *edata)
{
  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    {
      INKDebug(SWR_LOG_TAG, "[swr] MAIN_HANDLER::INK_HTTP_READ_REQUEST_HDR_HOOK");
      INKHttpTxn txnp = (INKHttpTxn) edata;

      // Create new continuation
      INKCont workerCont = INKContCreate(plugin_worker_handler, NULL);

      // Add local hooks for the new continuation
      INKHttpTxnHookAdd(txnp, INK_HTTP_READ_REQUEST_HDR_HOOK, workerCont);
      INKHttpTxnHookAdd(txnp, INK_HTTP_TXN_CLOSE_HOOK, workerCont);
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      break;
    }
  default:
    {
      INKDebug(SWR_LOG_TAG, "[swr] default event");
      INKHttpTxn txnp = (INKHttpTxn) edata;
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
      break;
    }
  }

  return 1;
}

static void
parse_config_line(char *line)
{
  std::istringstream conf_line(line);

  std::string key, value;
  conf_line >> key;
  conf_line >> value;
  if (key.empty() || value.empty())
    return;

  if (key == "stale_while_revalidate_window")
    STALE_WHILE_REVALIDATE_WINDOW = atol(value.c_str());
  else if (key == "time_to_wait")
    TIME_TO_WAIT = atol(value.c_str());
  else if (key == "max_age")
    MAX_AGE = atol(value.c_str());
}

static bool
read_config(const char *file_name)
{
  INKFile conf_file;
  conf_file = INKfopen(file_name, "r");

  if (conf_file != NULL) {
    char buf[1024];
    while (INKfgets(conf_file, buf, sizeof(buf) - 1) != NULL) {
      if (strlen(buf) == 0 || buf[0] == '#')
        continue;
      parse_config_line(buf);
    }
    INKfclose(conf_file);
  } else {
    fprintf(stderr, "Failed to open stale while revalidate config file %s\n", file_name);
    return false;
  }
  INKDebug(SWR_LOG_TAG, "[swr] STALE_WHILE_REVALIDATE_WINDOW = %ld", STALE_WHILE_REVALIDATE_WINDOW);
  INKDebug(SWR_LOG_TAG, "[swr] TIME_TO_WAIT = %ld", TIME_TO_WAIT);
  return true;
}

void
INKPluginInit(int argc, const char *argv[])
{
  char default_filename[1024];
  const char *conf_filename;

  if (argc > 1) {
    conf_filename = argv[1];
  } else {
    sprintf(default_filename, "%s/stale_while_revalidate.conf", INKPluginDirGet());
    conf_filename = default_filename;
  }

  if (!read_config(conf_filename)) {
    if (argc > 1) {
      INKError(SWR_LOG_TAG, "[swr] Plugin conf not valid.");
    } else {
      INKError(SWR_LOG_TAG, "[swr] No config file specified in plugin.conf");
    }
    INKError(SWR_LOG_TAG, "[swr] Continuing with default values for config parameters");
  }
  // Creates parent continuation
  INKCont mainCont = INKContCreate(plugin_main_handler, NULL);

  // Add global hooks with continuation to be called when the event has to be processed
  INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, mainCont);
}
