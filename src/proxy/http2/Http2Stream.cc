/** @file

  Http2Stream.cc

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

#include "proxy/http2/Http2Stream.h"

#include "proxy/http2/HTTP2.h"
#include "proxy/http2/Http2ClientSession.h"
#include "proxy/http2/Http2ServerSession.h"
#include "proxy/http/HttpDebugNames.h"
#include "proxy/http/HttpSM.h"
#include "tscore/Diags.h"
#include "tscore/HTTPVersion.h"
#include "tscore/ink_assert.h"

#include <numeric>

#define REMEMBER(e, r)                                    \
  {                                                       \
    this->_history.push_back(MakeSourceLocation(), e, r); \
  }

namespace
{
DbgCtl dbg_ctl_http2_stream{"http2_stream"};

} // end anonymous namespace

#define Http2StreamDebug(fmt, ...) \
  SsnDbg(_proxy_ssn, dbg_ctl_http2_stream, "[%" PRId64 "] [%u] " fmt, _proxy_ssn->connection_id(), this->get_id(), ##__VA_ARGS__);

ClassAllocator<Http2Stream, true> http2StreamAllocator("http2StreamAllocator");

Http2Stream::Http2Stream(ProxySession *session, Http2StreamId sid, ssize_t initial_peer_rwnd, ssize_t initial_local_rwnd,
                         bool registered_stream)
  : super(session), _id(sid), _registered_stream(registered_stream), _peer_rwnd(initial_peer_rwnd), _local_rwnd(initial_local_rwnd)
{
  SET_HANDLER(&Http2Stream::main_event_handler);

  this->mark_milestone(Http2StreamMilestone::OPEN);

  this->_sm     = nullptr;
  this->_thread = this_ethread();
  this->_state  = Http2StreamState::HTTP2_STREAM_STATE_IDLE;

  auto const *proxy_session = get_proxy_ssn();
  ink_assert(proxy_session != nullptr);
  auto const *h2_session = dynamic_cast<Http2CommonSession const *>(proxy_session);
  ink_assert(h2_session != nullptr);
  this->_is_outbound = h2_session->is_outbound();

  this->_reader = this->_receive_buffer.alloc_reader();

  if (this->is_outbound_connection()) { // Flip the sense of the expected headers.  Fix naming later
    _receive_header.create(HTTPType::RESPONSE);
    _send_header.create(HTTPType::REQUEST, HTTP_2_0);
  } else {
    this->upstream_outbound_options = *(session->accept_options);
    _receive_header.create(HTTPType::REQUEST);
    _send_header.create(HTTPType::RESPONSE, HTTP_2_0);
  }

  http_parser_init(&http_parser);
}

Http2Stream::~Http2Stream()
{
  REMEMBER(NO_EVENT, this->reentrancy_count);
  Http2StreamDebug("Destroy stream, sent %" PRIu64 " bytes, registered: %s", this->bytes_sent,
                   (_registered_stream ? "true" : "false"));

  // In the case of a temporary stream used to parse the header to keep the HPACK
  // up to date, there may not be a mutex.  Nothing was set up, so nothing to
  // clean up in the destructor
  if (this->mutex == nullptr) {
    return;
  }

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  // Clean up after yourself if this was an EOS
  ink_release_assert(this->closed);
  ink_release_assert(reentrancy_count == 0);

  uint64_t cid = 0;

  if (_registered_stream) {
    // Safe to initiate SSN_CLOSE if this is the last stream
    if (_proxy_ssn) {
      cid = _proxy_ssn->connection_id();

      SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
      Http2ConnectionState &connection_state = this->get_connection_state();

      // Make sure the stream is removed from the stream list and priority tree
      // In many cases, this has been called earlier, so this call is a no-op
      connection_state.delete_stream(this);

      connection_state.decrement_peer_stream_count();

      // Update session's stream counts, so it accurately goes into keep-alive state
      connection_state.release_stream();

      // Do not access `_proxy_ssn` in below. It might be freed by `release_stream`.
    }
  } // Otherwise, not registered with the connection_state (i.e. a temporary stream used for HPACK header processing)

  // Clean up the write VIO in case of inactivity timeout
  this->do_io_write(nullptr, 0, nullptr);

  this->_milestones.mark(Http2StreamMilestone::CLOSE);

  ink_hrtime total_time = this->_milestones.elapsed(Http2StreamMilestone::OPEN, Http2StreamMilestone::CLOSE);
  Metrics::Counter::increment(http2_rsb.total_transactions_time, total_time);

  // Slow Log
  if (Http2::stream_slow_log_threshold != 0 && ink_hrtime_from_msec(Http2::stream_slow_log_threshold) < total_time) {
    Error("[%" PRIu64 "] [%" PRIu32 "] [%" PRId64 "] Slow H2 Stream: "
          "open: %" PRIu64 " "
          "dec_hdrs: %.3f "
          "txn: %.3f "
          "enc_hdrs: %.3f "
          "tx_hdrs: %.3f "
          "tx_data: %.3f "
          "close: %.3f",
          cid, static_cast<uint32_t>(this->_id), this->_http_sm_id,
          ink_hrtime_to_msec(this->_milestones[Http2StreamMilestone::OPEN]),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::START_DECODE_HEADERS),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::START_TXN),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::START_ENCODE_HEADERS),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::START_TX_HEADERS_FRAMES),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::START_TX_DATA_FRAMES),
          this->_milestones.difference_sec(Http2StreamMilestone::OPEN, Http2StreamMilestone::CLOSE));
  }

  _receive_header.destroy();
  _send_header.destroy();

  // Drop references to all buffer data
  this->_receive_buffer.clear();

  // Free the mutexes in the VIO
  read_vio.mutex.clear();
  write_vio.mutex.clear();

  ats_free(header_blocks);

  _clear_timers();
  clear_io_events();
  http_parser_clear(&http_parser);
}

int
Http2Stream::main_event_handler(int event, void *edata)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  REMEMBER(event, this->reentrancy_count);

  if (!this->_switch_thread_if_not_on_right_thread(event, edata)) {
    // Not on the right thread
    return 0;
  }
  ink_release_assert(this->_thread == this_ethread());

  Event *e = static_cast<Event *>(edata);
  reentrancy_count++;
  if (e == _read_vio_event) {
    _read_vio_event = nullptr;
    this->signal_read_event(e->callback_event);
    reentrancy_count--;
    return 0;
  } else if (e == _write_vio_event) {
    _write_vio_event = nullptr;
    this->signal_write_event(e->callback_event);
    reentrancy_count--;
    return 0;
  } else if (e == cross_thread_event) {
    cross_thread_event = nullptr;
  } else if (e == read_event) {
    read_event = nullptr;
  } else if (e == write_event) {
    write_event = nullptr;
  }

  switch (event) {
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    if (_sm == nullptr && closed != true) {
      // TIMEOUT without HttpSM - assuming incomplete header timeout
      Http2StreamDebug("timeout event=%d", event);

      ip_port_text_buffer ipb;
      const char         *remote_ip = ats_ip_ntop(this->_proxy_ssn->get_remote_addr(), ipb, sizeof(ipb));

      Error("HTTP/2 stream error timeout remote_ip=%s session_id=%" PRId64 " stream_id=%u event=%d", remote_ip,
            this->_proxy_ssn->connection_id(), this->_id, event);

      // Close stream
      do_io_close();
      terminate_stream = true;

      // Close connection because this stream doesn't read HEADERS/CONTINUATION frames anymore that makes HPACK dynamic table
      // out of sync.
      Http2ConnectionState &connection_state = get_connection_state();
      {
        SCOPED_MUTEX_LOCK(lock, connection_state.mutex, this_ethread());
        Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR,
                         "stream timeout");
        connection_state.handleEvent(HTTP2_SESSION_EVENT_ERROR, &error);
      }
    } else if (_sm && read_vio.ntodo() > 0) {
      this->signal_read_event(event);
    } else if (_sm && write_vio.ntodo() > 0) {
      this->signal_write_event(event);
    } else {
      Warning("HTTP/2 unknown case of %d event - session_id=%" PRId64 " stream_id=%u", event, this->_proxy_ssn->connection_id(),
              this->_id);
    }
    break;
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE:
    _timeout.update_inactivity();
    if (e->cookie == &write_vio) {
      if (write_vio.mutex && write_vio.cont && this->_sm) {
        this->signal_write_event(event);
      }
    } else {
      this->update_write_request(true);
    }
    break;
  case VC_EVENT_READ_COMPLETE:
    read_vio.nbytes = read_vio.ndone;
    /* fall through */
  case VC_EVENT_READ_READY:
    _timeout.update_inactivity();
    if (e->cookie == &read_vio) {
      if (read_vio.mutex && read_vio.cont && this->_sm) {
        this->signal_read_event(event);
      }
    } else {
      this->update_read_request(true);
    }
    break;
  case VC_EVENT_EOS:
    if (e->cookie == &read_vio) {
      SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
      if (read_vio.cont) {
        read_vio.cont->handleEvent(VC_EVENT_EOS, &read_vio);
      }
    } else if (e->cookie == &write_vio) {
      SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
      if (write_vio.cont) {
        write_vio.cont->handleEvent(VC_EVENT_EOS, &write_vio);
      }
    }
    break;
  }
  reentrancy_count--;
  // Clean stream up if the terminate flag is set and we are at the bottom of the handler stack
  terminate_if_possible();

  return 0;
}

Http2ErrorCode
Http2Stream::decode_header_blocks(HpackHandle &hpack_handle, uint32_t maximum_table_size)
{
  Http2ErrorCode error =
    http2_decode_header_blocks(&_receive_header, (const uint8_t *)header_blocks, header_blocks_length, nullptr, hpack_handle,
                               _trailing_header_is_possible, maximum_table_size, this->is_outbound_connection());
  if (error != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    Http2StreamDebug("Error decoding header blocks: %u", static_cast<uint32_t>(error));
  }
  return error;
}

void
Http2Stream::send_headers(Http2ConnectionState & /* cstate ATS_UNUSED */)
{
  if (closed) {
    return;
  }
  REMEMBER(NO_EVENT, this->reentrancy_count);

  // Convert header to HTTP/1.1 format. Trailing headers need no conversion
  // because they, by definition, do not contain pseudo headers.
  if (this->trailing_header_is_possible()) {
    Http2StreamDebug("trailing header: Skipping send_headers initialization.");
  } else {
    if (http2_convert_header_from_2_to_1_1(&_receive_header) == ParseResult::ERROR) {
      Http2StreamDebug("Error converting HTTP/2 headers to HTTP/1.1.");
      if (_receive_header.type_get() == HTTPType::REQUEST) {
        // There's no way to cause Bad Request directly at this time.
        // Set an invalid method so it causes an error later.
        _receive_header.method_set(std::string_view{"\xffVOID", 1});
      }
    }

    if (_receive_header.type_get() == HTTPType::REQUEST) {
      // Check whether the request uses CONNECT method
      auto method{_receive_header.method_get()};
      if (method == static_cast<std::string_view>(HTTP_METHOD_CONNECT)) {
        this->_is_tunneling = true;
      }
    }
    ink_release_assert(this->_sm != nullptr);
    this->_http_sm_id = this->_sm->sm_id;
  }

  // Write header to a buffer.  Borrowing logic from HttpSM::write_header_into_buffer.
  // Seems like a function like this ought to be in HTTPHdr directly
  int bufindex;
  int dumpoffset = 0;
  // The name dumpoffset is used here for parity with
  // HttpSM::write_header_into_buffer, but create an alias for clarity in the
  // use of this variable below this loop.
  int &num_header_bytes = dumpoffset;
  int  done, tmp;
  do {
    bufindex             = 0;
    tmp                  = dumpoffset;
    IOBufferBlock *block = this->_receive_buffer.get_current_block();
    if (!block) {
      this->_receive_buffer.add_block();
      block = this->_receive_buffer.get_current_block();
    }
    done        = _receive_header.print(block->end(), block->write_avail(), &bufindex, &tmp);
    dumpoffset += bufindex;
    this->_receive_buffer.fill(bufindex);
    if (!done) {
      this->_receive_buffer.add_block();
    }
  } while (!done);

  if (num_header_bytes == 0) {
    // No data to signal read event
    return;
  }

  // Is the _sm ready to process the header?
  if (this->read_vio.nbytes > 0) {
    if (this->receive_end_stream) {
      // These headers may be standard or trailer headers:
      //
      // * If they are standard, then there is no body (note again that the
      // END_STREAM flag was sent with them), data_length will be 0, and
      // num_header_bytes will simply be the length of the headers.
      //
      // * If they are trailers, then the tunnel behind the SM was set up after
      // the original headers were sent, and thus nbytes should not include the
      // size of the original standard headers. Rather, for trailers, nbytes
      // only needs to include the body length (i.e., DATA frame payload
      // length), and the length of these current trailer headers calculated in
      // num_header_bytes.
      this->read_vio.nbytes = this->data_length + num_header_bytes;
      Http2StreamDebug("nbytes: %" PRId64 ", ndone: %" PRId64 ", num_header_bytes: %d, data_length: %" PRId64,
                       this->read_vio.nbytes, this->read_vio.ndone, num_header_bytes, this->data_length);
      if (this->is_outbound_connection()) {
        // This is a response header.
        // We don't set ndone because the VC_EVENT_EOS will
        // first flush the remaining content to consumers,
        // after which the TUNNEL_EVENT_DONE will be fired
        // and the header handler will be set up.
        // The header handler will read the buffer, and not
        // get its content from the VIO
        // This can break if the implementation
        // changes.
        this->signal_read_event(VC_EVENT_EOS);
      } else {
        // Request headers.
        this->read_vio.ndone = this->read_vio.nbytes;
        this->signal_read_event(VC_EVENT_READ_COMPLETE);
      }
    } else {
      // End of header but not end of stream, must have some body frames coming
      this->has_body = true;
      this->signal_read_event(VC_EVENT_READ_READY);
    }
  }
}

bool
Http2Stream::change_state(uint8_t type, uint8_t flags)
{
  auto const initial_state = _state;
  switch (_state) {
  case Http2StreamState::HTTP2_STREAM_STATE_IDLE:
    if (type == HTTP2_FRAME_TYPE_HEADERS) {
      if (receive_end_stream) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else if (send_end_stream) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL;
      } else {
        _state = Http2StreamState::HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_CONTINUATION) {
      if (receive_end_stream) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else if (send_end_stream) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL;
      } else {
        _state = Http2StreamState::HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_PUSH_PROMISE) {
      _state = Http2StreamState::HTTP2_STREAM_STATE_RESERVED_LOCAL;
    } else if (type == HTTP2_FRAME_TYPE_RST_STREAM) {
      _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
    } else {
      return false;
    }
    break;

  case Http2StreamState::HTTP2_STREAM_STATE_OPEN:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM) {
      _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
    } else if (type == HTTP2_FRAME_TYPE_HEADERS || type == HTTP2_FRAME_TYPE_DATA) {
      if (receive_end_stream) {
        if (send_end_stream) {
          _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
        } else {
          _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
        }
      } else if (send_end_stream) {
        if (receive_end_stream) {
          _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
        } else {
          _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL;
        }
      } else {
        // Do not change state
      }
    } else {
      // A stream in the "open" state may be used by both peers to send frames of any type.
      return true;
    }
    break;

  case Http2StreamState::HTTP2_STREAM_STATE_RESERVED_LOCAL:
    if (type == HTTP2_FRAME_TYPE_HEADERS) {
      if (flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      }
    } else if (type == HTTP2_FRAME_TYPE_CONTINUATION) {
      if (flags & HTTP2_FLAGS_CONTINUATION_END_HEADERS) {
        _state = Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      }
    } else {
      return false;
    }
    break;

  case Http2StreamState::HTTP2_STREAM_STATE_RESERVED_REMOTE:
    // Currently ATS supports only HTTP/2 server features
    return false;

  case Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM || receive_end_stream) {
      _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
    }
    break;

  case Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM || send_end_stream) {
      _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;
    } else if (type == HTTP2_FRAME_TYPE_HEADERS) { // w/o END_STREAM flag
      // No state change here. Expect a following DATA frame with END_STREAM flag.
      return true;
    } else if (type == HTTP2_FRAME_TYPE_CONTINUATION) { // w/o END_STREAM flag
      // No state change here. Expect a following DATA frame with END_STREAM flag.
      return true;
    }
    break;

  case Http2StreamState::HTTP2_STREAM_STATE_CLOSED:
    // No state changing
    return true;

  default:
    return false;
  }

  Http2StreamDebug("%s -> %s", Http2DebugNames::get_state_name(initial_state), Http2DebugNames::get_state_name(_state));

  return true;
}

VIO *
Http2Stream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (buf) {
    read_vio.set_writer(buf);
  } else {
    read_vio.buffer.clear();
  }

  read_vio.mutex     = c ? c->mutex : this->mutex;
  read_vio.cont      = c;
  read_vio.nbytes    = nbytes;
  read_vio.ndone     = 0;
  read_vio.vc_server = this;
  read_vio.op        = VIO::READ;

  // TODO: re-enable read_vio

  return &read_vio;
}

VIO *
Http2Stream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *abuffer, bool /* owner ATS_UNUSED */)
{
  if (abuffer) {
    write_vio.set_reader(abuffer);
  } else {
    write_vio.buffer.clear();
  }
  write_vio.mutex     = c ? c->mutex : this->mutex;
  write_vio.cont      = c;
  write_vio.nbytes    = nbytes;
  write_vio.ndone     = 0;
  write_vio.vc_server = this;
  write_vio.op        = VIO::WRITE;
  _send_reader        = abuffer;

  if (c != nullptr && nbytes > 0 && this->is_state_writeable()) {
    update_write_request(false);
  } else if (!this->is_state_writeable()) {
    // Cannot start a write on a closed stream
    return nullptr;
  }
  return &write_vio;
}

// Initiated from SM
void
Http2Stream::do_io_close(int /* flags */)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  if (!closed) {
    REMEMBER(NO_EVENT, this->reentrancy_count);
    Http2StreamDebug("do_io_close");

    // Let the other end know we are going away.
    // We only need to do this for the client side since we only need to pass through RST_STREAM
    // from the server. If a client sends a RST_STREAM, we need to keep the server side alive so
    // the background fill can function as intended.
    if (!this->is_outbound_connection() && this->is_state_writeable()) {
      this->get_connection_state().send_rst_stream_frame(_id, Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
    }

    // When we get here, the SM has initiated the shutdown.  Either it received a WRITE_COMPLETE, or it is shutting down.  Any
    // remaining IO operations back to client should be abandoned.  The SM-side buffers backing these operations will be deleted
    // by the time this is called from transaction_done.
    closed = true;

    // Adjust state, so we don't process any more data
    _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;

    _clear_timers();
    clear_io_events();

    // Otherwise, Wait until transaction_done is called from HttpSM to signal that the TXN_CLOSE hook has been executed
  }
}

/*
 *  HttpSM has called TXN_close hooks.
 */
void
Http2Stream::transaction_done()
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  super::transaction_done();

  if (!closed) {
    do_io_close(); // Make sure we've been closed.  If we didn't close the _proxy_ssn session better still be open
  }
  Http2ConnectionState &state = this->get_connection_state();
  ink_release_assert(closed || !state.is_state_closed());
  _sm = nullptr;

  if (closed) {
    // Safe to initiate SSN_CLOSE if this is the last stream
    ink_assert(cross_thread_event == nullptr);
    // Schedule the destroy to occur after we unwind here.  IF we call directly, may delete with reference on the stack.
    terminate_stream = true;
    terminate_if_possible();
  }
}

void
Http2Stream::terminate_if_possible()
{
  // if (terminate_stream && reentrancy_count == 0) {
  if (reentrancy_count == 0 && closed && terminate_stream) {
    REMEMBER(NO_EVENT, this->reentrancy_count);

    SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
    THREAD_FREE(this, http2StreamAllocator, this_ethread());
  }
}

// Initiated from the Http2 side
void
Http2Stream::initiating_close()
{
  if (!closed) {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    REMEMBER(NO_EVENT, this->reentrancy_count);
    Http2StreamDebug("initiating_close client_window=%zd session_window=%zd", _peer_rwnd,
                     this->get_connection_state().get_peer_rwnd());

    if (!this->is_outbound_connection() && this->is_state_writeable()) { // Let the other end know we are going away
      this->get_connection_state().send_rst_stream_frame(_id, Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
    }

    // Set the state of the connection to closed
    // TODO - these states should be combined
    closed = true;
    _state = Http2StreamState::HTTP2_STREAM_STATE_CLOSED;

    // leaving the reference to the SM, so we can detach from the SM when we actually destroy
    // _sm = NULL;
    // Leaving reference to client session as well, so we can signal once the
    // TXN_CLOSE has been sent
    // _proxy_ssn = NULL;

    _clear_timers();
    clear_io_events();

    // This should result in do_io_close or release being called.  That will schedule the final
    // kill yourself signal
    // We are sending signals rather than calling the handlers directly to avoid the case where
    // the HttpTunnel handler causes the HttpSM to be deleted on the stack.
    bool sent_write_complete = false;
    if (_sm) {
      // Push out any last IO events
      // First look for active write or read
      if (write_vio.cont && write_vio.nbytes > 0 && write_vio.ndone == write_vio.nbytes &&
          (!is_outbound_connection() || get_state() == Http2StreamState::HTTP2_STREAM_STATE_OPEN)) {
        SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
        Http2StreamDebug("Send tracked event VC_EVENT_WRITE_COMPLETE on write_vio. sm_id: %" PRId64, _sm->sm_id);
        write_event         = send_tracked_event(write_event, VC_EVENT_WRITE_COMPLETE, &write_vio);
        sent_write_complete = true;
      }

      if (!sent_write_complete) {
        if (write_vio.cont && write_vio.get_writer() &&
            (!is_outbound_connection() || get_state() == Http2StreamState::HTTP2_STREAM_STATE_OPEN ||
             get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL)) {
          SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());
          Http2StreamDebug("Send tracked event VC_EVENT_EOS on write_vio. sm_id: %" PRId64, _sm->sm_id);
          write_event = send_tracked_event(write_event, VC_EVENT_EOS, &write_vio);
        } else if (read_vio.cont && read_vio.get_writer()) {
          SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
          Http2StreamDebug("Send tracked event VC_EVENT_EOS on read_vio. sm_id: %" PRId64, _sm->sm_id);
          read_event = send_tracked_event(read_event, VC_EVENT_EOS, &read_vio);
        } else {
          Http2StreamDebug("send EOS to SM");
          // Just send EOS to the _sm
          _sm->handleEvent(VC_EVENT_EOS, nullptr);
        }
      }
    } else {
      Http2StreamDebug("No SM to signal");
      // Transaction is already gone or not started. Kill yourself
      terminate_stream = true;
      terminate_if_possible();
    }
  }
}

bool
Http2Stream::is_outbound_connection() const
{
  return _is_outbound;
}

bool
Http2Stream::is_tunneling() const
{
  return _is_tunneling;
}

/* Replace existing event only if the new event is different than the inprogress event */
Event *
Http2Stream::send_tracked_event(Event *event, int send_event, VIO *vio)
{
  if (event != nullptr) {
    if (event->callback_event != send_event) {
      event->cancel();
      event = nullptr;
    }
  }

  if (event == nullptr) {
    REMEMBER(send_event, this->reentrancy_count);
    event = this_ethread()->schedule_imm(this, send_event, vio);
  }

  return event;
}

void
Http2Stream::update_read_request(bool call_update)
{
  if (closed || _proxy_ssn == nullptr || _sm == nullptr || read_vio.mutex == nullptr) {
    return;
  }

  if (!this->_switch_thread_if_not_on_right_thread(VC_EVENT_READ_READY, nullptr)) {
    // Not on the right thread
    return;
  }
  ink_release_assert(this->_thread == this_ethread());

  SCOPED_MUTEX_LOCK(lock, read_vio.mutex, this_ethread());
  if (read_vio.nbytes == 0 || read_vio.is_disabled()) {
    return;
  }

  // Try to be smart and only signal if there was additional data
  int send_event = VC_EVENT_READ_READY;
  if (read_vio.ntodo() == 0 || (this->receive_end_stream && this->read_vio.nbytes != INT64_MAX)) {
    send_event = VC_EVENT_READ_COMPLETE;
  }

  int64_t read_avail = this->read_vio.get_writer()->max_read_avail();
  if (read_avail > 0 || send_event == VC_EVENT_READ_COMPLETE) {
    if (call_update) { // Safe to call vio handler directly
      _timeout.update_inactivity();
      if (read_vio.cont && this->_sm) {
        read_vio.cont->handleEvent(send_event, &read_vio);
      }
    } else { // Called from do_io_read.  Still setting things up.  Send event
      // to handle this after the dust settles
      read_event = send_tracked_event(read_event, send_event, &read_vio);
    }
  }
}

void
Http2Stream::restart_sending()
{
  // Make sure the stream is in a good state to be sending
  if (this->is_closed()) {
    return;
  }
  if (!this->parsing_header_done) {
    this->update_write_request(true);
    return;
  }
  if (this->is_outbound_connection()) {
    if (this->get_state() != Http2StreamState::HTTP2_STREAM_STATE_OPEN || write_vio.ntodo() == 0) {
      return;
    }
  } else {
    if (this->get_state() != Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE) {
      return;
    }
  }

  IOBufferReader *reader = this->get_data_reader_for_send();
  if (reader && !reader->is_read_avail_more_than(0)) {
    return;
  }

  if (this->write_vio.mutex && this->write_vio.ntodo() == 0) {
    return;
  }

  this->send_body(true);
}

void
Http2Stream::update_write_request(bool call_update)
{
  if (!this->is_state_writeable() || closed || _proxy_ssn == nullptr || write_vio.mutex == nullptr ||
      write_vio.get_reader() == nullptr || this->_send_reader == nullptr) {
    return;
  }

  if (!this->_switch_thread_if_not_on_right_thread(VC_EVENT_WRITE_READY, nullptr)) {
    // Not on the right thread
    return;
  }
  ink_release_assert(this->_thread == this_ethread());

  Http2StreamDebug("update_write_request parse_done=%d", parsing_header_done);

  Http2ConnectionState &connection_state = this->get_connection_state();

  SCOPED_MUTEX_LOCK(lock, write_vio.mutex, this_ethread());

  IOBufferReader *vio_reader = write_vio.get_reader();

  if (write_vio.ntodo() > 0 && (!vio_reader->is_read_avail_more_than(0))) {
    Http2StreamDebug("update_write_request give up without doing anything ntodo=%" PRId64 " is_read_avail=%d client_window=%zd"
                     " session_window=%zd",
                     write_vio.ntodo(), vio_reader->is_read_avail_more_than(0), _peer_rwnd,
                     this->get_connection_state().get_peer_rwnd());
    return;
  }

  // Process the new data
  if (!this->parsing_header_done) {
    // Still parsing the request or response header
    int         bytes_used = 0;
    ParseResult state;
    if (this->is_outbound_connection()) {
      state = this->_send_header.parse_req(&http_parser, this->_send_reader, &bytes_used, false);
    } else {
      state = this->_send_header.parse_resp(&http_parser, this->_send_reader, &bytes_used, false);
    }
    // HTTPHdr::parse_resp() consumed the send_reader in above
    write_vio.ndone += bytes_used;

    switch (state) {
    case ParseResult::DONE: {
      this->parsing_header_done = true;
      Http2StreamDebug("update_write_request parsing done, read %d bytes", bytes_used);

      // Schedule session shutdown if response header has "Connection: close"
      MIMEField *field = this->_send_header.field_find(static_cast<std::string_view>(MIME_FIELD_CONNECTION));
      if (field) {
        auto value{field->value_get()};
        if (value == static_cast<std::string_view>(HTTP_VALUE_CLOSE)) {
          SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
          if (connection_state.get_shutdown_state() == HTTP2_SHUTDOWN_NONE) {
            connection_state.set_shutdown_state(HTTP2_SHUTDOWN_NOT_INITIATED, Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
          }
        }
      }

      {
        SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
        // Send the response header back
        connection_state.send_headers_frame(this);
      }

      // Roll back states of response header to read final response
      if (!this->is_outbound_connection() && this->_send_header.expect_final_response()) {
        this->parsing_header_done = false;
      }
      if (this->is_outbound_connection() || this->_send_header.expect_final_response()) {
        _send_header.destroy();
        _send_header.create(this->is_outbound_connection() ? HTTPType::REQUEST : HTTPType::RESPONSE, HTTP_2_0);
        http_parser_clear(&http_parser);
        http_parser_init(&http_parser);
      }
      bool final_write = this->write_vio.ntodo() == 0;
      if (final_write) {
        this->signal_write_event(VC_EVENT_WRITE_COMPLETE, !CALL_UPDATE);
      }

      if (!final_write && this->_send_reader->is_read_avail_more_than(0)) {
        Http2StreamDebug("update_write_request done parsing, still more to send");
        this->_milestones.mark(Http2StreamMilestone::START_TX_DATA_FRAMES);
        this->send_body(call_update);
      }
      break;
    }
    case ParseResult::CONT:
      // Let it ride for next time
      Http2StreamDebug("update_write_request still parsing, read %d bytes", bytes_used);
      break;
    default:
      Http2StreamDebug("update_write_request  state %d, read %d bytes", static_cast<int>(state), bytes_used);
      break;
    }
  } else {
    this->_milestones.mark(Http2StreamMilestone::START_TX_DATA_FRAMES);
    this->send_body(call_update);
  }

  return;
}

void
Http2Stream::signal_read_event(int event)
{
  if (this->read_vio.cont == nullptr || this->read_vio.cont->mutex == nullptr || this->read_vio.op == VIO::NONE ||
      this->terminate_stream) {
    return;
  }

  reentrancy_count++;
  MUTEX_TRY_LOCK(lock, read_vio.cont->mutex, this_ethread());
  if (lock.is_locked()) {
    if (read_event) {
      read_event->cancel();
      read_event = nullptr;
    }
    _timeout.update_inactivity();
    this->read_vio.cont->handleEvent(event, &this->read_vio);
  } else {
    if (this->_read_vio_event) {
      this->_read_vio_event->cancel();
    }
    this->_read_vio_event = this_ethread()->schedule_in(this, retry_delay, event, &read_vio);
  }
  reentrancy_count--;
  // Clean stream up if the terminate flag is set and we are at the bottom of the handler stack
  terminate_if_possible();
}

void
Http2Stream::signal_write_event(int event, bool call_update)
{
  // Don't signal a write event if in fact nothing was written
  if (this->write_vio.cont == nullptr || this->write_vio.cont->mutex == nullptr || this->write_vio.op == VIO::NONE ||
      this->terminate_stream) {
    return;
  }

  reentrancy_count++;
  if (call_update) {
    MUTEX_TRY_LOCK(lock, write_vio.cont->mutex, this_ethread());
    if (lock.is_locked()) {
      if (write_event) {
        write_event->cancel();
        write_event = nullptr;
      }
      _timeout.update_inactivity();
      this->write_vio.cont->handleEvent(event, &this->write_vio);
    } else {
      if (this->_write_vio_event) {
        this->_write_vio_event->cancel();
      }
      this->_write_vio_event = this_ethread()->schedule_in(this, retry_delay, event, &write_vio);
    }
  } else {
    // Called from do_io_write. Might still be setting up state. Send an event to let the dust settle
    write_event = send_tracked_event(write_event, event, &write_vio);
  }
  reentrancy_count--;
  // Clean stream up if the terminate flag is set and we are at the bottom of the handler stack
  terminate_if_possible();
}

bool
Http2Stream::push_promise(URL &url, const MIMEField *accept_encoding)
{
  SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
  return this->get_connection_state().send_push_promise_frame(this, url, accept_encoding);
}

void
Http2Stream::send_body(bool /* call_update ATS_UNUSED */)
{
  Http2ConnectionState &connection_state = this->get_connection_state();
  _timeout.update_inactivity();

  reentrancy_count++;

  SCOPED_MUTEX_LOCK(lock, _proxy_ssn->mutex, this_ethread());
  if (Http2::stream_priority_enabled) {
    connection_state.schedule_stream_to_send_priority_frames(this);
    // signal_write_event() will be called from `Http2ConnectionState::send_data_frames_depends_on_priority()`
    // when write_vio is consumed
  } else {
    connection_state.send_data_frames(this);
    // XXX The call to signal_write_event can destroy/free the Http2Stream.
    // Don't modify the Http2Stream after calling this method.
  }

  reentrancy_count--;
  terminate_if_possible();
}

void
Http2Stream::reenable_write()
{
  if (this->_proxy_ssn) {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    update_write_request(true);
  }
}

void
Http2Stream::reenable(VIO *vio)
{
  if (this->_proxy_ssn) {
    if (vio->op == VIO::WRITE) {
      SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
      update_write_request(true);
    } else if (vio->op == VIO::READ) {
      SCOPED_MUTEX_LOCK(ssn_lock, _proxy_ssn->mutex, this_ethread());
      Http2ConnectionState &connection_state = this->get_connection_state();
      connection_state.restart_receiving(this);
    }
  }
}

IOBufferReader *
Http2Stream::get_data_reader_for_send() const
{
  return this->_send_reader;
}

void
Http2Stream::set_active_timeout(ink_hrtime timeout_in)
{
  _timeout.set_active_timeout(timeout_in);
}

void
Http2Stream::set_inactivity_timeout(ink_hrtime timeout_in)
{
  _timeout.set_inactive_timeout(timeout_in);
}

void
Http2Stream::cancel_active_timeout()
{
  _timeout.cancel_active_timeout();
}

void
Http2Stream::cancel_inactivity_timeout()
{
  _timeout.cancel_inactive_timeout();
}

bool
Http2Stream::is_active_timeout_expired(ink_hrtime now)
{
  return _timeout.is_active_timeout_expired(now);
}

bool
Http2Stream::is_inactive_timeout_expired(ink_hrtime now)
{
  return _timeout.is_inactive_timeout_expired(now);
}

void
Http2Stream::clear_io_events()
{
  if (cross_thread_event) {
    cross_thread_event->cancel();
    cross_thread_event = nullptr;
  }

  if (read_event) {
    read_event->cancel();
    read_event = nullptr;
  }

  if (write_event) {
    write_event->cancel();
    write_event = nullptr;
  }

  if (this->_read_vio_event) {
    this->_read_vio_event->cancel();
    this->_read_vio_event = nullptr;
  }

  if (this->_write_vio_event) {
    this->_write_vio_event->cancel();
    this->_write_vio_event = nullptr;
  }
}

//  release and do_io_close are the same for the HTTP/2 protocol
void
Http2Stream::release()
{
  this->do_io_close();
}

void
Http2Stream::increment_transactions_stat()
{
  if (this->is_outbound_connection()) {
    Metrics::Gauge::increment(http2_rsb.current_server_stream_count);
    Metrics::Counter::increment(http2_rsb.total_server_stream_count);
  } else {
    Metrics::Gauge::increment(http2_rsb.current_client_stream_count);
    Metrics::Counter::increment(http2_rsb.total_client_stream_count);
  }
}

void
Http2Stream::decrement_transactions_stat()
{
  if (this->is_outbound_connection()) {
    Metrics::Gauge::decrement(http2_rsb.current_server_stream_count);
  } else {
    Metrics::Gauge::decrement(http2_rsb.current_client_stream_count);
  }
}

ssize_t
Http2Stream::get_peer_rwnd() const
{
  return this->_peer_rwnd;
}

Http2ErrorCode
Http2Stream::increment_peer_rwnd(size_t amount)
{
  this->_peer_rwnd += amount;

  this->_recent_rwnd_increment[this->_recent_rwnd_increment_index] = amount;
  ++this->_recent_rwnd_increment_index;
  this->_recent_rwnd_increment_index %= this->_recent_rwnd_increment.size();
  double sum = std::accumulate(this->_recent_rwnd_increment.begin(), this->_recent_rwnd_increment.end(), 0.0);
  double avg = sum / this->_recent_rwnd_increment.size();
  if (avg < Http2::min_avg_window_update) {
    return Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
  }
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2Stream::decrement_peer_rwnd(size_t amount)
{
  this->_peer_rwnd -= amount;
  if (this->_peer_rwnd < 0) {
    return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
  } else {
    return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
  }
}

ssize_t
Http2Stream::get_local_rwnd() const
{
  return this->_local_rwnd;
}

Http2ErrorCode
Http2Stream::increment_local_rwnd(size_t amount)
{
  this->_local_rwnd += amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2Stream::decrement_local_rwnd(size_t amount)
{
  this->_local_rwnd -= amount;
  if (this->_local_rwnd < 0) {
    return Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
  } else {
    return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
  }
}

bool
Http2Stream::_switch_thread_if_not_on_right_thread(int event, void *edata)
{
  if (this->_thread != this_ethread()) {
    SCOPED_MUTEX_LOCK(stream_lock, this->mutex, this_ethread());
    if (cross_thread_event == nullptr) {
      // Send to the right thread
      cross_thread_event = this->_thread->schedule_imm(this, event, edata);
    }
    return false;
  }
  return true;
}

int
Http2Stream::get_transaction_priority_weight() const
{
  return priority_node ? priority_node->weight : 0;
}

int
Http2Stream::get_transaction_priority_dependence() const
{
  if (!priority_node) {
    return -1;
  } else {
    return priority_node->parent ? priority_node->parent->id : 0;
  }
}

int64_t
Http2Stream::read_vio_read_avail()
{
  MIOBuffer *writer = this->read_vio.get_writer();
  if (writer) {
    return writer->max_read_avail();
  }

  return 0;
}

bool
Http2Stream::has_request_body(int64_t /* content_length ATS_UNUSED */, bool /* is_chunked_set ATS_UNUSED */) const
{
  return has_body;
}

Http2ConnectionState &
Http2Stream::get_connection_state()
{
  if (this->is_outbound_connection()) {
    Http2ServerSession *session = static_cast<Http2ServerSession *>(_proxy_ssn);
    return session->connection_state;
  } else {
    Http2ClientSession *session = static_cast<Http2ClientSession *>(_proxy_ssn);
    return session->connection_state;
  }
}

bool
Http2Stream::is_read_closed() const
{
  return this->receive_end_stream;
}

bool
Http2Stream::expect_send_trailer() const
{
  return this->_expect_send_trailer;
}

void
Http2Stream::set_expect_send_trailer()
{
  _expect_send_trailer = true;
  parsing_header_done  = false;
  reset_send_headers();
}
bool
Http2Stream::expect_receive_trailer() const
{
  return this->_expect_receive_trailer;
}

void
Http2Stream::set_expect_receive_trailer()
{
  _expect_receive_trailer = true;
}

void
Http2Stream::set_rx_error_code(ProxyError e)
{
  if (!this->is_outbound_connection() && this->_sm) {
    this->_sm->t_state.client_info.rx_error_code = e;
  }
}

void
Http2Stream::set_tx_error_code(ProxyError e)
{
  if (!this->is_outbound_connection() && this->_sm) {
    if (!(this->_sm->t_state.client_info.tx_error_code.cls == ProxyErrorClass::SSN && e.cls == ProxyErrorClass::TXN)) {
      // if there is not an error already set for the session set the transaction level error
      this->_sm->t_state.client_info.tx_error_code = e;
    }
  }
}

HTTPVersion
Http2Stream::get_version(HTTPHdr & /* hdr ATS_UNUSED */) const
{
  return HTTP_2_0;
}
