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
#include "ProxySession.h"
#include "Http2ConnectionState.h"
#include <string_view>
#include "tscore/ink_inet.h"
#include "tscore/History.h"

// Name                       Edata                 Description
// HTTP2_SESSION_EVENT_INIT   Http2ClientSession *  HTTP/2 session is born
// HTTP2_SESSION_EVENT_FINI   Http2ClientSession *  HTTP/2 session is ended
// HTTP2_SESSION_EVENT_RECV   Http2Frame *          Received a frame
// HTTP2_SESSION_EVENT_XMIT   Http2Frame *          Send this frame

#define HTTP2_SESSION_EVENT_INIT (HTTP2_SESSION_EVENTS_START + 1)
#define HTTP2_SESSION_EVENT_FINI (HTTP2_SESSION_EVENTS_START + 2)
#define HTTP2_SESSION_EVENT_RECV (HTTP2_SESSION_EVENTS_START + 3)
#define HTTP2_SESSION_EVENT_XMIT (HTTP2_SESSION_EVENTS_START + 4)
#define HTTP2_SESSION_EVENT_SHUTDOWN_INIT (HTTP2_SESSION_EVENTS_START + 5)
#define HTTP2_SESSION_EVENT_SHUTDOWN_CONT (HTTP2_SESSION_EVENTS_START + 6)

enum class Http2SessionCod : int {
  NOT_PROVIDED,
  HIGH_ERROR_RATE,
};

size_t const HTTP2_HEADER_BUFFER_SIZE_INDEX = CLIENT_CONNECTION_FIRST_READ_BUFFER_SIZE_INDEX;

// To support Upgrade: h2c
struct Http2UpgradeContext {
  Http2UpgradeContext() {}
  ~Http2UpgradeContext()
  {
    if (req_header) {
      req_header->clear();
      delete req_header;
    }
  }

  // Modified request header
  HTTPHdr *req_header = nullptr;

  // Decoded HTTP2-Settings Header Field
  Http2ConnectionSettings client_settings;
};

class Http2Frame
{
public:
  Http2Frame(const Http2FrameHeader &h, IOBufferReader *r) : hdr(h), ioreader(r) {}
  Http2Frame(Http2FrameType type, Http2StreamId streamid, uint8_t flags) : hdr({0, (uint8_t)type, flags, streamid}) {}

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

  // noncopyable
  Http2Frame(Http2Frame &) = delete;
  Http2Frame &operator=(const Http2Frame &) = delete;

private:
  Http2FrameHeader hdr;       // frame header
  Ptr<IOBufferBlock> ioblock; // frame payload
  IOBufferReader *ioreader = nullptr;
};

class Http2ClientSession : public ProxySession
{
public:
  typedef ProxySession super; ///< Parent type.
  typedef int (Http2ClientSession::*SessionHandler)(int, void *);

  Http2ClientSession();

  // Implement ProxySession interface.
  void start() override;
  void destroy() override;
  void free() override;
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;

  bool
  ready_to_free() const
  {
    return kill_me;
  }

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  NetVConnection *
  get_netvc() const override
  {
    return client_vc;
  }

  sockaddr const *
  get_client_addr() override
  {
    return client_vc ? client_vc->get_remote_addr() : &cached_client_addr.sa;
  }

  sockaddr const *
  get_local_addr() override
  {
    return client_vc ? client_vc->get_local_addr() : &cached_local_addr.sa;
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
  release(ProxyTransaction *trans) override
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

  int
  populate_protocol(std::string_view *result, int size) const override
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

  const char *
  protocol_contains(std::string_view prefix) const override
  {
    const char *retval = nullptr;

    if (prefix.size() <= IP_PROTO_TAG_HTTP_2_0.size() && strncmp(IP_PROTO_TAG_HTTP_2_0.data(), prefix.data(), prefix.size()) == 0) {
      retval = IP_PROTO_TAG_HTTP_2_0.data();
    } else {
      retval = super::protocol_contains(prefix);
    }
    return retval;
  }

  void increment_current_active_client_connections_stat() override;
  void decrement_current_active_client_connections_stat() override;

  void set_half_close_local_flag(bool flag);
  bool
  get_half_close_local_flag() const
  {
    return half_close_local;
  }

  bool
  is_url_pushed(const char *url, int url_len)
  {
    return h2_pushed_urls.find(url) != h2_pushed_urls.end();
  }

  void
  add_url_to_pushed_table(const char *url, int url_len)
  {
    if (h2_pushed_urls.size() < Http2::push_diary_size) {
      h2_pushed_urls.emplace(url);
    }
  }

  int64_t
  write_buffer_size()
  {
    return write_buffer->max_read_avail();
  }

  // Record history from Http2ConnectionState
  void remember(const SourceLocation &location, int event, int reentrant = NO_REENTRANT);

  // noncopyable
  Http2ClientSession(Http2ClientSession &) = delete;
  Http2ClientSession &operator=(const Http2ClientSession &) = delete;

private:
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

  int64_t total_write_len        = 0;
  SessionHandler session_handler = nullptr;
  NetVConnection *client_vc      = nullptr;
  MIOBuffer *read_buffer         = nullptr;
  IOBufferReader *sm_reader      = nullptr;
  MIOBuffer *write_buffer        = nullptr;
  IOBufferReader *sm_writer      = nullptr;
  Http2FrameHeader current_hdr   = {0, 0, 0, 0};

  IpEndpoint cached_client_addr;
  IpEndpoint cached_local_addr;

  History<HISTORY_DEFAULT_SIZE> _history;

  // For Upgrade: h2c
  Http2UpgradeContext upgrade_context;

  VIO *write_vio                 = nullptr;
  int dying_event                = 0;
  bool kill_me                   = false;
  Http2SessionCod cause_of_death = Http2SessionCod::NOT_PROVIDED;
  bool half_close_local          = false;
  int recursion                  = 0;

  std::unordered_set<std::string> h2_pushed_urls;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;
