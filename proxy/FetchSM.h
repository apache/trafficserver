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

/***************************************************************************
 *
 *  FetchSM.h header file for Fetch Page
 *
 ****************************************************************************/

#ifndef _FETCH_SM_H
#define _FETCH_SM_H

#include "P_Net.h"
#include "ts.h"
#include "HttpSM.h"

class FetchSM: public Continuation
{
public:
  FetchSM()
  { }

  void init(Continuation* cont, TSFetchWakeUpOptions options, TSFetchEvent events, const char* headers, int length, unsigned int ip, int port)
  {
    //_headers.assign(headers);
    Debug("FetchSM", "[%s] FetchSM initialized for request with headers\n--\n%.*s\n--", __FUNCTION__, length, headers);
    req_finished = 0;
    resp_finished = 0;
    header_done = 0;
    req_buffer = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
    req_reader = req_buffer->alloc_reader();
    resp_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    resp_reader = resp_buffer->alloc_reader();
    response_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    response_reader = response_buffer->alloc_reader();
    contp = cont;
    http_parser_init(&http_parser);
    client_response_hdr.create(HTTP_TYPE_RESPONSE);
    client_response  = NULL;
    mutex = new_ProxyMutex();
    callback_events = events;
    callback_options = options;
    _ip = ip;
    _port = port;
    writeRequest(headers,length);
    SET_HANDLER(&FetchSM::fetch_handler);
  }
  int fetch_handler(int event, void *data);
  void process_fetch_read(int event);
  void process_fetch_write(int event);
  void httpConnect();
  void cleanUp();
  void get_info_from_buffer(IOBufferReader *reader);
  char* resp_get(int* length);

private:
  int InvokePlugin(int event, void*data);

  void writeRequest(const char *headers,int length)
  {
    if(length == -1)
    req_buffer->write(headers, strlen(headers));
    else
    req_buffer->write(headers,length);
  }

  int64_t getReqLen() const { return req_reader->read_avail(); }

  TSVConn http_vc;
  VIO *read_vio;
  VIO *write_vio;
  MIOBuffer *response_buffer;   // response to FetchSM call
  IOBufferReader *response_reader;      // response to FetchSM call
  MIOBuffer *req_buffer;
  IOBufferReader *req_reader;
  char *client_response;
  int  client_bytes;
  MIOBuffer *resp_buffer;       // response to HttpConnect Call
  IOBufferReader *resp_reader;
  Continuation *contp;
  HTTPParser http_parser;
  HTTPHdr client_response_hdr;
  TSFetchEvent callback_events;
  TSFetchWakeUpOptions callback_options;
  bool req_finished;
  bool header_done;
  bool resp_finished;
  unsigned int _ip;
  int _port;
};

#endif
