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

#define STATE_ENTER(state_name, event) do { \
  DebugSsn(this, "http2_cs", "[%" PRId64 "] [%s, %s]", this->connection_id(), \
    #state_name, HttpDebugNames::get_event_name(event)); \
} while (0)

#define DebugHttp2Ssn(fmt, ...) \
  DebugSsn(this, "http2_cs",  "[%" PRId64 "] " fmt, this->connection_id(), __VA_ARGS__)

#define DebugHttp2Ssn0(msg) \
  DebugSsn(this, "http2_cs",  "[%" PRId64 "] " msg, this->connection_id())

#define HTTP2_SET_SESSION_HANDLER(handler) do { \
  this->session_handler = (handler); \
} while (0)

ClassAllocator<Http2ClientSession> http2ClientSessionAllocator("http2ClientSessionAllocator");

// memcpy the requested bytes from the IOBufferReader, returning how many were actually copied.
static inline unsigned
copy_from_buffer_reader(void * dst, IOBufferReader * reader, unsigned nbytes)
{
    char * end;

    end = reader->memcpy(dst, nbytes, 0 /* offset */);
    return end - (char *)dst;
}

static int
send_connection_event(Continuation * cont, int event, void * edata)
{
  MUTEX_LOCK(lock, cont->mutex, this_ethread());
  return cont->handleEvent(event, edata);
}

Http2ClientSession::Http2ClientSession()
  : con_id(0), client_vc(NULL), read_buffer(NULL), sm_reader(NULL), write_buffer(NULL), sm_writer(NULL)
{
}

void
Http2ClientSession::destroy()
{
  DebugHttp2Ssn0("session destroy");

  ink_release_assert(this->client_vc == NULL);

  free_MIOBuffer(this->read_buffer);
  ProxyClientSession::cleanup();
  http2ClientSessionAllocator.free(this);
}

void
Http2ClientSession::start()
{
  VIO * read_vio;

  MUTEX_LOCK(lock, this->mutex, this_ethread());

  SET_HANDLER(&Http2ClientSession::main_event_handler);
  HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_read_connection_preface);

  read_vio = this->do_io_read(this, INT64_MAX, this->read_buffer);
  this->do_io_write(this, INT64_MAX, this->sm_writer);

  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_INIT, this);
  this->handleEvent(VC_EVENT_READ_READY, read_vio);
}

void
Http2ClientSession::new_connection(NetVConnection * new_vc, MIOBuffer * iobuf, IOBufferReader * reader, bool backdoor)
{
  // HTTP/2 for the backdoor connections? Let's not deal woth that yet.
  ink_release_assert(backdoor == false);

  // Unique client session identifier.
  this->con_id = ProxyClientSession::next_connection_id();
  this->client_vc = new_vc;
  this->mutex = new_vc->mutex;

  this->connection_state.mutex = new_ProxyMutex();

  DebugHttp2Ssn("session born, netvc %p", this->client_vc);

  this->read_buffer = iobuf ? iobuf : new_MIOBuffer(HTTP2_HEADER_BUFFER_SIZE_INDEX);
  this->sm_reader = reader ? reader : this->read_buffer->alloc_reader();

  this->write_buffer = new_MIOBuffer(HTTP2_HEADER_BUFFER_SIZE_INDEX);
  this->sm_writer = this->write_buffer->alloc_reader();

  do_api_callout(TS_HTTP_SSN_START_HOOK);
}

VIO *
Http2ClientSession::do_io_read(Continuation * c, int64_t nbytes, MIOBuffer * buf)
{
  return this->client_vc->do_io_read(c, nbytes, buf);
}

VIO *
Http2ClientSession::do_io_write(Continuation * c, int64_t nbytes, IOBufferReader * buf, bool owner)
{
  return this->client_vc->do_io_write(c, nbytes, buf, owner);
}

void
Http2ClientSession::do_io_shutdown(ShutdownHowTo_t howto)
{
  this->client_vc->do_io_shutdown(howto);
}

// XXX Currently, we don't have a half-closed state, but we will need to implement that. After we send a GOAWAY, there
// are scenarios where we would like to complete the outstanding streams.

void
Http2ClientSession::do_io_close(int alerrno)
{
  DebugHttp2Ssn0("session closed");

  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_FINI, this);

  this->client_vc->do_io_close(alerrno);
  this->client_vc = NULL;

  do_api_callout(TS_HTTP_SSN_CLOSE_HOOK);
}

void
Http2ClientSession::reenable(VIO * vio)
{
  this->client_vc->reenable(vio);
}

int
Http2ClientSession::main_event_handler(int event, void * edata)
{
  ink_assert(this->mutex->thread_holding == this_ethread());

  switch (event) {
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_READ_READY:
    return (this->*session_handler)(event, edata);

  case HTTP2_SESSION_EVENT_XMIT: {
    Http2Frame * frame = (Http2Frame *)edata;
    frame->xmit(this->write_buffer);
    return 0;
  }

  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ERROR:
  case VC_EVENT_EOS:
    this->do_io_close();
    return 0;

  case VC_EVENT_WRITE_COMPLETE:
  case VC_EVENT_WRITE_READY:
    return 0;

  default:
    DebugHttp2Ssn("unexpected event=%d edata=%p", event, edata);
    ink_release_assert(0);
    return 0;
  }

}

int
Http2ClientSession::state_read_connection_preface(int event, void * edata)
{
  VIO * vio = (VIO *)edata;

  STATE_ENTER(&Http2ClientSession::state_read_connection_preface, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);

  if (this->sm_reader->read_avail() >= (int64_t)HTTP2_CONNECTION_PREFACE_LEN) {
    char buf[HTTP2_CONNECTION_PREFACE_LEN];
    unsigned nbytes;

    nbytes = copy_from_buffer_reader(buf, this->sm_reader, sizeof(buf));
    ink_release_assert(nbytes == HTTP2_CONNECTION_PREFACE_LEN);

    if (memcmp(HTTP2_CONNECTION_PREFACE, buf, nbytes) != 0) {
      DebugHttp2Ssn0("invalid connection preface");
      this->do_io_close();
      return 0;
    }

    DebugHttp2Ssn0("received connection preface");
    this->sm_reader->consume(nbytes);
    HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_start_frame_read);

    // XXX set activity timeouts ...

    // XXX start the write VIO ...

    // If we have unconsumed data, start tranferring frames now.
    if (this->sm_reader->is_read_avail_more_than(0)) {
      return this->handleEvent(VC_EVENT_READ_READY, vio);
    }
  }

  // XXX We don't have enough data to check the connection preface. We should reset the accept inactivity
  // timeout. We should have a maximum timeout to get the session started though.

  vio->reenable();
  return 0;
}

int
Http2ClientSession::state_start_frame_read(int event, void * edata)
{
  VIO * vio = (VIO *)edata;

  STATE_ENTER(&Http2ClientSession::state_start_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);

  if (this->sm_reader->read_avail() >= (int64_t)HTTP2_FRAME_HEADER_LEN) {
    uint8_t buf[HTTP2_FRAME_HEADER_LEN];
    unsigned nbytes;

    DebugHttp2Ssn0("receiving frame header");
    nbytes = copy_from_buffer_reader(buf, this->sm_reader, sizeof(buf));

    if (!http2_parse_frame_header(make_iovec(buf), this->current_hdr)) {
      DebugHttp2Ssn0("frame header parse failure");
      this->do_io_close();
      return 0;
    }

    DebugHttp2Ssn("frame header length=%u, type=%u, flags=0x%x, streamid=%u",
        (unsigned)this->current_hdr.length, (unsigned)this->current_hdr.type,
        (unsigned)this->current_hdr.flags, this->current_hdr.streamid);

    this->sm_reader->consume(nbytes);

    if (!http2_frame_header_is_valid(this->current_hdr)) {
      // XXX nuke it with HTTP2_ERROR_PROTOCOL_ERROR!
    }

    // If we know up front that the payload is too long, nuke this connection.
    if (this->current_hdr.length > HTTP2_MAX_FRAME_PAYLOAD) {
      // XXX nuke it with HTTP2_ERROR_FRAME_SIZE_ERROR!
    }

    if (!http2_is_client_streamid(this->current_hdr.streamid)) {
      // XXX nuke it with HTTP2_ERROR_PROTOCOL_ERROR!
    }

    HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_complete_frame_read);
    if (this->sm_reader->read_avail() >= this->current_hdr.length) {
      return this->handleEvent(VC_EVENT_READ_READY, vio);
    }
  }

  vio->reenable();
  return 0;
}

int
Http2ClientSession::state_complete_frame_read(int event, void * edata)
{
  VIO * vio = (VIO *)edata;

  STATE_ENTER(&Http2ClientSession::state_complete_frame_read, event);
  ink_assert(event == VC_EVENT_READ_COMPLETE || event == VC_EVENT_READ_READY);

  if (this->sm_reader->read_avail() < this->current_hdr.length) {
    vio->reenable();
    return 0;
  }

  DebugHttp2Ssn("completed frame read, %" PRId64 " bytes available", this->sm_reader->read_avail());

  // XXX parse the frame and handle it ...

  Http2Frame frame(this->current_hdr, this->sm_reader);

  send_connection_event(&this->connection_state, HTTP2_SESSION_EVENT_RECV, &frame);
  this->sm_reader->consume(this->current_hdr.length);

  HTTP2_SET_SESSION_HANDLER(&Http2ClientSession::state_start_frame_read);
  if (this->sm_reader->is_read_avail_more_than(0)) {
    return this->handleEvent(VC_EVENT_READ_READY, vio);
  }

  vio->reenable();
  return 0;
}
