/** @file

  Http2ConnectionState.

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

#include <atomic>
#include "HTTP2.h"
#include "HPACK.h"
#include "Http2Stream.h"
#include "Http2DependencyTree.h"

class Http2ClientSession;

enum class Http2SendDataFrameResult {
  NO_ERROR = 0,
  NO_WINDOW,
  NO_PAYLOAD,
  ERROR,
  DONE,
};

enum Http2ShutdownState { HTTP2_SHUTDOWN_NONE, HTTP2_SHUTDOWN_NOT_INITIATED, HTTP2_SHUTDOWN_INITIATED, HTTP2_SHUTDOWN_IN_PROGRESS };

class Http2ConnectionSettings
{
public:
  Http2ConnectionSettings()
  {
    // 6.5.2.  Defined SETTINGS Parameters. These should generally not be
    // modified,
    // only if the protocol changes should these change.
    settings[indexof(HTTP2_SETTINGS_ENABLE_PUSH)]            = HTTP2_ENABLE_PUSH;
    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = HTTP2_MAX_CONCURRENT_STREAMS;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)]    = HTTP2_INITIAL_WINDOW_SIZE;
    settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)]         = HTTP2_MAX_FRAME_SIZE;
    settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)]      = HTTP2_HEADER_TABLE_SIZE;
    settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)]   = HTTP2_MAX_HEADER_LIST_SIZE;
  }

  void
  settings_from_configs()
  {
    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = Http2::max_concurrent_streams_in;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)]    = Http2::initial_window_size;
    settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)]         = Http2::max_frame_size;
    settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)]      = Http2::header_table_size;
    settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)]   = Http2::max_header_list_size;
  }

  unsigned
  get(Http2SettingsIdentifier id) const
  {
    if (0 < id && id < HTTP2_SETTINGS_MAX) {
      return this->settings[indexof(id)];
    } else {
      ink_assert(!"Bad Settings Identifier");
    }

    return 0;
  }

  unsigned
  set(Http2SettingsIdentifier id, unsigned value)
  {
    if (0 < id && id < HTTP2_SETTINGS_MAX) {
      return this->settings[indexof(id)] = value;
    } else {
      // Do nothing - 6.5.2 Unsupported parameters MUST be ignored
    }

    return 0;
  }

private:
  // Settings ID is 1-based, so convert it to a 0-based index.
  static unsigned
  indexof(Http2SettingsIdentifier id)
  {
    ink_assert(0 < id && id < HTTP2_SETTINGS_MAX);

    return id - 1;
  }

  unsigned settings[HTTP2_SETTINGS_MAX - 1];
};

// Http2ConnectionState
//
// Capture the semantics of a HTTP/2 connection. The client session captures the
// frame layer, and the
// connection state captures the connection-wide state.

class Http2ConnectionState : public Continuation
{
public:
  Http2ConnectionState() : stream_list() { SET_HANDLER(&Http2ConnectionState::main_event_handler); }

  ProxyError rx_error_code;
  ProxyError tx_error_code;
  Http2ClientSession *ua_session   = nullptr;
  HpackHandle *local_hpack_handle  = nullptr;
  HpackHandle *remote_hpack_handle = nullptr;
  DependencyTree *dependency_tree  = nullptr;

  // Settings.
  Http2ConnectionSettings server_settings;
  Http2ConnectionSettings client_settings;

  void
  init()
  {
    local_hpack_handle  = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
    remote_hpack_handle = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
    dependency_tree     = new DependencyTree(Http2::max_concurrent_streams_in);
  }

  void
  destroy()
  {
    if (shutdown_cont_event) {
      shutdown_cont_event->cancel();
    }
    cleanup_streams();

    delete local_hpack_handle;
    local_hpack_handle = nullptr;
    delete remote_hpack_handle;
    remote_hpack_handle = nullptr;
    delete dependency_tree;
    dependency_tree  = nullptr;
    this->ua_session = nullptr;

    if (fini_event) {
      fini_event->cancel();
    }
    if (zombie_event) {
      zombie_event->cancel();
    }
    // release the mutex after the events are cancelled and sessions are destroyed.
    mutex = nullptr; // magic happens - assigning to nullptr frees the ProxyMutex
  }

  // Event handlers
  int main_event_handler(int, void *);
  int state_closed(int, void *);

  // Stream control interfaces
  Http2Stream *create_stream(Http2StreamId new_id, Http2Error &error);
  Http2Stream *find_stream(Http2StreamId id) const;
  void restart_streams();
  bool delete_stream(Http2Stream *stream);
  void release_stream(Http2Stream *stream);
  void cleanup_streams();

  void update_initial_rwnd(Http2WindowSize new_size);

  Http2StreamId
  get_latest_stream_id_in() const
  {
    return latest_streamid_in;
  }

  Http2StreamId
  get_latest_stream_id_out() const
  {
    return latest_streamid_out;
  }

  int
  get_stream_requests() const
  {
    return stream_requests;
  }

  void
  increment_stream_requests()
  {
    stream_requests++;
  }

  // Continuated header decoding
  Http2StreamId
  get_continued_stream_id() const
  {
    return continued_stream_id;
  }
  void
  set_continued_stream_id(Http2StreamId stream_id)
  {
    continued_stream_id = stream_id;
  }
  void
  clear_continued_stream_id()
  {
    continued_stream_id = 0;
  }

  uint32_t
  get_client_stream_count() const
  {
    return client_streams_in_count;
  }

  double
  get_stream_error_rate() const
  {
    int total = get_stream_requests();
    if (total > 0) {
      return (double)stream_error_count / (double)total;
    } else {
      return 0;
    }
  }

  Http2ErrorCode
  get_shutdown_reason() const
  {
    return shutdown_reason;
  }

  // HTTP/2 frame sender
  void schedule_stream(Http2Stream *stream);
  void send_data_frames_depends_on_priority();
  void send_data_frames(Http2Stream *stream);
  Http2SendDataFrameResult send_a_data_frame(Http2Stream *stream, size_t &payload_length);
  void send_headers_frame(Http2Stream *stream);
  void send_push_promise_frame(Http2Stream *stream, URL &url, const MIMEField *accept_encoding);
  void send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_settings_frame(const Http2ConnectionSettings &new_settings);
  void send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data);
  void send_goaway_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_window_update_frame(Http2StreamId id, uint32_t size);

  bool
  is_state_closed() const
  {
    return ua_session == nullptr || fini_received;
  }

  bool
  is_recursing() const
  {
    return recursion > 0;
  }

  bool
  is_valid_streamid(Http2StreamId id) const
  {
    if (http2_is_client_streamid(id)) {
      return id <= get_latest_stream_id_in();
    } else {
      return id <= get_latest_stream_id_out();
    }
  }

  Http2ShutdownState
  get_shutdown_state() const
  {
    return shutdown_state;
  }

  void
  set_shutdown_state(Http2ShutdownState state, Http2ErrorCode reason = Http2ErrorCode::HTTP2_ERROR_NO_ERROR)
  {
    shutdown_state  = state;
    shutdown_reason = reason;
  }

  // noncopyable
  Http2ConnectionState(const Http2ConnectionState &) = delete;
  Http2ConnectionState &operator=(const Http2ConnectionState &) = delete;

  Event *
  get_zombie_event()
  {
    return zombie_event;
  }

  void
  schedule_zombie_event()
  {
    if (Http2::zombie_timeout_in) { // If we have zombie debugging enabled
      if (zombie_event) {
        zombie_event->cancel();
      }
      zombie_event = this_ethread()->schedule_in(this, HRTIME_SECONDS(Http2::zombie_timeout_in));
    }
  }

  void increment_received_settings_count(uint32_t count);
  uint32_t get_received_settings_count();
  void increment_received_settings_frame_count();
  uint32_t get_received_settings_frame_count();
  void increment_received_ping_frame_count();
  uint32_t get_received_ping_frame_count();
  void increment_received_priority_frame_count();
  uint32_t get_received_priority_frame_count();

  ssize_t client_rwnd() const;
  Http2ErrorCode increment_client_rwnd(size_t amount);
  Http2ErrorCode decrement_client_rwnd(size_t amount);
  ssize_t server_rwnd() const;
  Http2ErrorCode increment_server_rwnd(size_t amount);
  Http2ErrorCode decrement_server_rwnd(size_t amount);

private:
  unsigned _adjust_concurrent_stream();

  // NOTE: 'stream_list' has only active streams.
  //   If given Stream Identifier is not found in stream_list and it is less
  //   than or equal to latest_streamid_in, the state of Stream
  //   is CLOSED.
  //   If given Stream Identifier is not found in stream_list and it is greater
  //   than latest_streamid_in, the state of Stream is IDLE.
  Queue<Http2Stream> stream_list;
  Http2StreamId latest_streamid_in  = 0;
  Http2StreamId latest_streamid_out = 0;
  std::atomic<int> stream_requests  = 0;

  // Counter for current active streams which is started by client
  std::atomic<uint32_t> client_streams_in_count = 0;

  // Counter for current active streams which is started by server
  std::atomic<uint32_t> client_streams_out_count = 0;

  // Counter for current active streams and streams in the process of shutting down
  std::atomic<uint32_t> total_client_streams_count = 0;

  // Counter for stream errors ATS sent
  uint32_t stream_error_count = 0;

  // Connection level window size
  ssize_t _client_rwnd = HTTP2_INITIAL_WINDOW_SIZE;
  ssize_t _server_rwnd = Http2::initial_window_size;

  std::vector<size_t> _recent_rwnd_increment = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int _recent_rwnd_increment_index           = 0;

  // Counter for settings received within last 60 seconds
  // Each item holds a count for 30 seconds.
  uint16_t settings_count[2]            = {0};
  ink_hrtime settings_count_last_update = 0;
  // Counters for frames received within last 60 seconds
  // Each item in an array holds a count for 30 seconds.
  uint16_t settings_frame_count[2]            = {0};
  ink_hrtime settings_frame_count_last_update = 0;
  uint16_t ping_frame_count[2]                = {0};
  ink_hrtime ping_frame_count_last_update     = 0;
  uint16_t priority_frame_count[2]            = {0};
  ink_hrtime priority_frame_count_last_update = 0;

  // NOTE: Id of stream which MUST receive CONTINUATION frame.
  //   - [RFC 7540] 6.2 HEADERS
  //     "A HEADERS frame without the END_HEADERS flag set MUST be followed by a
  //     CONTINUATION frame for the same stream."
  //   - [RFC 7540] 6.10 CONTINUATION
  //     "If the END_HEADERS bit is not set, this frame MUST be followed by
  //     another CONTINUATION frame."
  Http2StreamId continued_stream_id = 0;
  bool _scheduled                   = false;
  bool fini_received                = false;
  int recursion                     = 0;
  Http2ShutdownState shutdown_state = HTTP2_SHUTDOWN_NONE;
  Http2ErrorCode shutdown_reason    = Http2ErrorCode::HTTP2_ERROR_MAX;
  Event *shutdown_cont_event        = nullptr;
  Event *fini_event                 = nullptr;
  Event *zombie_event               = nullptr;
};
