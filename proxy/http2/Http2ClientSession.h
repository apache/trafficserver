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

#pragma once

#include "HTTP2.h"
#include "Plugin.h"
#include "ProxyClientSession.h"
#include "Http2ConnectionState.h"
#include <ts/MemView.h>
#include <ts/ink_inet.h>

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
  typedef int (Http2ClientSession::*SessionHandler)(int, void *);

  Http2ClientSession();

  // Implement ProxyClientSession interface.
  void start() override;
  void destroy() override;
  void free() override;
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor) override;

  bool
  ready_to_free() const
  {
    return kill_me;
  }

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = NULL, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  NetVConnection *
  get_netvc() const override
  {
    return client_vc;
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

  int
  get_transact_count() const override
  {
    return connection_state.get_stream_requests();
  }

  void
  release(ProxyClientTransaction *trans) override
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

  const char *
  get_protocol_string() const override
  {
    return "http/2";
  }

  virtual int
  populate_protocol(ts::StringView *result, int size) const override
  {
    int retval = 0;
    if (size > retval) {
      result[retval++] = IP_PROTO_TAG_HTTP_2_0;
      if (size > retval) {
        retval += super::populate_protocol(result + retval, size - retval);
      }
    }
    return retval;
  }

  virtual const char *
  protocol_contains(ts::StringView prefix) const override
  {
    const char *retval = nullptr;

    if (prefix.size() <= IP_PROTO_TAG_HTTP_2_0.size() && strncmp(IP_PROTO_TAG_HTTP_2_0.ptr(), prefix.ptr(), prefix.size()) == 0) {
      retval = IP_PROTO_TAG_HTTP_2_0.ptr();
    } else {
      retval = super::protocol_contains(prefix);
    }
    return retval;
  }

  void set_half_close_local_flag(bool flag);
  bool
  get_half_close_local_flag() const
  {
    return half_close_local;
  }

  bool
  is_url_pushed(const char *url, int url_len)
  {
    char *dup_url            = ats_strndup(url, url_len);
    InkHashTableEntry *entry = ink_hash_table_lookup_entry(h2_pushed_urls, dup_url);
    ats_free(dup_url);
    return entry != nullptr;
  }

  void
  add_url_to_pushed_table(const char *url, int url_len)
  {
    if (h2_pushed_urls_size < Http2::push_diary_size) {
      char *dup_url = ats_strndup(url, url_len);
      ink_hash_table_insert(h2_pushed_urls, dup_url, nullptr);
      h2_pushed_urls_size++;
      ats_free(dup_url);
    }
  }

  int64_t
  write_buffer_size()
  {
    return write_buffer->max_read_avail();
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

  InkHashTable *h2_pushed_urls = nullptr;
  uint32_t h2_pushed_urls_size = 0;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;
