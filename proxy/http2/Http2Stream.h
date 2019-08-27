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
#include "../ProxyTransaction.h"
#include "Http2DebugNames.h"
#include "../http/HttpTunnel.h" // To get ChunkedHandler
#include "Http2DependencyTree.h"
#include "tscore/History.h"
#include "Milestones.h"

class Http2Stream;
class Http2ConnectionState;

typedef Http2DependencyTree::Tree<Http2Stream *> DependencyTree;

enum class Http2StreamMilestone {
  OPEN = 0,
  START_DECODE_HEADERS,
  START_TXN,
  START_ENCODE_HEADERS,
  START_TX_HEADERS_FRAMES,
  START_TX_DATA_FRAMES,
  CLOSE,
  LAST_ENTRY,
};

class Http2Stream : public ProxyTransaction
{
public:
  using super = ProxyTransaction; ///< Parent type.

  Http2Stream(Http2StreamId sid = 0, ssize_t initial_rwnd = Http2::initial_window_size);

  void init(Http2StreamId sid, ssize_t initial_rwnd);

  int main_event_handler(int event, void *edata);

  void destroy() override;
  void release(IOBufferReader *r) override;
  void reenable(VIO *vio) override;
  void transaction_done() override;

  void do_io_shutdown(ShutdownHowTo_t) override {}
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;

  Http2ErrorCode decode_header_blocks(HpackHandle &hpack_handle, uint32_t maximum_table_size);
  void send_request(Http2ConnectionState &cstate);
  void initiating_close();
  void terminate_if_possible();
  void update_read_request(int64_t read_len, bool send_update, bool check_eos = false);
  void update_write_request(IOBufferReader *buf_reader, int64_t write_len, bool send_update);
  void signal_write_event(bool call_update);
  void restart_sending();
  void push_promise(URL &url, const MIMEField *accept_encoding);

  // Stream level window size
  ssize_t client_rwnd() const;
  Http2ErrorCode increment_client_rwnd(size_t amount);
  Http2ErrorCode decrement_client_rwnd(size_t amount);
  ssize_t server_rwnd() const;
  Http2ErrorCode increment_server_rwnd(size_t amount);
  Http2ErrorCode decrement_server_rwnd(size_t amount);

  /////////////////
  // Accessors
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_inactivity_timeout() override;

  bool allow_half_open() const override;
  bool is_first_transaction() const override;
  void increment_client_transactions_stat() override;
  void decrement_client_transactions_stat() override;
  int get_transaction_id() const override;

  void clear_inactive_timer();
  void clear_active_timer();
  void clear_timers();
  void clear_io_events();

  bool is_client_state_writeable() const;
  bool is_closed() const;
  bool response_is_chunked() const;
  IOBufferReader *response_get_data_reader() const;

  void mark_milestone(Http2StreamMilestone type);

  void increment_data_length(uint64_t length);
  bool payload_length_is_valid() const;
  bool is_body_done() const;
  void mark_body_done();
  void update_sent_count(unsigned num_bytes);
  Http2StreamId get_id() const;
  Http2StreamState get_state() const;
  bool change_state(uint8_t type, uint8_t flags);
  void update_initial_rwnd(Http2WindowSize new_size);
  bool has_trailing_header() const;
  void set_request_headers(HTTPHdr &h2_headers);

  //////////////////
  // Variables
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
  EThread *_thread = nullptr;
  Http2StreamId _id;
  Http2StreamState _state = Http2StreamState::HTTP2_STREAM_STATE_IDLE;
  int64_t _http_sm_id     = -1;

  HTTPHdr _req_header;
  VIO read_vio;
  VIO write_vio;

  History<HISTORY_DEFAULT_SIZE> _history;
  Milestones<Http2StreamMilestone, static_cast<size_t>(Http2StreamMilestone::LAST_ENTRY)> _milestones;

  bool trailing_header = false;
  bool body_done       = false;
  bool chunked         = false;

  // A brief discussion of similar flags and state variables:  _state, closed, terminate_stream
  //
  // _state tracks the HTTP2 state of the stream.  This field completely coincides with the H2 spec.
  //
  // closed is a flag that gets set when the framework indicates that the stream should be shutdown.  This flag
  // is set from either do_io_close, which indicates that the HttpSM is starting the close, or initiating_close,
  // which indicates that the HTTP2 infrastructure is starting the close (e.g. due to the HTTP2 session shutting down
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

  ssize_t _client_rwnd;
  ssize_t _server_rwnd = Http2::initial_window_size;

  std::vector<size_t> _recent_rwnd_increment = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int _recent_rwnd_increment_index           = 0;

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
