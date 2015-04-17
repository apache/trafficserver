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
#include "FetchSM.h"

class Http2ClientSession;

class Http2ConnectionSettings
{
public:
  Http2ConnectionSettings()
  {
    // 6.5.2.  Defined SETTINGS Parameters. These should generally not be modified,
    // only if the protocol changes should these change.
    settings[indexof(HTTP2_SETTINGS_ENABLE_PUSH)] = 0; // Disabled for now

    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = HTTP2_MAX_CONCURRENT_STREAMS;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)] = HTTP2_INITIAL_WINDOW_SIZE;
    settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)] = HTTP2_MAX_FRAME_SIZE;
    settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)] = HTTP2_HEADER_TABLE_SIZE;
    settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)] = HTTP2_MAX_HEADER_LIST_SIZE;
  }

  void
  settings_from_configs()
  {
    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = Http2::max_concurrent_streams;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)] = Http2::initial_window_size;
    settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)] = Http2::max_frame_size;
    settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)] = Http2::header_table_size;
    settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)] = Http2::max_header_list_size;
  }

  unsigned
  get(Http2SettingsIdentifier id) const
  {
    ink_assert(id <= HTTP2_SETTINGS_MAX - 1);

    if (id > HTTP2_SETTINGS_MAX - 1) {
      return 0;
    }
    return this->settings[indexof(id)];
  }

  unsigned
  set(Http2SettingsIdentifier id, unsigned value)
  {
    ink_assert(id <= HTTP2_SETTINGS_MAX - 1);

    if (id > HTTP2_SETTINGS_MAX - 1) {
      return 0;
    }
    return this->settings[indexof(id)] = value;
  }

private:
  // Settings ID is 1-based, so convert it to a 0-based index.
  static unsigned
  indexof(Http2SettingsIdentifier id)
  {
    ink_assert(id <= HTTP2_SETTINGS_MAX - 1);

    return id - 1;
  }

  unsigned settings[HTTP2_SETTINGS_MAX - 1];
};

class Http2ConnectionState;

class Http2Stream
{
public:
  Http2Stream(Http2StreamId sid = 0, ssize_t initial_rwnd = Http2::initial_window_size)
    : client_rwnd(initial_rwnd), server_rwnd(initial_rwnd), _id(sid), _state(HTTP2_STREAM_STATE_IDLE), _fetch_sm(NULL),
      body_done(false), data_length(0)
  {
    _req_header.create(HTTP_TYPE_REQUEST);
  }

  ~Http2Stream()
  {
    _req_header.destroy();

    if (_fetch_sm) {
      _fetch_sm->ext_destroy();
      _fetch_sm = NULL;
    }
  }

  // Operate FetchSM
  void init_fetcher(Http2ConnectionState &cstate);
  void set_body_to_fetcher(const void *data, size_t len);
  FetchSM *
  get_fetcher()
  {
    return _fetch_sm;
  }
  bool
  is_body_done() const
  {
    return body_done;
  }
  void
  mark_body_done()
  {
    body_done = true;
  }

  const Http2StreamId
  get_id() const
  {
    return _id;
  }
  const Http2StreamState
  get_state() const
  {
    return _state;
  }
  bool change_state(uint8_t type, uint8_t flags);

  int64_t
  decode_request_header(const IOVec &iov, Http2DynamicTable &dynamic_table, bool cont)
  {
    return http2_parse_header_fragment(&_req_header, iov, dynamic_table, cont);
  }

  // Check entire DATA payload length if content-length: header is exist
  void
  increment_data_length(uint64_t length)
  {
    data_length += length;
  }
  bool
  payload_length_is_valid() const
  {
    uint32_t content_length = _req_header.get_content_length();
    return content_length == 0 || content_length == data_length;
  }

  // Stream level window size
  ssize_t client_rwnd, server_rwnd;

  LINK(Http2Stream, link);

private:
  Http2StreamId _id;
  Http2StreamState _state;

  HTTPHdr _req_header;
  FetchSM *_fetch_sm;
  bool body_done;
  uint64_t data_length;
};


// Http2ConnectionState
//
// Capture the semantics of a HTTP/2 connection. The client session captures the frame layer, and the
// connection state captures the connection-wide state.

class Http2ConnectionState : public Continuation
{
public:
  Http2ConnectionState()
    : Continuation(NULL), ua_session(NULL), client_rwnd(Http2::initial_window_size), server_rwnd(Http2::initial_window_size),
      stream_list(), latest_streamid(0), client_streams_count(0), continued_id(0)
  {
    SET_HANDLER(&Http2ConnectionState::main_event_handler);
  }

  Http2ClientSession *ua_session;
  Http2DynamicTable *local_dynamic_table;
  Http2DynamicTable *remote_dynamic_table;

  // Settings.
  Http2ConnectionSettings server_settings;
  Http2ConnectionSettings client_settings;

  void
  init()
  {
    local_dynamic_table = new Http2DynamicTable();
    remote_dynamic_table = new Http2DynamicTable();

    continued_buffer.iov_base = NULL;
    continued_buffer.iov_len = 0;

    // Load the server settings from the records.config / RecordsConfig.cc settings.
    server_settings.settings_from_configs();
  }

  void
  destroy()
  {
    cleanup_streams();

    delete local_dynamic_table;
    delete remote_dynamic_table;

    ats_free(continued_buffer.iov_base);
  }

  // Event handlers
  int main_event_handler(int, void *);
  int state_closed(int, void *);

  // Stream control interfaces
  Http2Stream *create_stream(Http2StreamId new_id);
  Http2Stream *find_stream(Http2StreamId id) const;
  void restart_streams();
  void delete_stream(Http2Stream *stream);
  void cleanup_streams();

  void update_initial_rwnd(Http2WindowSize new_size);

  // Continuated header decoding
  Http2StreamId
  get_continued_id() const
  {
    return continued_id;
  }
  const IOVec &
  get_continued_headers() const
  {
    return continued_buffer;
  }
  void set_continued_headers(const char *buf, uint32_t len, Http2StreamId id);
  void finish_continued_headers();

  // Connection level window size
  ssize_t client_rwnd, server_rwnd;

  // HTTP/2 frame sender
  void send_data_frame(FetchSM *fetch_sm);
  void send_headers_frame(FetchSM *fetch_sm);
  void send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data);
  void send_goaway_frame(Http2StreamId id, Http2ErrorCode ec);
  void send_window_update_frame(Http2StreamId id, uint32_t size);

  bool
  is_state_closed() const
  {
    return ua_session == NULL;
  }

private:
  Http2ConnectionState(const Http2ConnectionState &);            // noncopyable
  Http2ConnectionState &operator=(const Http2ConnectionState &); // noncopyable

  DLL<Http2Stream> stream_list;
  Http2StreamId latest_streamid;

  // Counter for current acive streams which is started by client
  uint32_t client_streams_count;

  // The buffer used for storing incomplete fragments of a header field which consists of multiple frames.
  Http2StreamId continued_id;
  IOVec continued_buffer;
};

#endif // __HTTP2_CONNECTION_STATE_H__
