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
#include "Http2Stream.h"
#include "Http2DebugNames.h"

#define DebugHttp2Con(ua_session, fmt, ...) \
  DebugSsn(ua_session, "http2_con", "[%" PRId64 "] " fmt, ua_session->connection_id(), ##__VA_ARGS__);

#define DebugHttp2Stream(ua_session, stream_id, fmt, ...) \
  DebugSsn(ua_session, "http2_con", "[%" PRId64 "] [%u] " fmt, ua_session->connection_id(), stream_id, ##__VA_ARGS__);

typedef Http2Error (*http2_frame_dispatch)(Http2ConnectionState &, const Http2Frame &);

static const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] = {
  BUFFER_SIZE_INDEX_8K,  // HTTP2_FRAME_TYPE_DATA
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_HEADERS
  -1,                    // HTTP2_FRAME_TYPE_PRIORITY
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_RST_STREAM
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_SETTINGS
  -1,                    // HTTP2_FRAME_TYPE_PUSH_PROMISE
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_PING
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_GOAWAY
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_WINDOW_UPDATE
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_CONTINUATION
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

static Http2Error
rcv_data_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA])];
  unsigned nbytes = 0;
  Http2StreamId id = frame.header().streamid;
  uint8_t pad_length = 0;
  const uint32_t payload_length = frame.header().length;

  DebugHttp2Stream(cstate.ua_session, id, "Received DATA frame");

  // If a DATA frame is received whose stream identifier field is 0x0, the
  // recipient MUST
  // respond with a connection error of type PROTOCOL_ERROR.
  if (!http2_is_client_streamid(id)) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  Http2Stream *stream = cstate.find_stream(id);
  if (stream == NULL) {
    if (id <= cstate.get_latest_stream_id()) {
      return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_STREAM_CLOSED);
    } else {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  // Check to see if FetchSM is NULL
  if (stream->get_fetcher() == NULL) {
    return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_STREAM_CLOSED);
  }

  // If a DATA frame is received whose stream is not in "open" or "half closed
  // (local)" state,
  // the recipient MUST respond with a stream error of type STREAM_CLOSED.
  if (stream->get_state() != HTTP2_STREAM_STATE_OPEN && stream->get_state() != HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL) {
    return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_STREAM_CLOSED);
  }

  if (frame.header().flags & HTTP2_FLAGS_DATA_PADDED) {
    frame.reader()->memcpy(&pad_length, HTTP2_DATA_PADLEN_LEN, nbytes);
    nbytes += HTTP2_DATA_PADLEN_LEN;
    if (pad_length > payload_length) {
      // If the length of the padding is the length of the
      // frame payload or greater, the recipient MUST treat this as a
      // connection error of type PROTOCOL_ERROR.
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  stream->increment_data_length(payload_length - pad_length - nbytes);
  if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
    if (!stream->change_state(frame.header().type, frame.header().flags)) {
      cstate.send_rst_stream_frame(id, HTTP2_ERROR_STREAM_CLOSED);
      return Http2Error(HTTP2_ERROR_CLASS_NONE);
    }
    if (!stream->payload_length_is_valid()) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  // If Data length is 0, do nothing.
  if (payload_length == 0) {
    return Http2Error(HTTP2_ERROR_CLASS_NONE);
  }

  // Check whether Window Size is acceptable
  if (cstate.server_rwnd < payload_length) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FLOW_CONTROL_ERROR);
  }
  if (stream->server_rwnd < payload_length) {
    return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_FLOW_CONTROL_ERROR);
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

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

/*
 * [RFC 7540] 6.2 HEADERS Frame
 *
 * NOTE: HEADERS Frame and CONTINUATION Frame
 *   1. A HEADERS frame with the END_STREAM flag set can be followed by
 *      CONTINUATION frames on the same stream.
 *   2. A HEADERS frame without the END_HEADERS flag set MUST be followed by a
 *      CONTINUATION frame
 */
static Http2Error
rcv_headers_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  DebugHttp2Stream(cstate.ua_session, stream_id, "Received HEADERS frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  Http2Stream *stream = NULL;
  if (stream_id <= cstate.get_latest_stream_id()) {
    stream = cstate.find_stream(stream_id);
    if (stream == NULL || !stream->has_trailing_header()) {
      return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_STREAM_CLOSED);
    }
  } else {
    // Create new stream
    stream = cstate.create_stream(stream_id);
    if (!stream) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  // keep track of how many bytes we get in the frame
  stream->request_header_length += payload_length;
  if (stream->request_header_length > Http2::max_request_header_size) {
    Error("HTTP/2 payload for headers exceeded: %u", stream->request_header_length);
    // XXX Should we respond with 431 (Request Header Fields Too Large) ?
    return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  Http2HeadersParameter params;
  uint32_t header_block_fragment_offset = 0;
  uint32_t header_block_fragment_length = payload_length;

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM) {
    stream->end_stream = true;
  }

  // NOTE: Strip padding if exists
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PADDED) {
    uint8_t buf[HTTP2_HEADERS_PADLEN_LEN] = {0};
    frame.reader()->memcpy(buf, HTTP2_HEADERS_PADLEN_LEN);

    if (!http2_parse_headers_parameter(make_iovec(buf, HTTP2_HEADERS_PADLEN_LEN), params)) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    if (params.pad_length > payload_length) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    header_block_fragment_offset += HTTP2_HEADERS_PADLEN_LEN;
    header_block_fragment_length -= (HTTP2_HEADERS_PADLEN_LEN + params.pad_length);
  }

  // NOTE: Parse priority parameters if exists
  // TODO: Currently priority is NOT supported. TS-3535 will fix this.
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PRIORITY) {
    uint8_t buf[HTTP2_PRIORITY_LEN] = {0};

    frame.reader()->memcpy(buf, HTTP2_PRIORITY_LEN, header_block_fragment_offset);
    if (!http2_parse_priority_parameter(make_iovec(buf, HTTP2_PRIORITY_LEN), params.priority)) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
    // Protocol error if the stream depends on itself
    if (stream_id == params.priority.stream_dependency) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    header_block_fragment_offset += HTTP2_PRIORITY_LEN;
    header_block_fragment_length -= HTTP2_PRIORITY_LEN;
  }

  stream->header_blocks = static_cast<uint8_t *>(ats_malloc(header_block_fragment_length));
  frame.reader()->memcpy(stream->header_blocks, header_block_fragment_length, header_block_fragment_offset);

  stream->header_blocks_length = header_block_fragment_length;

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    if (!stream->change_state(HTTP2_FRAME_TYPE_HEADERS, frame.header().flags) && stream->has_trailing_header() == false) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    bool skip_fetcher = false;
    if (stream->has_trailing_header()) {
      if (!(frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM)) {
        return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
      }
      // If the flag has already been set before decoding header blocks, this is the trailing header.
      // Set a flag to avoid initializing fetcher for now.
      // Decoding header blocks is stil needed to maintain a HPACK dynamic table.
      // TODO: TS-3812
      skip_fetcher = true;
    }

    Http2ErrorCode result = stream->decode_header_blocks(*cstate.local_hpack_handle);

    if (result != HTTP2_ERROR_NO_ERROR) {
      if (result == HTTP2_ERROR_COMPRESSION_ERROR) {
        return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_COMPRESSION_ERROR);
      } else {
        return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    if (!skip_fetcher) {
      if (!stream->init_fetcher(cstate)) {
        return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }
  } else {
    // NOTE: Expect CONTINUATION Frame. Do NOT change state of stream or decode
    // Header Blocks.
    DebugHttp2Stream(cstate.ua_session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
    cstate.set_continued_stream_id(stream_id);
  }

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_priority_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  DebugHttp2Stream(cstate.ua_session, frame.header().streamid, "Received PRIORITY frame");

  // If a PRIORITY frame is received with a stream identifier of 0x0, the
  // recipient MUST respond with a connection error of type PROTOCOL_ERROR.
  if (frame.header().streamid == 0) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  // A PRIORITY frame with a length other than 5 octets MUST be treated as
  // a stream error (Section 5.4.2) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_PRIORITY_LEN) {
    return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_FRAME_SIZE_ERROR);
  }

  // TODO Pick stream dependencies and weight
  // Supporting PRIORITY is not essential so its temporarily ignored.

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_rst_stream_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2RstStream rst_stream;
  char buf[HTTP2_RST_STREAM_LEN];
  char *end;
  const Http2StreamId stream_id = frame.header().streamid;

  DebugHttp2Stream(cstate.ua_session, frame.header().streamid, "Received RST_STREAM frame");

  // RST_STREAM frames MUST be associated with a stream.  If a RST_STREAM
  // frame is received with a stream identifier of 0x0, the recipient MUST
  // treat this as a connection error (Section 5.4.1) of type
  // PROTOCOL_ERROR.
  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  Http2Stream *stream = cstate.find_stream(stream_id);
  if (stream == NULL) {
    if (stream_id <= cstate.get_latest_stream_id()) {
      return Http2Error(HTTP2_ERROR_CLASS_NONE);
    } else {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  // A RST_STREAM frame with a length other than 4 octets MUST be treated
  // as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_RST_STREAM_LEN) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FRAME_SIZE_ERROR);
  }

  if (stream == NULL || !stream->change_state(frame.header().type, frame.header().flags)) {
    // If a RST_STREAM frame identifying an idle stream is received, the
    // recipient MUST treat this as a connection error of type PROTOCOL_ERROR.
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  end = frame.reader()->memcpy(buf, sizeof(buf), 0);

  if (!http2_parse_rst_stream(make_iovec(buf, end - buf), rst_stream)) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  if (stream != NULL) {
    DebugHttp2Stream(cstate.ua_session, stream_id, "RST_STREAM: Error Code: %u", rst_stream.error_code);

    cstate.delete_stream(stream);
  }

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_settings_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2SettingsParameter param;
  char buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned nbytes = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  DebugHttp2Stream(cstate.ua_session, stream_id, "Received SETTINGS frame");

  // [RFC 7540] 6.5. The stream identifier for a SETTINGS frame MUST be zero.
  // If an endpoint receives a SETTINGS frame whose stream identifier field is
  // anything other than 0x0, the endpoint MUST respond with a connection
  // error (Section 5.4.1) of type PROTOCOL_ERROR.
  if (stream_id != 0) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  // [RFC 7540] 6.5. Receipt of a SETTINGS frame with the ACK flag set and a
  // length field value other than 0 MUST be treated as a connection
  // error of type FRAME_SIZE_ERROR.
  if (frame.header().flags & HTTP2_FLAGS_SETTINGS_ACK) {
    if (frame.header().length == 0) {
      return Http2Error(HTTP2_ERROR_CLASS_NONE);
    } else {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FRAME_SIZE_ERROR);
    }
  }

  // A SETTINGS frame with a length other than a multiple of 6 octets MUST
  // be treated as a connection error (Section 5.4.1) of type
  // FRAME_SIZE_ERROR.
  if (frame.header().length % 6 != 0) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FRAME_SIZE_ERROR);
  }

  while (nbytes < frame.header().length) {
    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_settings_parameter(make_iovec(buf, read_bytes), param)) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    if (!http2_settings_parameter_is_valid(param)) {
      if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
        return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FLOW_CONTROL_ERROR);
      } else {
        return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    DebugHttp2Stream(cstate.ua_session, stream_id, "   %s : %u", Http2DebugNames::get_settings_param_name(param.id), param.value);

    // [RFC 7540] 6.9.2. When the value of SETTINGS_INITIAL_WINDOW_SIZE
    // changes, a receiver MUST adjust the size of all stream flow control
    // windows that it maintains by the difference between the new value and
    // the old value.
    if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
      cstate.update_initial_rwnd(param.value);
    }

    cstate.client_settings.set((Http2SettingsIdentifier)param.id, param.value);
  }

  // [RFC 7540] 6.5. Once all values have been applied, the recipient MUST
  // immediately emit a SETTINGS frame with the ACK flag set.
  Http2Frame ackFrame(HTTP2_FRAME_TYPE_SETTINGS, 0, HTTP2_FLAGS_SETTINGS_ACK);
  cstate.ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ackFrame);

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_push_promise_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  DebugHttp2Stream(cstate.ua_session, frame.header().streamid, "Received PUSH_PROMISE frame");

  // [RFC 7540] 8.2. A client cannot push. Thus, servers MUST treat the receipt of a
  // PUSH_PROMISE frame as a connection error of type PROTOCOL_ERROR.
  return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
}

static Http2Error
rcv_ping_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  uint8_t opaque_data[HTTP2_PING_LEN];
  const Http2StreamId stream_id = frame.header().streamid;

  DebugHttp2Stream(cstate.ua_session, stream_id, "Received PING frame");

  //  If a PING frame is received with a stream identifier field value other
  //  than 0x0, the recipient MUST respond with a connection error of type
  //  PROTOCOL_ERROR.
  if (stream_id != 0x0) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  // Receipt of a PING frame with a length field value other than 8 MUST
  // be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_PING_LEN) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FRAME_SIZE_ERROR);
  }

  // An endpoint MUST NOT respond to PING frames containing this flag.
  if (frame.header().flags & HTTP2_FLAGS_PING_ACK) {
    return Http2Error(HTTP2_ERROR_CLASS_NONE);
  }

  frame.reader()->memcpy(opaque_data, HTTP2_PING_LEN, 0);

  // ACK (0x1): An endpoint MUST set this flag in PING responses.
  cstate.send_ping_frame(stream_id, HTTP2_FLAGS_PING_ACK, opaque_data);

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_goaway_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2Goaway goaway;
  char buf[HTTP2_GOAWAY_LEN];
  unsigned nbytes = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  DebugHttp2Stream(cstate.ua_session, stream_id, "Received GOAWAY frame");

  // An endpoint MUST treat a GOAWAY frame with a stream identifier other
  // than 0x0 as a connection error of type PROTOCOL_ERROR.
  if (stream_id != 0x0) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  while (nbytes < frame.header().length) {
    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_goaway(make_iovec(buf, read_bytes), goaway)) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  DebugHttp2Stream(cstate.ua_session, stream_id, "GOAWAY: last stream id=%d, error code=%d", goaway.last_streamid,
                   goaway.error_code);

  cstate.handleEvent(HTTP2_SESSION_EVENT_FINI, NULL);
  // eventProcessor.schedule_imm(&cs, ET_NET, VC_EVENT_ERROR);

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_window_update_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[HTTP2_WINDOW_UPDATE_LEN];
  uint32_t size;
  const Http2StreamId sid = frame.header().streamid;

  //  A WINDOW_UPDATE frame with a length other than 4 octets MUST be
  //  treated as a connection error of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_WINDOW_UPDATE_LEN) {
    DebugHttp2Stream(cstate.ua_session, sid, "Received WINDOW_UPDATE frame - length incorrect");
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FRAME_SIZE_ERROR);
  }

  if (sid == 0) {
    // Connection level window update
    frame.reader()->memcpy(buf, sizeof(buf), 0);
    http2_parse_window_update(make_iovec(buf, sizeof(buf)), size);

    DebugHttp2Stream(cstate.ua_session, sid, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (cstate.client_rwnd + size), size);

    // A receiver MUST treat the receipt of a WINDOW_UPDATE frame with a
    // connection
    // flow control window increment of 0 as a connection error of type
    // PROTOCOL_ERROR;
    if (size == 0) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - cstate.client_rwnd) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_FLOW_CONTROL_ERROR);
    }

    cstate.client_rwnd += size;
    cstate.restart_streams();
  } else {
    // Stream level window update
    Http2Stream *stream = cstate.find_stream(sid);

    if (stream == NULL) {
      if (sid <= cstate.get_latest_stream_id()) {
        return Http2Error(HTTP2_ERROR_CLASS_NONE);
      } else {
        return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    frame.reader()->memcpy(buf, sizeof(buf), 0);
    http2_parse_window_update(make_iovec(buf, sizeof(buf)), size);

    DebugHttp2Stream(cstate.ua_session, sid, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (stream->client_rwnd + size), size);

    // A receiver MUST treat the receipt of a WINDOW_UPDATE frame with an
    // flow control window increment of 0 as a stream error of type
    // PROTOCOL_ERROR;
    if (size == 0) {
      return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - stream->client_rwnd) {
      return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_FLOW_CONTROL_ERROR);
    }

    stream->client_rwnd += size;
    ssize_t wnd = min(cstate.client_rwnd, stream->client_rwnd);
    if (stream->get_state() == HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE && wnd > 0) {
      cstate.send_data_frame(stream->get_fetcher());
    }
  }

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
}

/*
 * [RFC 7540] 6.10 CONTINUATION
 *
 * NOTE: Logically, the CONTINUATION frames are part of the HEADERS frame. ([RFC
 *7540] 6.2 HEADERS)
 *
 */
static Http2Error
rcv_continuation_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  DebugHttp2Stream(cstate.ua_session, stream_id, "Received CONTINUATION frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  // Find opened stream
  // CONTINUATION frames MUST be associated with a stream.  If a
  // CONTINUATION frame is received whose stream identifier field is 0x0,
  // the recipient MUST respond with a connection error ([RFC 7540] Section
  // 5.4.1) of type PROTOCOL_ERROR.
  Http2Stream *stream = cstate.find_stream(stream_id);
  if (stream == NULL) {
    if (stream_id <= cstate.get_latest_stream_id()) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_STREAM_CLOSED);
    } else {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  } else {
    switch (stream->get_state()) {
    case HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_STREAM_CLOSED);
    case HTTP2_STREAM_STATE_IDLE:
      break;
    default:
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }
  }

  // keep track of how many bytes we get in the frame
  stream->request_header_length += payload_length;
  if (stream->request_header_length > Http2::max_request_header_size) {
    Error("HTTP/2 payload for headers exceeded: %u", stream->request_header_length);
    return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
  }

  uint32_t header_blocks_offset = stream->header_blocks_length;
  stream->header_blocks_length += payload_length;

  stream->header_blocks = static_cast<uint8_t *>(ats_realloc(stream->header_blocks, stream->header_blocks_length));
  frame.reader()->memcpy(stream->header_blocks + header_blocks_offset, payload_length);

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    cstate.clear_continued_stream_id();

    if (!stream->change_state(HTTP2_FRAME_TYPE_CONTINUATION, frame.header().flags)) {
      return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_PROTOCOL_ERROR);
    }

    Http2ErrorCode result = stream->decode_header_blocks(*cstate.local_hpack_handle);

    if (result != HTTP2_ERROR_NO_ERROR) {
      if (result == HTTP2_ERROR_COMPRESSION_ERROR) {
        return Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_COMPRESSION_ERROR);
      } else {
        return Http2Error(HTTP2_ERROR_CLASS_STREAM, HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    stream->init_fetcher(cstate);
  } else {
    // NOTE: Expect another CONTINUATION Frame. Do nothing.
    DebugHttp2Stream(cstate.ua_session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
  }

  return Http2Error(HTTP2_ERROR_CLASS_NONE);
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

    // [RFC 7540] 3.5. HTTP/2 Connection Preface. Upon establishment of a TCP connection and
    // determination that HTTP/2 will be used by both peers, each endpoint MUST
    // send a connection preface as a final confirmation ... The server
    // connection
    // preface consists of a potentially empty SETTINGS frame.

    // Load the server settings from the records.config / RecordsConfig.cc
    // settings.
    Http2ConnectionSettings configured_settings;
    configured_settings.settings_from_configs();
    configured_settings.set(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, _adjust_concurrent_stream());

    send_settings_frame(configured_settings);

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
    const Http2Frame *frame = (Http2Frame *)edata;
    const Http2StreamId stream_id = frame->header().streamid;
    Http2Error error;

    // [RFC 7540] 5.5. Extending HTTP/2
    //   Implementations MUST discard frames that have unknown or unsupported types.
    if (frame->header().type >= HTTP2_FRAME_TYPE_MAX) {
      DebugHttp2Stream(ua_session, stream_id, "Discard a frame which has unknown type, type=%x", frame->header().type);
      return 0;
    }

    if (frame_handlers[frame->header().type]) {
      error = frame_handlers[frame->header().type](*this, *frame);
    } else {
      error = Http2Error(HTTP2_ERROR_CLASS_CONNECTION, HTTP2_ERROR_INTERNAL_ERROR);
    }

    if (error.cls != HTTP2_ERROR_CLASS_NONE) {
      if (error.cls == HTTP2_ERROR_CLASS_CONNECTION) {
        this->send_goaway_frame(stream_id, error.code);
        cleanup_streams();
        // XXX We need to think a bit harder about how to coordinate the client
        // session and the
        // protocol connection. At this point, the protocol is shutting down,
        // but there's no way
        // to tell that to the client session. Perhaps this could be solved by
        // implementing the
        // half-closed state ...
        SET_HANDLER(&Http2ConnectionState::state_closed);
      } else if (error.cls == HTTP2_ERROR_CLASS_STREAM) {
        this->send_rst_stream_frame(stream_id, error.code);
      }
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
    DebugHttp2Con(ua_session, "unexpected event=%d edata=%p", event, edata);
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
  if (client_streams_count >= server_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) {
    return NULL;
  }

  Http2Stream *new_stream = new Http2Stream(new_id, client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));
  stream_list.push(new_stream);
  latest_streamid = new_id;

  ink_assert(client_streams_count < UINT32_MAX);
  ++client_streams_count;
  ua_session->get_netvc()->add_to_active_queue();

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
    if (s->get_state() == HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE && min(this->client_rwnd, s->client_rwnd) > 0) {
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
  if (!is_state_closed()) {
    ua_session->get_netvc()->add_to_keep_alive_queue();
  }
}

void
Http2ConnectionState::delete_stream(Http2Stream *stream)
{
  stream_list.remove(stream);
  delete stream;

  ink_assert(client_streams_count > 0);
  --client_streams_count;

  if (client_streams_count == 0) {
    ua_session->get_netvc()->add_to_keep_alive_queue();
  }
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
  if (fetch_sm == NULL) {
    return;
  }

  size_t buf_len = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]) - HTTP2_FRAME_HEADER_LEN;
  uint8_t payload_buffer[buf_len];

  Http2Stream *stream = static_cast<Http2Stream *>(fetch_sm->ext_get_user_data());

  if (stream->get_state() == HTTP2_STREAM_STATE_CLOSED) {
    return;
  }

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
    DebugHttp2Stream(ua_session, stream->get_id(), "Send DATA frame - client window con: %zd stream: %zd payload: %zd", client_rwnd,
                     stream->client_rwnd, payload_length);
    Http2Frame data(HTTP2_FRAME_TYPE_DATA, stream->get_id(), flags);
    data.alloc(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
    http2_write_data(payload_buffer, payload_length, data.write());
    data.finalize(payload_length);

    // Change state to 'closed' if its end of DATAs.
    if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
      DebugHttp2Stream(ua_session, stream->get_id(), "End of DATA frame");
      if (!stream->change_state(data.header().type, data.header().flags)) {
        this->send_goaway_frame(stream->get_id(), HTTP2_ERROR_PROTOCOL_ERROR);
      }
    }

    // xmit event
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &data);

    if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
      // Delete a stream immediately
      // TODO its should not be deleted for a several time to handling
      // RST_STREAM and WINDOW_UPDATE.
      // See 'closed' state written at [RFC 7540] 5.1.
      this->delete_stream(stream);
      break;
    }
  }
}

void
Http2ConnectionState::send_headers_frame(FetchSM *fetch_sm)
{
  uint8_t *buf = NULL;
  uint32_t buf_len = 0;
  uint32_t header_blocks_size = 0;
  int payload_length = 0;
  uint64_t sent = 0;
  uint8_t flags = 0x00;

  Http2Stream *stream = static_cast<Http2Stream *>(fetch_sm->ext_get_user_data());
  HTTPHdr *resp_header = reinterpret_cast<HTTPHdr *>(fetch_sm->resp_hdr_bufp());

  DebugHttp2Stream(ua_session, stream->get_id(), "Send HEADERS frame");

  http2_convert_header_from_1_1_to_2(resp_header);
  buf_len = resp_header->length_get() * 2; // Make it double just in case
  buf = (uint8_t *)ats_malloc(buf_len);
  if (buf == NULL) {
    return;
  }
  Http2ErrorCode result = http2_encode_header_blocks(resp_header, buf, buf_len, &header_blocks_size, *(this->remote_hpack_handle));
  if (result != HTTP2_ERROR_NO_ERROR) {
    ats_free(buf);
    return;
  }

  // Send a HEADERS frame
  if (header_blocks_size <= BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]) - HTTP2_FRAME_HEADER_LEN) {
    payload_length = header_blocks_size;
    flags |= HTTP2_FLAGS_HEADERS_END_HEADERS;
    if (resp_header->presence(MIME_PRESENCE_CONTENT_LENGTH) && resp_header->get_content_length() == 0) {
      flags |= HTTP2_FLAGS_HEADERS_END_STREAM;
    }
  } else {
    payload_length = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]) - HTTP2_FRAME_HEADER_LEN;
  }
  Http2Frame headers(HTTP2_FRAME_TYPE_HEADERS, stream->get_id(), flags);
  headers.alloc(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]);
  http2_write_headers(buf, payload_length, headers.write());
  headers.finalize(payload_length);
  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &headers);
  sent += payload_length;

  // Send CONTINUATION frames
  flags = 0;
  while (sent < header_blocks_size) {
    DebugHttp2Stream(ua_session, stream->get_id(), "Send CONTINUATION frame");
    payload_length = MIN(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION]) - HTTP2_FRAME_HEADER_LEN,
                         header_blocks_size - sent);
    if (sent + payload_length == header_blocks_size) {
      flags |= HTTP2_FLAGS_CONTINUATION_END_HEADERS;
    }
    Http2Frame headers(HTTP2_FRAME_TYPE_CONTINUATION, stream->get_id(), flags);
    headers.alloc(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION]);
    http2_write_headers(buf + sent, payload_length, headers.write());
    headers.finalize(payload_length);
    // xmit event
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &headers);
    sent += payload_length;
  }

  ats_free(buf);
}

void
Http2ConnectionState::send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec)
{
  DebugHttp2Stream(ua_session, id, "Send RST_STREAM frame");

  if (ec != HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_STREAM_ERRORS_COUNT, this_ethread());
  }

  Http2Frame rst_stream(HTTP2_FRAME_TYPE_RST_STREAM, id, 0);

  rst_stream.alloc(buffer_size_index[HTTP2_FRAME_TYPE_RST_STREAM]);
  http2_write_rst_stream(static_cast<uint32_t>(ec), rst_stream.write());
  rst_stream.finalize(HTTP2_RST_STREAM_LEN);

  // change state to closed
  Http2Stream *stream = find_stream(id);
  if (stream != NULL) {
    if (!stream->change_state(HTTP2_FRAME_TYPE_RST_STREAM, 0)) {
      this->send_goaway_frame(stream->get_id(), HTTP2_ERROR_PROTOCOL_ERROR);
      return;
    }
  }

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &rst_stream);
}

void
Http2ConnectionState::send_settings_frame(const Http2ConnectionSettings &new_settings)
{
  const Http2StreamId stream_id = 0;

  DebugHttp2Stream(ua_session, stream_id, "Send SETTINGS frame");

  Http2Frame settings(HTTP2_FRAME_TYPE_SETTINGS, stream_id, 0);
  settings.alloc(buffer_size_index[HTTP2_FRAME_TYPE_SETTINGS]);

  IOVec iov = settings.write();
  uint32_t settings_length = 0;

  for (int i = HTTP2_SETTINGS_HEADER_TABLE_SIZE; i < HTTP2_SETTINGS_MAX; ++i) {
    Http2SettingsIdentifier id = static_cast<Http2SettingsIdentifier>(i);
    unsigned settings_value = new_settings.get(id);

    // Send only difference
    if (settings_value != server_settings.get(id)) {
      const Http2SettingsParameter param = {static_cast<uint16_t>(id), settings_value};

      // Write settings to send buffer
      if (!http2_write_settings(param, iov)) {
        send_goaway_frame(0, HTTP2_ERROR_INTERNAL_ERROR);
        return;
      }
      iov.iov_base = reinterpret_cast<uint8_t *>(iov.iov_base) + HTTP2_SETTINGS_PARAMETER_LEN;
      iov.iov_len -= HTTP2_SETTINGS_PARAMETER_LEN;
      settings_length += HTTP2_SETTINGS_PARAMETER_LEN;

      // Update current settings
      server_settings.set(id, new_settings.get(id));

      DebugHttp2Stream(ua_session, stream_id, "  %s : %u", Http2DebugNames::get_settings_param_name(param.id), param.value);
    }
  }

  settings.finalize(settings_length);
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &settings);
}

void
Http2ConnectionState::send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data)
{
  DebugHttp2Stream(ua_session, id, "Send PING frame");

  Http2Frame ping(HTTP2_FRAME_TYPE_PING, id, flag);

  ping.alloc(buffer_size_index[HTTP2_FRAME_TYPE_PING]);
  http2_write_ping(opaque_data, ping.write());
  ping.finalize(HTTP2_PING_LEN);

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ping);
}

void
Http2ConnectionState::send_goaway_frame(Http2StreamId id, Http2ErrorCode ec)
{
  DebugHttp2Stream(ua_session, id, "Send GOAWAY frame");

  if (ec != HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CONNECTION_ERRORS_COUNT, this_ethread());
  }

  Http2Frame frame(HTTP2_FRAME_TYPE_GOAWAY, 0, 0);
  Http2Goaway goaway;

  ink_assert(this->ua_session != NULL);

  goaway.last_streamid = id;
  goaway.error_code = ec;

  frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_GOAWAY]);
  http2_write_goaway(goaway, frame.write());
  frame.finalize(HTTP2_GOAWAY_LEN);

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &frame);

  handleEvent(HTTP2_SESSION_EVENT_FINI, NULL);
}

void
Http2ConnectionState::send_window_update_frame(Http2StreamId id, uint32_t size)
{
  DebugHttp2Stream(ua_session, id, "Send WINDOW_UPDATE frame");

  // Create WINDOW_UPDATE frame
  Http2Frame window_update(HTTP2_FRAME_TYPE_WINDOW_UPDATE, id, 0x0);
  window_update.alloc(buffer_size_index[HTTP2_FRAME_TYPE_WINDOW_UPDATE]);
  http2_write_window_update(static_cast<uint32_t>(size), window_update.write());
  window_update.finalize(sizeof(uint32_t));

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &window_update);
}

// Return min_concurrent_streams_in when current client streams number is larger than max_active_streams_in.
// Main purpose of this is preventing DDoS Attacks.
unsigned
Http2ConnectionState::_adjust_concurrent_stream()
{
  if (Http2::max_active_streams_in == 0) {
    // Throttling down is disabled.
    return Http2::max_concurrent_streams_in;
  }

  int64_t current_client_streams = 0;
  RecGetRawStatSum(http2_rsb, HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT, &current_client_streams);

  DebugHttp2Con(ua_session, "current client streams: %" PRId64, current_client_streams);

  if (current_client_streams >= Http2::max_active_streams_in) {
    if (!Http2::throttling) {
      Warning("too many streams: %" PRId64 ", reduce SETTINGS_MAX_CONCURRENT_STREAMS to %d", current_client_streams,
              Http2::min_concurrent_streams_in);
      Http2::throttling = true;
    }

    return Http2::min_concurrent_streams_in;
  } else {
    if (Http2::throttling) {
      Note("revert SETTINGS_MAX_CONCURRENT_STREAMS to %d", Http2::max_concurrent_streams_in);
      Http2::throttling = false;
    }
  }

  return Http2::max_concurrent_streams_in;
}
