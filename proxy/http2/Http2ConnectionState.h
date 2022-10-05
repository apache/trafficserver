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

#include "NetTimeout.h"

#include "HTTP2.h"
#include "HPACK.h"
#include "Http2Stream.h"
#include "Http2DependencyTree.h"
#include "Http2FrequencyCounter.h"

class Http2CommonSession;
class Http2Frame;

enum class Http2SendDataFrameResult {
  NO_ERROR = 0,
  NO_WINDOW,
  NO_PAYLOAD,
  NOT_WRITE_AVAIL,
  ERROR,
  DONE,
};

enum Http2ShutdownState { HTTP2_SHUTDOWN_NONE, HTTP2_SHUTDOWN_NOT_INITIATED, HTTP2_SHUTDOWN_INITIATED, HTTP2_SHUTDOWN_IN_PROGRESS };

class Http2ConnectionSettings
{
public:
  Http2ConnectionSettings();

  void settings_from_configs();
  unsigned get(Http2SettingsIdentifier id) const;
  unsigned set(Http2SettingsIdentifier id, unsigned value);

private:
  // Settings ID is 1-based, so convert it to a 0-based index.
  static unsigned indexof(Http2SettingsIdentifier id);

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
  Http2ConnectionState();

  // noncopyable
  Http2ConnectionState(const Http2ConnectionState &) = delete;
  Http2ConnectionState &operator=(const Http2ConnectionState &) = delete;

  ProxyError rx_error_code;
  ProxyError tx_error_code;
  Http2CommonSession *session      = nullptr;
  HpackHandle *local_hpack_handle  = nullptr;
  HpackHandle *remote_hpack_handle = nullptr;
  DependencyTree *dependency_tree  = nullptr;
  ActivityCop<Http2Stream> _cop;

  /** Whether the session window is in a shrinking state before we send the
   * first WINDOW_UPDATE frame.
   *
   * Unlike HTTP/2 streams, the HTTP/2 session window has no way to initialize
   * it to a value lower than 65,535. If the initial value is lower than
   * 65,535, the session window will have to shrink while we receive DATA
   * frames without incrementing the window via WINDOW_UPDATE frames. Once the
   * window gets to the desired size, we start maintaining the window via
   * WINDOW_UPDATE frames.
   */
  bool server_rwnd_is_shrinking = false;

  // Settings.
  Http2ConnectionSettings server_settings;
  Http2ConnectionSettings client_settings;

  void init(Http2CommonSession *ssn);
  void send_connection_preface();
  void destroy();
  void rcv_frame(const Http2Frame *frame);

  // Event handlers
  int main_event_handler(int, void *);
  int state_closed(int, void *);

  // Stream control interfaces
  Http2Stream *create_stream(Http2StreamId new_id, Http2Error &error);
  Http2Stream *find_stream(Http2StreamId id) const;
  void restart_streams();
  bool delete_stream(Http2Stream *stream);
  void release_stream();
  void cleanup_streams();
  void restart_receiving(Http2Stream *stream);
  void update_initial_rwnd(Http2WindowSize new_size);

  Http2StreamId get_latest_stream_id_in() const;
  Http2StreamId get_latest_stream_id_out() const;
  int get_stream_requests() const;
  void increment_stream_requests();

  // Continuated header decoding
  Http2StreamId get_continued_stream_id() const;
  void set_continued_stream_id(Http2StreamId stream_id);
  void clear_continued_stream_id();

  uint32_t get_client_stream_count() const;
  void decrement_stream_count();
  double get_stream_error_rate() const;
  Http2ErrorCode get_shutdown_reason() const;

  // HTTP/2 frame sender
  void schedule_stream(Http2Stream *stream);
  void send_data_frames_depends_on_priority();
  void send_data_frames(Http2Stream *stream);
  Http2SendDataFrameResult send_a_data_frame(Http2Stream *stream, size_t &payload_length);
  void send_headers_frame(Http2Stream *stream);
  bool send_push_promise_frame(Http2Stream *stream, URL &url, const MIMEField *accept_encoding);
  void send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_settings_frame(const Http2ConnectionSettings &new_settings);
  void send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data);
  void send_goaway_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_window_update_frame(Http2StreamId id, uint32_t size);

  bool is_state_closed() const;
  bool is_recursing() const;
  bool is_valid_streamid(Http2StreamId id) const;

  Http2ShutdownState get_shutdown_state() const;
  void set_shutdown_state(Http2ShutdownState state, Http2ErrorCode reason = Http2ErrorCode::HTTP2_ERROR_NO_ERROR);

  Event *get_zombie_event();
  void schedule_zombie_event();

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
  ssize_t _server_rwnd = 0;

  std::vector<size_t> _recent_rwnd_increment = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int _recent_rwnd_increment_index           = 0;

  Http2FrequencyCounter _received_settings_counter;
  Http2FrequencyCounter _received_settings_frame_counter;
  Http2FrequencyCounter _received_ping_frame_counter;
  Http2FrequencyCounter _received_priority_frame_counter;

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
  bool in_destroy                   = false;
  int recursion                     = 0;
  Http2ShutdownState shutdown_state = HTTP2_SHUTDOWN_NONE;
  Http2ErrorCode shutdown_reason    = Http2ErrorCode::HTTP2_ERROR_MAX;
  Event *shutdown_cont_event        = nullptr;
  Event *fini_event                 = nullptr;
  Event *zombie_event               = nullptr;
};

///////////////////////////////////////////////
// INLINE
//
inline Http2StreamId
Http2ConnectionState::get_latest_stream_id_in() const
{
  return latest_streamid_in;
}

inline Http2StreamId
Http2ConnectionState::get_latest_stream_id_out() const
{
  return latest_streamid_out;
}

inline int
Http2ConnectionState::get_stream_requests() const
{
  return stream_requests;
}

inline void
Http2ConnectionState::increment_stream_requests()
{
  stream_requests++;
}

// Continuated header decoding
inline Http2StreamId
Http2ConnectionState::get_continued_stream_id() const
{
  return continued_stream_id;
}

inline void
Http2ConnectionState::set_continued_stream_id(Http2StreamId stream_id)
{
  continued_stream_id = stream_id;
}

inline void
Http2ConnectionState::clear_continued_stream_id()
{
  continued_stream_id = 0;
}

inline uint32_t
Http2ConnectionState::get_client_stream_count() const
{
  return client_streams_in_count;
}

inline void
Http2ConnectionState::decrement_stream_count()
{
  --total_client_streams_count;
}

inline double
Http2ConnectionState::get_stream_error_rate() const
{
  int total = get_stream_requests();

  if (static_cast<uint32_t>(total) < Http2::stream_error_sampling_threshold) {
    return 0;
  }

  if (total >= (1 / Http2::stream_error_rate_threshold)) {
    return (double)stream_error_count / (double)total;
  } else {
    return 0;
  }
}

inline Http2ErrorCode
Http2ConnectionState::get_shutdown_reason() const
{
  return shutdown_reason;
}

inline bool
Http2ConnectionState::is_state_closed() const
{
  return session == nullptr || fini_received;
}

inline bool
Http2ConnectionState::is_recursing() const
{
  return recursion > 0;
}

inline bool
Http2ConnectionState::is_valid_streamid(Http2StreamId id) const
{
  if (http2_is_client_streamid(id)) {
    return id <= get_latest_stream_id_in();
  } else {
    return id <= get_latest_stream_id_out();
  }
}

inline Http2ShutdownState
Http2ConnectionState::get_shutdown_state() const
{
  return shutdown_state;
}

inline void
Http2ConnectionState::set_shutdown_state(Http2ShutdownState state, Http2ErrorCode reason)
{
  shutdown_state  = state;
  shutdown_reason = reason;
}

inline Event *
Http2ConnectionState::get_zombie_event()
{
  return zombie_event;
}

inline void
Http2ConnectionState::schedule_zombie_event()
{
  if (Http2::zombie_timeout_in) { // If we have zombie debugging enabled
    if (zombie_event) {
      zombie_event->cancel();
    }
    zombie_event = this_ethread()->schedule_in(this, HRTIME_SECONDS(Http2::zombie_timeout_in));
  }
}
