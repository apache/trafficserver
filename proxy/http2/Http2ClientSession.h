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
#include "Milestones.h"

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
#define HTTP2_SESSION_EVENT_REENABLE (HTTP2_SESSION_EVENTS_START + 7)

enum class Http2SessionCod : int {
  NOT_PROVIDED,
  HIGH_ERROR_RATE,
};

enum class Http2SsnMilestone {
  OPEN = 0,
  CLOSE,
  LAST_ENTRY,
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
  // Input frame constructor
  Http2Frame(const Http2FrameHeader &h, IOBufferReader *r) : hdr(h), ioreader(r) {}
  // Output frame contstructor
  Http2Frame(Http2FrameType type, Http2StreamId streamid, uint8_t flags, int index) : hdr({0, (uint8_t)type, flags, streamid})
  {
    alloc(index);
  }
  ~Http2Frame() {}

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
    this->ioblock->fill(HTTP2_FRAME_HEADER_LEN);
  }

  // Return the writeable buffer space for frame payload
  IOVec
  write()
  {
    return make_iovec(this->ioblock->end(), this->ioblock->write_avail());
  }

  void
  finalize(size_t nbytes)
  {
    this->set_payload_size(nbytes);
    if (this->ioblock) {
      this->ioblock->fill(nbytes);
    }
  }

  void
  xmit(MIOBuffer *out_iobuffer)
  {
    // Write frame header to the frame_buffer
    http2_write_frame_header(hdr, make_iovec(this->ioblock->start(), HTTP2_FRAME_HEADER_LEN));

    // Write the whole block to the output buffer
    if (ioblock) {
      int block_size = ioblock->read_avail();
      if (block_size > 0) {
        out_iobuffer->append_block(this->ioblock.get());

        // payload should already have been written unless it doesn't all
        // fit in the single block
        if (out_reader) {
          out_iobuffer->write(out_reader, hdr.length + HTTP2_FRAME_HEADER_LEN - block_size);
          out_reader->consume(hdr.length + HTTP2_FRAME_HEADER_LEN - block_size);
        }
      }
    }
  }

  int64_t
  size()
  {
    return HTTP2_FRAME_HEADER_LEN + hdr.length;
  }

  void
  set_payload_size(size_t length)
  {
    hdr.length = length;
  }

  void
  add_reader(IOBufferReader *reader)
  {
    out_reader = reader;
  }

  // noncopyable
  Http2Frame(Http2Frame &) = delete;
  Http2Frame &operator=(const Http2Frame &) = delete;

private:
  Http2FrameHeader hdr;       // frame header
  Ptr<IOBufferBlock> ioblock; // frame payload
  IOBufferReader *out_reader = nullptr;
  IOBufferReader *ioreader   = nullptr;
};

class Http2ClientSession : public ProxySession
{
public:
  using super          = ProxySession; ///< Parent type.
  using SessionHandler = int (Http2ClientSession::*)(int, void *);

  Http2ClientSession();

  /////////////////////
  // Methods

  // Implement VConnection interface
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  // Implement ProxySession interface
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;
  void start() override;
  void destroy() override;
  void release(ProxyTransaction *trans) override;
  void free() override;

  // more methods
  void write_reenable();

  ////////////////////
  // Accessors
  NetVConnection *get_netvc() const override;
  sockaddr const *get_client_addr() override;
  sockaddr const *get_local_addr() override;
  int get_transact_count() const override;
  const char *get_protocol_string() const override;
  int populate_protocol(std::string_view *result, int size) const override;
  const char *protocol_contains(std::string_view prefix) const override;
  void increment_current_active_client_connections_stat() override;
  void decrement_current_active_client_connections_stat() override;

  void set_upgrade_context(HTTPHdr *h);
  const Http2UpgradeContext &get_upgrade_context() const;
  void set_dying_event(int event);
  int get_dying_event() const;
  bool ready_to_free() const;
  bool is_recursing() const;
  void set_half_close_local_flag(bool flag);
  bool get_half_close_local_flag() const;
  bool is_url_pushed(const char *url, int url_len);
  void add_url_to_pushed_table(const char *url, int url_len);
  int64_t write_buffer_size();

  // Record history from Http2ConnectionState
  void remember(const SourceLocation &location, int event, int reentrant = NO_REENTRANT);

  // noncopyable
  Http2ClientSession(Http2ClientSession &) = delete;
  Http2ClientSession &operator=(const Http2ClientSession &) = delete;

  ///////////////////
  // Variables
  Http2ConnectionState connection_state;

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

  bool _should_do_something_else();

  int64_t total_write_len        = 0;
  SessionHandler session_handler = nullptr;
  NetVConnection *client_vc      = nullptr;
  MIOBuffer *read_buffer         = nullptr;
  IOBufferReader *_reader        = nullptr;
  MIOBuffer *write_buffer        = nullptr;
  IOBufferReader *sm_writer      = nullptr;
  Http2FrameHeader current_hdr   = {0, 0, 0, 0};

  IpEndpoint cached_client_addr;
  IpEndpoint cached_local_addr;

  History<HISTORY_DEFAULT_SIZE> _history;
  Milestones<Http2SsnMilestone, static_cast<size_t>(Http2SsnMilestone::LAST_ENTRY)> _milestones;

  // For Upgrade: h2c
  Http2UpgradeContext upgrade_context;

  VIO *write_vio                 = nullptr;
  int dying_event                = 0;
  bool kill_me                   = false;
  Http2SessionCod cause_of_death = Http2SessionCod::NOT_PROVIDED;
  bool half_close_local          = false;
  int recursion                  = 0;

  std::unordered_set<std::string> h2_pushed_urls;

  Event *_reenable_event = nullptr;
  int _n_frame_read      = 0;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;
