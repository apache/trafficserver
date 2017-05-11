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
#include "Plugin.h"
#include "ProxyClientSession.h"
#include "Http2ConnectionState.h"

// Name                       Edata                 Description
// HTTP2_SESSION_EVENT_INIT   Http2ClientSession *  HTTP/2 session is born
// HTTP2_SESSION_EVENT_FINI   Http2ClientSession *  HTTP/2 session is ended
// HTTP2_SESSION_EVENT_RECV   Http2Frame *          Received a frame
// HTTP2_SESSION_EVENT_XMIT   Http2Frame *          Send this frame

#define HTTP2_SESSION_EVENT_INIT (HTTP2_SESSION_EVENTS_START + 1)
#define HTTP2_SESSION_EVENT_FINI (HTTP2_SESSION_EVENTS_START + 2)
#define HTTP2_SESSION_EVENT_RECV (HTTP2_SESSION_EVENTS_START + 3)
#define HTTP2_SESSION_EVENT_XMIT (HTTP2_SESSION_EVENTS_START + 4)

size_t const HTTP2_HEADER_BUFFER_SIZE_INDEX = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

// To support Upgrade: h2c
struct Http2UpgradeContext {
  Http2UpgradeContext() : req_header(NULL) {}
  ~Http2UpgradeContext()
  {
    if (req_header) {
      req_header->clear();
      delete req_header;
    }
  }

  // Modified request header
  HTTPHdr *req_header;

  // Decoded HTTP2-Settings Header Field
  Http2ConnectionSettings client_settings;
};

class Http2Frame
{
public:
  Http2Frame(const Http2FrameHeader &h, IOBufferReader *r)
  {
    this->hdr      = h;
    this->ioreader = r;
  }

  Http2Frame(Http2FrameType type, Http2StreamId streamid, uint8_t flags)
  {
    this->hdr      = {0, (uint8_t)type, flags, streamid};
    this->ioreader = NULL;
  }

  IOBufferReader *
  reader() const
  {
    return ioreader;
  }

  const Http2FrameHeader &
  header() const
  {
    return this->hdr;
  }

  // Allocate an IOBufferBlock for payload of this frame.
  void
  alloc(int index)
  {
    this->ioblock = new_IOBufferBlock();
    this->ioblock->alloc(index);
  }

  // Return the writeable buffer space for frame payload
  IOVec
  write()
  {
    return make_iovec(this->ioblock->end(), this->ioblock->write_avail());
  }

  // Once the frame has been serialized, update the payload length of frame header.
  void
  finalize(size_t nbytes)
  {
    if (this->ioblock) {
      ink_assert((int64_t)nbytes <= this->ioblock->write_avail());
      this->ioblock->fill(nbytes);

      this->hdr.length = this->ioblock->size();
    }
  }

  void
  xmit(MIOBuffer *iobuffer)
  {
    // Write frame header
    uint8_t buf[HTTP2_FRAME_HEADER_LEN];
    http2_write_frame_header(hdr, make_iovec(buf));
    iobuffer->write(buf, sizeof(buf));

    // Write frame payload
    // It could be empty (e.g. SETTINGS frame with ACK flag)
    if (ioblock && ioblock->read_avail() > 0) {
      iobuffer->append_block(this->ioblock.get());
    }
  }

  int64_t
  size()
  {
    if (ioblock) {
      return HTTP2_FRAME_HEADER_LEN + ioblock->size();
    } else {
      return HTTP2_FRAME_HEADER_LEN;
    }
  }

private:
  Http2Frame(Http2Frame &);                  // noncopyable
  Http2Frame &operator=(const Http2Frame &); // noncopyable

  Http2FrameHeader hdr;       // frame header
  Ptr<IOBufferBlock> ioblock; // frame payload
  IOBufferReader *ioreader;
};

class Http2ClientSession : public ProxyClientSession
{
public:
  typedef ProxyClientSession super; ///< Parent type.
  Http2ClientSession();

  typedef int (Http2ClientSession::*SessionHandler)(int, void *);

  // Implement ProxyClientSession interface.
  void start();
  virtual void destroy();
  virtual void free();
  virtual void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor);

  bool
  ready_to_free() const
  {
    return kill_me;
  }

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0);
  VIO *do_io_write(Continuation *c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false);
  void do_io_close(int lerrno = -1);
  void do_io_shutdown(ShutdownHowTo_t howto);
  void reenable(VIO *vio);

  virtual NetVConnection *
  get_netvc() const
  {
    return client_vc;
  }

  virtual void
  release_netvc()
  {
    // Make sure the vio's are also released to avoid later surprises in inactivity timeout
    if (client_vc) {
      client_vc->do_io_read(NULL, 0, NULL);
      client_vc->do_io_write(NULL, 0, NULL);
      client_vc->set_action(NULL);
    }
  }

  sockaddr const *
  get_client_addr()
  {
    return client_vc->get_remote_addr();
  }

  void
  write_reenable()
  {
    write_vio->reenable();
  }

  void set_upgrade_context(HTTPHdr *h);

  const Http2UpgradeContext &
  get_upgrade_context() const
  {
    return upgrade_context;
  }

  virtual int
  get_transact_count() const
  {
    return connection_state.get_stream_requests();
  }

  virtual void
  release(ProxyClientTransaction *trans)
  {
  }

  Http2ConnectionState connection_state;
  void
  set_dying_event(int event)
  {
    dying_event = event;
  }

  int
  get_dying_event() const
  {
    return dying_event;
  }

  bool
  is_recursing() const
  {
    return recursion > 0;
  }

  virtual const char *
  get_protocol_string() const
  {
    return "http/2";
  }

  virtual int
  populate_protocol(const char **result, int size) const
  {
    int retval = 0;
    if (size > 0) {
      result[0] = TS_PROTO_TAG_HTTP_2_0;
      retval    = 1;
      if (size > 1) {
        retval += super::populate_protocol(result + 1, size - 1);
      }
    }
    return retval;
  }

  virtual const char *
  protocol_contains(const char *tag_prefix) const
  {
    const char *retval   = NULL;
    unsigned int tag_len = strlen(tag_prefix);
    if (tag_len <= strlen(TS_PROTO_TAG_HTTP_2_0) && strncmp(tag_prefix, TS_PROTO_TAG_HTTP_2_0, tag_len) == 0) {
      retval = TS_PROTO_TAG_HTTP_2_0;
    } else {
      retval = super::protocol_contains(tag_prefix);
    }
    return retval;
  }

  void set_half_close_local_flag(bool flag);
  bool
  get_half_close_local_flag() const
  {
    return half_close_local;
  }

private:
  Http2ClientSession(Http2ClientSession &);                  // noncopyable
  Http2ClientSession &operator=(const Http2ClientSession &); // noncopyable

  int main_event_handler(int, void *);

  int state_read_connection_preface(int, void *);
  int state_start_frame_read(int, void *);
  int do_start_frame_read(Http2ErrorCode &ret_error);
  int state_complete_frame_read(int, void *);
  int do_complete_frame_read();
  // state_start_frame_read and state_complete_frame_read are set up as
  // event handler.  Both feed into state_process_frame_read which may iterate
  // if there are multiple frames ready on the wire
  int state_process_frame_read(int event, VIO *vio, bool inside_frame);

  int64_t total_write_len;
  SessionHandler session_handler;
  NetVConnection *client_vc;
  MIOBuffer *read_buffer;
  IOBufferReader *sm_reader;
  MIOBuffer *write_buffer;
  IOBufferReader *sm_writer;
  Http2FrameHeader current_hdr;

  // For Upgrade: h2c
  Http2UpgradeContext upgrade_context;

  VIO *write_vio;
  int dying_event;
  bool kill_me;
  bool half_close_local;
  int recursion;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;

#endif // __HTTP2_CLIENT_SESSION_H__
