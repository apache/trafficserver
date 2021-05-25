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

/**
   @startuml
   title HTTP/2 Session Handler - state of reading HTTP/2 frame
   hide empty description

   [*]                           --> state_read_connection_preface : start()
   state_read_connection_preface --> state_start_frame_read        : receive connection preface
   state_start_frame_read        --> state_start_frame_read        : do_complete_frame_read()
   state_start_frame_read        --> state_complete_frame_read     : reading HTTP/2 frame is halfway but no data in the buffer
   state_complete_frame_read     --> state_start_frame_read        : do_complete_frame_read()

   @enduml
 */
class Http2ClientSession : public ProxySession
{
public:
  using super          = ProxySession; ///< Parent type.
  using SessionHandler = int (Http2ClientSession::*)(int, void *);

  Http2ClientSession();

  /////////////////////
  // Methods

  // Implement VConnection interface
  void do_io_close(int lerrno = -1) override;

  // Implement ProxySession interface
  void new_connection(NetVConnection *new_vc, MIOBuffer *iobuf, IOBufferReader *reader) override;
  void start() override;
  void destroy() override;
  void release(ProxyTransaction *trans) override;
  void free() override;

  // more methods
  void write_reenable();
  int64_t xmit(const Http2TxFrame &frame, bool flush = true);
  void flush();

  ////////////////////
  // Accessors
  sockaddr const *get_remote_addr() const override;
  sockaddr const *get_local_addr() override;
  int get_transact_count() const override;
  const char *get_protocol_string() const override;
  int populate_protocol(std::string_view *result, int size) const override;
  const char *protocol_contains(std::string_view prefix) const override;
  HTTPVersion get_version(HTTPHdr &hdr) const override;
  void increment_current_active_connections_stat() override;
  void decrement_current_active_connections_stat() override;

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

  // SessionHandler(s) - state of reading frame
  int state_read_connection_preface(int, void *);
  int state_start_frame_read(int, void *);
  int state_complete_frame_read(int, void *);

  // state_start_frame_read and state_complete_frame_read are set up as session event handler.
  // Both feed into do_process_frame_read which may iterate if there are multiple frames ready on the wire
  int do_process_frame_read(int event, VIO *vio, bool inside_frame);
  int do_start_frame_read(Http2ErrorCode &ret_error);
  int do_complete_frame_read();

  bool _should_do_something_else();

  ////////
  // Variables
  SessionHandler session_handler = nullptr;

  MIOBuffer *read_buffer              = nullptr;
  IOBufferReader *_read_buffer_reader = nullptr;

  VIO *write_vio                       = nullptr;
  MIOBuffer *write_buffer              = nullptr;
  IOBufferReader *_write_buffer_reader = nullptr;

  Http2FrameHeader current_hdr        = {0, 0, 0, 0};
  uint32_t _write_size_threshold      = 0;
  uint32_t _write_time_threshold      = 100;
  ink_hrtime _write_buffer_last_flush = 0;

  IpEndpoint cached_client_addr;
  IpEndpoint cached_local_addr;

  History<HISTORY_DEFAULT_SIZE> _history;
  Milestones<Http2SsnMilestone, static_cast<size_t>(Http2SsnMilestone::LAST_ENTRY)> _milestones;

  int dying_event                = 0;
  bool kill_me                   = false;
  Http2SessionCod cause_of_death = Http2SessionCod::NOT_PROVIDED;
  bool half_close_local          = false;
  int recursion                  = 0;

  std::unordered_set<std::string> *_h2_pushed_urls = nullptr;

  Event *_reenable_event = nullptr;
  int _n_frame_read      = 0;

  uint32_t _pending_sending_data_size = 0;

  int64_t read_from_early_data   = 0;
  bool cur_frame_from_early_data = false;
};

extern ClassAllocator<Http2ClientSession, true> http2ClientSessionAllocator;

///////////////////////////////////////////////
// INLINE

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
  if (_h2_pushed_urls == nullptr) {
    return false;
  }

  return _h2_pushed_urls->find(url) != _h2_pushed_urls->end();
}
