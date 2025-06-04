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

#include "iocore/net/NetTimeout.h"

#include "proxy/http2/HTTP2.h"
#include "proxy/ProxyTransaction.h"
#include "proxy/http2/Http2DebugNames.h"
#include "proxy/http2/Http2DependencyTree.h"
#include "tscore/History.h"
#include "proxy/Milestones.h"

class Http2Stream;
class Http2ConnectionState;

using DependencyTree = Http2DependencyTree::Tree<Http2Stream *>;

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

constexpr bool STREAM_IS_REGISTERED = true;

class Http2Stream : public ProxyTransaction
{
public:
  const int retry_delay = HRTIME_MSECONDS(10);
  using super           = ProxyTransaction; ///< Parent type.

  Http2Stream() {} // Just to satisfy ClassAllocator
  Http2Stream(ProxySession *session, Http2StreamId sid, ssize_t initial_peer_rwnd, ssize_t initial_local_rwnd,
              bool registered_stream);
  ~Http2Stream() override;

  int main_event_handler(int event, void *edata);

  void release() override;
  void reenable(VIO *vio) override;
  void reenable_write();
  void transaction_done() override;

  void
  do_io_shutdown(ShutdownHowTo_t) override
  {
  }
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;

  bool expect_send_trailer() const override;
  void set_expect_send_trailer() override;
  bool expect_receive_trailer() const override;
  void set_expect_receive_trailer() override;

  Http2ErrorCode decode_header_blocks(HpackHandle &hpack_handle, uint32_t maximum_table_size);
  void           send_headers(Http2ConnectionState &cstate);
  void           initiating_close();
  bool           is_outbound_connection() const;
  bool           is_tunneling() const;
  void           terminate_if_possible();
  void           update_read_request(bool send_update);
  void           update_write_request(bool send_update);

  void                  signal_read_event(int event);
  static constexpr auto CALL_UPDATE = true;
  void                  signal_write_event(int event, bool call_update = CALL_UPDATE);

  void restart_sending();
  bool push_promise(URL &url, const MIMEField *accept_encoding);

  // Stream level window size
  // The following peer versions are our accounting of how many bytes we can
  // send to the peer in order to respect their advertised receive window.
  ssize_t        get_peer_rwnd() const;
  Http2ErrorCode increment_peer_rwnd(size_t amount);
  Http2ErrorCode decrement_peer_rwnd(size_t amount);

  // The following local versions are the accounting of how big our receive
  // window is that we have communicated to the peer and which the peer needs
  // to respect when sending us data. We use this for calculating whether the
  // peer has exceeded the window size by sending us too many bytes and we also
  // use this to calculate WINDOW_UPDATE frame increment values to send to the
  // peer.
  ssize_t        get_local_rwnd() const;
  Http2ErrorCode increment_local_rwnd(size_t amount);
  Http2ErrorCode decrement_local_rwnd(size_t amount);

  /////////////////
  // Accessors
  void set_active_timeout(ink_hrtime timeout_in) override;
  void set_inactivity_timeout(ink_hrtime timeout_in) override;
  void cancel_active_timeout() override;
  void cancel_inactivity_timeout() override;
  bool is_active_timeout_expired(ink_hrtime now);
  bool is_inactive_timeout_expired(ink_hrtime now);

  bool is_first_transaction() const override;
  void increment_transactions_stat() override;
  void decrement_transactions_stat() override;
  void set_transaction_id(int new_id);
  int  get_transaction_id() const override;
  int  get_transaction_priority_weight() const override;
  int  get_transaction_priority_dependence() const override;
  bool is_read_closed() const override;

  HTTPHdr *
  get_send_header()
  {
    return &_send_header;
  }

  void update_read_length(int count);
  void set_read_done();

  void clear_io_events();

  bool            is_state_writeable() const;
  bool            is_closed() const;
  IOBufferReader *get_data_reader_for_send() const;
  void            set_rx_error_code(ProxyError e) override;
  void            set_tx_error_code(ProxyError e) override;

  bool        has_request_body(int64_t content_length, bool is_chunked_set) const override;
  HTTPVersion get_version(HTTPHdr &hdr) const override;

  void mark_milestone(Http2StreamMilestone type);

  void             increment_data_length(uint64_t length);
  bool             payload_length_is_valid() const;
  bool             is_write_vio_done() const;
  void             update_sent_count(unsigned num_bytes);
  Http2StreamId    get_id() const;
  Http2StreamState get_state() const;
  bool             change_state(uint8_t type, uint8_t flags);
  void             set_peer_rwnd(Http2WindowSize new_size);
  void             set_local_rwnd(Http2WindowSize new_size);
  bool             trailing_header_is_possible() const;
  void             set_trailing_header_is_possible();
  void             set_receive_headers(HTTPHdr &h2_headers);
  void             reset_receive_headers();
  void             reset_send_headers();
  MIOBuffer       *read_vio_writer() const;
  int64_t          read_vio_read_avail();
  bool             is_read_enabled() const;

  //////////////////
  // Variables
  uint8_t *header_blocks        = nullptr;
  uint32_t header_blocks_length = 0; // total length of header blocks (not include
                                     // Padding or other fields)

  bool receive_end_stream = false;
  bool send_end_stream    = false;

  bool parsing_header_done       = false;
  bool is_first_transaction_flag = false;

  HTTPHdr                    _send_header;
  IOBufferReader            *_send_reader  = nullptr;
  Http2DependencyTree::Node *priority_node = nullptr;

  Http2ConnectionState &get_connection_state();

private:
  Event *send_tracked_event(Event *event, int send_event, VIO *vio);
  void   send_body(bool call_update);
  void   _clear_timers();

  /**
   * Check if this thread is the right thread to process events for this
   * continuation.
   * Return true if the caller can continue event processing.
   */
  bool _switch_thread_if_not_on_right_thread(int event, void *edata);

  NetTimeout       _timeout{};
  HTTPParser       http_parser;
  EThread         *_thread     = nullptr;
  Http2StreamId    _id         = -1;
  Http2StreamState _state      = Http2StreamState::HTTP2_STREAM_STATE_IDLE;
  int64_t          _http_sm_id = -1;

  HTTPHdr   _receive_header;
  MIOBuffer _receive_buffer{BUFFER_SIZE_INDEX_4K};
  VIO       read_vio;
  VIO       write_vio;

  History<HISTORY_DEFAULT_SIZE>                                                           _history;
  Milestones<Http2StreamMilestone, static_cast<size_t>(Http2StreamMilestone::LAST_ENTRY)> _milestones;

  /** Any headers received while this is true are trailing headers.
   *
   * This is set to true when processing DATA frames are done. Therefore any
   * headers seen after that point are trailing headers. The qualification
   * "possible" is added because the peer may or may not send trailing headers.
   */
  bool _trailing_header_is_possible = false;
  bool _expect_send_trailer         = false;
  bool _expect_receive_trailer      = false;

  bool has_body = false;

  /** Whether this is an outbound (toward the origin) connection.
   *
   * We store this upon construction as a cached version of the session's
   * is_outbound() call. In some circumstances we need this value after a
   * session close in which is_outbound is not accessible.
   */
  bool _is_outbound = false;

  /** Whether CONNECT method is used.
   *
   * We cannot buffer outgoing data if this stream is used for tunneling (CONNECT method), because we don't know the
   * protocol used in the tunnel and we cannot expect additional data from (following read event) from the server side
   * without sending the data ATS currently has.
   */
  bool _is_tunneling = false;

  /** Whether the stream has been registered with the connection state. */
  bool _registered_stream = true;

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
  int  reentrancy_count = 0;
  bool terminate_stream = false;

  uint64_t data_length = 0;
  uint64_t bytes_sent  = 0;

  ssize_t _peer_rwnd  = 0;
  ssize_t _local_rwnd = 0;

  std::array<size_t, 5> _recent_rwnd_increment       = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int                   _recent_rwnd_increment_index = 0;

  Event *cross_thread_event = nullptr;
  Event *read_event         = nullptr;
  Event *write_event        = nullptr;
  Event *_read_vio_event    = nullptr;
  Event *_write_vio_event   = nullptr;
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
  bytes_sent            += num_bytes;
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

inline void
Http2Stream::set_transaction_id(int new_id)
{
  _id = new_id;
}

inline Http2StreamState
Http2Stream::get_state() const
{
  return _state;
}

inline void
Http2Stream::set_peer_rwnd(Http2WindowSize new_size)
{
  this->_peer_rwnd = new_size;
}

inline void
Http2Stream::set_local_rwnd(Http2WindowSize new_size)
{
  this->_local_rwnd = new_size;
}

inline bool
Http2Stream::trailing_header_is_possible() const
{
  return _trailing_header_is_possible;
}

inline void
Http2Stream::set_trailing_header_is_possible()
{
  _trailing_header_is_possible = true;
}

inline void
Http2Stream::set_receive_headers(HTTPHdr &h2_headers)
{
  _receive_header.copy(&h2_headers);
}

inline void
Http2Stream::reset_receive_headers()
{
  this->_receive_header.destroy();
  this->_receive_header.create(HTTPType::RESPONSE);
}

inline void
Http2Stream::reset_send_headers()
{
  this->_send_header.destroy();
  this->_send_header.create(HTTPType::RESPONSE);
}

// Check entire DATA payload length if content-length: header exists
inline void
Http2Stream::increment_data_length(uint64_t length)
{
  data_length += length;
}

inline bool
Http2Stream::payload_length_is_valid() const
{
  uint32_t content_length = _receive_header.get_content_length();
  if (content_length != 0 && content_length != data_length) {
    Warning("Bad payload length content_length=%d data_legnth=%d session_id=%" PRId64, content_length,
            static_cast<int>(data_length), _proxy_ssn->connection_id());
  }
  return content_length == 0 || content_length == data_length;
}

inline bool
Http2Stream::is_state_writeable() const
{
  return _state == Http2StreamState::HTTP2_STREAM_STATE_OPEN || _state == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE ||
         _state == Http2StreamState::HTTP2_STREAM_STATE_RESERVED_LOCAL ||
         (this->is_outbound_connection() && _state == Http2StreamState::HTTP2_STREAM_STATE_IDLE);
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

inline bool
Http2Stream::is_read_enabled() const
{
  return !this->read_vio.is_disabled();
}

inline void
Http2Stream::_clear_timers()
{
  _timeout.cancel_active_timeout();
  _timeout.cancel_inactive_timeout();
}

inline void
Http2Stream::update_read_length(int count)
{
  read_vio.ndone += count;
}

inline void
Http2Stream::set_read_done()
{
  read_vio.nbytes = read_vio.ndone;
}
