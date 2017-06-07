/** @file

  Limited URL fetcher..

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

#define __STDC_LIMIT_MACROS   // need INT64_MAX definition
#include <stdint.h>
#include <sys/time.h>

#include "HttpFetch.h"
#include "UrlComponents.h"
#include "Common.h"

using std::string;

/**********************************************************/
// just 'pass through' and call object's handlEvent fn

static int
handleHttpFetchIOEvents(TSCont cont, TSEvent event, void *edata)
{
  HttpFetch *fetchObj = static_cast<HttpFetch *> (TSContDataGet(cont));
  if (NULL == fetchObj) {
    TSDebug(DEBUG_FETCH_TAG, "handleHttpFetchEvents continuation data NULL");
    TSAssert(fetchObj);
  }
  return fetchObj->handleIOEvent(cont, event, edata);
}

/**********************************************************/
HttpFetch::HttpFetch(const std::string &url, HashAlgorithm *hashAlgo,
    HashNode *hashNode,const char *method)
{
  TSMBuffer bufp;
  _url = url;
  _respInfo = NULL;
  _reqInfo  = NULL;
  _hashAlgo = hashAlgo;
  _hashNode = hashNode;
  _hcTimeoutSecond = DEFAULT_HEALTH_CHECK_TIMEOUT;

  bufp = TSMBufferCreate();

  if (bufp != NULL) {
    TSMLoc urlp;
    if (TSUrlCreate(bufp, &urlp) == TS_SUCCESS) {
      const char *start = url.data();
      if (TSUrlParse(bufp, urlp, &start, start + url.length()) == TS_PARSE_DONE) {
        UrlComponents reqUrl;
        string sPath;
        string sHost;
        reqUrl.populate(bufp, urlp);
        reqUrl.getCompletePathString(sPath);
        reqUrl.getCompleteHostString(sHost);
        _request = string(method) + " " + sPath + " HTTP/1.0\r\nHost: " + sHost + "\r\n";
        _request += CARP_ROUTED_HEADER + ": 1\r\n";
        _request += "\r\n";
      }
      TSHandleMLocRelease(bufp, NULL, urlp);
    }
  }
  TSMBufferDestroy(bufp);
  TSDebug(DEBUG_FETCH_TAG, "HttpFetch assembled this request %s", _request.c_str());
  _responseStatus=TS_HTTP_STATUS_NONE;
  _ready = true;
}

/**********************************************************/
HttpFetch::~HttpFetch()
{
}
/**********************************************************/
void
HttpFetch::setHealthcheckTimeout(int timeout) {
  _hcTimeoutSecond = timeout;
}
/**********************************************************/
void
HttpFetch::makeAsyncRequest(struct sockaddr const* serverAddr)
{
  _ready = false;
  __sync_synchronize();
  _result = UNKNOWN;
  TSCont fetchCont = TSContCreate(handleHttpFetchIOEvents, TSMutexCreate());
  //TSCont fetchCont = TSContCreate(handleHttpFetchIOEvents, NULL);
  TSContDataSet(fetchCont, static_cast<void *> (this));

  // save server addr
  memmove((void *) &_serverAddr, (void *) serverAddr, sizeof (struct sockaddr));

  // initiate request
  TSDebug(DEBUG_FETCH_TAG, "TSNetConnect()");

  struct timeval tvStart;
  gettimeofday(&tvStart, NULL);
  _startTime = tvStart.tv_sec*1000 + tvStart.tv_usec/1000;

  struct sockaddr_in tempServerAddr;
  memmove((void *) &tempServerAddr, (void *) &_serverAddr, sizeof (struct sockaddr));
  TSDebug(DEBUG_FETCH_TAG, "serverAddr: %s:%d", inet_ntoa(tempServerAddr.sin_addr), ntohs(tempServerAddr.sin_port) );
  _hcTimeout = TSContSchedule(fetchCont, _hcTimeoutSecond * 1000, TS_THREAD_POOL_DEFAULT);
  _connAction = TSNetConnect(fetchCont, (const struct sockaddr *) &_serverAddr);

}

/**********************************************************/
void
HttpFetch::parseResponse()
{
  TSIOBufferBlock block;
  TSParseResult pr = TS_PARSE_CONT;
  int64_t avail = 0;
  char *start = NULL;
  char *initialStart = NULL;

  TSDebug(DEBUG_FETCH_TAG, "Entering parse_response");

  block = TSIOBufferReaderStart(_respIOBufReader);

  if (!_respInfo->headerParsed) {
    while ((pr == TS_PARSE_CONT) && (block != NULL)) {
      initialStart = start = (char *) TSIOBufferBlockReadStart(block, _respIOBufReader, &avail);
      if (avail > 0) {
        pr = TSHttpHdrParseResp(_respInfo->parser, _respInfo->buf, _respInfo->http_hdr_loc, (const char **) &start, (const char *) (start + avail));
        _responseHeaders.append(initialStart, start - initialStart);
      }
      block = TSIOBufferBlockNext(block);
    }
    // update avail
    if (start && initialStart) {
      avail -= (start - initialStart);
    }
    if (pr != TS_PARSE_CONT) {
      _respInfo->status = TSHttpHdrStatusGet(_respInfo->buf, _respInfo->http_hdr_loc);
      _responseStatus = _respInfo->status;
      _respInfo->headerParsed = true;
      TSDebug(DEBUG_FETCH_TAG, "HTTP Status: %d", _respInfo->status);
    }
  }

  if (_respInfo->headerParsed) {
    if (avail && start) { // if bytes left from header parsing, get those
      _responseBody.append(start, avail);
      if(block) {
        block = TSIOBufferBlockNext(block);
      }
    }

    while (block != NULL) {
      start = (char *) TSIOBufferBlockReadStart(block, _respIOBufReader, &avail);
      if (avail > 0) {
        _responseBody.append(start, avail);
      }
      block = TSIOBufferBlockNext(block);
    }
  }
  TSDebug(DEBUG_FETCH_TAG, "Leaving parseResponse");
}


/**********************************************************/
int
HttpFetch::handleIOEvent(TSCont cont, TSEvent event, void *edata)
{
  int64_t avail;
  bool bCleanUp = false; // free everything when transaction is complete


  TSDebug(DEBUG_FETCH_TAG, "Entering handleIOEvent");

  switch (event) {
  case TS_EVENT_NET_CONNECT: // connected to server
    TSDebug(DEBUG_FETCH_TAG, "Connected (maybe)");
    _connAction = NULL;
    _respInfo = createResponseInfo();
    _reqInfo = createRequestInfo();

    _reqIOBuf = TSIOBufferCreate();
    _reqIOBufReader = TSIOBufferReaderAlloc(_reqIOBuf);
    _respIOBuf = TSIOBufferCreate();
    _respIOBufReader = TSIOBufferReaderAlloc(_respIOBuf);

    TSHttpHdrPrint(_reqInfo->buf, _reqInfo->http_hdr_loc, _reqIOBuf);
    TSIOBufferWrite(_reqIOBuf, "\r\n", 2);

    _vConn = static_cast<TSVConn> (edata); // get connection

    _rVIO = TSVConnRead(_vConn, cont, _respIOBuf, INT64_MAX);

    TSDebug(DEBUG_FETCH_TAG, "Writing %ld bytes", TSIOBufferReaderAvail(_reqIOBufReader));
    _wVIO = TSVConnWrite(_vConn, cont, _reqIOBufReader, TSIOBufferReaderAvail(_reqIOBufReader));
    break;
  case TS_EVENT_NET_CONNECT_FAILED:
    TSDebug(DEBUG_FETCH_TAG, "Connect failed");
    _connAction = NULL;
    TSActionCancel(_hcTimeout);
    _hcTimeout = NULL;
    _result = FAILURE;
    break;
  case TS_EVENT_ERROR:
    TSDebug(DEBUG_FETCH_TAG, "Error event");
    _result = FAILURE;
    TSVConnClose(_vConn);
    bCleanUp = true;
    break;
  case TS_EVENT_TIMEOUT:
    if (_hcTimeout) {
      _hcTimeout = NULL;
    }

    if (_connAction == NULL) {
      TSVConnAbort(_vConn, TS_VC_CLOSE_ABORT);
    // clean up only when _vConn have been set up
      bCleanUp = true;
    } else {
      TSActionCancel(_connAction);
      _connAction = NULL;
    }

    TSDebug(DEBUG_FETCH_TAG, "health check timeout");
    _result = TIMEOUT;
    break;
  case TS_EVENT_VCONN_WRITE_READY:
    // We shouldn't get here because we specify the exact size of the buffer.
    TSDebug(DEBUG_FETCH_TAG, "Write Ready");
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(DEBUG_FETCH_TAG, "Write Complete");
    //TSDebug(LOG_PREFIX, "TSVConnShutdown()");
    //TSVConnShutdown(state->vconn, 0, 1);
    //TSVIOReenable(state->w_vio);
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSDebug(DEBUG_FETCH_TAG, "Read Ready");

    avail = TSIOBufferReaderAvail(_respIOBufReader);

    if (_respInfo) {
      parseResponse();
    }

    // Consume data
    avail = TSIOBufferReaderAvail(_respIOBufReader);
    TSIOBufferReaderConsume(_respIOBufReader, avail);
    TSVIONDoneSet(_rVIO, TSVIONDoneGet(_rVIO) + avail);
    TSVIOReenable(_rVIO);
    break;
  case TS_EVENT_VCONN_READ_COMPLETE:
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    if (event == TS_EVENT_VCONN_INACTIVITY_TIMEOUT) {
      TSDebug(DEBUG_FETCH_TAG, "Inactivity Timeout");
      TSDebug(DEBUG_FETCH_TAG, "TSVConnAbort()");
      TSVConnAbort(_vConn, TS_VC_CLOSE_ABORT);
      _result = TIMEOUT;
    } else {
      if (event == TS_EVENT_VCONN_READ_COMPLETE) {
        TSDebug(DEBUG_FETCH_TAG, "Read Complete");
      } else if (event == TS_EVENT_VCONN_EOS) {
        TSDebug(DEBUG_FETCH_TAG, "EOS");
      }
      TSDebug(DEBUG_FETCH_TAG, "TSVConnClose()");
      TSVConnClose(_vConn);
      _result = SUCCESS;
    }

    avail = TSIOBufferReaderAvail(_respIOBufReader);

    if (_respInfo) {
      parseResponse();
    }

    // Consume data
    avail = TSIOBufferReaderAvail(_respIOBufReader);
    TSIOBufferReaderConsume(_respIOBufReader, avail);
    TSVIONDoneSet(_rVIO, TSVIONDoneGet(_rVIO) + avail);
    bCleanUp = true;
    break;
  default:
    TSDebug(DEBUG_FETCH_TAG, "Unknown event %d. edata=%p", event, edata);
    TSError("Unknown event %d.", event);
    break;
  }

  // if the transaction is done, free buffers and other stuff..
  if (bCleanUp) {
    if(_connAction) {
      TSActionCancel(_connAction);
      _connAction = NULL;
    }

    if(_hcTimeout) {
      TSActionCancel(_hcTimeout);
      _hcTimeout = NULL;
    }

    bool bGood = false;
    struct timeval tvEnd;
    gettimeofday(&tvEnd, NULL);
    _endTime = tvEnd.tv_sec*1000 + tvEnd.tv_usec/1000;
    TSDebug(DEBUG_FETCH_TAG, "Fetch end, get response status");
    if (_result == SUCCESS) {
      if (getResponseStatusCode() == TS_HTTP_STATUS_OK) {
        bGood = true;
      }
    }

    if (_hashAlgo && _hashNode) {

      // use name and port to get hashNode from HashAlgo. Need loop the HashNode list to find 
      // proper  Node.
      //_hashAlgo->setStatus(_hashNode->name, _hashNode->listenPort, bGood, time(NULL), 0);

      // use hashNode reference
      _hashAlgo->setStatus(_hashNode, bGood, time(NULL), _endTime - _startTime);
    }

    TSDebug(DEBUG_FETCH_TAG, "Cleaning up");
    _responseHeaders.clear();
    _responseBody.clear();
    freeRequestInfo();
    freeResponseInfo();
    _responseStatus = TS_HTTP_STATUS_NONE;
    TSIOBufferReaderFree(_reqIOBufReader);
    TSIOBufferDestroy(_reqIOBuf);
    TSIOBufferReaderFree(_respIOBufReader);
    TSIOBufferDestroy(_respIOBuf);
    TSDebug(DEBUG_FETCH_TAG, "Destroying Cont");
    TSContDestroy(cont);
    __sync_synchronize();
    _ready = true;
  }

  TSDebug(DEBUG_FETCH_TAG, "Leaving handleIOEvent");
  return 0;
}

/**********************************************************/
HttpFetch::ResponseInfo*
HttpFetch::createResponseInfo(void)
{
  ResponseInfo *respInfo;

  //TSDebug(DEBUG_FETCH_TAG, "Entering create_response_info");

  respInfo = (ResponseInfo *) TSmalloc(sizeof (ResponseInfo));

  respInfo->buf = TSMBufferCreate();
  respInfo->http_hdr_loc = TSHttpHdrCreate(respInfo->buf);
  respInfo->parser = TSHttpParserCreate();
  respInfo->headerParsed = false;

  //TSDebug(DEBUG_FETCH_TAG, "Leaving create_reseponse_info");

  return respInfo;
}

/**********************************************************/
void
HttpFetch::freeResponseInfo()
{
  //TSDebug(DEBUG_FETCH_TAG, "Entering freeResponseInfo");
  if (_respInfo) {
    TSHandleMLocRelease(_respInfo->buf, TS_NULL_MLOC, _respInfo->http_hdr_loc);
    TSMBufferDestroy(_respInfo->buf);
    TSHttpParserDestroy(_respInfo->parser);
    TSfree(_respInfo);
    _respInfo = NULL;
  }
  //TSDebug(DEBUG_FETCH_TAG, "Leaving freeResponseInfo");
}

/**********************************************************/
HttpFetch::RequestInfo*
HttpFetch::createRequestInfo()
{
  RequestInfo *reqInfo;

  //TSDebug(DEBUG_FETCH_TAG, "Entering create_request_info");

  reqInfo = (RequestInfo *) TSmalloc(sizeof (RequestInfo));

  reqInfo->buf = TSMBufferCreate();
  reqInfo->http_hdr_loc = TSHttpHdrCreate(reqInfo->buf);


  TSHttpParser parser;

  parser = TSHttpParserCreate();
  const char *start = _request.data();
  const char *end = start + _request.size();
  TSParseResult parseResult = TSHttpHdrParseReq(parser, reqInfo->buf, reqInfo->http_hdr_loc, &start, end);
  if (parseResult != TS_PARSE_DONE) { // should be done since the entire request is available
    TSDebug(DEBUG_FETCH_TAG, "TSHttpHdrParseReq is not done, internal error?");
  }
  TSHttpParserDestroy(parser);

  //  reqInfo->client_addr = static_cast<sockaddr *> (TSmalloc(sizeof (struct sockaddr)));
  //  memmove((void *) reqInfo->client_addr, serverAddr, sizeof (struct sockaddr));

  //TSDebug(DEBUG_FETCH_TAG, "Leaving create_request_info");

  return reqInfo;
}

/**********************************************************/
void
HttpFetch::freeRequestInfo()
{
  //TSDebug(DEBUG_FETCH_TAG, "Entering freeRequestInfo");
  if (_reqInfo) {
    TSHandleMLocRelease(_reqInfo->buf, TS_NULL_MLOC, _reqInfo->http_hdr_loc);
    //TSDebug(DEBUG_FETCH_TAG, "Destroy Buffer");
    TSMBufferDestroy(_reqInfo->buf);
    //TSDebug(DEBUG_FETCH_TAG, "Free Request Info");
    TSfree(_reqInfo);
    _reqInfo = NULL;
  }
  //TSDebug(DEBUG_FETCH_TAG, "Leaving freeRequestInfo");
}


