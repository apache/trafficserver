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

#define HTTP2_SESSION_EVENT_GRACEFUL_SHUTDOWN (HTTP2_SESSION_EVENTS_START + 1)
#define HTTP2_SESSION_EVENT_REENABLE (HTTP2_SESSION_EVENTS_START + 2)

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
   title ProxySession Continuation Handler
   hide empty description

   [*]        --> main_event_handler : start()
   main_event_Handler --> state_api_callout : do_api_callout()/handle_api_return()

   @enduml

   @startuml
   title HTTP/2 Session States
   hide empty description

   [*]        --> state_open : start()
   state_open --> state_open : VC_EVENT_READ_READY\lVC_EVENT_READ_COMPLETE\lVC_EVENT_WRITE_READY\lVC_EVENT_WRITE_COMPLETE

   state_open    --> state_aborted : VC_EVENT_EOS\lVC_EVENT_ERROR\lrecv GOAWAY frame with error code
   state_aborted --> state_closed  : all transaction is done

   state_open              --> state_graceful_shutdown : graceful shutdown
   state_graceful_shutdown --> state_closed            : all transaction is done

   state_open   --> state_goaway : VC_EVENT_ACTIVE_TIMEOUT\lVC_EVENT_INACTIVITY_TIMEOUT\lsend GOAWAY frame with error code
   state_goaway --> state_closed : VC_EVENT_WRITE_COMPLETE\l(completed sending GOAWAY frame)

   state_closed --> [*] : destroy()

   @enduml

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
  using SessionState   = int (Http2ClientSession::*)(int, void *);
  using SessionHandler = int (Http2ClientSession::*)(int, void *);

  Http2ClientSession();

  /////////////////////
  // Methods

  // Implement VConnection interface
  VIO *do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf) override;
  VIO *do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *reader, bool owner) override;
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

  bool is_url_pushed(const char *url, int url_len) const;
  void add_url_to_pushed_table(const char *url, int url_len);

  // Record history from Http2ConnectionState
  void remember(const SourceLocation &location, int event, int reentrant = NO_REENTRANT);

  int64_t write_avail();

  void critical_error(Http2ErrorCode err);
  void add_to_keep_alive_queue();

  bool is_state_open() const;
  bool is_state_closed() const;
  bool is_state_readable() const;
  bool is_state_writable() const;

  // noncopyable
  Http2ClientSession(Http2ClientSession &) = delete;
  Http2ClientSession &operator=(const Http2ClientSession &) = delete;

  ///////////////////
  // Variables
  Http2ConnectionState connection_state;

private:
  // Continuation Handler
  int main_event_handler(int event, void *edata);

  // Session States
  int state_open(int event, void *edata);
  int state_aborted(int event, void *edata);
  int state_goaway(int event, void *edata);
  int state_graceful_shutdown(int event, void *edata);
  int state_closed(int event, void *edata);

  // Session Handler - state of reading frame
  int state_read_connection_preface(int, void *);
  int state_start_frame_read(int, void *);
  int state_complete_frame_read(int, void *);

  // state_start_frame_read and state_complete_frame_read are set up as session event handler.
  // Both feed into do_process_frame_read which may iterate if there are multiple frames ready on the wire
  int do_process_frame_read(int event, VIO *vio, bool inside_frame);
  int do_start_frame_read(Http2ErrorCode &ret_error);
  int do_complete_frame_read();

  bool _should_do_something_else();
  bool _should_start_graceful_shutdown();

  void _set_state_goaway(Http2ErrorCode err);
  void _set_state_closed();

  int _handle_ssn_reenable_event(int event, void *edata);
  int _handle_vc_event(int event, void *edata);

  ////////
  // Variables
  SessionState session_state     = nullptr;
  SessionHandler session_handler = nullptr;

  VIO *read_vio                       = nullptr;
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
  Http2SessionCod cause_of_death = Http2SessionCod::NOT_PROVIDED;
  int recursion                  = 0;

  std::unordered_set<std::string> *_h2_pushed_urls = nullptr;

  Event *_graceful_shutdown_event = nullptr;
  Event *_reenable_event          = nullptr;
  int _n_frame_read               = 0;

  uint32_t _pending_sending_data_size = 0;

  int64_t read_from_early_data   = 0;
  bool cur_frame_from_early_data = false;
};

extern ClassAllocator<Http2ClientSession, true> http2ClientSessionAllocator;

///////////////////////////////////////////////
// INLINE

inline bool
Http2ClientSession::is_url_pushed(const char *url, int url_len) const
{
  if (_h2_pushed_urls == nullptr) {
    return false;
  }

  return _h2_pushed_urls->find(url) != _h2_pushed_urls->end();
}
