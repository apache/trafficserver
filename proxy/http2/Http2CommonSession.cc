/** @file

  Http2CommonSession.cc

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

#include "Http2CommonSession.h"
#include "HttpDebugNames.h"

#define REMEMBER(e, r)                          \
  {                                             \
    this->remember(MakeSourceLocation(), e, r); \
  }

#define STATE_ENTER(state_name, event)                                                                                          \
  do {                                                                                                                          \
    REMEMBER(event, this->recursion)                                                                                            \
    Debug("http2_cs", "[%" PRId64 "] [%s, %s]", this->get_connection_id(), #state_name, HttpDebugNames::get_event_name(event)); \
  } while (0)

#define Http2SsnDebug(fmt, ...) Debug("http2_cs", "[%" PRId64 "] " fmt, this->get_connection_id(), ##__VA_ARGS__)

#define HTTP2_SET_SESSION_HANDLER(handler) \
  do {                                     \
    REMEMBER(NO_EVENT, this->recursion);   \
    this->session_handler = (handler);     \
  } while (0)

// memcpy the requested bytes from the IOBufferReader, returning how many were
// actually copied.
static inline unsigned
copy_from_buffer_reader(void *dst, IOBufferReader *reader, unsigned nbytes)
{
  char *end;

  end = reader->memcpy(dst, nbytes, 0 /* offset */);
  return end - static_cast<char *>(dst);
}

void
Http2CommonSession::remember(const SourceLocation &location, int event, int reentrant)
{
  this->_history.push_back(location, event, reentrant);
}

bool
Http2CommonSession::common_free(ProxySession *ssn)
{
  if (this->_reenable_event) {
    this->_reenable_event->cancel();
    this->_reenable_event = nullptr;
  }

  // Make sure the we are at the bottom of the stack
  if (this->connection_state.is_recursing() || this->recursion != 0) {
    // Note that we are ready to be cleaned up
    // One of the event handlers will catch it
    this->kill_me = true;
    return false;
  }

  REMEMBER(NO_EVENT, this->recursion)
  Http2SsnDebug("session free");

  // Don't free active ProxySession
  ink_release_assert(ssn->is_active() == false);

  this->_milestones.mark(Http2SsnMilestone::CLOSE);
  ink_hrtime total_time = this->_milestones.elapsed(Http2SsnMilestone::OPEN, Http2SsnMilestone::CLOSE);

  // Slow Log
  if (Http2::con_slow_log_threshold != 0 && ink_hrtime_from_msec(Http2::con_slow_log_threshold) < total_time) {
    Error("[%" PRIu64 "] Slow H2 Connection: open: %" PRIu64 " close: %.3f", ssn->connection_id(),
          ink_hrtime_to_msec(this->_milestones[Http2SsnMilestone::OPEN]),
          this->_milestones.difference_sec(Http2SsnMilestone::OPEN, Http2SsnMilestone::CLOSE));
  }
  // Update stats on how we died.  May want to eliminate this.  Was useful for
  // tracking down which cases we were having problems cleaning up.  But for general
  // use probably not worth the effort
  if (cause_of_death != Http2SessionCod::NOT_PROVIDED) {
    switch (cause_of_death) {
    case Http2SessionCod::HIGH_ERROR_RATE:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_HIGH_ERROR_RATE, this_ethread());
      break;
    case Http2SessionCod::NOT_PROVIDED:
      // Can't happen but this case is here to not have default case.
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_OTHER, this_ethread());
      break;
    }
  } else {
    switch (dying_event) {
    case VC_EVENT_NONE:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_DEFAULT, this_ethread());
      break;
    case VC_EVENT_ACTIVE_TIMEOUT:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_ACTIVE, this_ethread());
      break;
    case VC_EVENT_INACTIVITY_TIMEOUT:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_INACTIVE, this_ethread());
      break;
    case VC_EVENT_ERROR:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_ERROR, this_ethread());
      break;
    case VC_EVENT_EOS:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_EOS, this_ethread());
      break;
    default:
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_SESSION_DIE_OTHER, this_ethread());
      break;
    }
  }

  delete _h2_pushed_urls;
  _h2_pushed_urls = nullptr;
  this->connection_state.destroy();

  free_MIOBuffer(this->read_buffer);
  this->read_buffer = nullptr;
  free_MIOBuffer(this->write_buffer);
  this->write_buffer = nullptr;
  return true;
}

void
Http2CommonSession::set_half_close_local_flag(bool flag)
{
  if (!half_close_local && flag) {
    Http2SsnDebug("session half-close local");
  }
  half_close_local = flag;
}

int64_t
Http2CommonSession::xmit(const Http2TxFrame &frame, bool flush)
{
  int64_t len = frame.write_to(this->write_buffer);
  this->_pending_sending_data_size += len;
  // Force flush for some cases
  if (!flush) {
    // Flush if we already use half of the buffer to avoid adding a new block to the chain.
    // A frame size can be 16MB at maximum so blocks can be added, but that's fine.
    if (this->_pending_sending_data_size >= this->_write_size_threshold) {
      flush = true;
    }
  }

  if (flush) {
    this->flush();
  }

  return len;
}

void
Http2CommonSession::flush()
{
  if (this->_pending_sending_data_size > 0) {
    this->_pending_sending_data_size = 0;
    this->_write_buffer_last_flush   = Thread::get_hrtime();
    write_reenable();
  }
}

int
Http2CommonSession::state_read_connection_preface(int event, void *edata)
{
  VIO *vio = static_cast<VIO *>(edata);

  STATE_ENTER(&Http2CommonSession::state_read_connection_preface, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);

  if (this->_read_buffer_reader->read_avail() >= static_cast<int64_t>(HTTP2_CONNECTION_PREFACE_LEN)) {
    char buf[HTTP2_CONNECTION_PREFACE_LEN];
    unsigned nbytes;

    nbytes = copy_from_buffer_reader(buf, this->_read_buffer_reader, sizeof(buf));
    ink_release_assert(nbytes == HTTP2_CONNECTION_PREFACE_LEN);

    if (memcmp(HTTP2_CONNECTION_PREFACE, buf, nbytes) != 0) {
      Http2SsnDebug("invalid connection preface");
      this->get_proxy_session()->do_io_close();
      return 0;
    }

    // Check whether data is read from early data
    if (this->read_from_early_data > 0) {
      this->read_from_early_data -= this->read_from_early_data > nbytes ? nbytes : this->read_from_early_data;
    }

    Http2SsnDebug("received connection preface");
    this->_read_buffer_reader->consume(nbytes);
    HTTP2_SET_SESSION_HANDLER(&Http2CommonSession::state_start_frame_read);

    this->get_netvc()->set_inactivity_timeout(HRTIME_SECONDS(Http2::no_activity_timeout_in));
    this->get_netvc()->set_active_timeout(HRTIME_SECONDS(Http2::active_timeout_in));

    // XXX start the write VIO ...

    // If we have unconsumed data, start tranferring frames now.
    if (this->_read_buffer_reader->is_read_avail_more_than(0)) {
      return this->get_proxy_session()->handleEvent(VC_EVENT_READ_READY, vio);
    }
  }

  // XXX We don't have enough data to check the connection preface. We should
  // reset the accept inactivity
  // timeout. We should have a maximum timeout to get the session started
  // though.

  vio->reenable();
  return 0;
}

int
Http2CommonSession::state_start_frame_read(int event, void *edata)
{
  VIO *vio = static_cast<VIO *>(edata);

  STATE_ENTER(&Http2CommonSession::state_start_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);
  return do_process_frame_read(event, vio, false);
}

int
Http2CommonSession::do_start_frame_read(Http2ErrorCode &ret_error)
{
  ret_error = Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
  ink_release_assert(this->_read_buffer_reader->read_avail() >= (int64_t)HTTP2_FRAME_HEADER_LEN);

  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  unsigned nbytes;

  Http2SsnDebug("receiving frame header");
  nbytes = copy_from_buffer_reader(buf, this->_read_buffer_reader, sizeof(buf));

  this->cur_frame_from_early_data = false;
  if (!http2_parse_frame_header(make_iovec(buf), this->current_hdr)) {
    Http2SsnDebug("frame header parse failure");
    this->get_proxy_session()->do_io_close();
    return -1;
  }

  // Check whether data is read from early data
  if (this->read_from_early_data > 0) {
    this->read_from_early_data -= this->read_from_early_data > nbytes ? nbytes : this->read_from_early_data;
    this->cur_frame_from_early_data = true;
  }

  Http2SsnDebug("frame header length=%u, type=%u, flags=0x%x, streamid=%u", (unsigned)this->current_hdr.length,
                (unsigned)this->current_hdr.type, (unsigned)this->current_hdr.flags, this->current_hdr.streamid);

  this->_read_buffer_reader->consume(nbytes);

  if (!http2_frame_header_is_valid(this->current_hdr, this->connection_state.server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE))) {
    ret_error = Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    return -1;
  }

  // If we know up front that the payload is too long, nuke this connection.
  if (this->current_hdr.length > this->connection_state.server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE)) {
    ret_error = Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR;
    return -1;
  }

  // CONTINUATIONs MUST follow behind HEADERS which doesn't have END_HEADERS
  Http2StreamId continued_stream_id = this->connection_state.get_continued_stream_id();

  if (continued_stream_id != 0 &&
      (continued_stream_id != this->current_hdr.streamid || this->current_hdr.type != HTTP2_FRAME_TYPE_CONTINUATION)) {
    ret_error = Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR;
    return -1;
  }
  return 0;
}

int
Http2CommonSession::state_complete_frame_read(int event, void *edata)
{
  VIO *vio = static_cast<VIO *>(edata);
  STATE_ENTER(&Http2CommonSession::state_complete_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);
  if (this->_read_buffer_reader->read_avail() < this->current_hdr.length) {
    if (this->_should_do_something_else()) {
      if (this->_reenable_event == nullptr) {
        vio->disable();
        this->_reenable_event = this->get_mutex()->thread_holding->schedule_in(this->get_proxy_session(), HRTIME_MSECONDS(1),
                                                                               HTTP2_SESSION_EVENT_REENABLE, vio);
      } else {
        vio->reenable();
      }
    } else {
      vio->reenable();
    }
    return 0;
  }
  Http2SsnDebug("completed frame read, %" PRId64 " bytes available", this->_read_buffer_reader->read_avail());

  return do_process_frame_read(event, vio, true);
}

int
Http2CommonSession::do_complete_frame_read()
{
  // XXX parse the frame and handle it ...
  ink_release_assert(this->_read_buffer_reader->read_avail() >= this->current_hdr.length);

  Http2Frame frame(this->current_hdr, this->_read_buffer_reader, this->cur_frame_from_early_data);
  connection_state.rcv_frame(&frame);

  // Check whether data is read from early data
  if (this->read_from_early_data > 0) {
    this->read_from_early_data -=
      this->read_from_early_data > this->current_hdr.length ? this->current_hdr.length : this->read_from_early_data;
  }
  this->_read_buffer_reader->consume(this->current_hdr.length);
  ++(this->_n_frame_read);

  // Set the event handler if there is no more data to process a new frame
  HTTP2_SET_SESSION_HANDLER(&Http2CommonSession::state_start_frame_read);

  return 0;
}

int
Http2CommonSession::do_process_frame_read(int event, VIO *vio, bool inside_frame)
{
  if (inside_frame) {
    do_complete_frame_read();
  }

  while (this->_read_buffer_reader->read_avail() >= static_cast<int64_t>(HTTP2_FRAME_HEADER_LEN)) {
    // Cancel reading if there was an error or connection is closed
    if (connection_state.tx_error_code.code != static_cast<uint32_t>(Http2ErrorCode::HTTP2_ERROR_NO_ERROR) ||
        connection_state.is_state_closed()) {
      Http2SsnDebug("reading a frame has been canceled (%u)", connection_state.tx_error_code.code);
      break;
    }

    Http2ErrorCode err = Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
    if (this->connection_state.get_stream_error_rate() > std::min(1.0, Http2::stream_error_rate_threshold * 2.0)) {
      ip_port_text_buffer ipb;
      const char *client_ip = ats_ip_ntop(this->get_proxy_session()->get_remote_addr(), ipb, sizeof(ipb));
      SiteThrottledWarning("HTTP/2 session error client_ip=%s session_id=%" PRId64
                           " closing a connection, because its stream error rate (%f) exceeded the threshold (%f)",
                           client_ip, this->get_connection_id(), this->connection_state.get_stream_error_rate(),
                           Http2::stream_error_rate_threshold);
      err = Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
    }

    // Return if there was an error
    if (err > Http2ErrorCode::HTTP2_ERROR_NO_ERROR || do_start_frame_read(err) < 0) {
      // send an error if specified.  Otherwise, just go away
      if (err > Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
        if (!this->connection_state.is_state_closed()) {
          this->connection_state.send_goaway_frame(this->connection_state.get_latest_stream_id_in(), err);
          this->set_half_close_local_flag(true);
        }
      }
      return 0;
    }

    // If there is no more data to finish the frame, set up the event handler and reenable
    if (this->_read_buffer_reader->read_avail() < this->current_hdr.length) {
      HTTP2_SET_SESSION_HANDLER(&Http2CommonSession::state_complete_frame_read);
      break;
    }
    do_complete_frame_read();

    if (this->_should_do_something_else()) {
      if (this->_reenable_event == nullptr) {
        vio->disable();
        this->_reenable_event = this->get_mutex()->thread_holding->schedule_in(this->get_proxy_session(), HRTIME_MSECONDS(1),
                                                                               HTTP2_SESSION_EVENT_REENABLE, vio);
        return 0;
      }
    }
  }

  // If the client hasn't shut us down, reenable
  if (!this->get_proxy_session()->is_peer_closed()) {
    vio->reenable();
  }
  return 0;
}

bool
Http2CommonSession::_should_do_something_else()
{
  // Do something else every 128 incoming frames if connection state isn't closed
  return (this->_n_frame_read & 0x7F) == 0 && !connection_state.is_state_closed();
}

int64_t
Http2CommonSession::write_avail()
{
  return this->write_buffer->write_avail();
}

bool
Http2CommonSession::is_write_high_water() const
{
  return this->write_buffer->high_water();
}

void
Http2CommonSession::write_reenable()
{
  write_vio->reenable();
}

void
Http2CommonSession::add_url_to_pushed_table(const char *url, int url_len)
{
  // Delay std::unordered_set allocation until when it used
  if (_h2_pushed_urls == nullptr) {
    this->_h2_pushed_urls = new std::unordered_set<std::string>();
    this->_h2_pushed_urls->reserve(Http2::push_diary_size);
  }

  if (_h2_pushed_urls->size() < Http2::push_diary_size) {
    _h2_pushed_urls->emplace(url);
  }
}
