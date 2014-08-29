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

#ifndef __HTTP2_CLIENT_SESSION_H__
#define __HTTP2_CLIENT_SESSION_H__

#include "HTTP2.h"
#include "ProxyClientSession.h"
#include "Http2ConnectionState.h"

// Name                       Edata                 Description
// HTTP2_SESSION_EVENT_INIT   Http2ClientSession *  HTTP/2 session is born
// HTTP2_SESSION_EVENT_FINI   Http2ClientSession *  HTTP/2 session is ended
// HTTP2_SESSION_EVENT_RECV   Http2Frame *          Received a frame
// HTTP2_SESSION_EVENT_XMIT   Http2Frame *          Send this frame

#define HTTP2_SESSION_EVENT_INIT  (HTTP2_SESSION_EVENTS_START + 1)
#define HTTP2_SESSION_EVENT_FINI  (HTTP2_SESSION_EVENTS_START + 2)
#define HTTP2_SESSION_EVENT_RECV  (HTTP2_SESSION_EVENTS_START + 3)
#define HTTP2_SESSION_EVENT_XMIT  (HTTP2_SESSION_EVENTS_START + 4)

static size_t const HTTP2_HEADER_BUFFER_SIZE_INDEX = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

class Http2Frame
{
public:
  Http2Frame(const Http2FrameHeader& h, IOBufferReader * r) {
    this->hdr.cooked = h;
    this->ioreader = r;
  }

  Http2Frame(Http2FrameType type, Http2StreamId streamid, uint8_t flags = 0) {
    Http2FrameHeader hdr = { 0, (uint8_t)type, flags, streamid };
    http2_write_frame_header(hdr, make_iovec(this->hdr.raw));
  }

  IOBufferReader * reader() const {
    return ioreader;
  }

  const Http2FrameHeader& header() const {
    return this->hdr.cooked;
  }

  // Allocate an IOBufferBlock for this frame. This switches us from using the in-line header
  // buffer, to an external buffer block.
  void alloc(int index) {
    this->ioblock = new_IOBufferBlock();
    this->ioblock->alloc(index);
    memcpy(this->ioblock->start(), this->hdr.raw, sizeof(this->hdr.raw));
    this->ioblock->fill(sizeof(this->hdr.raw));

    http2_parse_frame_header(make_iovec(this->ioblock->start(), HTTP2_FRAME_HEADER_LEN), this->hdr.cooked);
  }

  // Return the writeable buffer space.
  IOVec write() {
    return make_iovec(this->ioblock->end(), this->ioblock->write_avail());
  }

  // Once the frame has been serialized, update the length.
  void finalize(size_t nbytes) {
    if (this->ioblock) {
      ink_assert((int64_t)nbytes <= this->ioblock->write_avail());
      this->ioblock->fill(nbytes);

      this->hdr.cooked.length = this->ioblock->size() - HTTP2_FRAME_HEADER_LEN;
      http2_write_frame_header(this->hdr.cooked, make_iovec(this->ioblock->start(), HTTP2_FRAME_HEADER_LEN));
    }
  }

  void xmit(MIOBuffer * iobuffer) {
    if (ioblock) {
      iobuffer->append_block(this->ioblock);
    } else {
      iobuffer->write(this->hdr.raw, sizeof(this->hdr.raw));
    }
  }

private:
  Http2Frame(Http2Frame &); // noncopyable
  Http2Frame& operator=(const Http2Frame &); // noncopyable

  Ptr<IOBufferBlock>  ioblock;
  IOBufferReader *    ioreader;

  union {
    Http2FrameHeader cooked;
    uint8_t raw[HTTP2_FRAME_HEADER_LEN];
  } hdr;
};

class Http2ClientSession : public ProxyClientSession
{
public:
  Http2ClientSession();

  typedef int (Http2ClientSession::*SessionHandler)(int, void *);

  // Implement ProxyClientSession interface.
  void start();
  void destroy();
  void new_connection(NetVConnection * new_vc, MIOBuffer * iobuf, IOBufferReader * reader, bool backdoor);

  // Implement VConnection interface.
  VIO * do_io_read(Continuation * c, int64_t nbytes = INT64_MAX, MIOBuffer * buf = 0);
  VIO * do_io_write(Continuation * c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader * buf = 0, bool owner = false);
  void do_io_close(int lerrno = -1);
  void do_io_shutdown(ShutdownHowTo_t howto);
  void reenable(VIO * vio);

  int64_t connection_id() const {
    return this->con_id;
  }

private:

  Http2ClientSession(Http2ClientSession &); // noncopyable
  Http2ClientSession& operator=(const Http2ClientSession &); // noncopyable

  int main_event_handler(int, void *);

  int state_read_connection_preface(int, void *);
  int state_start_frame_read(int, void *);
  int state_complete_frame_read(int, void *);

  int64_t               con_id;
  SessionHandler        session_handler;
  NetVConnection *      client_vc;
  MIOBuffer *           read_buffer;
  IOBufferReader *      sm_reader;
  MIOBuffer *           write_buffer;
  IOBufferReader *      sm_writer;
  Http2FrameHeader      current_hdr;
  Http2ConnectionState  connection_state;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;

#endif // __HTTP2_CLIENT_SESSION_H__
