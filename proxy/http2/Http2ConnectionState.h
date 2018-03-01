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

#ifndef __HTTP2_CONNECTION_STATE_H__
#define __HTTP2_CONNECTION_STATE_H__

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
      ink_assert(!"Bad Settings Identifier");
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

    continued_buffer.iov_base = nullptr;
    continued_buffer.iov_len  = 0;

    dependency_tree = new DependencyTree(Http2::max_concurrent_streams_in);
  }

  void
  destroy()
  {
    if (shutdown_cont_event) {
      shutdown_cont_event->cancel();
    }
    cleanup_streams();

    mutex = nullptr; // magic happens - assigning to nullptr frees the ProxyMutex
    delete local_hpack_handle;
    delete remote_hpack_handle;

    ats_free(continued_buffer.iov_base);

    delete dependency_tree;
    this->ua_session = nullptr;

    if (fini_event) {
      fini_event->cancel();
    }
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

  // Connection level window size
  ssize_t client_rwnd = HTTP2_INITIAL_WINDOW_SIZE;
  ssize_t server_rwnd = Http2::initial_window_size;

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
  set_shutdown_state(Http2ShutdownState state)
  {
    shutdown_state = state;
  }

  // noncopyable
  Http2ConnectionState(const Http2ConnectionState &) = delete;
  Http2ConnectionState &operator=(const Http2ConnectionState &) = delete;

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
  int stream_requests               = 0;

  // Counter for current active streams which is started by client
  uint32_t client_streams_in_count = 0;

  // Counter for current acive streams which is started by server
  uint32_t client_streams_out_count = 0;

  // Counter for current active streams and streams in the process of shutting down
  uint32_t total_client_streams_count = 0;

  // NOTE: Id of stream which MUST receive CONTINUATION frame.
  //   - [RFC 7540] 6.2 HEADERS
  //     "A HEADERS frame without the END_HEADERS flag set MUST be followed by a
  //     CONTINUATION frame for the same stream."
  //   - [RFC 7540] 6.10 CONTINUATION
  //     "If the END_HEADERS bit is not set, this frame MUST be followed by
  //     another CONTINUATION frame."
  Http2StreamId continued_stream_id = 0;
  IOVec continued_buffer;
  bool _scheduled                   = false;
  bool fini_received                = false;
  int recursion                     = 0;
  Http2ShutdownState shutdown_state = HTTP2_SHUTDOWN_NONE;
  Event *shutdown_cont_event        = nullptr;
  Event *fini_event                 = nullptr;
};

#endif // __HTTP2_CONNECTION_STATE_H__
