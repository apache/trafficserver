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

#ifndef __HTTPFETCH_H__
#define __HTTPFETCH_H__ 1

#include <string>
#include <ts/ts.h>
#include <arpa/inet.h>

#include "CarpHashAlgorithm.h"

class HashNode;
class HashAlgorithm;

class HttpFetch 
{
private:
  // some extra encapsulation for internal data types
  struct RequestInfo
  {
    TSMBuffer buf;
    TSMLoc http_hdr_loc;
//    struct sockaddr *client_addr;
  };

  struct ResponseInfo
  {
    TSMBuffer buf;
    TSMLoc http_hdr_loc;
    TSHttpParser parser;
    bool headerParsed;
    TSHttpStatus status;
  };

 
public:

  enum HttpFetcherEvent
  {
    UNKNOWN = 10000,
    SUCCESS, 
    TIMEOUT, 
    FAILURE
  };

  HttpFetch(const std::string &url, HashAlgorithm * hashAlgo, HashNode * hashNode,
      const char *method = TS_HTTP_METHOD_GET);
  ~HttpFetch();
  
  void makeAsyncRequest(struct sockaddr const* serverAddr);

  HttpFetcherEvent    getResponseResult() { return _result; }
  TSHttpStatus        getResponseStatusCode() { return _responseStatus; }
  const std::string&  getResponseBody() { return _responseBody; }
  const std::string&  getResponseHeaders() { return _responseHeaders; }
  void                setHealthcheckTimeout(int timeout);
  //whether it is ready to call makeAsyncRequest
  bool                isReady() { return _ready;}

  /*
   * Used internally to handle event callbacks
   */
  int  handleIOEvent(TSCont cont, TSEvent event, void *edata);
  
private:
  HttpFetch();
  ResponseInfo*     createResponseInfo();
  void              freeResponseInfo();
  RequestInfo*      createRequestInfo();
  void              freeRequestInfo();

  void              parseResponse();

  HashAlgorithm *   _hashAlgo;
  HashNode  *       _hashNode;

  uint64_t          _startTime;
  uint64_t          _endTime;

  std::string       _url;
  std::string       _request;
  TSHttpStatus      _responseStatus;
  std::string       _responseHeaders;
  std::string       _responseBody;
  HttpFetcherEvent  _result;

  volatile bool     _ready;
  TSAction            _hcTimeout;
  TSAction            _connAction;
  int               _hcTimeoutSecond;

  ResponseInfo*     _respInfo;
  RequestInfo*      _reqInfo;
  
  TSIOBuffer        _reqIOBuf;
  TSIOBufferReader  _reqIOBufReader;
  TSVIO             _rVIO;
        
  TSIOBuffer        _respIOBuf;
  TSIOBufferReader  _respIOBufReader;  
  TSVConn           _vConn;
  TSVIO             _wVIO;
      
  struct sockaddr   _serverAddr;

};


#endif 
