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

#include "P_Net.h"
#include "Http2ConnectionState.h"
#include "Http2ClientSession.h"

#define DebugHttp2Ssn(fmt, ...) DebugSsn("http2_cs", "[%" PRId64 "] " fmt, this->con_id, __VA_ARGS__)

// Currently use only HTTP/1.1 for requesting to origin server
const static char *HTTP2_FETCHING_HTTP_VERSION = "HTTP/1.1";

typedef Http2ErrorCode (*http2_frame_dispatch)(Http2ClientSession &, Http2ConnectionState &, const Http2Frame &);

static const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] = {
  BUFFER_SIZE_INDEX_8K,  // HTTP2_FRAME_TYPE_DATA
  BUFFER_SIZE_INDEX_4K,  // HTTP2_FRAME_TYPE_HEADERS
  -1,                    // HTTP2_FRAME_TYPE_PRIORITY
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_RST_STREAM
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_SETTINGS
  -1,                    // HTTP2_FRAME_TYPE_PUSH_PROMISE
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_PING
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_GOAWAY
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_WINDOW_UPDATE
  BUFFER_SIZE_INDEX_4K,  // HTTP2_FRAME_TYPE_CONTINUATION
};

inline static unsigned
read_rcv_buffer(char *buf, size_t bufsize, unsigned &nbytes, const Http2Frame &frame)
{
  char *end;

  if (frame.header().length - nbytes > bufsize) {
    end = frame.reader()->memcpy(buf, bufsize, nbytes);
  } else {
    end = frame.reader()->memcpy(buf, frame.header().length - nbytes, nbytes);
  }
  nbytes += end - buf;

  return end - buf;
}

static Http2ErrorCode
rcv_data_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA])];
  unsigned nbytes = 0;
  Http2StreamId id = frame.header().streamid;
  Http2Stream *stream = cstate.find_stream(id);
  uint8_t pad_length = 0;
  const uint32_t payload_length = frame.header().length;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received DATA frame.", cs.connection_id());

  // If a DATA frame is received whose stream identifier field is 0x0, the recipient MUST
  // respond with a connection error of type PROTOCOL_ERROR.
  if (id == 0 || stream == NULL) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // If a DATA frame is received whose stream is not in "open" or "half closed (local)" state,
  // the recipient MUST respond with a stream error of type STREAM_CLOSED.
  if (stream->get_state() != HTTP2_STREAM_STATE_OPEN && stream->get_state() != HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL) {
    cstate.send_rst_stream_frame(id, HTTP2_ERROR_STREAM_CLOSED);
    return HTTP2_ERROR_NO_ERROR;
  }

  stream->increment_data_length(payload_length);
  if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
    if (!stream->change_state(frame.header().type, frame.header().flags)) {
      cstate.send_rst_stream_frame(id, HTTP2_ERROR_STREAM_CLOSED);
      return HTTP2_ERROR_NO_ERROR;
    }
    if (!stream->payload_length_is_valid()) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  if (frame.header().flags & HTTP2_FLAGS_DATA_PADDED) {
    frame.reader()->memcpy(&pad_length, HTTP2_DATA_PADLEN_LEN, nbytes);

    if (pad_length > payload_length) {
      // If the length of the padding is the length of the
      // frame payload or greater, the recipient MUST treat this as a
      // connection error of type PROTOCOL_ERROR.
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  // If Data length is 0, do nothing.
  if (payload_length == 0) {
    return HTTP2_ERROR_NO_ERROR;
  }

  // Check whether Window Size is appeptable.
  if (cstate.server_rwnd < payload_length || stream->server_rwnd < payload_length) {
    return HTTP2_ERROR_FLOW_CONTROL_ERROR;
  }

  // Update Window size
  cstate.server_rwnd -= payload_length;
  stream->server_rwnd -= payload_length;

  const uint32_t unpadded_length = payload_length - pad_length;
  while (nbytes < payload_length - pad_length) {
    size_t read_len = sizeof(buf);
    if (nbytes + read_len > unpadded_length)
      read_len -= nbytes + read_len - unpadded_length;
    unsigned read_bytes = read_rcv_buffer(buf, read_len, nbytes, frame);
    stream->set_body_to_fetcher(buf, read_bytes);
  }

  uint32_t initial_rwnd = cstate.server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  uint32_t min_rwnd = min(initial_rwnd, cstate.server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE));
  // Connection level WINDOW UPDATE
  if (cstate.server_rwnd <= min_rwnd) {
    Http2WindowSize diff_size = initial_rwnd - cstate.server_rwnd;
    cstate.server_rwnd += diff_size;
    cstate.send_window_update_frame(0, diff_size);
  }
  // Stream level WINDOW UPDATE
  if (stream->server_rwnd <= min_rwnd) {
    Http2WindowSize diff_size = initial_rwnd - stream->server_rwnd;
    stream->server_rwnd += diff_size;
    cstate.send_window_update_frame(stream->get_id(), diff_size);
  }

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_headers_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS])];
  unsigned nbytes = 0;
  Http2StreamId id = frame.header().streamid;
  Http2HeadersParameter params;
  const uint32_t payload_length = frame.header().length;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received HEADERS frame.", cs.connection_id());

  if (!http2_is_client_streamid(id)) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // Create new stream
  Http2Stream *stream = cstate.create_stream(id);
  if (!stream) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // A receiver MUST treat the receipt of any other type of frame or
  // a frame on a different stream as a connection error of type PROTOCOL_ERROR.
  if (cstate.get_continued_id() != 0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // Change state. If changing is invalid, raise PROTOCOL_ERROR
  if (!stream->change_state(frame.header().type, frame.header().flags)) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // Check whether padding exists or not.
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PADDED) {
    frame.reader()->memcpy(buf, HTTP2_HEADERS_PADLEN_LEN, nbytes);
    nbytes += HTTP2_HEADERS_PADLEN_LEN;
    if (!http2_parse_headers_parameter(make_iovec(buf, HTTP2_HEADERS_PADLEN_LEN), params)) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    if (params.pad_length > payload_length) {
      // If the length of the padding is the length of the
      // frame payload or greater, the recipient MUST treat this as a
      // connection error of type PROTOCOL_ERROR.
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }
  } else {
    params.pad_length = 0;
  }

  // Check whether parameters of priority exist or not.
  // TODO Currently priority is NOT supported.
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PRIORITY) {
    frame.reader()->memcpy(buf, HTTP2_PRIORITY_LEN, nbytes);
    nbytes += HTTP2_PRIORITY_LEN;
    if (!http2_parse_priority_parameter(make_iovec(buf, HTTP2_PRIORITY_LEN), params.priority)) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  // Parse request headers encoded by HPACK
  const uint32_t unpadded_length = payload_length - params.pad_length;
  uint32_t remaining_bytes = 0;
  for (;;) {
    size_t read_len = sizeof(buf) - remaining_bytes;
    if (nbytes + read_len > unpadded_length)
      read_len -= nbytes + read_len - unpadded_length;
    unsigned read_bytes = read_rcv_buffer(buf + remaining_bytes, read_len, nbytes, frame);
    IOVec header_block_fragment = make_iovec(buf, read_bytes + remaining_bytes);

    bool cont = nbytes < payload_length || !(frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS);
    int64_t decoded_bytes = stream->decode_request_header(header_block_fragment, *cstate.local_dynamic_table, cont);

    // 4.3. A receiver MUST terminate the connection with a
    // connection error of type COMPRESSION_ERROR if it does
    // not decompress a header block.
    if (decoded_bytes == 0 || decoded_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
      return HTTP2_ERROR_COMPRESSION_ERROR;
    }

    if (decoded_bytes == HPACK_ERROR_HTTP2_PROTOCOL_ERROR) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    remaining_bytes = header_block_fragment.iov_len - decoded_bytes;
    memmove(buf, buf + header_block_fragment.iov_len - remaining_bytes, remaining_bytes);

    if (nbytes >= payload_length - params.pad_length) {
      if (!(frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS)) {
        cstate.set_continued_headers(buf, remaining_bytes, id);
      }
      break;
    }
  }

  // backposting
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    stream->init_fetcher(cstate);
  }

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_priority_frame(Http2ClientSession &cs, Http2ConnectionState & /*cstate*/, const Http2Frame &frame)
{
  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] received PRIORITY frame", cs.connection_id());

  // If a PRIORITY frame is received with a stream identifier of 0x0, the
  // recipient MUST respond with a connection error of type PROTOCOL_ERROR.
  if (frame.header().streamid == 0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // A PRIORITY frame with a length other than 5 octets MUST be treated as
  // a stream error (Section 5.4.2) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_PRIORITY_LEN) {
    return HTTP2_ERROR_FRAME_SIZE_ERROR;
  }

  // TODO Pick stream dependencies and weight
  // Supporting PRIORITY is not essential so its temporarily ignored.

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_rst_stream_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2RstStream rst_stream;
  char buf[HTTP2_RST_STREAM_LEN];
  char *end;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received RST_STREAM frame.", cs.connection_id());

  Http2Stream *stream = cstate.find_stream(frame.header().streamid);
  if (frame.header().streamid == 0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  if (frame.header().length != HTTP2_RST_STREAM_LEN) {
    return HTTP2_ERROR_FRAME_SIZE_ERROR;
  }

  if (stream != NULL && !stream->change_state(frame.header().type, frame.header().flags)) {
    // If a RST_STREAM frame identifying an idle stream is received, the
    // recipient MUST treat this as a connection error of type PROTOCOL_ERROR.
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  end = frame.reader()->memcpy(buf, sizeof(buf), 0);

  if (!http2_parse_rst_stream(make_iovec(buf, end - buf), rst_stream)) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  if (stream != NULL) {
    DebugSsn(&cs, "http2_cs", "[%" PRId64 "] RST_STREAM: Stream ID: %u, Error Code: %u)", cs.connection_id(), stream->get_id(),
             rst_stream.error_code);
    cstate.delete_stream(stream);
  }

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_settings_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2SettingsParameter param;
  char buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned nbytes = 0;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received SETTINGS frame.", cs.connection_id());

  // 6.5 The stream identifier for a SETTINGS frame MUST be zero.
  if (frame.header().streamid != 0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // 6.5 Receipt of a SETTINGS frame with the ACK flag set and a
  // length field value other than 0 MUST be treated as a connection
  // error of type FRAME_SIZE_ERROR.
  if (frame.header().flags & HTTP2_FLAGS_SETTINGS_ACK) {
    return frame.header().length == 0 ? HTTP2_ERROR_NO_ERROR : HTTP2_ERROR_FRAME_SIZE_ERROR;
  }

  while (nbytes < frame.header().length) {
    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_settings_parameter(make_iovec(buf, read_bytes), param)) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    if (!http2_settings_parameter_is_valid(param)) {
      return param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE ? HTTP2_ERROR_FLOW_CONTROL_ERROR : HTTP2_ERROR_PROTOCOL_ERROR;
    }

    DebugSsn(&cs, "http2_cs", "[%" PRId64 "] setting param=%d value=%u", cs.connection_id(), param.id, param.value);

    // 6.9.2. When the value of SETTINGS_INITIAL_WINDOW_SIZE
    // changes, a receiver MUST adjust the size of all stream flow control
    // windows that it maintains by the difference between the new value and
    // the old value.
    if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
      cstate.update_initial_rwnd(param.value);
    }

    cstate.client_settings.set((Http2SettingsIdentifier)param.id, param.value);
  }

  // 6.5 Once all values have been applied, the recipient MUST immediately emit a
  // SETTINGS frame with the ACK flag set.
  Http2Frame ackFrame(HTTP2_FRAME_TYPE_SETTINGS, 0, HTTP2_FLAGS_SETTINGS_ACK);
  cstate.ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ackFrame);

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_push_promise_frame(Http2ClientSession &cs, Http2ConnectionState & /*cstate*/, const Http2Frame & /*frame*/)
{
  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] received PUSH_PROMISE frame", cs.connection_id());

  // 8.2. A client cannot push.  Thus, servers MUST treat the receipt of a
  // PUSH_PROMISE frame as a connection error of type PROTOCOL_ERROR.
  return HTTP2_ERROR_PROTOCOL_ERROR;
}

// 6.7.  PING
static Http2ErrorCode
rcv_ping_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  uint8_t opaque_data[HTTP2_PING_LEN];

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received PING frame.", cs.connection_id());

  //  If a PING frame is received with a stream identifier field value other than
  //  0x0, the recipient MUST respond with a connection error of type PROTOCOL_ERROR.
  if (frame.header().streamid != 0x0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // Receipt of a PING frame with a length field value other than 8 MUST
  // be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_PING_LEN) {
    return HTTP2_ERROR_FRAME_SIZE_ERROR;
  }

  // An endpoint MUST NOT respond to PING frames containing this flag.
  if (frame.header().flags & HTTP2_FLAGS_PING_ACK) {
    return HTTP2_ERROR_NO_ERROR;
  }

  frame.reader()->memcpy(opaque_data, HTTP2_PING_LEN, 0);

  // ACK (0x1): An endpoint MUST set this flag in PING responses.
  cstate.send_ping_frame(frame.header().streamid, HTTP2_FLAGS_PING_ACK, opaque_data);

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_goaway_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2Goaway goaway;
  char buf[HTTP2_GOAWAY_LEN];
  unsigned nbytes = 0;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] received GOAWAY frame", cs.connection_id());

  // An endpoint MUST treat a GOAWAY frame with a stream identifier other
  // than 0x0 as a connection error of type PROTOCOL_ERROR.
  if (frame.header().streamid != 0x0) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  while (nbytes < frame.header().length) {
    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_goaway(make_iovec(buf, read_bytes), goaway)) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }
  }

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] GOAWAY: last stream id=%d, error code=%d.", cs.connection_id(), goaway.last_streamid,
           goaway.error_code);

  cstate.handleEvent(HTTP2_SESSION_EVENT_FINI, NULL);
  // eventProcessor.schedule_imm(&cs, ET_NET, VC_EVENT_ERROR);

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_window_update_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[HTTP2_WINDOW_UPDATE_LEN];
  uint32_t size;
  Http2StreamId sid = frame.header().streamid;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received WINDOW_UPDATE frame.", cs.connection_id());

  //  A WINDOW_UPDATE frame with a length other than 4 octets MUST be
  //  treated as a connection error of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_WINDOW_UPDATE_LEN) {
    return HTTP2_ERROR_FRAME_SIZE_ERROR;
  }

  if (sid == 0) {
    // Connection level window update
    frame.reader()->memcpy(buf, sizeof(buf), 0);
    http2_parse_window_update(make_iovec(buf, sizeof(buf)), size);

    // A receiver MUST treat the receipt of a WINDOW_UPDATE frame with a connection
    // flow control window increment of 0 as a connection error of type PROTOCOL_ERROR;
    if (size == 0) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    cstate.client_rwnd += size;
    cstate.restart_streams();
  } else {
    // Stream level window update
    Http2Stream *stream = cstate.find_stream(sid);

    // This means that a receiver could receive a
    // WINDOW_UPDATE frame on a "half closed (remote)" or "closed" stream.
    // A receiver MUST NOT treat this as an error.
    if (stream == NULL) {
      // Temporarily ignore WINDOW_UPDATE
      // TODO After supporting PRIORITY, it should be handled correctly.
      return HTTP2_ERROR_NO_ERROR;
    }

    frame.reader()->memcpy(buf, sizeof(buf), 0);
    http2_parse_window_update(make_iovec(buf, sizeof(buf)), size);

    // A receiver MUST treat the receipt of a WINDOW_UPDATE frame with an
    // flow control window increment of 0 as a stream error of type PROTOCOL_ERROR;
    if (size == 0) {
      cstate.send_rst_stream_frame(sid, HTTP2_ERROR_PROTOCOL_ERROR);
      return HTTP2_ERROR_NO_ERROR;
    }

    stream->client_rwnd += size;
    ssize_t wnd = min(cstate.client_rwnd, stream->client_rwnd);
    if (wnd > 0) {
      cstate.send_data_frame(stream->get_fetcher());
    }
  }

  return HTTP2_ERROR_NO_ERROR;
}

static Http2ErrorCode
rcv_continuation_frame(Http2ClientSession &cs, Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION])];
  unsigned nbytes = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  DebugSsn(&cs, "http2_cs", "[%" PRId64 "] Received CONTINUATION frame.", cs.connection_id());

  // Find opened stream
  Http2Stream *stream = cstate.find_stream(stream_id);
  if (stream == NULL) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // A CONTINUATION frame MUST be preceded by a HEADERS, PUSH_PROMISE or
  // CONTINUATION frame without the END_HEADERS flag set.
  if (stream->get_state() != HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE && stream->get_state() != HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  // A receiver MUST treat the receipt of any other type of frame or
  // a frame on a different stream as a connection error of type PROTOCOL_ERROR.
  if (stream->get_id() != cstate.get_continued_id()) {
    return HTTP2_ERROR_PROTOCOL_ERROR;
  }

  const IOVec remaining_data = cstate.get_continued_headers();
  uint32_t remaining_bytes = remaining_data.iov_len;
  if (remaining_bytes && remaining_data.iov_base) {
    memcpy(buf, remaining_data.iov_base, remaining_data.iov_len);
  }

  // Parse request headers encoded by HPACK
  for (;;) {
    unsigned read_bytes = read_rcv_buffer(buf + remaining_bytes, sizeof(buf) - remaining_bytes, nbytes, frame);
    IOVec header_block_fragment = make_iovec(buf, read_bytes + remaining_bytes);

    bool cont = nbytes < frame.header().length || !(frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS);
    int64_t decoded_bytes = stream->decode_request_header(header_block_fragment, *cstate.local_dynamic_table, cont);

    // A receiver MUST terminate the connection with a
    // connection error of type COMPRESSION_ERROR if it does
    // not decompress a header block.
    if (decoded_bytes == 0 || decoded_bytes == HPACK_ERROR_COMPRESSION_ERROR) {
      return HTTP2_ERROR_COMPRESSION_ERROR;
    }

    if (decoded_bytes == HPACK_ERROR_HTTP2_PROTOCOL_ERROR) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    remaining_bytes = header_block_fragment.iov_len - decoded_bytes;
    memmove(buf, buf + header_block_fragment.iov_len - remaining_bytes, remaining_bytes);

    if (nbytes >= frame.header().length) {
      if (!(frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS)) {
        cstate.set_continued_headers(buf, remaining_bytes, stream_id);
      }
      break;
    }
  }

  // backposting
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    cstate.finish_continued_headers();
    stream->init_fetcher(cstate);
  }

  return HTTP2_ERROR_NO_ERROR;
}

static const http2_frame_dispatch frame_handlers[HTTP2_FRAME_TYPE_MAX] = {
  rcv_data_frame,          // HTTP2_FRAME_TYPE_DATA
  rcv_headers_frame,       // HTTP2_FRAME_TYPE_HEADERS
  rcv_priority_frame,      // HTTP2_FRAME_TYPE_PRIORITY
  rcv_rst_stream_frame,    // HTTP2_FRAME_TYPE_RST_STREAM
  rcv_settings_frame,      // HTTP2_FRAME_TYPE_SETTINGS
  rcv_push_promise_frame,  // HTTP2_FRAME_TYPE_PUSH_PROMISE
  rcv_ping_frame,          // HTTP2_FRAME_TYPE_PING
  rcv_goaway_frame,        // HTTP2_FRAME_TYPE_GOAWAY
  rcv_window_update_frame, // HTTP2_FRAME_TYPE_WINDOW_UPDATE
  rcv_continuation_frame,  // HTTP2_FRAME_TYPE_CONTINUATION
};

int
Http2ConnectionState::main_event_handler(int event, void *edata)
{
  switch (event) {
  // Initialize HTTP/2 Connection
  case HTTP2_SESSION_EVENT_INIT: {
    ink_assert(this->ua_session == NULL);
    this->ua_session = (Http2ClientSession *)edata;

    // 3.5 HTTP/2 Connection Preface. Upon establishment of a TCP connection and
    // determination that HTTP/2 will be used by both peers, each endpoint MUST
    // send a connection preface as a final confirmation ... The server connection
    // preface consists of a potentially empty SETTINGS frame.
    Http2Frame settings(HTTP2_FRAME_TYPE_SETTINGS, 0, 0);
    settings.alloc(buffer_size_index[HTTP2_FRAME_TYPE_SETTINGS]);

    // Send all settings values
    IOVec iov = settings.write();
    for (int i = 1; i < HTTP2_SETTINGS_MAX; i++) {
      Http2SettingsIdentifier id = static_cast<Http2SettingsIdentifier>(i);
      Http2SettingsParameter param;
      param.id = id;
      param.value = server_settings.get(id);
      http2_write_settings(param, iov);
      iov.iov_base = reinterpret_cast<uint8_t *>(iov.iov_base) + HTTP2_SETTINGS_PARAMETER_LEN;
      iov.iov_len -= HTTP2_SETTINGS_PARAMETER_LEN;
    }

    settings.finalize(HTTP2_SETTINGS_PARAMETER_LEN * (HTTP2_SETTINGS_MAX - 1));
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &settings);

    if (server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) > HTTP2_INITIAL_WINDOW_SIZE) {
      send_window_update_frame(0, server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - HTTP2_INITIAL_WINDOW_SIZE);
    }

    return 0;
  }

  // Finalize HTTP/2 Connection
  case HTTP2_SESSION_EVENT_FINI: {
    this->ua_session = NULL;
    cleanup_streams();
    SET_HANDLER(&Http2ConnectionState::state_closed);
    return 0;
  }

  // Parse received HTTP/2 frames
  case HTTP2_SESSION_EVENT_RECV: {
    Http2Frame *frame = (Http2Frame *)edata;
    Http2StreamId last_streamid = frame->header().streamid;
    Http2ErrorCode error;

    //  Implementations MUST ignore and discard any frame that has a type that is unknown.
    ink_assert(frame->header().type < countof(frame_handlers));
    if (frame->header().type > countof(frame_handlers)) {
      return 0;
    }

    if (frame_handlers[frame->header().type]) {
      error = frame_handlers[frame->header().type](*this->ua_session, *this, *frame);
    } else {
      error = HTTP2_ERROR_INTERNAL_ERROR;
    }

    if (error != HTTP2_ERROR_NO_ERROR) {
      this->send_goaway_frame(last_streamid, error);
      cleanup_streams();
      // XXX We need to think a bit harder about how to coordinate the client session and the
      // protocol connection. At this point, the protocol is shutting down, but there's no way
      // to tell that to the client session. Perhaps this could be solved by implementing the
      // half-closed state ...
      SET_HANDLER(&Http2ConnectionState::state_closed);
    }

    return 0;
  }

  // Process response headers from origin server
  case TS_FETCH_EVENT_EXT_HEAD_DONE: {
    FetchSM *fetch_sm = reinterpret_cast<FetchSM *>(edata);
    this->send_headers_frame(fetch_sm);
    return 0;
  }

  // Process a part of response body from origin server
  case TS_FETCH_EVENT_EXT_BODY_READY: {
    FetchSM *fetch_sm = reinterpret_cast<FetchSM *>(edata);
    this->send_data_frame(fetch_sm);
    return 0;
  }

  // Process final part of response body from origin server
  case TS_FETCH_EVENT_EXT_BODY_DONE: {
    FetchSM *fetch_sm = reinterpret_cast<FetchSM *>(edata);
    Http2Stream *stream = static_cast<Http2Stream *>(fetch_sm->ext_get_user_data());
    stream->mark_body_done();
    this->send_data_frame(fetch_sm);
    return 0;
  }

  default:
    DebugSsn(this->ua_session, "http2_cs", "unexpected event=%d edata=%p", event, edata);
    ink_release_assert(0);
    return 0;
  }

  return 0;
}

int
Http2ConnectionState::state_closed(int /* event */, void * /* edata */)
{
  return 0;
}

Http2Stream *
Http2ConnectionState::create_stream(Http2StreamId new_id)
{
  // The identifier of a newly established stream MUST be numerically
  // greater than all streams that the initiating endpoint has opened or
  // reserved.
  if (new_id <= latest_streamid) {
    return NULL;
  }

  // Endpoints MUST NOT exceed the limit set by their peer.  An endpoint
  // that receives a HEADERS frame that causes their advertised concurrent
  // stream limit to be exceeded MUST treat this as a stream error.
  if (client_streams_count >= client_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) {
    return NULL;
  }

  Http2Stream *new_stream = new Http2Stream(new_id, client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));
  stream_list.push(new_stream);
  latest_streamid = new_id;

  ink_assert(client_streams_count < UINT32_MAX);
  ++client_streams_count;

  return new_stream;
}

Http2Stream *
Http2ConnectionState::find_stream(Http2StreamId id) const
{
  for (Http2Stream *s = stream_list.head; s; s = s->link.next) {
    if (s->get_id() == id)
      return s;
  }
  return NULL;
}

void
Http2ConnectionState::restart_streams()
{
  // Currently lookup retryable streams sequentially.
  // TODO considering to stream weight and dependencies.
  Http2Stream *s = stream_list.head;
  while (s) {
    Http2Stream *next = s->link.next;
    if (min(this->client_rwnd, s->client_rwnd) > 0) {
      this->send_data_frame(s->get_fetcher());
    }
    s = next;
  }
}

void
Http2ConnectionState::cleanup_streams()
{
  Http2Stream *s = stream_list.head;
  while (s) {
    Http2Stream *next = s->link.next;
    stream_list.remove(s);
    delete s;
    s = next;
  }
  client_streams_count = 0;
}

void
Http2ConnectionState::set_continued_headers(const char *buf, uint32_t len, Http2StreamId id)
{
  if (buf && len > 0) {
    if (!continued_buffer.iov_base) {
      continued_buffer.iov_base = static_cast<uint8_t *>(ats_malloc(len));
    } else if (continued_buffer.iov_len < len) {
      continued_buffer.iov_base = ats_realloc(continued_buffer.iov_base, len);
    }
    continued_buffer.iov_len = len;

    memcpy(continued_buffer.iov_base, buf, continued_buffer.iov_len);
  }

  continued_id = id;
}

void
Http2ConnectionState::finish_continued_headers()
{
  continued_id = 0;
  ats_free(continued_buffer.iov_base);
  continued_buffer.iov_base = NULL;
  continued_buffer.iov_len = 0;
}

void
Http2ConnectionState::delete_stream(Http2Stream *stream)
{
  stream_list.remove(stream);
  delete stream;

  ink_assert(client_streams_count > 0);
  --client_streams_count;
}

void
Http2ConnectionState::update_initial_rwnd(Http2WindowSize new_size)
{
  // Update stream level window sizes
  for (Http2Stream *s = stream_list.head; s; s = s->link.next) {
    s->client_rwnd = new_size - (client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - s->client_rwnd);
  }
}

void
Http2ConnectionState::send_data_frame(FetchSM *fetch_sm)
{
  size_t buf_len = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]) - HTTP2_FRAME_HEADER_LEN;
  uint8_t payload_buffer[buf_len];

  DebugSsn(this->ua_session, "http2_cs", "[%" PRId64 "] Send DATA frame.", this->ua_session->connection_id());

  Http2Stream *stream = static_cast<Http2Stream *>(fetch_sm->ext_get_user_data());

  for (;;) {
    uint8_t flags = 0x00;

    // Select appropriate payload size
    if (this->client_rwnd <= 0 || stream->client_rwnd <= 0)
      break;
    size_t window_size = min(this->client_rwnd, stream->client_rwnd);
    size_t send_size = min(buf_len, window_size);

    size_t payload_length = fetch_sm->ext_read_data(reinterpret_cast<char *>(payload_buffer), send_size);

    // If we break here, we never send the END_STREAM in the case of a
    // early terminating OS.  Ok if there is no body yet.  Otherwise
    // continue on to delete the stream
    if (payload_length == 0 && !stream->is_body_done()) {
      break;
    }

    // Update window size
    this->client_rwnd -= payload_length;
    stream->client_rwnd -= payload_length;

    if (stream->is_body_done() && payload_length < send_size) {
      flags |= HTTP2_FLAGS_DATA_END_STREAM;
    }

    // Create frame
    Http2Frame data(HTTP2_FRAME_TYPE_DATA, stream->get_id(), flags);
    data.alloc(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
    http2_write_data(payload_buffer, payload_length, data.write());
    data.finalize(payload_length);

    // Change state to 'closed' if its end of DATAs.
    if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
      if (!stream->change_state(data.header().type, data.header().flags)) {
        this->send_goaway_frame(stream->get_id(), HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    // xmit event
    MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &data);

    if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
      // Delete a stream immediately
      // TODO its should not be deleted for a several time to handling RST_STREAM and WINDOW_UPDATE.
      // See 'closed' state written at https://tools.ietf.org/html/draft-ietf-httpbis-http2-16#section-5.1
      this->delete_stream(stream);
      break;
    }
  }
}

void
Http2ConnectionState::send_headers_frame(FetchSM *fetch_sm)
{
  const size_t buf_len = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]) - HTTP2_FRAME_HEADER_LEN;
  uint8_t payload_buffer[buf_len];
  size_t payload_length = 0;
  uint8_t flags = 0x00;

  Http2Stream *stream = static_cast<Http2Stream *>(fetch_sm->ext_get_user_data());
  HTTPHdr *resp_header = reinterpret_cast<HTTPHdr *>(fetch_sm->resp_hdr_bufp());

  // Write psuedo headers
  payload_length += http2_write_psuedo_headers(resp_header, payload_buffer, buf_len, *(this->remote_dynamic_table));

  // If response body is empry, set END_STREAM flag to HEADERS frame
  // Must check to ensure content-length is there.  Otherwise the value defaults to 0
  if (resp_header->presence(MIME_PRESENCE_CONTENT_LENGTH) && resp_header->get_content_length() == 0) {
    flags |= HTTP2_FLAGS_HEADERS_END_STREAM;
  }

  MIMEFieldIter field_iter;
  bool cont = false;
  do {
    // Handle first sending frame is as HEADERS
    Http2FrameType type = cont ? HTTP2_FRAME_TYPE_CONTINUATION : HTTP2_FRAME_TYPE_HEADERS;

    // Encode by HPACK naive
    payload_length += http2_write_header_fragment(resp_header, field_iter, payload_buffer + payload_length,
                                                  buf_len - payload_length, *(this->remote_dynamic_table), cont);

    // If buffer size is enough to send rest of headers, set END_HEADERS flag
    if (buf_len >= payload_length && !cont) {
      flags |= HTTP2_FLAGS_HEADERS_END_HEADERS;
    }

    // Create HEADERS or CONTINUATION frame
    Http2Frame headers(type, stream->get_id(), flags);
    headers.alloc(buffer_size_index[type]);
    http2_write_headers(payload_buffer, payload_length, headers.write());
    headers.finalize(payload_length);

    // xmit event
    MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &headers);
  } while (cont);
}

void
Http2ConnectionState::send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec)
{
  Http2Frame rst_stream(HTTP2_FRAME_TYPE_RST_STREAM, id, 0);

  rst_stream.alloc(buffer_size_index[HTTP2_FRAME_TYPE_RST_STREAM]);
  http2_write_rst_stream(static_cast<uint32_t>(ec), rst_stream.write());
  rst_stream.finalize(HTTP2_RST_STREAM_LEN);

  // xmit event
  MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &rst_stream);
}

void
Http2ConnectionState::send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data)
{
  Http2Frame ping(HTTP2_FRAME_TYPE_PING, id, flag);

  ping.alloc(buffer_size_index[HTTP2_FRAME_TYPE_PING]);
  http2_write_ping(opaque_data, ping.write());
  ping.finalize(HTTP2_PING_LEN);

  // xmit event
  MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ping);
}

void
Http2ConnectionState::send_goaway_frame(Http2StreamId id, Http2ErrorCode ec)
{
  Http2Frame frame(HTTP2_FRAME_TYPE_GOAWAY, 0, 0);
  Http2Goaway goaway;

  ink_assert(this->ua_session != NULL);

  goaway.last_streamid = id;
  goaway.error_code = ec;

  frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_GOAWAY]);
  http2_write_goaway(goaway, frame.write());
  frame.finalize(HTTP2_GOAWAY_LEN);

  // xmit event
  MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &frame);

  handleEvent(HTTP2_SESSION_EVENT_FINI, NULL);
}

void
Http2ConnectionState::send_window_update_frame(Http2StreamId id, uint32_t size)
{
  // Create WINDOW_UPDATE frame
  Http2Frame window_update(HTTP2_FRAME_TYPE_WINDOW_UPDATE, id, 0x0);
  window_update.alloc(buffer_size_index[HTTP2_FRAME_TYPE_WINDOW_UPDATE]);
  http2_write_window_update(static_cast<uint32_t>(size), window_update.write());
  window_update.finalize(sizeof(uint32_t));

  // xmit event
  MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &window_update);
}

void
Http2Stream::init_fetcher(Http2ConnectionState &cstate)
{
  extern ClassAllocator<FetchSM> FetchSMAllocator;

  // Convert header to HTTP/1.1 format
  convert_from_2_to_1_1_header(&_req_header);

  // Get null-terminated URL and method
  Arena arena;
  int url_len, method_len;
  const char *url_ref = _req_header.url_get()->string_get_ref(&url_len);
  const char *url = arena.str_store(url_ref, url_len);
  const char *method_ref = _req_header.method_get(&method_len);
  const char *method = arena.str_store(method_ref, method_len);

  // Initialize FetchSM
  _fetch_sm = FetchSMAllocator.alloc();
  _fetch_sm->ext_init((Continuation *)cstate.ua_session, method, url, HTTP2_FETCHING_HTTP_VERSION,
                      cstate.ua_session->get_client_addr(), (TS_FETCH_FLAGS_DECHUNK | TS_FETCH_FLAGS_NOT_INTERNAL_REQUEST));

  // Set request header
  MIMEFieldIter fiter;
  for (const MIMEField *field = _req_header.iter_get_first(&fiter); field != NULL; field = _req_header.iter_get_next(&fiter)) {
    int name_len, value_len;
    const char *name = field->name_get(&name_len);
    const char *value = field->value_get(&value_len);

    _fetch_sm->ext_add_header(name, name_len, value, value_len);
  }

  _fetch_sm->ext_set_user_data(this);
  _fetch_sm->ext_launch();
}

void
Http2Stream::set_body_to_fetcher(const void *data, size_t len)
{
  ink_assert(_fetch_sm != NULL);

  _fetch_sm->ext_write_data(data, len);
}

/*
 * 5.1.  Stream States
 *
 *                       +--------+
 *                 PP    |        |    PP
 *              ,--------|  idle  |--------.
 *             /         |        |         \
 *            v          +--------+          v
 *     +----------+          |           +----------+
 *     |          |          | H         |          |
 * ,---| reserved |          |           | reserved |---.
 * |   | (local)  |          v           | (remote) |   |
 * |   +----------+      +--------+      +----------+   |
 * |      |          ES  |        |  ES          |      |
 * |      | H    ,-------|  open  |-------.      | H    |
 * |      |     /        |        |        \     |      |
 * |      v    v         +--------+         v    v      |
 * |   +----------+          |           +----------+   |
 * |   |   half   |          |           |   half   |   |
 * |   |  closed  |          | R         |  closed  |   |
 * |   | (remote) |          |           | (local)  |   |
 * |   +----------+          |           +----------+   |
 * |        |                v                 |        |
 * |        |  ES / R    +--------+  ES / R    |        |
 * |        `----------->|        |<-----------'        |
 * |  R                  | closed |                  R  |
 * `-------------------->|        |<--------------------'
 *                       +--------+
 */
bool
Http2Stream::change_state(uint8_t type, uint8_t flags)
{
  switch (_state) {
  case HTTP2_STREAM_STATE_IDLE:
    if (type == HTTP2_FRAME_TYPE_HEADERS) {
      if (flags & HTTP2_FLAGS_HEADERS_END_STREAM) {
        // Skip OPEN _state
        _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else {
        _state = HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_PUSH_PROMISE) {
      // XXX Server Push have been supported yet.
    } else {
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_OPEN:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM) {
      _state = HTTP2_STREAM_STATE_CLOSED;
    } else if (type == HTTP2_FRAME_TYPE_DATA && flags & HTTP2_FLAGS_DATA_END_STREAM) {
      _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
    } else {
      // Currently ATS supports only HTTP/2 server features
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_RESERVED_LOCAL:
    // Currently ATS supports only HTTP/2 server features
    return false;

  case HTTP2_STREAM_STATE_RESERVED_REMOTE:
    // XXX Server Push have been supported yet.
    return false;

  case HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL:
    // Currently ATS supports only HTTP/2 server features
    return false;

  case HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
    if (type == HTTP2_FRAME_TYPE_RST_STREAM || (type == HTTP2_FRAME_TYPE_HEADERS && flags & HTTP2_FLAGS_HEADERS_END_STREAM) ||
        (type == HTTP2_FRAME_TYPE_DATA && flags & HTTP2_FLAGS_DATA_END_STREAM)) {
      _state = HTTP2_STREAM_STATE_CLOSED;
    } else {
      return false;
    }
    break;

  case HTTP2_STREAM_STATE_CLOSED:
    // No state changing
    return false;

  default:
    return false;
  }

  return true;
}
