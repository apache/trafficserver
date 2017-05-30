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
#include "HttpSM.h"
#include "HttpTunnel.h"

class PluginVC;

class FetchSM : public Continuation
{
public:
  FetchSM() {}
  void
  init_comm()
  {
    cont_mutex.clear();
    req_buffer  = new_MIOBuffer(HTTP_HEADER_BUFFER_SIZE_INDEX);
    req_reader  = req_buffer->alloc_reader();
    resp_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_32K);
    resp_reader = resp_buffer->alloc_reader();
    http_parser_init(&http_parser);
    client_response_hdr.create(HTTP_TYPE_RESPONSE);
    SET_HANDLER(&FetchSM::fetch_handler);
  }

  void
  init(Continuation *cont, TSFetchWakeUpOptions options, TSFetchEvent events, const char *headers, int length, sockaddr const *addr)
  {
    Debug("FetchSM", "[%s] FetchSM initialized for request with headers\n--\n%.*s\n--", __FUNCTION__, length, headers);
    init_comm();
    contp            = cont;
    callback_events  = events;
    callback_options = options;
    _addr.assign(addr);
    fetch_flags = TS_FETCH_FLAGS_DECHUNK;
    writeRequest(headers, length);
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
  char *resp_get(int *length);

  TSMBuffer resp_hdr_bufp();
  TSMLoc resp_hdr_mloc();

  //
  // Extended APIs for FetchSM
  //
  // *flags* can be bitwise OR of several TSFetchFlags
  //
  void ext_init(Continuation *cont, const char *method, const char *url, const char *version, const sockaddr *client_addr,
                int flags);
  void ext_add_header(const char *name, int name_len, const char *value, int value_len);
  void ext_launch();
  void ext_destroy();
  ssize_t ext_read_data(char *buf, size_t len);
  void ext_write_data(const void *data, size_t len);
  void ext_set_user_data(void *data);
  void *ext_get_user_data();
  bool
  get_internal_request()
  {
    return is_internal_request;
  }
  void
  set_internal_request(bool val)
  {
    is_internal_request = val;
  }

private:
  int InvokePlugin(int event, void *data);
  void InvokePluginExt(int error_event = 0);

  void
  writeRequest(const char *headers, int length)
  {
    if (length == -1)
      req_buffer->write(headers, strlen(headers));
    else
      req_buffer->write(headers, length);
  }

  int64_t
  getReqLen() const
  {
    return req_reader->read_avail();
  }
  /// Check if the comma supproting MIME field @a name has @a value in it.
  bool check_for_field_value(const char *name, size_t name_len, char const *value, size_t value_len);

  bool has_body();
  bool check_body_done();
  bool check_chunked();
  bool check_connection_close();
  int dechunk_body();

  int recursion               = 0;
  PluginVC *http_vc           = nullptr;
  VIO *read_vio               = nullptr;
  VIO *write_vio              = nullptr;
  MIOBuffer *req_buffer       = nullptr;
  IOBufferReader *req_reader  = nullptr;
  char *client_response       = nullptr;
  int client_bytes            = -1;
  MIOBuffer *resp_buffer      = nullptr; // response to HttpConnect Call
  IOBufferReader *resp_reader = nullptr;
  Continuation *contp         = nullptr;
  Ptr<ProxyMutex> cont_mutex;
  HTTPParser http_parser;
  HTTPHdr client_response_hdr;
  ChunkedHandler chunked_handler;
  TSFetchEvent callback_events;
  TSFetchWakeUpOptions callback_options = NO_CALLBACK;
  bool req_finished                     = false;
  bool header_done                      = false;
  bool is_method_head                   = false;
  bool is_internal_request              = true;
  bool destroyed                        = false;
  IpEndpoint _addr;
  int resp_is_chunked            = -1;
  int resp_received_close        = -1;
  int fetch_flags                = 0;
  void *user_data                = nullptr;
  bool has_sent_header           = false;
  int64_t req_content_length     = 0;
  int64_t resp_content_length    = -1;
  int64_t resp_received_body_len = 0;
};

#endif
