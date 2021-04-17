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

#include "NetTimeout.h"

#include "HTTP2.h"
#include "ProxyTransaction.h"
#include "Http2DebugNames.h"
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
  const int retry_delay = HRTIME_MSECONDS(10);
  using super           = ProxyTransaction; ///< Parent type.

  Http2Stream() {} // Just to satisfy ClassAllocator
  Http2Stream(ProxySession *session, Http2StreamId sid, ssize_t initial_rwnd);
  ~Http2Stream();

  int main_event_handler(int event, void *edata);

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
  void update_read_request(bool send_update);
  void update_write_request(bool send_update);

  void signal_read_event(int event);
  void signal_write_event(int event);
  void signal_write_event(bool call_update);

  void restart_sending();
  bool push_promise(URL &url, const MIMEField *accept_encoding);

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
  void cancel_active_timeout() override;
  void cancel_inactivity_timeout() override;
  bool is_active_timeout_expired(ink_hrtime now);
  bool is_inactive_timeout_expired(ink_hrtime now);

  bool is_first_transaction() const override;
  void increment_client_transactions_stat() override;
  void decrement_client_transactions_stat() override;
  int get_transaction_id() const override;
  int get_transaction_priority_weight() const override;
  int get_transaction_priority_dependence() const override;

  void clear_io_events();

  bool is_client_state_writeable() const;
  bool is_closed() const;
  IOBufferReader *response_get_data_reader() const;

  bool has_request_body(int64_t content_length, bool is_chunked_set) const override;

  void mark_milestone(Http2StreamMilestone type);

  void increment_data_length(uint64_t length);
  bool payload_length_is_valid() const;
  bool is_write_vio_done() const;
  void update_sent_count(unsigned num_bytes);
  Http2StreamId get_id() const;
  Http2StreamState get_state() const;
  bool change_state(uint8_t type, uint8_t flags);
  void update_initial_rwnd(Http2WindowSize new_size);
  bool has_trailing_header() const;
  void set_request_headers(HTTPHdr &h2_headers);
  MIOBuffer *read_vio_writer() const;
  int64_t read_vio_read_avail();

  //////////////////
  // Variables
  uint8_t *header_blocks        = nullptr;
  uint32_t header_blocks_length = 0; // total length of header blocks (not include Padding or other fields)

  bool recv_end_stream = false;
  bool send_end_stream = false;

  bool sent_request_header       = false;
  bool response_header_done      = false;
  bool request_sent              = false;
  bool is_first_transaction_flag = false;

  HTTPHdr response_header;
  Http2DependencyTree::Node *priority_node = nullptr;

private:
  bool response_is_data_available() const;
  Event *send_tracked_event(Event *event, int send_event, VIO *vio);
  void send_response_body(bool call_update);
  void _clear_timers();

  /**
   * Check if this thread is the right thread to process events for this
   * continuation.
   * Return true if the caller can continue event processing.
   */
  bool _switch_thread_if_not_on_right_thread(int event, void *edata);

  NetTimeout _timeout{};
  HTTPParser http_parser;
  EThread *_thread = nullptr;
  Http2StreamId _id;
  Http2StreamState _state = Http2StreamState::HTTP2_STREAM_STATE_IDLE;
  int64_t _http_sm_id     = -1;

  HTTPHdr _req_header;
  MIOBuffer _request_buffer = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;
  int64_t read_vio_nbytes;
  VIO read_vio;
  VIO write_vio;

  History<HISTORY_DEFAULT_SIZE> _history;
  Milestones<Http2StreamMilestone, static_cast<size_t>(Http2StreamMilestone::LAST_ENTRY)> _milestones;

  bool trailing_header = false;
  bool has_body        = false;

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

  ssize_t _client_rwnd = 0;
  ssize_t _server_rwnd = 0;

  std::vector<size_t> _recent_rwnd_increment = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int _recent_rwnd_increment_index           = 0;

  Event *cross_thread_event      = nullptr;
  Event *buffer_full_write_event = nullptr;

  Event *read_event       = nullptr;
  Event *write_event      = nullptr;
  Event *_read_vio_event  = nullptr;
  Event *_write_vio_event = nullptr;
};

extern ClassAllocator<Http2Stream, true> http2StreamAllocator;

////////////////////////////////////////////////////
// INLINE

inline void
Http2Stream::mark_milestone(Http2StreamMilestone type)
{
  this->_milestones.mark(type);
}

inline bool
Http2Stream::is_write_vio_done() const
{
  return this->write_vio.ntodo() == 0;
}

inline void
Http2Stream::update_sent_count(unsigned num_bytes)
{
  bytes_sent += num_bytes;
  this->write_vio.ndone += num_bytes;
}

inline Http2StreamId
Http2Stream::get_id() const
{
  return _id;
}

inline int
Http2Stream::get_transaction_id() const
{
  return _id;
}

inline Http2StreamState
Http2Stream::get_state() const
{
  return _state;
}

inline void
Http2Stream::update_initial_rwnd(Http2WindowSize new_size)
{
  this->_client_rwnd = new_size;
}

inline bool
Http2Stream::has_trailing_header() const
{
  return trailing_header;
}

inline void
Http2Stream::set_request_headers(HTTPHdr &h2_headers)
{
  _req_header.copy(&h2_headers);
}

// Check entire DATA payload length if content-length: header is exist
inline void
Http2Stream::increment_data_length(uint64_t length)
{
  data_length += length;
}

inline bool
Http2Stream::payload_length_is_valid() const
{
  uint32_t content_length = _req_header.get_content_length();
  return content_length == 0 || content_length == data_length;
}

inline bool
Http2Stream::is_client_state_writeable() const
{
  return _state == Http2StreamState::HTTP2_STREAM_STATE_OPEN || _state == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE ||
         _state == Http2StreamState::HTTP2_STREAM_STATE_RESERVED_LOCAL;
}

inline bool
Http2Stream::is_closed() const
{
  return closed;
}

inline bool
Http2Stream::is_first_transaction() const
{
  return is_first_transaction_flag;
}

inline MIOBuffer *
Http2Stream::read_vio_writer() const
{
  return this->read_vio.get_writer();
}

inline void
Http2Stream::_clear_timers()
{
  _timeout.cancel_active_timeout();
  _timeout.cancel_inactive_timeout();
}
