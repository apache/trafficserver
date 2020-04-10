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
#include "Http2Frame.h"
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
  Http2UpgradeContext() : req_header(nullptr) {}
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

class Http2ClientSession : public ProxyClientSession
{
public:
  using super          = ProxyClientSession; ///< Parent type.
  using SessionHandler = int (Http2ClientSession::*)(int, void *);

  Http2ClientSession();

  /////////////////////
  // Methods

  // Implement VConnection interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = nullptr) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = nullptr,
                   bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  // Implement ProxyClientSession interface.
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader, bool backdoor) override;
  void start() override;
  void destroy() override;
  void release(ProxyClientTransaction *trans) override;
  void free() override;

  // more methods
  void write_reenable();
  int64_t xmit(const Http2TxFrame &frame);

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

  // Record history from Http2ConnectionState
  void remember(const SourceLocation &location, int event, int reentrant = NO_REENTRANT);

  int64_t write_avail();

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
  IOBufferReader *sm_reader      = nullptr;
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

  InkHashTable *h2_pushed_urls = nullptr;
  uint32_t h2_pushed_urls_size = 0;

  Event *_reenable_event = nullptr;
  int _n_frame_read      = 0;
};

extern ClassAllocator<Http2ClientSession> http2ClientSessionAllocator;

///////////////////////////////////////////////
// INLINE

inline const Http2UpgradeContext &
Http2ClientSession::get_upgrade_context() const
{
  return upgrade_context;
}

inline bool
Http2ClientSession::ready_to_free() const
{
  return kill_me;
}

inline void
Http2ClientSession::set_dying_event(int event)
{
  dying_event = event;
}

inline int
Http2ClientSession::get_dying_event() const
{
  return dying_event;
}

inline bool
Http2ClientSession::is_recursing() const
{
  return recursion > 0;
}

inline bool
Http2ClientSession::get_half_close_local_flag() const
{
  return half_close_local;
}

inline bool
Http2ClientSession::is_url_pushed(const char *url, int url_len)
{
  char *dup_url            = ats_strndup(url, url_len);
  InkHashTableEntry *entry = ink_hash_table_lookup_entry(h2_pushed_urls, dup_url);
  ats_free(dup_url);
  return entry != nullptr;
}
