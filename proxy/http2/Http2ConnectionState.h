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
#include <queue>

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
  Http2ConnectionState(const Http2ConnectionState &)            = delete;
  Http2ConnectionState &operator=(const Http2ConnectionState &) = delete;

  ProxyError rx_error_code;
  ProxyError tx_error_code;
  Http2CommonSession *session     = nullptr;
  HpackHandle *local_hpack_handle = nullptr;
  HpackHandle *peer_hpack_handle  = nullptr;
  DependencyTree *dependency_tree = nullptr;
  ActivityCop<Http2Stream> _cop;

  /** The HTTP/2 settings configured by ATS and dictated to the peer via
   * SETTINGS frames. */
  Http2ConnectionSettings local_settings;

  /** The latest set of settings that have been acknowledged by the peer.
   *
   * The default constructed value of this via the Http2ConnectionSettings
   * constructor (i.e., the value before any SETTINGS ACK frames have been
   * received) will instantiate these settings to the default HTTP/2 settings
   * values.
   *
   * @note that @a local_settings are our latest configured settings which have
   * been sent to the peer but may not be acknowledged yet. @a
   * last_acknowledged_settings are the latest settings of which the peer has
   * acknowledged receipt. For this reason, window enforcement behavior and
   * WINDOW_UPDATE calculations should be based upon @a
   * last_acknowledged_settings.
   */
  Http2ConnectionSettings acknowledged_local_settings;

  /** The HTTP/2 settings configured by the peer and dictated to ATS via
   * SETTINGS frames. */
  Http2ConnectionSettings peer_settings;

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

  /** Update all streams for the peer's newly dictated stream window size. */
  void update_initial_peer_rwnd_in(Http2WindowSize new_size);

  /** Update all streams for our newly dictated stream window size. */
  void update_initial_local_rwnd_in(Http2WindowSize new_size);

  Http2StreamId get_latest_stream_id_in() const;
  Http2StreamId get_latest_stream_id_out() const;
  int get_stream_requests() const;
  void increment_stream_requests();

  // Continuated header decoding
  Http2StreamId get_continued_stream_id() const;
  void set_continued_stream_id(Http2StreamId stream_id);
  void clear_continued_stream_id();

  uint32_t get_peer_stream_count() const;
  void decrement_peer_stream_count();
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

  /** Send a SETTINGS frame to the peer.
   *
   * local_settings is updated to the value of @a new_settings as a byproduct
   * of this call.
   *
   * @param[in] new_settings The settings to send to the peer.
   */
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

  ssize_t get_peer_rwnd_in() const;
  Http2ErrorCode increment_peer_rwnd_in(size_t amount);
  Http2ErrorCode decrement_peer_rwnd_in(size_t amount);
  ssize_t get_local_rwnd_in() const;
  Http2ErrorCode increment_local_rwnd_in(size_t amount);
  Http2ErrorCode decrement_local_rwnd_in(size_t amount);

private:
  Http2Error rcv_data_frame(const Http2Frame &);
  Http2Error rcv_headers_frame(const Http2Frame &);
  Http2Error rcv_priority_frame(const Http2Frame &);
  Http2Error rcv_rst_stream_frame(const Http2Frame &);
  Http2Error rcv_settings_frame(const Http2Frame &);
  Http2Error rcv_push_promise_frame(const Http2Frame &);
  Http2Error rcv_ping_frame(const Http2Frame &);
  Http2Error rcv_goaway_frame(const Http2Frame &);
  Http2Error rcv_window_update_frame(const Http2Frame &);
  Http2Error rcv_continuation_frame(const Http2Frame &);

  using http2_frame_dispatch = Http2Error (Http2ConnectionState::*)(const Http2Frame &);
  static constexpr http2_frame_dispatch _frame_handlers[HTTP2_FRAME_TYPE_MAX] = {
    &Http2ConnectionState::rcv_data_frame,          // HTTP2_FRAME_TYPE_DATA
    &Http2ConnectionState::rcv_headers_frame,       // HTTP2_FRAME_TYPE_HEADERS
    &Http2ConnectionState::rcv_priority_frame,      // HTTP2_FRAME_TYPE_PRIORITY
    &Http2ConnectionState::rcv_rst_stream_frame,    // HTTP2_FRAME_TYPE_RST_STREAM
    &Http2ConnectionState::rcv_settings_frame,      // HTTP2_FRAME_TYPE_SETTINGS
    &Http2ConnectionState::rcv_push_promise_frame,  // HTTP2_FRAME_TYPE_PUSH_PROMISE
    &Http2ConnectionState::rcv_ping_frame,          // HTTP2_FRAME_TYPE_PING
    &Http2ConnectionState::rcv_goaway_frame,        // HTTP2_FRAME_TYPE_GOAWAY
    &Http2ConnectionState::rcv_window_update_frame, // HTTP2_FRAME_TYPE_WINDOW_UPDATE
    &Http2ConnectionState::rcv_continuation_frame,  // HTTP2_FRAME_TYPE_CONTINUATION
  };

  unsigned _adjust_concurrent_stream();

  /** Receive and process a SETTINGS frame with the ACK flag set.
   *
   * This function will process any settings updates that have now been
   * acknowledged by the peer.
   */
  void _process_incoming_settings_ack_frame();

  /** Calculate the initial session window size that we communicate to peers.
   *
   * @return The initial receive window size.
   */
  uint32_t _get_configured_receive_session_window_size_in() const;

  /** Whether our stream window can change over the lifetime of a session.
   *
   * @return @c true if the stream window can change, @c false otherwise.
   */
  bool _has_dynamic_stream_window() const;

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

  // Counter for current active streams which are started by the client.
  std::atomic<uint32_t> peer_streams_count_in = 0;

  // Counter for current active streams which are started by the origin.
  std::atomic<uint32_t> peer_streams_count_out = 0;

  // Counter for current active streams (from either the client or origin side)
  // and streams in the process of shutting down
  std::atomic<uint32_t> total_peer_streams_count = 0;

  // Counter for stream errors ATS sent
  uint32_t stream_error_count = 0;

  // Connection level window size

  /** The client-side session level window that we have to respect when we send
   * data to the peer.
   *
   * This is the session window configured by the peer via WINDOW_UPDATE
   * frames. Per specification, this defaults to HTTP2_INITIAL_WINDOW_SIZE (see
   * RFC 9113, section 6.9.2). As we send data, we decrement this value. If it
   * reaches zero, we stop sending data to respect the peer's flow control
   * specification. When we receive WINDOW_UPDATE frames, we increment this
   * value.
   */
  ssize_t _peer_rwnd_in = HTTP2_INITIAL_WINDOW_SIZE;

  /** The session window we maintain with the client-side peer via
   * WINDOW_UPDATE frames.
   *
   * We maintain the window we expect the peer to respect by sending
   * WINDOW_UPDATE frames to the peer. As we receive data, we decrement this
   * value, as we send WINDOW_UPDATE frames, we increment it. If it reaches
   * zero, we generate a connection-level error.
   */
  ssize_t _local_rwnd_in = 0;

  /** Whether the client-side session window is in a shrinking state before we
   * send the first WINDOW_UPDATE frame.
   *
   * Unlike HTTP/2 streams, the HTTP/2 session window has no way to initialize
   * it to a value lower than 65,535. If the initial value is lower than
   * 65,535, the session window will have to shrink while we receive DATA
   * frames without incrementing the window via WINDOW_UPDATE frames. Once the
   * window gets to the desired size, we start maintaining the window via
   * WINDOW_UPDATE frames.
   */
  bool _local_rwnd_is_shrinking_in = false;

  std::array<size_t, 5> _recent_rwnd_increment = {SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX, SIZE_MAX};
  int _recent_rwnd_increment_index             = 0;

  Http2FrequencyCounter _received_settings_counter;
  Http2FrequencyCounter _received_settings_frame_counter;
  Http2FrequencyCounter _received_ping_frame_counter;
  Http2FrequencyCounter _received_priority_frame_counter;

  /** Records the various settings for each SETTINGS frame that we've sent.
   *
   * There are certain SETTINGS values that we send but cannot act upon until the
   * peer acknowledges them. For instance, we cannot enforce reduced stream
   * window sizes until the peer acknowledges the SETTINGS frame that shrinks the
   * size.
   *
   * There is an OutstandingSettingsFrame instance for each SETTINGS frame that
   * we send. We store these in a queue and associate each SETTINGS frame with
   * an ACK flag from the peer with a corresponding OutstandingSettingsFrame
   * instance.
   *
   * For details about SETTINGS synchronization via the ACK flag, see:
   *
   *   [RFC 9113] 6.5.3 Settings Synchronization
   */
  class OutstandingSettingsFrame
  {
  public:
    OutstandingSettingsFrame(const Http2ConnectionSettings &settings) : _settings(settings) {}

    /** Returns the settings parameters that were configured via the SETTINGS frame
     * associated with this instance.
     *
     * @return The settings parameters that were configured at the time the
     * associated SETTINGS frame was sent. @note that this is not just the
     * values in the SETTINGS frame, but those values along with all the local
     * settings that were in place but not explicitly configured via the frame.
     * Thus this returns the snapshot of the entire set of settings configured
     * when the SETTINGS frame was sent.
     */
    Http2ConnectionSettings const &
    get_outstanding_settings() const
    {
      return _settings;
    }

  private:
    /** The SETTINGS parameters that were set at the time of the associated
     * SETTINGS frame being sent.
     */
    Http2ConnectionSettings const _settings;
  };

  /** The queue of SETTINGS frames that we have sent but have not yet been
   * acknowledged by the peer. */
  std::queue<OutstandingSettingsFrame> _outstanding_settings_frames_in;

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
Http2ConnectionState::get_peer_stream_count() const
{
  return peer_streams_count_in;
}

inline void
Http2ConnectionState::decrement_peer_stream_count()
{
  --total_peer_streams_count;
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
