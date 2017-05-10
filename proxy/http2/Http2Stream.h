/** @file

  Http2Stream.h

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

#ifndef __HTTP2_STREAM_H__
#define __HTTP2_STREAM_H__

#include "HTTP2.h"
#include "../ProxyClientTransaction.h"
#include "Http2DebugNames.h"
#include "../http/HttpTunnel.h" // To get ChunkedHandler
#include "Http2DependencyTree.h"

class Http2Stream;
class Http2ConnectionState;

typedef Http2DependencyTree<Http2Stream *> DependencyTree;

class Http2Stream : public ProxyClientTransaction
{
public:
  typedef ProxyClientTransaction super; ///< Parent type.
  Http2Stream(Http2StreamId sid = 0, ssize_t initial_rwnd = Http2::initial_window_size) : client_rwnd(initial_rwnd), _id(sid)
  {
    SET_HANDLER(&Http2Stream::main_event_handler);
  }

  void
  init(Http2StreamId sid, ssize_t initial_rwnd)
  {
    _id               = sid;
    _start_time       = Thread::get_hrtime();
    _thread           = this_ethread();
    this->client_rwnd = initial_rwnd;
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT, _thread);
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_TOTAL_CLIENT_STREAM_COUNT, _thread);
    sm_reader = request_reader = request_buffer.alloc_reader();
    http_parser_init(&http_parser);
    // FIXME: Are you sure? every "stream" needs request_header?
    _req_header.create(HTTP_TYPE_REQUEST);
    response_header.create(HTTP_TYPE_RESPONSE);
  }

  ~Http2Stream() { this->destroy(); }
  int main_event_handler(int event, void *edata);

  void destroy();

  bool
  is_body_done() const
  {
    return body_done;
  }

  void
  mark_body_done()
  {
    body_done = true;
  }

  void
  update_sent_count(unsigned num_bytes)
  {
    bytes_sent += num_bytes;
  }

  Http2StreamId
  get_id() const
  {
    return _id;
  }

  Http2StreamState
  get_state() const
  {
    return _state;
  }

  bool change_state(uint8_t type, uint8_t flags);

  void
  set_id(Http2StreamId sid)
  {
    _id = sid;
  }

  void
  update_initial_rwnd(Http2WindowSize new_size)
  {
    client_rwnd = new_size;
  }

  bool
  has_trailing_header() const
  {
    return trailing_header;
  }

  void
  set_request_headers(HTTPHdr &h2_headers)
  {
    _req_header.copy(&h2_headers);
  }

  // Check entire DATA payload length if content-length: header is exist
  void
  increment_data_length(uint64_t length)
  {
    data_length += length;
  }

  bool
  payload_length_is_valid() const
  {
    uint32_t content_length = _req_header.get_content_length();
    return content_length == 0 || content_length == data_length;
  }

  Http2ErrorCode decode_header_blocks(HpackHandle &hpack_handle);
  void send_request(Http2ConnectionState &cstate);
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf);
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner = false);
  void do_io_close(int lerrno = -1);
  void initiating_close();
  void do_io_shutdown(ShutdownHowTo_t) {}
  void update_read_request(int64_t read_len, bool send_update);
  bool update_write_request(IOBufferReader *buf_reader, int64_t write_len, bool send_update);
  void reenable(VIO *vio);
  virtual void transaction_done();
  void send_response_body();
  void push_promise(URL &url);

  // Stream level window size
  ssize_t client_rwnd, server_rwnd;

  LINK(Http2Stream, link);

  uint8_t *header_blocks        = nullptr;
  uint32_t header_blocks_length = 0;  // total length of header blocks (not include
                                      // Padding or other fields)
  uint32_t request_header_length = 0; // total length of payload (include Padding
                                      // and other fields)
  bool recv_end_stream = false;
  bool send_end_stream = false;

  bool sent_request_header       = false;
  bool response_header_done      = false;
  bool request_sent              = false;
  bool is_first_transaction_flag = false;

  HTTPHdr response_header;
  IOBufferReader *response_reader     = nullptr;
  IOBufferReader *request_reader      = nullptr;
  MIOBuffer request_buffer            = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  DependencyTree::Node *priority_node = nullptr;

  EThread *
  get_thread()
  {
    return _thread;
  }

  IOBufferReader *response_get_data_reader() const;
  bool
  response_is_chunked() const
  {
    return chunked;
  }

  void release(IOBufferReader *r);

  virtual bool
  allow_half_open() const
  {
    return false;
  }

  virtual void set_active_timeout(ink_hrtime timeout_in);
  virtual void set_inactivity_timeout(ink_hrtime timeout_in);
  virtual void cancel_inactivity_timeout();
  void clear_inactive_timer();
  void clear_active_timer();
  void clear_timers();
  void clear_io_events();
  bool
  is_client_state_writeable() const
  {
    return _state == Http2StreamState::HTTP2_STREAM_STATE_OPEN ||
           _state == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE ||
           _state == Http2StreamState::HTTP2_STREAM_STATE_RESERVED_LOCAL;
  }

  bool
  is_closed() const
  {
    return closed;
  }

  bool
  is_first_transaction() const
  {
    return is_first_transaction_flag;
  }

private:
  void response_initialize_data_handling(bool &is_done);
  void response_process_data(bool &is_done);
  bool response_is_data_available() const;
  Event *send_tracked_event(Event *event, int send_event, VIO *vio);
  HTTPParser http_parser;
  ink_hrtime _start_time;
  EThread *_thread = nullptr;
  Http2StreamId _id;
  Http2StreamState _state = Http2StreamState::HTTP2_STREAM_STATE_IDLE;

  MIOBuffer response_buffer;
  HTTPHdr _req_header;
  VIO read_vio;
  VIO write_vio;

  bool trailing_header = false;
  bool body_done       = false;
  bool closed          = false;
  bool sent_delete     = false;
  bool chunked         = false;

  uint64_t data_length = 0;
  uint64_t bytes_sent  = 0;

  ChunkedHandler chunked_handler;
  Event *cross_thread_event = nullptr;

  // Support stream-specific timeouts
  ink_hrtime active_timeout = 0;
  Event *active_event       = nullptr;

  ink_hrtime inactive_timeout    = 0;
  ink_hrtime inactive_timeout_at = 0;
  Event *inactive_event          = nullptr;

  Event *read_event  = nullptr;
  Event *write_event = nullptr;
};

extern ClassAllocator<Http2Stream> http2StreamAllocator;

extern bool check_continuation(Continuation *cont);
extern bool check_stream_thread(Continuation *cont);

#endif
