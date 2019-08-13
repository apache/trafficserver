/** @file

  Http2ClientSession.

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

#include "Http2ClientSession.h"
#include "HttpDebugNames.h"
#include "tscore/ink_base64.h"

#define STATE_ENTER(state_name, event)                                                       \
  do {                                                                                       \
    SsnDebug(this, "http2_cs", "[%" PRId64 "] [%s, %s]", this->connection_id(), #state_name, \
             HttpDebugNames::get_event_name(event));                                         \
  } while (0)

#define Http2SsnDebug(fmt, ...) SsnDebug(this, "http2_cs", "[%" PRId64 "] " fmt, this->connection_id(), ##__VA_ARGS__)

#define HTTP2_SET_SESSION_HANDLER(handler) \
  do {                                     \
    this->session_handler = (handler);     \
  } while (0)

ClassAllocator<Http2ClientSession> http2ClientSessionAllocator("http2ClientSessionAllocator");

// memcpy the requested bytes from the IOBufferReader, returning how many were
// actually copied.
static inline unsigned
copy_from_buffer_reader(void *dst, IOBufferReader *reader, unsigned nbytes)
{
  char *end;

  end = reader->memcpy(dst, nbytes, 0 /* offset */);
  return end - (char *)dst;
}

static int
send_connection_event(Continuation *cont, int event, void *edata)
{
  SCOPED_MUTEX_LOCK(lock, cont->mutex, this_ethread());
  return cont->handleEvent(event, edata);
}

Http2ClientSession::Http2ClientSession() {}

void
Http2ClientSession::destroy()
{
  if (!in_destroy) {
    in_destroy = true;
    Http2SsnDebug("session destroy");
    // Let everyone know we are going down
    do_api_callout(TS_HTTP_SSN_CLOSE_HOOK);
  }
}

void
Http2ClientSession::free()
{
  if (this->_reenable_event) {
    this->_reenable_event->cancel();
    this->_reenable_event = nullptr;
  }

  if (h2_pushed_urls) {
    this->h2_pushed_urls = ink_hash_table_destroy(this->h2_pushed_urls);
  }

  if (client_vc) {
    client_vc->do_io_close();
    client_vc = nullptr;
  }

  // Make sure the we are at the bottom of the stack
  if (connection_state.is_recursing() || this->recursion != 0) {
    // Note that we are ready to be cleaned up
    // One of the event handlers will catch it
    kill_me = true;
    return;
  }

  Http2SsnDebug("session free");

  HTTP2_DECREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_SESSION_COUNT, this->mutex->thread_holding);

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

  ink_release_assert(this->client_vc == nullptr);

  this->connection_state.destroy();

  super::free();

  free_MIOBuffer(this->read_buffer);
  free_MIOBuffer(this->write_buffer);
  THREAD_FREE(this, http2ClientSessionAllocator, this_ethread());
}

void
Http2ClientSession::start()
{
  VIO *read_vio;

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  SET_HANDLER(&Http2ClientSession::main_event_handler);
  HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_read_connection_preface);

  read_vio  = this->do_io_read(this, INT64_MAX, this->read_buffer);
  write_vio = this->do_io_write(this, INT64_MAX, this->sm_writer);

  // 3.5 HTTP/2 Connection Preface. Upon establishment of a TCP connection and
  // determination that HTTP/2 will be used by both peers, each endpoint MUST
  // send a connection preface as a final confirmation ...
  // this->write_buffer->write(HTTP2_CONNECTION_PREFACE,
  // HTTP2_CONNECTION_PREFACE_LEN);

  this->connection_state.init();
  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_INIT, this);
  this->handleEvent(VC_EVENT_READ_READY, read_vio);
}

void
Http2ClientSession::new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor)
{
  ink_assert(new_vc->mutex->thread_holding == this_ethread());
  HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_SESSION_COUNT, new_vc->mutex->thread_holding);
  HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_TOTAL_CLIENT_CONNECTION_COUNT, new_vc->mutex->thread_holding);

  // HTTP/2 for the backdoor connections? Let's not deal woth that yet.
  ink_release_assert(backdoor == false);

  // Unique client session identifier.
  this->con_id    = ProxyClientSession::next_connection_id();
  this->client_vc = new_vc;
  client_vc->set_inactivity_timeout(HRTIME_SECONDS(Http2::accept_no_activity_timeout));
  this->schedule_event = nullptr;
  this->mutex          = new_vc->mutex;
  this->in_destroy     = false;

  this->connection_state.mutex = this->mutex;

  Http2SsnDebug("session born, netvc %p", this->client_vc);

  this->client_vc->set_tcp_congestion_control(CLIENT_SIDE);

  this->read_buffer             = iobuf ? iobuf : new_MIOBuffer(HTTP2_HEADER_BUFFER_SIZE_INDEX);
  this->read_buffer->water_mark = connection_state.server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE);
  this->sm_reader               = reader ? reader : this->read_buffer->alloc_reader();
  this->h2_pushed_urls          = ink_hash_table_create(InkHashTableKeyType_String);
  this->h2_pushed_urls_size     = 0;

  this->write_buffer = new_MIOBuffer(HTTP2_HEADER_BUFFER_SIZE_INDEX);
  this->sm_writer    = this->write_buffer->alloc_reader();

  do_api_callout(TS_HTTP_SSN_START_HOOK);
}

void
Http2ClientSession::set_upgrade_context(HTTPHdr *h)
{
  upgrade_context.req_header = new HTTPHdr();
  upgrade_context.req_header->copy(h);

  MIMEField *settings = upgrade_context.req_header->field_find(MIME_FIELD_HTTP2_SETTINGS, MIME_LEN_HTTP2_SETTINGS);
  ink_release_assert(settings != nullptr);
  int svlen;
  const char *sv = settings->value_get(&svlen);

  // Maybe size of data decoded by Base64URL is lower than size of encoded data.
  unsigned char out_buf[svlen];
  if (sv && svlen > 0) {
    size_t decoded_len;
    ats_base64_decode(sv, svlen, out_buf, svlen, &decoded_len);
    for (size_t nbytes = 0; nbytes < decoded_len; nbytes += HTTP2_SETTINGS_PARAMETER_LEN) {
      Http2SettingsParameter param;
      if (!http2_parse_settings_parameter(make_iovec(out_buf + nbytes, HTTP2_SETTINGS_PARAMETER_LEN), param) ||
          !http2_settings_parameter_is_valid(param)) {
        // TODO ignore incoming invalid parameters and send suitable SETTINGS
        // frame.
      }
      upgrade_context.client_settings.set(static_cast<Http2SettingsIdentifier>(param.id), param.value);
    }
  }

  // Such intermediaries SHOULD also remove other connection-
  // specific header fields, such as Keep-Alive, Proxy-Connection,
  // Transfer-Encoding and Upgrade, even if they are not nominated by
  // Connection.
  upgrade_context.req_header->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  upgrade_context.req_header->field_delete(MIME_FIELD_KEEP_ALIVE, MIME_LEN_KEEP_ALIVE);
  upgrade_context.req_header->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);
  upgrade_context.req_header->field_delete(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
  upgrade_context.req_header->field_delete(MIME_FIELD_UPGRADE, MIME_LEN_UPGRADE);
  upgrade_context.req_header->field_delete(MIME_FIELD_HTTP2_SETTINGS, MIME_LEN_HTTP2_SETTINGS);
}

VIO *
Http2ClientSession::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return this->client_vc->do_io_read(c, nbytes, buf);
}

VIO *
Http2ClientSession::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return this->client_vc->do_io_write(c, nbytes, buf, owner);
}

void
Http2ClientSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  this->client_vc->do_io_shutdown(howto);
}

// XXX Currently, we don't have a half-closed state, but we will need to
// implement that. After we send a GOAWAY, there
// are scenarios where we would like to complete the outstanding streams.

void
Http2ClientSession::do_io_close(int alerrno)
{
  Http2SsnDebug("session closed");

  ink_assert(this->mutex->thread_holding == this_ethread());
  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_FINI, this);

  // Don't send the SSN_CLOSE_HOOK until we got rid of all the streams
  // And handled all the TXN_CLOSE_HOOK's
  if (client_vc) {
    // Copy aside the client address before releasing the vc
    cached_client_addr.assign(client_vc->get_remote_addr());
    cached_local_addr.assign(client_vc->get_local_addr());
    client_vc->do_io_close();
    client_vc = nullptr;
  }

  {
    SCOPED_MUTEX_LOCK(lock, this->connection_state.mutex, this_ethread());
    this->connection_state.release_stream(nullptr);
  }

  this->clear_session_active();
}

void
Http2ClientSession::reenable(VIO *vio)
{
  this->client_vc->reenable(vio);
}

void
Http2ClientSession::set_half_close_local_flag(bool flag)
{
  if (!half_close_local && flag) {
    Http2SsnDebug("session half-close local");
  }
  half_close_local = flag;
}

int
Http2ClientSession::main_event_handler(int event, void *edata)
{
  ink_assert(this->mutex->thread_holding == this_ethread());
  int retval;

  recursion++;

  Event *e = static_cast<Event *>(edata);
  if (e == schedule_event) {
    schedule_event = nullptr;
  }

  switch (event) {
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_READ_READY: {
    bool is_zombie = connection_state.get_zombie_event() != nullptr;
    retval         = (this->*session_handler)(event, edata);
    if (is_zombie && connection_state.get_zombie_event() != nullptr) {
      Warning("Processed read event for zombie session %" PRId64, connection_id());
    }
    break;
  }

  case HTTP2_SESSION_EVENT_XMIT: {
    Http2Frame *frame = (Http2Frame *)edata;
    total_write_len += frame->size();
    write_vio->nbytes = total_write_len;
    frame->xmit(this->write_buffer);
    write_reenable();
    retval = 0;
    break;
  }

  case HTTP2_SESSION_EVENT_REENABLE:
    // VIO will be reenableed in this handler
    retval = (this->*session_handler)(VC_EVENT_READ_READY, static_cast<VIO *>(e->cookie));
    // Clear the event after calling session_handler to not reschedule REENABLE in it
    this->_reenable_event = nullptr;
    break;

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    this->set_dying_event(event);
    this->do_io_close();
    retval = 0;
    break;

  case VC_EVENT_WRITE_READY:
    retval = 0;
    break;

  case VC_EVENT_WRITE_COMPLETE:
    // Seems as this is being closed already
    retval = 0;
    break;

  default:
    Http2SsnDebug("unexpected event=%d edata=%p", event, edata);
    ink_release_assert(0);
    retval = 0;
    break;
  }

  if (!this->is_draining() && this->connection_state.get_shutdown_reason() == Http2ErrorCode::HTTP2_ERROR_MAX) {
    this->connection_state.set_shutdown_state(HTTP2_SHUTDOWN_NONE);
  }

  if (this->connection_state.get_shutdown_state() == HTTP2_SHUTDOWN_NONE) {
    if (this->is_draining()) { // For a case we already checked Connection header and it didn't exist
      Http2SsnDebug("Preparing for graceful shutdown because of draining state");
      this->connection_state.set_shutdown_state(HTTP2_SHUTDOWN_NOT_INITIATED);
    } else if (this->connection_state.get_stream_error_rate() >
               Http2::stream_error_rate_threshold) { // For a case many stream errors happened
      ip_port_text_buffer ipb;
      const char *client_ip = ats_ip_ntop(get_client_addr(), ipb, sizeof(ipb));
      Error("HTTP/2 session error client_ip=%s session_id=%" PRId64
            " closing a connection, because its stream error rate (%f) exceeded the threshold (%f)",
            client_ip, connection_id(), this->connection_state.get_stream_error_rate(), Http2::stream_error_rate_threshold);
      Http2SsnDebug("Preparing for graceful shutdown because of a high stream error rate");
      cause_of_death = Http2SessionCod::HIGH_ERROR_RATE;
      this->connection_state.set_shutdown_state(HTTP2_SHUTDOWN_NOT_INITIATED, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM);
    }
  }

  if (this->connection_state.get_shutdown_state() == HTTP2_SHUTDOWN_NOT_INITIATED) {
    send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_SHUTDOWN_INIT, this);
  }

  recursion--;
  if (!connection_state.is_recursing() && this->recursion == 0 && kill_me) {
    this->free();
  }
  return retval;
}

int
Http2ClientSession::state_read_connection_preface(int event, void *edata)
{
  VIO *vio = (VIO *)edata;

  STATE_ENTER(&Http2ClientSession::state_read_connection_preface, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);

  if (this->sm_reader->read_avail() >= (int64_t)HTTP2_CONNECTION_PREFACE_LEN) {
    char buf[HTTP2_CONNECTION_PREFACE_LEN];
    unsigned nbytes;

    nbytes = copy_from_buffer_reader(buf, this->sm_reader, sizeof(buf));
    ink_release_assert(nbytes == HTTP2_CONNECTION_PREFACE_LEN);

    if (memcmp(HTTP2_CONNECTION_PREFACE, buf, nbytes) != 0) {
      Http2SsnDebug("invalid connection preface");
      this->do_io_close();
      return 0;
    }

    Http2SsnDebug("received connection preface");
    this->sm_reader->consume(nbytes);
    HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_start_frame_read);

    client_vc->set_inactivity_timeout(HRTIME_SECONDS(Http2::no_activity_timeout_in));
    client_vc->set_active_timeout(HRTIME_SECONDS(Http2::active_timeout_in));

    // XXX start the write VIO ...

    // If we have unconsumed data, start tranferring frames now.
    if (this->sm_reader->is_read_avail_more_than(0)) {
      return this->handleEvent(VC_EVENT_READ_READY, vio);
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
Http2ClientSession::state_start_frame_read(int event, void *edata)
{
  VIO *vio = (VIO *)edata;

  STATE_ENTER(&Http2ClientSession::state_start_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);
  return state_process_frame_read(event, vio, false);
}

int
Http2ClientSession::do_start_frame_read(Http2ErrorCode &ret_error)
{
  ret_error = Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
  ink_release_assert(this->sm_reader->read_avail() >= (int64_t)HTTP2_FRAME_HEADER_LEN);

  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  unsigned nbytes;

  Http2SsnDebug("receiving frame header");
  nbytes = copy_from_buffer_reader(buf, this->sm_reader, sizeof(buf));

  if (!http2_parse_frame_header(make_iovec(buf), this->current_hdr)) {
    Http2SsnDebug("frame header parse failure");
    this->do_io_close();
    return -1;
  }

  Http2SsnDebug("frame header length=%u, type=%u, flags=0x%x, streamid=%u", (unsigned)this->current_hdr.length,
                (unsigned)this->current_hdr.type, (unsigned)this->current_hdr.flags, this->current_hdr.streamid);

  this->sm_reader->consume(nbytes);

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
Http2ClientSession::state_complete_frame_read(int event, void *edata)
{
  VIO *vio = (VIO *)edata;
  STATE_ENTER(&Http2ClientSession::state_complete_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);
  if (this->sm_reader->read_avail() < this->current_hdr.length) {
    if (this->_should_do_something_else()) {
      if (this->_reenable_event == nullptr) {
        vio->disable();
        this->_reenable_event = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(1), HTTP2_SESSION_EVENT_REENABLE, vio);
      } else {
        vio->reenable();
      }
    } else {
      vio->reenable();
    }
    return 0;
  }
  Http2SsnDebug("completed frame read, %" PRId64 " bytes available", this->sm_reader->read_avail());

  return state_process_frame_read(event, vio, true);
}

int
Http2ClientSession::do_complete_frame_read()
{
  // XXX parse the frame and handle it ...
  ink_release_assert(this->sm_reader->read_avail() >= this->current_hdr.length);

  Http2Frame frame(this->current_hdr, this->sm_reader);
  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_RECV, &frame);
  this->sm_reader->consume(this->current_hdr.length);
  ++(this->_n_frame_read);

  // Set the event handler if there is no more data to process a new frame
  HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_start_frame_read);

  return 0;
}

int
Http2ClientSession::state_process_frame_read(int event, VIO *vio, bool inside_frame)
{
  if (inside_frame) {
    do_complete_frame_read();
  }

  while (this->sm_reader->read_avail() >= (int64_t)HTTP2_FRAME_HEADER_LEN) {
    // Cancel reading if there was an error
    if (connection_state.tx_error_code.code != static_cast<uint32_t>(Http2ErrorCode::HTTP2_ERROR_NO_ERROR)) {
      Http2SsnDebug("reading a frame has been canceled (%u)", connection_state.tx_error_code.code);
      break;
    }

    Http2ErrorCode err = Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
    if (this->connection_state.get_stream_error_rate() > std::min(1.0, Http2::stream_error_rate_threshold * 2.0)) {
      ip_port_text_buffer ipb;
      const char *client_ip = ats_ip_ntop(get_client_addr(), ipb, sizeof(ipb));
      Error("HTTP/2 session error client_ip=%s session_id=%" PRId64
            " closing a connection, because its stream error rate (%f) is too high",
            client_ip, connection_id(), this->connection_state.get_stream_error_rate());
      err = Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
    }

    // Return if there was an error
    if (err > Http2ErrorCode::HTTP2_ERROR_NO_ERROR || do_start_frame_read(err) < 0) {
      // send an error if specified.  Otherwise, just go away
      if (err > Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
        SCOPED_MUTEX_LOCK(lock, this->connection_state.mutex, this_ethread());
        if (!this->connection_state.is_state_closed()) {
          this->connection_state.send_goaway_frame(this->connection_state.get_latest_stream_id_in(), err);
          this->set_half_close_local_flag(true);
          this->do_io_close();
        }
      }
      return 0;
    }

    // If there is no more data to finish the frame, set up the event handler and reenable
    if (this->sm_reader->read_avail() < this->current_hdr.length) {
      HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_complete_frame_read);
      break;
    }
    do_complete_frame_read();

    if (this->_should_do_something_else()) {
      if (this->_reenable_event == nullptr) {
        vio->disable();
        this->_reenable_event = mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(1), HTTP2_SESSION_EVENT_REENABLE, vio);
        return 0;
      }
    }
  }

  // If the client hasn't shut us down, reenable
  if (!this->is_client_closed()) {
    vio->reenable();
  }
  return 0;
}

void
Http2ClientSession::increment_current_active_client_connections_stat()
{
  HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_COUNT, this_ethread());
}

void
Http2ClientSession::decrement_current_active_client_connections_stat()
{
  HTTP2_DECREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_ACTIVE_CLIENT_CONNECTION_COUNT, this_ethread());
}

bool
Http2ClientSession::_should_do_something_else()
{
  // Do something else every 128 incoming frames
  return (this->_n_frame_read & 0x7F) == 0;
}
