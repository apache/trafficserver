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
#include "HttpTunnel.h"

class FetchSM: public Continuation
{
public:
  FetchSM()
  { }

  void init_comm()
  {
    recursion = 0;
    req_finished = 0;
    resp_finished = 0;
    header_done = 0;
    user_data = NULL;
    has_sent_header = false;
    proto_stack = (1u << TS_PROTO_HTTP);
    req_method = TS_FETCH_METHOD_NONE;
    req_content_length = 0;
    resp_is_chunked = -1;
    resp_content_length = -1;
    resp_recived_body_len = 0;
    cont_mutex.clear();
    req_buffer = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
    req_reader = req_buffer->alloc_reader();
    resp_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    resp_reader = resp_buffer->alloc_reader();
    http_parser_init(&http_parser);
    client_response_hdr.create(HTTP_TYPE_RESPONSE);
    client_response  = NULL;
    SET_HANDLER(&FetchSM::fetch_handler);
  }

  void init(Continuation* cont, TSFetchWakeUpOptions options,
            TSFetchEvent events, const char* headers, int length,
            sockaddr const *addr)
  {
    Debug("FetchSM", "[%s] FetchSM initialized for request with headers\n--\n%.*s\n--",
          __FUNCTION__, length, headers);
    init_comm();
    contp = cont;
    callback_events = events;
    callback_options = options;
    _addr.assign(addr);
    fetch_flags = TS_FETCH_FLAGS_DECHUNK;
    writeRequest(headers,length);
    mutex = new_ProxyMutex();

    //
    // We had dropped response_buffer/respone_reader to avoid unnecessary
    // memory copying. But for the original TSFetchURL() API, PluginVC may
    // stop adding data to resp_buffer when the pending data in resp_buffer
    // reach its water_mark.
    //
    // So we should set the water_mark of resp_buffer with a large value,
    // INT64_MAX would be reasonable.
    resp_buffer->water_mark = INT64_MAX;
  }

  int fetch_handler(int event, void *data);
  void process_fetch_read(int event);
  void process_fetch_write(int event);
  void httpConnect();
  void cleanUp();
  void get_info_from_buffer(IOBufferReader *reader);
  char* resp_get(int* length);

  TSMBuffer resp_hdr_bufp();
  TSMLoc resp_hdr_mloc();

  //
  // Extended APIs for FetchSM
  //
  // *flags* can be bitwise OR of several TSFetchFlags
  //
  void ext_init(Continuation *cont, TSFetchMethod method,
                const char *url, const char *version,
                const sockaddr *client_addr, int flags);
  void ext_add_header(const char *name, int name_len,
                      const char *value, int value_len);
  void ext_lanuch();
  void ext_destroy();
  ssize_t ext_read_data(char *buf, size_t len);
  void ext_write_data(const void *data, size_t len);
  void ext_set_user_data(void *data);
  void* ext_get_user_data();
  void ext_set_proto_stack(TSClientProtoStack proto_stack);
  TSClientProtoStack ext_get_proto_stack();

private:
  int InvokePlugin(int event, void*data);
  void InvokePluginExt(int error_event = 0);

  void writeRequest(const char *headers,int length)
  {
    if(length == -1)
    req_buffer->write(headers, strlen(headers));
    else
    req_buffer->write(headers,length);
  }

  int64_t getReqLen() const { return req_reader->read_avail(); }

  bool has_body();
  bool check_body_done();
  bool check_chunked();
  int dechunk_body();

  int recursion;
  TSVConn http_vc;
  VIO *read_vio;
  VIO *write_vio;
  MIOBuffer *req_buffer;
  IOBufferReader *req_reader;
  char *client_response;
  int  client_bytes;
  MIOBuffer *resp_buffer;       // response to HttpConnect Call
  IOBufferReader *resp_reader;
  Continuation *contp;
  Ptr<ProxyMutex> cont_mutex;
  HTTPParser http_parser;
  HTTPHdr client_response_hdr;
  ChunkedHandler chunked_handler;
  TSFetchEvent callback_events;
  TSFetchWakeUpOptions callback_options;
  bool req_finished;
  bool header_done;
  bool resp_finished;
  IpEndpoint _addr;
  int resp_is_chunked;
  int fetch_flags;
  void *user_data;
  bool has_sent_header;
  TSClientProtoStack proto_stack;
  TSFetchMethod req_method;
  int64_t req_content_length;
  int64_t resp_content_length;
  int64_t resp_recived_body_len;
};

#endif
