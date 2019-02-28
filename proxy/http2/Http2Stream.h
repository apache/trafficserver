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

#pragma once

#include "HTTP2.h"
#include "../ProxyClientTransaction.h"
#include "Http2DebugNames.h"
#include "../http/HttpTunnel.h" // To get ChunkedHandler
#include "Http2DependencyTree.h"
#include "tscore/History.h"

class Http2Stream;
class Http2ConnectionState;

typedef Http2DependencyTree::Tree<Http2Stream *> DependencyTree;

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
    sm_reader = request_reader = request_buffer.alloc_reader();
    http_parser_init(&http_parser);
    // FIXME: Are you sure? every "stream" needs request_header?
    _req_header.create(HTTP_TYPE_REQUEST);
    response_header.create(HTTP_TYPE_RESPONSE);
  }

  int main_event_handler(int event, void *edata);

  void destroy() override;

  bool
  is_body_done() const
  {
    return body_done;
  }

  void
  mark_body_done()
  {
    body_done = true;
    if (response_is_chunked()) {
      ink_assert(chunked_handler.state == ChunkedHandler::CHUNK_READ_DONE ||
                 chunked_handler.state == ChunkedHandler::CHUNK_READ_ERROR);
      this->write_vio.nbytes = response_header.length_get() + chunked_handler.dechunked_size;
    }
  }

  void
  update_sent_count(unsigned num_bytes)
  {
    bytes_sent += num_bytes;
    this->write_vio.ndone += num_bytes;
  }

  Http2StreamId
  get_id() const
  {
    return _id;
  }

  int
  get_transaction_id() const override
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

  Http2ErrorCode decode_header_blocks(HpackHandle &hpack_handle, uint32_t maximum_table_size);
  void send_request(Http2ConnectionState &cstate);
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void initiating_close();
  void terminate_if_possible();
  void do_io_shutdown(ShutdownHowTo_t) override {}
  void update_read_request(int64_t read_len, bool send_update, bool check_eos = false);
  void update_write_request(IOBufferReader *buf_reader, int64_t write_len, bool send_update);
  void signal_write_event(bool call_update);
  void reenable(VIO *vio) override;
  void transaction_done() override;

  void restart_sending();
  void push_promise(URL &url, const MIMEField *accept_encoding);

  // Stream level window size
  ssize_t client_rwnd;
  ssize_t server_rwnd = Http2::initial_window_size;

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
  IOBufferReader *response_reader          = nullptr;
  IOBufferReader *request_reader           = nullptr;
  MIOBuffer request_buffer                 = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  Http2DependencyTree::Node *priority_node = nullptr;

  IOBufferReader *response_get_data_reader() const;
  bool
  response_is_chunked() const
  {
    return chunked;
  }

  void release(IOBufferReader *r) override;

  bool
  allow_half_open() const override
  {
    return false;
  }

  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;
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
  is_first_transaction() const override
  {
    return is_first_transaction_flag;
  }

  void increment_client_transactions_stat() override;
  void decrement_client_transactions_stat() override;

private:
  void response_initialize_data_handling(bool &is_done);
  void response_process_data(bool &is_done);
  bool response_is_data_available() const;
  Event *send_tracked_event(Event *event, int send_event, VIO *vio);
  void send_response_body(bool call_update);

  /**
   * Check if this thread is the right thread to process events for this
   * continuation.
   * Return true if the caller can continue event processing.
   */
  bool _switch_thread_if_not_on_right_thread(int event, void *edata);

  HTTPParser http_parser;
  ink_hrtime _start_time = 0;
  EThread *_thread       = nullptr;
  Http2StreamId _id;
  Http2StreamState _state = Http2StreamState::HTTP2_STREAM_STATE_IDLE;

  HTTPHdr _req_header;
  VIO read_vio;
  VIO write_vio;

  History<HISTORY_DEFAULT_SIZE> _history;

  bool trailing_header = false;
  bool body_done       = false;
  bool chunked         = false;

  // A brief disucssion of similar flags and state variables:  _state, closed, terminate_stream
  //
  // _state tracks the HTTP2 state of the stream.  This field completely coincides with the H2 spec.
  //
  // closed is a flag that gets set when the framework indicates that the stream should be shutdown.  This flag
  // is set from either do_io_close, which indicates that the HttpSM is starting the close, or initiating_close,
  // which indicates that the HTTP2 infrastructure is starting the close (e.g. due to the HTTP2 session shuttig down
  // or a end of stream frame being received.  The closed flag does not indicate that it is safe to delete the stream
  // immediately. Perhaps the closed flag could be folded into the _state field.
  //
  // terminate_stream flag gets set from the transaction_done() method.  This means that the HttpSM has shutdown.  Now
  // we can delete the stream object.  To ensure that the session and transaction close hooks are executed in the correct order
  // we need to enforce that the stream is not deleted until after the state machine has shutdown.  The reentrancy_count is
  // associated with the terminate_stream flag.  We need to make sure that we don't delete the stream object while we have stream
  // methods on the stack.  The reentrancy count is incremented as we enter the stream event handler.  As we leave the event
  // handler we decrement the reentrancy count, and check to see if the teriminate_stream flag and destroy the object if that is the
  // case.
  // The same pattern is used with HttpSM for object clean up.
  //
  bool closed           = false;
  int reentrancy_count  = 0;
  bool terminate_stream = false;

  uint64_t data_length = 0;
  uint64_t bytes_sent  = 0;

  ChunkedHandler chunked_handler;
  Event *cross_thread_event      = nullptr;
  Event *buffer_full_write_event = nullptr;

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
