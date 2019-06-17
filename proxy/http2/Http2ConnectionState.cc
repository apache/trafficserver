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
#include "HttpDebugNames.h"
#include <sstream>

#define REMEMBER(e, r)                                        \
  {                                                           \
    if (this->ua_session) {                                   \
      this->ua_session->remember(MakeSourceLocation(), e, r); \
    }                                                         \
  }

#define Http2ConDebug(ua_session, fmt, ...) \
  SsnDebug(ua_session, "http2_con", "[%" PRId64 "] " fmt, ua_session->connection_id(), ##__VA_ARGS__);

#define Http2StreamDebug(ua_session, stream_id, fmt, ...) \
  SsnDebug(ua_session, "http2_con", "[%" PRId64 "] [%u] " fmt, ua_session->connection_id(), stream_id, ##__VA_ARGS__);

using http2_frame_dispatch = Http2Error (*)(Http2ConnectionState &, const Http2Frame &);

static const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] = {
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_DATA
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_HEADERS
  -1,                    // HTTP2_FRAME_TYPE_PRIORITY
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_RST_STREAM
  BUFFER_SIZE_INDEX_128, // HTTP2_FRAME_TYPE_SETTINGS
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_PUSH_PROMISE
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
  unsigned nbytes               = 0;
  Http2StreamId id              = frame.header().streamid;
  uint8_t pad_length            = 0;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(cstate.ua_session, id, "Received DATA frame");

  if (cstate.get_zombie_event()) {
    Warning("Data frame for zombied session %" PRId64, cstate.ua_session->connection_id());
  }

  // If a DATA frame is received whose stream identifier field is 0x0, the
  // recipient MUST
  // respond with a connection error of type PROTOCOL_ERROR.
  if (!http2_is_client_streamid(id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv data bad frame client id");
  }

  Http2Stream *stream = cstate.find_stream(id);
  if (stream == nullptr) {
    if (cstate.is_valid_streamid(id)) {
      // This error occurs fairly often, and is probably innocuous (SM initiates the shutdown)
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED, nullptr);
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data stream freed with invalid id");
    }
  }

  // If a DATA frame is received whose stream is not in "open" or "half closed
  // (local)" state,
  // the recipient MUST respond with a stream error of type STREAM_CLOSED.
  if (stream->get_state() != Http2StreamState::HTTP2_STREAM_STATE_OPEN &&
      stream->get_state() != Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                      "recv data stream closed");
  }

  if (frame.header().flags & HTTP2_FLAGS_DATA_PADDED) {
    frame.reader()->memcpy(&pad_length, HTTP2_DATA_PADLEN_LEN, nbytes);
    nbytes += HTTP2_DATA_PADLEN_LEN;
    if (pad_length > payload_length) {
      // If the length of the padding is the length of the
      // frame payload or greater, the recipient MUST treat this as a
      // connection error of type PROTOCOL_ERROR.
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data pad > payload");
    }
  }

  stream->increment_data_length(payload_length - pad_length - nbytes);
  if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
    stream->recv_end_stream = true;
    if (!stream->change_state(frame.header().type, frame.header().flags)) {
      cstate.send_rst_stream_frame(id, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED);
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
    if (!stream->payload_length_is_valid()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data bad payload length");
    }
  }

  // If Data length is 0, do nothing.
  if (payload_length == 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  // Check whether Window Size is acceptable
  if (cstate.server_rwnd < payload_length) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data cstate.server_rwnd < payload_length");
  }
  if (stream->server_rwnd < payload_length) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data stream->server_rwnd < payload_length");
  }

  // Update Window size
  cstate.server_rwnd -= payload_length;
  stream->server_rwnd -= payload_length;

  const uint32_t unpadded_length = payload_length - pad_length;
  // If we call write() multiple times, we must keep the same reader, so we can
  // update its offset via consume.  Otherwise, we will read the same data on the
  // second time through
  IOBufferReader *myreader = frame.reader()->clone();
  // Skip pad length field
  if (frame.header().flags & HTTP2_FLAGS_DATA_PADDED) {
    myreader->consume(HTTP2_DATA_PADLEN_LEN);
  }
  while (nbytes < payload_length - pad_length) {
    size_t read_len = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
    if (nbytes + read_len > unpadded_length) {
      read_len -= nbytes + read_len - unpadded_length;
    }
    nbytes += stream->request_buffer.write(myreader, read_len);
    myreader->consume(nbytes);
    // If there is an outstanding read, update the buffer
    stream->update_read_request(INT64_MAX, true);
  }
  myreader->writer()->dealloc_reader(myreader);

  uint32_t initial_rwnd = cstate.server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  uint32_t min_rwnd     = std::min(initial_rwnd, cstate.server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE));
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

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
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

  Http2StreamDebug(cstate.ua_session, stream_id, "Received HEADERS frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv headers bad client id");
  }

  Http2Stream *stream = nullptr;
  bool new_stream     = false;

  if (cstate.is_valid_streamid(stream_id)) {
    stream = cstate.find_stream(stream_id);
    if (stream == nullptr || !stream->has_trailing_header()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "recv headers cannot find existing stream_id");
    }
  } else {
    // Create new stream
    Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    stream     = cstate.create_stream(stream_id, error);
    new_stream = true;
    if (!stream) {
      return error;
    }
  }

  // Ignoring HEADERS frame on a closed stream.  The HdrHeap has gone away and it will core.
  if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  // keep track of how many bytes we get in the frame
  stream->request_header_length += payload_length;
  if (stream->request_header_length > Http2::max_header_list_size) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv headers payload for headers greater than header length");
  }

  Http2HeadersParameter params;
  uint32_t header_block_fragment_offset = 0;
  uint32_t header_block_fragment_length = payload_length;

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM) {
    stream->recv_end_stream = true;
  }

  // NOTE: Strip padding if exists
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PADDED) {
    uint8_t buf[HTTP2_HEADERS_PADLEN_LEN] = {0};
    frame.reader()->memcpy(buf, HTTP2_HEADERS_PADLEN_LEN);

    if (!http2_parse_headers_parameter(make_iovec(buf, HTTP2_HEADERS_PADLEN_LEN), params)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers failed to parse");
    }

    if (params.pad_length > payload_length) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers pad > payload length");
    }

    header_block_fragment_offset += HTTP2_HEADERS_PADLEN_LEN;
    header_block_fragment_length -= (HTTP2_HEADERS_PADLEN_LEN + params.pad_length);
  }

  // NOTE: Parse priority parameters if exists
  if (frame.header().flags & HTTP2_FLAGS_HEADERS_PRIORITY) {
    uint8_t buf[HTTP2_PRIORITY_LEN] = {0};

    frame.reader()->memcpy(buf, HTTP2_PRIORITY_LEN, header_block_fragment_offset);
    if (!http2_parse_priority_parameter(make_iovec(buf, HTTP2_PRIORITY_LEN), params.priority)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers prioirity parameters failed parse");
    }
    // Protocol error if the stream depends on itself
    if (stream_id == params.priority.stream_dependency) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers self dependency");
    }

    header_block_fragment_offset += HTTP2_PRIORITY_LEN;
    header_block_fragment_length -= HTTP2_PRIORITY_LEN;
  }

  if (new_stream && Http2::stream_priority_enabled) {
    Http2DependencyTree::Node *node = cstate.dependency_tree->find(stream_id);
    if (node != nullptr) {
      stream->priority_node = node;
      node->t               = stream;
    } else {
      Http2StreamDebug(cstate.ua_session, stream_id, "HEADER PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d",
                       params.priority.stream_dependency, params.priority.weight, params.priority.exclusive_flag,
                       cstate.dependency_tree->size());

      stream->priority_node = cstate.dependency_tree->add(params.priority.stream_dependency, stream_id, params.priority.weight,
                                                          params.priority.exclusive_flag, stream);
    }
  }

  stream->header_blocks = static_cast<uint8_t *>(ats_malloc(header_block_fragment_length));
  frame.reader()->memcpy(stream->header_blocks, header_block_fragment_length, header_block_fragment_offset);

  stream->header_blocks_length = header_block_fragment_length;

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    if (!stream->change_state(HTTP2_FRAME_TYPE_HEADERS, frame.header().flags) && stream->has_trailing_header() == false) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers end headers and not trailing header");
    }

    bool empty_request = false;
    if (stream->has_trailing_header()) {
      if (!(frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM)) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "recv headers tailing header without endstream");
      }
      // If the flag has already been set before decoding header blocks, this is the trailing header.
      // Set a flag to avoid initializing fetcher for now.
      // Decoding header blocks is stil needed to maintain a HPACK dynamic table.
      // TODO: TS-3812
      empty_request = true;
    }

    Http2ErrorCode result =
      stream->decode_header_blocks(*cstate.local_hpack_handle, cstate.server_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

    if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
      if (result == Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR,
                          "recv headers compression error");
      } else if (result == Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                          "recv headers enhance your calm");
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "recv headers malformed request");
      }
    }

    // Set up the State Machine
    if (!empty_request) {
      SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
      stream->new_transaction();
      // Send request header to SM
      stream->send_request(cstate);
    }
  } else {
    // NOTE: Expect CONTINUATION Frame. Do NOT change state of stream or decode
    // Header Blocks.
    Http2StreamDebug(cstate.ua_session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
    cstate.set_continued_stream_id(stream_id);
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

/*
 * [RFC 7540] 6.3 PRIORITY
 *
 */
static Http2Error
rcv_priority_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(cstate.ua_session, stream_id, "Received PRIORITY frame");

  if (cstate.get_zombie_event()) {
    Warning("Priority frame for zombied session %" PRId64, cstate.ua_session->connection_id());
  }

  // If a PRIORITY frame is received with a stream identifier of 0x0, the
  // recipient MUST respond with a connection error of type PROTOCOL_ERROR.
  if (stream_id == 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "priority 0 stream_id");
  }

  // A PRIORITY frame with a length other than 5 octets MUST be treated as
  // a stream error (Section 5.4.2) of type FRAME_SIZE_ERROR.
  if (payload_length != HTTP2_PRIORITY_LEN) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "priority bad length");
  }

  uint8_t buf[HTTP2_PRIORITY_LEN] = {0};
  frame.reader()->memcpy(buf, HTTP2_PRIORITY_LEN, 0);

  Http2Priority priority;
  if (!http2_parse_priority_parameter(make_iovec(buf, HTTP2_PRIORITY_LEN), priority)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "priority parse error");
  }

  //  A stream cannot depend on itself.  An endpoint MUST treat this as a stream error of type PROTOCOL_ERROR.
  if (stream_id == priority.stream_dependency) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "PRIORITY frame depends on itself");
  }

  if (!Http2::stream_priority_enabled) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  Http2StreamDebug(cstate.ua_session, stream_id, "PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d",
                   priority.stream_dependency, priority.weight, priority.exclusive_flag, cstate.dependency_tree->size());

  Http2DependencyTree::Node *node = cstate.dependency_tree->find(stream_id);

  if (node != nullptr) {
    // [RFC 7540] 5.3.3 Reprioritization
    Http2StreamDebug(cstate.ua_session, stream_id, "Reprioritize");
    cstate.dependency_tree->reprioritize(node, priority.stream_dependency, priority.exclusive_flag);
    if (is_debug_tag_set("http2_priority")) {
      std::stringstream output;
      cstate.dependency_tree->dump_tree(output);
      Debug("http2_priority", "[%" PRId64 "] reprioritize %s", cstate.ua_session->connection_id(), output.str().c_str());
    }
  } else {
    // PRIORITY frame is received before HEADERS frame.

    // Restrict number of inactive node in dependency tree smaller than max_concurrent_streams.
    // Current number of inactive node is size of tree minus active node count.
    if (Http2::max_concurrent_streams_in > cstate.dependency_tree->size() - cstate.get_client_stream_count() + 1) {
      cstate.dependency_tree->add(priority.stream_dependency, stream_id, priority.weight, priority.exclusive_flag, nullptr);
    }
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_rst_stream_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2RstStream rst_stream;
  char buf[HTTP2_RST_STREAM_LEN];
  char *end;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(cstate.ua_session, frame.header().streamid, "Received RST_STREAM frame");

  // RST_STREAM frames MUST be associated with a stream.  If a RST_STREAM
  // frame is received with a stream identifier of 0x0, the recipient MUST
  // treat this as a connection error (Section 5.4.1) of type
  // PROTOCOL_ERROR.
  if (stream_id == 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "reset access stream with invalid id");
  }

  Http2Stream *stream = cstate.find_stream(stream_id);
  if (stream == nullptr) {
    if (cstate.is_valid_streamid(stream_id)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "reset frame bad id stream not found");
    }
  }

  // A RST_STREAM frame with a length other than 4 octets MUST be treated
  // as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_RST_STREAM_LEN) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "reset frame wrong length");
  }

  if (stream == nullptr || !stream->change_state(frame.header().type, frame.header().flags)) {
    // If a RST_STREAM frame identifying an idle stream is received, the
    // recipient MUST treat this as a connection error of type PROTOCOL_ERROR.
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "reset missing stream or bad stream state");
  }

  end = frame.reader()->memcpy(buf, sizeof(buf), 0);

  if (!http2_parse_rst_stream(make_iovec(buf, end - buf), rst_stream)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "reset failed to parse");
  }

  if (stream != nullptr) {
    Http2StreamDebug(cstate.ua_session, stream_id, "RST_STREAM: Error Code: %u", rst_stream.error_code);

    stream->set_rx_error_code({ProxyErrorClass::TXN, static_cast<uint32_t>(rst_stream.error_code)});
    cstate.delete_stream(stream);
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_settings_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2SettingsParameter param;
  char buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned nbytes               = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(cstate.ua_session, stream_id, "Received SETTINGS frame");

  if (cstate.get_zombie_event()) {
    Warning("Setting frame for zombied session %" PRId64, cstate.ua_session->connection_id());
  }

  // [RFC 7540] 6.5. The stream identifier for a SETTINGS frame MUST be zero.
  // If an endpoint receives a SETTINGS frame whose stream identifier field is
  // anything other than 0x0, the endpoint MUST respond with a connection
  // error (Section 5.4.1) of type PROTOCOL_ERROR.
  if (stream_id != 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv settings stream not 0");
  }

  // [RFC 7540] 6.5. Receipt of a SETTINGS frame with the ACK flag set and a
  // length field value other than 0 MUST be treated as a connection
  // error of type FRAME_SIZE_ERROR.
  if (frame.header().flags & HTTP2_FLAGS_SETTINGS_ACK) {
    if (frame.header().length == 0) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                        "recv settings ACK header length not 0");
    }
  }

  // A SETTINGS frame with a length other than a multiple of 6 octets MUST
  // be treated as a connection error (Section 5.4.1) of type
  // FRAME_SIZE_ERROR.
  if (frame.header().length % 6 != 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "recv settings header wrong length");
  }

  uint32_t n_settings = 0;
  while (nbytes < frame.header().length) {
    if (n_settings >= Http2::max_settings_per_frame) {
      Http2StreamDebug(cstate.ua_session, stream_id, "Observed too many settings in a frame");
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                        "recv settings too many settings in a frame");
    }

    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_settings_parameter(make_iovec(buf, read_bytes), param)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv settings parse failed");
    }

    if (!http2_settings_parameter_is_valid(param)) {
      if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                          "recv settings bad initial window size");
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "recv settings bad param");
      }
    }

    Http2StreamDebug(cstate.ua_session, stream_id, "   %s : %u", Http2DebugNames::get_settings_param_name(param.id), param.value);

    // [RFC 7540] 6.9.2. When the value of SETTINGS_INITIAL_WINDOW_SIZE
    // changes, a receiver MUST adjust the size of all stream flow control
    // windows that it maintains by the difference between the new value and
    // the old value.
    if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
      cstate.update_initial_rwnd(param.value);
    }

    cstate.client_settings.set(static_cast<Http2SettingsIdentifier>(param.id), param.value);
    ++n_settings;
  }

  // Update settigs count per minute
  cstate.increment_received_settings_count(n_settings);
  // Close this conection if its settings count received exceeds a limit
  if (cstate.get_received_settings_count() > Http2::max_settings_per_minute) {
    Http2StreamDebug(cstate.ua_session, stream_id, "Observed too frequent setting changes: %u settings within a last minute",
                     cstate.get_received_settings_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv settings too frequent setting changes");
  }

  // [RFC 7540] 6.5. Once all values have been applied, the recipient MUST
  // immediately emit a SETTINGS frame with the ACK flag set.
  Http2Frame ackFrame(HTTP2_FRAME_TYPE_SETTINGS, 0, HTTP2_FLAGS_SETTINGS_ACK);
  cstate.ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ackFrame);

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_push_promise_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2StreamDebug(cstate.ua_session, frame.header().streamid, "Received PUSH_PROMISE frame");

  // [RFC 7540] 8.2. A client cannot push. Thus, servers MUST treat the receipt of a
  // PUSH_PROMISE frame as a connection error of type PROTOCOL_ERROR.
  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                    "promise not allowed");
}

static Http2Error
rcv_ping_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  uint8_t opaque_data[HTTP2_PING_LEN];
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(cstate.ua_session, stream_id, "Received PING frame");

  cstate.schedule_zombie_event();

  //  If a PING frame is received with a stream identifier field value other
  //  than 0x0, the recipient MUST respond with a connection error of type
  //  PROTOCOL_ERROR.
  if (stream_id != 0x0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR, "ping id not 0");
  }

  // Receipt of a PING frame with a length field value other than 8 MUST
  // be treated as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_PING_LEN) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "ping bad length");
  }

  // An endpoint MUST NOT respond to PING frames containing this flag.
  if (frame.header().flags & HTTP2_FLAGS_PING_ACK) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  frame.reader()->memcpy(opaque_data, HTTP2_PING_LEN, 0);

  // ACK (0x1): An endpoint MUST set this flag in PING responses.
  cstate.send_ping_frame(stream_id, HTTP2_FLAGS_PING_ACK, opaque_data);

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_goaway_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  Http2Goaway goaway;
  char buf[HTTP2_GOAWAY_LEN];
  unsigned nbytes               = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(cstate.ua_session, stream_id, "Received GOAWAY frame");

  // An endpoint MUST treat a GOAWAY frame with a stream identifier other
  // than 0x0 as a connection error of type PROTOCOL_ERROR.
  if (stream_id != 0x0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "goaway id non-zero");
  }

  while (nbytes < frame.header().length) {
    unsigned read_bytes = read_rcv_buffer(buf, sizeof(buf), nbytes, frame);

    if (!http2_parse_goaway(make_iovec(buf, read_bytes), goaway)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "goaway failed parse");
    }
  }

  Http2StreamDebug(cstate.ua_session, stream_id, "GOAWAY: last stream id=%d, error code=%d", goaway.last_streamid,
                   static_cast<int>(goaway.error_code));

  cstate.rx_error_code = {ProxyErrorClass::SSN, static_cast<uint32_t>(goaway.error_code)};
  cstate.handleEvent(HTTP2_SESSION_EVENT_FINI, nullptr);

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

static Http2Error
rcv_window_update_frame(Http2ConnectionState &cstate, const Http2Frame &frame)
{
  char buf[HTTP2_WINDOW_UPDATE_LEN];
  uint32_t size;
  const Http2StreamId stream_id = frame.header().streamid;

  //  A WINDOW_UPDATE frame with a length other than 4 octets MUST be
  //  treated as a connection error of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_WINDOW_UPDATE_LEN) {
    Http2StreamDebug(cstate.ua_session, stream_id, "Received WINDOW_UPDATE frame - length incorrect");
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "window update bad length");
  }

  frame.reader()->memcpy(buf, sizeof(buf), 0);
  http2_parse_window_update(make_iovec(buf, sizeof(buf)), size);

  // A receiver MUST treat the receipt of a WINDOW_UPDATE frame with a flow
  // control window increment of 0 as a connection error of type PROTOCOL_ERROR;
  if (size == 0) {
    if (stream_id == 0) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "window update length=0 and id=0");
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "window update length=0");
    }
  }

  if (stream_id == 0) {
    // Connection level window update
    Http2StreamDebug(cstate.ua_session, stream_id, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (cstate.client_rwnd + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - cstate.client_rwnd) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big");
    }

    cstate.client_rwnd += size;
    cstate.restart_streams();
  } else {
    // Stream level window update
    Http2Stream *stream = cstate.find_stream(stream_id);

    if (stream == nullptr) {
      if (cstate.is_valid_streamid(stream_id)) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "window update stream invalid id");
      }
    }

    Http2StreamDebug(cstate.ua_session, stream_id, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (stream->client_rwnd + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - stream->client_rwnd) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big 2");
    }

    stream->client_rwnd += size;
    ssize_t wnd = std::min(cstate.client_rwnd, stream->client_rwnd);

    if (!stream->is_closed() && stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE && wnd > 0) {
      SCOPED_MUTEX_LOCK(lock, stream->mutex, this_ethread());
      stream->restart_sending();
    }
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
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

  Http2StreamDebug(cstate.ua_session, stream_id, "Received CONTINUATION frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "continuation bad client id");
  }

  // Find opened stream
  // CONTINUATION frames MUST be associated with a stream.  If a
  // CONTINUATION frame is received whose stream identifier field is 0x0,
  // the recipient MUST respond with a connection error ([RFC 7540] Section
  // 5.4.1) of type PROTOCOL_ERROR.
  Http2Stream *stream = cstate.find_stream(stream_id);
  if (stream == nullptr) {
    if (cstate.is_valid_streamid(stream_id)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "continuation stream freed with valid id");
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "continuation stream freed with invalid id");
    }
  } else {
    switch (stream->get_state()) {
    case Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE:
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "continuation half close remote");
    case Http2StreamState::HTTP2_STREAM_STATE_IDLE:
      break;
    default:
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "continuation bad state");
    }
  }

  // keep track of how many bytes we get in the frame
  stream->request_header_length += payload_length;
  if (stream->request_header_length > Http2::max_header_list_size) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "continuation payload for headers exceeded");
  }

  uint32_t header_blocks_offset = stream->header_blocks_length;
  stream->header_blocks_length += payload_length;

  stream->header_blocks = static_cast<uint8_t *>(ats_realloc(stream->header_blocks, stream->header_blocks_length));
  frame.reader()->memcpy(stream->header_blocks + header_blocks_offset, payload_length);

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    cstate.clear_continued_stream_id();

    if (!stream->change_state(HTTP2_FRAME_TYPE_CONTINUATION, frame.header().flags)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "continuation no state change");
    }

    Http2ErrorCode result =
      stream->decode_header_blocks(*cstate.local_hpack_handle, cstate.server_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

    if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
      if (result == Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR,
                          "continuation compression error");
      } else if (result == Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                          "continuation enhance your calm");
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "continuation malformed request");
      }
    }

    // Set up the State Machine
    SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
    stream->new_transaction();
    // Send request header to SM
    stream->send_request(cstate);
  } else {
    // NOTE: Expect another CONTINUATION Frame. Do nothing.
    Http2StreamDebug(cstate.ua_session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
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
  if (edata == zombie_event) {
    // zombie session is still around. Assert
    ink_release_assert(zombie_event == nullptr);
  } else if (edata == fini_event) {
    fini_event = nullptr;
  }
  ++recursion;
  switch (event) {
  // Initialize HTTP/2 Connection
  case HTTP2_SESSION_EVENT_INIT: {
    ink_assert(this->ua_session == nullptr);
    this->ua_session = static_cast<Http2ClientSession *>(edata);
    REMEMBER(event, this->recursion);

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

    break;
  }

  // Finalize HTTP/2 Connection
  case HTTP2_SESSION_EVENT_FINI: {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    REMEMBER(event, this->recursion);

    ink_assert(this->fini_received == false);
    this->fini_received = true;
    cleanup_streams();
    SET_HANDLER(&Http2ConnectionState::state_closed);
    this->release_stream(nullptr);
  } break;

  case HTTP2_SESSION_EVENT_XMIT: {
    REMEMBER(event, this->recursion);
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    send_data_frames_depends_on_priority();
    _scheduled = false;
  } break;

  // Parse received HTTP/2 frames
  case HTTP2_SESSION_EVENT_RECV: {
    REMEMBER(event, this->recursion);
    const Http2Frame *frame       = static_cast<Http2Frame *>(edata);
    const Http2StreamId stream_id = frame->header().streamid;
    Http2Error error;

    // [RFC 7540] 5.5. Extending HTTP/2
    //   Implementations MUST discard frames that have unknown or unsupported types.
    if (frame->header().type >= HTTP2_FRAME_TYPE_MAX) {
      Http2StreamDebug(ua_session, stream_id, "Discard a frame which has unknown type, type=%x", frame->header().type);
      break;
    }

    if (frame_handlers[frame->header().type]) {
      error = frame_handlers[frame->header().type](*this, *frame);
    } else {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR, "no handler");
    }

    if (error.cls != Http2ErrorClass::HTTP2_ERROR_CLASS_NONE) {
      ip_port_text_buffer ipb;
      const char *client_ip = ats_ip_ntop(ua_session->get_client_addr(), ipb, sizeof(ipb));
      if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION) {
        if (error.msg) {
          Error("HTTP/2 connection error client_ip=%s session_id=%" PRId64 " stream_id=%u %s", client_ip,
                ua_session->connection_id(), stream_id, error.msg);
        }
        this->send_goaway_frame(this->latest_streamid_in, error.code);
        this->ua_session->set_half_close_local_flag(true);
        if (fini_event == nullptr) {
          fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
        }

        // The streams will be cleaned up by the HTTP2_SESSION_EVENT_FINI event
        // The Http2ClientSession will shutdown because connection_state.is_state_closed() will be true
      } else if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM) {
        if (error.msg) {
          Error("HTTP/2 stream error client_ip=%s session_id=%" PRId64 " stream_id=%u %s", client_ip, ua_session->connection_id(),
                stream_id, error.msg);
        }
        this->send_rst_stream_frame(stream_id, error.code);
      }
    }

  } break;

  // Initiate a gracefull shutdown
  case HTTP2_SESSION_EVENT_SHUTDOWN_INIT: {
    REMEMBER(event, this->recursion);
    ink_assert(shutdown_state == HTTP2_SHUTDOWN_NOT_INITIATED);
    shutdown_state = HTTP2_SHUTDOWN_INITIATED;
    // [RFC 7540] 6.8.  GOAWAY
    // A server that is attempting to gracefully shut down a
    // connection SHOULD send an initial GOAWAY frame with the last stream
    // identifier set to 2^31-1 and a NO_ERROR code.
    send_goaway_frame(INT32_MAX, Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
    // After allowing time for any in-flight stream creation (at least one round-trip time),
    shutdown_cont_event = this_ethread()->schedule_in((Continuation *)this, HRTIME_SECONDS(2), HTTP2_SESSION_EVENT_SHUTDOWN_CONT);
  } break;

  // Continue a gracefull shutdown
  case HTTP2_SESSION_EVENT_SHUTDOWN_CONT: {
    REMEMBER(event, this->recursion);
    ink_assert(shutdown_state == HTTP2_SHUTDOWN_INITIATED);
    shutdown_cont_event = nullptr;
    shutdown_state      = HTTP2_SHUTDOWN_IN_PROGRESS;
    // [RFC 7540] 6.8.  GOAWAY
    // ..., the server can send another GOAWAY frame with an updated last stream identifier
    if (shutdown_reason == Http2ErrorCode::HTTP2_ERROR_MAX) {
      shutdown_reason = Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
    }
    send_goaway_frame(latest_streamid_in, shutdown_reason);
    // Stop creating new streams
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->set_half_close_local_flag(true);
  } break;

  default:
    Http2ConDebug(ua_session, "unexpected event=%d edata=%p", event, edata);
    ink_release_assert(0);
    break;
  }

  --recursion;
  if (recursion == 0 && ua_session && !ua_session->is_recursing()) {
    if (this->ua_session->ready_to_free()) {
      MUTEX_TRY_LOCK(lock, this->ua_session->mutex, this_ethread());
      if (lock.is_locked()) {
        this->ua_session->free();
        // After the free, the Http2ConnectionState object is also freed.
        // The Http2ConnectionState object is allocted within the Http2ClientSession object
      }
    }
  }

  return 0;
}

int
Http2ConnectionState::state_closed(int event, void *edata)
{
  REMEMBER(event, this->recursion);

  if (edata == zombie_event) {
    // Zombie session is still around.  Assert!
    ink_release_assert(zombie_event == nullptr);
  } else if (edata == fini_event) {
    fini_event = nullptr;
  } else if (edata == shutdown_cont_event) {
    shutdown_cont_event = nullptr;
  }
  return 0;
}

Http2Stream *
Http2ConnectionState::create_stream(Http2StreamId new_id, Http2Error &error)
{
  // In half_close state, TS doesn't create new stream. Because GOAWAY frame is sent to client
  if (ua_session->get_half_close_local_flag()) {
    error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                       "refused to create new stream, because ua_session is in half_close state");
    return nullptr;
  }

  bool client_streamid = http2_is_client_streamid(new_id);

  // 5.1.1 The identifier of a newly established stream MUST be numerically
  // greater than all streams that the initiating endpoint has opened or
  // reserved.  This governs streams that are opened using a HEADERS frame
  // and streams that are reserved using PUSH_PROMISE.  An endpoint that
  // receives an unexpected stream identifier MUST respond with a
  // connection error (Section 5.4.1) of type PROTOCOL_ERROR.
  if (client_streamid) {
    if (new_id <= latest_streamid_in) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                         "recv headers new client id less than latest stream id");
      return nullptr;
    }
  } else {
    if (new_id <= latest_streamid_out) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                         "recv headers new server id less than latest stream id");
      return nullptr;
    }
  }

  // Endpoints MUST NOT exceed the limit set by their peer.  An endpoint
  // that receives a HEADERS frame that causes their advertised concurrent
  // stream limit to be exceeded MUST treat this as a stream error.
  if (client_streamid) {
    if (client_streams_in_count >= server_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating inbound stream beyond max_concurrent limit");
      return nullptr;
    }
  } else {
    if (client_streams_out_count >= client_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating outbound stream beyond max_concurrent limit");
      return nullptr;
    }
  }

  Http2Stream *new_stream = THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread());
  new_stream->init(new_id, client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));

  ink_assert(nullptr != new_stream);
  ink_assert(!stream_list.in(new_stream));

  stream_list.enqueue(new_stream);
  if (client_streamid) {
    latest_streamid_in = new_id;
    ink_assert(client_streams_in_count < UINT32_MAX);
    ++client_streams_in_count;
  } else {
    latest_streamid_out = new_id;
    ink_assert(client_streams_out_count < UINT32_MAX);
    ++client_streams_out_count;
  }
  ++total_client_streams_count;

  if (zombie_event != nullptr) {
    zombie_event->cancel();
    zombie_event = nullptr;
  }

  new_stream->set_proxy_ssn(ua_session);
  new_stream->mutex                     = new_ProxyMutex();
  new_stream->is_first_transaction_flag = get_stream_requests() == 0;
  increment_stream_requests();
  ua_session->get_netvc()->add_to_active_queue();

  return new_stream;
}

Http2Stream *
Http2ConnectionState::find_stream(Http2StreamId id) const
{
  for (Http2Stream *s = stream_list.head; s; s = static_cast<Http2Stream *>(s->link.next)) {
    if (s->get_id() == id) {
      return s;
    }
    ink_assert(s != s->link.next);
  }
  return nullptr;
}

void
Http2ConnectionState::restart_streams()
{
  Http2Stream *s = stream_list.head;
  if (s) {
    Http2Stream *end = s;

    // This is a static variable, so it is shared in Http2ConnectionState instances and will get incremented in subsequent calls.
    // It doesn't need to be initialized with rand() nor time(), and doesn't need to be accessed with a lock, because it doesn't
    // need that randomness and accuracy.
    static uint16_t starting_point = 0;

    // Change the start point randomly
    for (int i = starting_point % total_client_streams_count; i >= 0; --i) {
      end = static_cast<Http2Stream *>(end->link.next ? end->link.next : stream_list.head);
    }
    s = static_cast<Http2Stream *>(end->link.next ? end->link.next : stream_list.head);

    // Call send_response_body() for each streams
    while (s != end) {
      Http2Stream *next = static_cast<Http2Stream *>(s->link.next ? s->link.next : stream_list.head);
      if (!s->is_closed() && s->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE &&
          std::min(this->client_rwnd, s->client_rwnd) > 0) {
        SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
        s->restart_sending();
      }
      ink_assert(s != next);
      s = next;
    }
    if (!s->is_closed() && s->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE &&
        std::min(this->client_rwnd, s->client_rwnd) > 0) {
      SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
      s->restart_sending();
    }

    ++starting_point;
  }
}

void
Http2ConnectionState::cleanup_streams()
{
  Http2Stream *s = stream_list.head;
  while (s) {
    Http2Stream *next = static_cast<Http2Stream *>(s->link.next);
    if (this->rx_error_code.cls != ProxyErrorClass::NONE) {
      s->set_rx_error_code(this->rx_error_code);
    }
    if (this->tx_error_code.cls != ProxyErrorClass::NONE) {
      s->set_tx_error_code(this->tx_error_code);
    }
    this->delete_stream(s);
    ink_assert(s != next);
    s = next;
  }

  ink_assert(stream_list.empty());

  if (!is_state_closed()) {
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());

    UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(ua_session->get_netvc());
    if (vc && vc->active_timeout_in == 0) {
      vc->add_to_keep_alive_queue();
    }
  }
}

bool
Http2ConnectionState::delete_stream(Http2Stream *stream)
{
  ink_assert(nullptr != stream);
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  // If stream has already been removed from the list, just go on
  if (!stream_list.in(stream)) {
    return false;
  }

  Http2StreamDebug(ua_session, stream->get_id(), "Delete stream");
  REMEMBER(NO_EVENT, this->recursion);

  if (Http2::stream_priority_enabled) {
    Http2DependencyTree::Node *node = stream->priority_node;
    if (node != nullptr) {
      if (node->active) {
        dependency_tree->deactivate(node, 0);
      }
      if (is_debug_tag_set("http2_priority")) {
        std::stringstream output;
        dependency_tree->dump_tree(output);
        Debug("http2_priority", "[%" PRId64 "] %s", ua_session->connection_id(), output.str().c_str());
      }
      dependency_tree->remove(node);
      // ink_release_assert(dependency_tree->find(stream->get_id()) == nullptr);
    }
    stream->priority_node = nullptr;
  }

  if (stream->get_state() != Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
    send_rst_stream_frame(stream->get_id(), Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
  }

  stream_list.remove(stream);
  if (http2_is_client_streamid(stream->get_id())) {
    ink_assert(client_streams_in_count > 0);
    --client_streams_in_count;
  } else {
    ink_assert(client_streams_out_count > 0);
    --client_streams_out_count;
  }
  // total_client_streams_count will be decremented in release_stream(), because it's a counter include streams in the process of
  // shutting down.

  stream->initiating_close();

  return true;
}

void
Http2ConnectionState::release_stream(Http2Stream *stream)
{
  REMEMBER(NO_EVENT, this->recursion)

  if (stream) {
    // Decrement total_client_streams_count here, because it's a counter include streams in the process of shutting down.
    // Other counters (client_streams_in_count/client_streams_out_count) are already decremented in delete_stream().
    --total_client_streams_count;
  }

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  if (this->ua_session) {
    ink_assert(this->mutex == ua_session->mutex);

    if (total_client_streams_count == 0) {
      if (fini_received) {
        // We were shutting down, go ahead and terminate the session
        // this is a member of Http2ConnectionState and will be freed
        // when ua_session is destroyed
        ua_session->destroy();

        // Can't do this because we just destroyed right here ^,
        // or we can use a local variable to do it.
        // ua_session = nullptr;
      } else if (shutdown_state == HTTP2_SHUTDOWN_IN_PROGRESS && fini_event == nullptr) {
        fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
      } else if (ua_session->is_active()) {
        // If the number of clients is 0, HTTP2_SESSION_EVENT_FINI is not received or sent, and ua_session is active,
        // then mark the connection as inactive
        ua_session->clear_session_active();
        UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(ua_session->get_netvc());
        if (vc && vc->active_timeout_in == 0) {
          // With heavy traffic, ua_session could be destroyed. Do not touch ua_session after this.
          vc->add_to_keep_alive_queue();
        }
      } else {
        schedule_zombie_event();
      }
    } else if (fini_received) {
      schedule_zombie_event();
    }
  }
}

void
Http2ConnectionState::update_initial_rwnd(Http2WindowSize new_size)
{
  // Update stream level window sizes
  for (Http2Stream *s = stream_list.head; s; s = static_cast<Http2Stream *>(s->link.next)) {
    SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
    s->client_rwnd = new_size - (client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - s->client_rwnd);
  }
}

void
Http2ConnectionState::schedule_stream(Http2Stream *stream)
{
  Http2StreamDebug(ua_session, stream->get_id(), "Scheduled");

  Http2DependencyTree::Node *node = stream->priority_node;
  ink_release_assert(node != nullptr);

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  dependency_tree->activate(node);

  if (!_scheduled) {
    _scheduled = true;

    SET_HANDLER(&Http2ConnectionState::main_event_handler);
    this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_XMIT);
  }
}

void
Http2ConnectionState::send_data_frames_depends_on_priority()
{
  Http2DependencyTree::Node *node = dependency_tree->top();

  // No node to send or no connection level window left
  if (node == nullptr || client_rwnd <= 0) {
    return;
  }

  Http2Stream *stream = static_cast<Http2Stream *>(node->t);
  ink_release_assert(stream != nullptr);
  Http2StreamDebug(ua_session, stream->get_id(), "top node, point=%d", node->point);

  size_t len                      = 0;
  Http2SendDataFrameResult result = send_a_data_frame(stream, len);

  switch (result) {
  case Http2SendDataFrameResult::NO_ERROR: {
    // No response body to send
    if (len == 0 && !stream->is_body_done()) {
      dependency_tree->deactivate(node, len);
    } else {
      dependency_tree->update(node, len);

      SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
      stream->signal_write_event(true);
    }
    break;
  }
  case Http2SendDataFrameResult::DONE: {
    dependency_tree->deactivate(node, len);
    delete_stream(stream);
    break;
  }
  default:
    // When no stream level window left, deactivate node once and wait window_update frame
    dependency_tree->deactivate(node, len);
    break;
  }

  this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_XMIT);
  return;
}

Http2SendDataFrameResult
Http2ConnectionState::send_a_data_frame(Http2Stream *stream, size_t &payload_length)
{
  const ssize_t window_size         = std::min(this->client_rwnd, stream->client_rwnd);
  const size_t buf_len              = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
  const size_t write_available_size = std::min(buf_len, static_cast<size_t>(window_size));
  payload_length                    = 0;

  uint8_t flags = 0x00;
  uint8_t payload_buffer[buf_len];
  IOBufferReader *current_reader = stream->response_get_data_reader();

  SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());

  if (!current_reader) {
    Http2StreamDebug(this->ua_session, stream->get_id(), "couldn't get data reader");
    return Http2SendDataFrameResult::ERROR;
  }

  // Select appropriate payload length
  if (current_reader->is_read_avail_more_than(0)) {
    // We only need to check for window size when there is a payload
    if (window_size <= 0) {
      Http2StreamDebug(this->ua_session, stream->get_id(), "No window");
      return Http2SendDataFrameResult::NO_WINDOW;
    }
    // Copy into the payload buffer. Seems like we should be able to skip this copy step
    payload_length = write_available_size;
    payload_length = current_reader->read(payload_buffer, static_cast<int64_t>(write_available_size));
  } else {
    payload_length = 0;
  }

  // Are we at the end?
  // If we return here, we never send the END_STREAM in the case of a early terminating OS.
  // OK if there is no body yet. Otherwise continue on to send a DATA frame and delete the stream
  if (!stream->is_body_done() && payload_length == 0) {
    Http2StreamDebug(this->ua_session, stream->get_id(), "No payload");
    return Http2SendDataFrameResult::NO_PAYLOAD;
  }

  if (stream->is_body_done() && !current_reader->is_read_avail_more_than(0)) {
    flags |= HTTP2_FLAGS_DATA_END_STREAM;
  }

  // Update window size
  this->client_rwnd -= payload_length;
  stream->client_rwnd -= payload_length;

  // Create frame
  Http2StreamDebug(ua_session, stream->get_id(), "Send a DATA frame - client window con: %5zd stream: %5zd payload: %5zd",
                   client_rwnd, stream->client_rwnd, payload_length);

  Http2Frame data(HTTP2_FRAME_TYPE_DATA, stream->get_id(), flags);
  data.alloc(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
  http2_write_data(payload_buffer, payload_length, data.write());
  data.finalize(payload_length);

  stream->update_sent_count(payload_length);

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &data);

  if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
    Http2StreamDebug(ua_session, stream->get_id(), "End of DATA frame");
    stream->send_end_stream = true;
    // Setting to the same state shouldn't be erroneous
    stream->change_state(data.header().type, data.header().flags);

    return Http2SendDataFrameResult::DONE;
  }

  return Http2SendDataFrameResult::NO_ERROR;
}

void
Http2ConnectionState::send_data_frames(Http2Stream *stream)
{
  // To follow RFC 7540 must not send more frames other than priority on
  // a closed stream.  So we return without sending
  if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_LOCAL ||
      stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
    Http2StreamDebug(this->ua_session, stream->get_id(), "Shutdown half closed local stream");
    this->delete_stream(stream);
    return;
  }

  size_t len                      = 0;
  Http2SendDataFrameResult result = Http2SendDataFrameResult::NO_ERROR;
  while (result == Http2SendDataFrameResult::NO_ERROR) {
    result = send_a_data_frame(stream, len);

    if (result == Http2SendDataFrameResult::DONE) {
      // Delete a stream immediately
      // TODO its should not be deleted for a several time to handling
      // RST_STREAM and WINDOW_UPDATE.
      // See 'closed' state written at [RFC 7540] 5.1.
      Http2StreamDebug(this->ua_session, stream->get_id(), "Shutdown stream");
      this->delete_stream(stream);
    }
  }

  return;
}

void
Http2ConnectionState::send_headers_frame(Http2Stream *stream)
{
  uint8_t *buf                = nullptr;
  uint32_t buf_len            = 0;
  uint32_t header_blocks_size = 0;
  int payload_length          = 0;
  uint64_t sent               = 0;
  uint8_t flags               = 0x00;

  HTTPHdr *resp_header = &stream->response_header;

  Http2StreamDebug(ua_session, stream->get_id(), "Send HEADERS frame");

  HTTPHdr h2_hdr;
  http2_generate_h2_header_from_1_1(resp_header, &h2_hdr);

  buf_len = resp_header->length_get() * 2; // Make it double just in case
  buf     = (uint8_t *)ats_malloc(buf_len);
  if (buf == nullptr) {
    h2_hdr.destroy();
    return;
  }
  Http2ErrorCode result = http2_encode_header_blocks(&h2_hdr, buf, buf_len, &header_blocks_size, *(this->remote_hpack_handle),
                                                     client_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    h2_hdr.destroy();
    ats_free(buf);
    return;
  }

  // Send a HEADERS frame
  if (header_blocks_size <= static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]))) {
    payload_length = header_blocks_size;
    flags |= HTTP2_FLAGS_HEADERS_END_HEADERS;
    if (h2_hdr.presence(MIME_PRESENCE_CONTENT_LENGTH) && h2_hdr.get_content_length() == 0) {
      flags |= HTTP2_FLAGS_HEADERS_END_STREAM;
      stream->send_end_stream = true;
    }
  } else {
    payload_length = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]);
  }
  Http2Frame headers(HTTP2_FRAME_TYPE_HEADERS, stream->get_id(), flags);
  headers.alloc(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]);
  http2_write_headers(buf, payload_length, headers.write());
  headers.finalize(payload_length);

  // Change stream state
  if (!stream->change_state(HTTP2_FRAME_TYPE_HEADERS, flags)) {
    this->send_goaway_frame(this->latest_streamid_in, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR);
    this->ua_session->set_half_close_local_flag(true);
    if (fini_event == nullptr) {
      fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
    }

    h2_hdr.destroy();
    ats_free(buf);
    return;
  }

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &headers);
  sent += payload_length;

  // Send CONTINUATION frames
  flags = 0;
  while (sent < header_blocks_size) {
    Http2StreamDebug(ua_session, stream->get_id(), "Send CONTINUATION frame");
    payload_length = std::min(static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION])),
                              static_cast<uint32_t>(header_blocks_size - sent));
    if (sent + payload_length == header_blocks_size) {
      flags |= HTTP2_FLAGS_CONTINUATION_END_HEADERS;
    }
    Http2Frame continuation_frame(HTTP2_FRAME_TYPE_CONTINUATION, stream->get_id(), flags);
    continuation_frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION]);
    http2_write_headers(buf + sent, payload_length, continuation_frame.write());
    continuation_frame.finalize(payload_length);
    stream->change_state(continuation_frame.header().type, continuation_frame.header().flags);
    // xmit event
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &continuation_frame);
    sent += payload_length;
  }

  h2_hdr.destroy();
  ats_free(buf);
}

void
Http2ConnectionState::send_push_promise_frame(Http2Stream *stream, URL &url, const MIMEField *accept_encoding)
{
  HTTPHdr h1_hdr, h2_hdr;
  uint8_t *buf                = nullptr;
  uint32_t buf_len            = 0;
  uint32_t header_blocks_size = 0;
  int payload_length          = 0;
  uint64_t sent               = 0;
  uint8_t flags               = 0x00;

  if (client_settings.get(HTTP2_SETTINGS_ENABLE_PUSH) == 0) {
    return;
  }

  Http2StreamDebug(ua_session, stream->get_id(), "Send PUSH_PROMISE frame");

  h1_hdr.create(HTTP_TYPE_REQUEST);
  h1_hdr.url_set(&url);
  h1_hdr.method_set("GET", 3);
  if (accept_encoding != nullptr) {
    MIMEField *f;
    const char *name;
    int name_len;
    const char *value;
    int value_len;

    name  = accept_encoding->name_get(&name_len);
    f     = h1_hdr.field_create(name, name_len);
    value = accept_encoding->value_get(&value_len);
    f->value_set(h1_hdr.m_heap, h1_hdr.m_mime, value, value_len);

    h1_hdr.field_attach(f);
  }

  http2_generate_h2_header_from_1_1(&h1_hdr, &h2_hdr);

  buf_len = h1_hdr.length_get() * 2; // Make it double just in case
  h1_hdr.destroy();
  buf = (uint8_t *)ats_malloc(buf_len);
  if (buf == nullptr) {
    h2_hdr.destroy();
    return;
  }
  Http2ErrorCode result = http2_encode_header_blocks(&h2_hdr, buf, buf_len, &header_blocks_size, *(this->remote_hpack_handle),
                                                     client_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    h2_hdr.destroy();
    ats_free(buf);
    return;
  }

  // Send a PUSH_PROMISE frame
  Http2PushPromise push_promise;
  if (header_blocks_size <=
      BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_PUSH_PROMISE]) - sizeof(push_promise.promised_streamid)) {
    payload_length = header_blocks_size;
    flags |= HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS;
  } else {
    payload_length =
      BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_PUSH_PROMISE]) - sizeof(push_promise.promised_streamid);
  }
  Http2Frame push_promise_frame(HTTP2_FRAME_TYPE_PUSH_PROMISE, stream->get_id(), flags);
  push_promise_frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_PUSH_PROMISE]);
  Http2StreamId id               = this->get_latest_stream_id_out() + 2;
  push_promise.promised_streamid = id;
  http2_write_push_promise(push_promise, buf, payload_length, push_promise_frame.write());
  push_promise_frame.finalize(sizeof(push_promise.promised_streamid) + payload_length);
  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &push_promise_frame);
  sent += payload_length;

  // Send CONTINUATION frames
  flags = 0;
  while (sent < header_blocks_size) {
    Http2StreamDebug(ua_session, stream->get_id(), "Send CONTINUATION frame");
    payload_length = std::min(static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION])),
                              static_cast<uint32_t>(header_blocks_size - sent));
    if (sent + payload_length == header_blocks_size) {
      flags |= HTTP2_FLAGS_CONTINUATION_END_HEADERS;
    }
    Http2Frame continuation_frame(HTTP2_FRAME_TYPE_CONTINUATION, stream->get_id(), flags);
    continuation_frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION]);
    http2_write_headers(buf + sent, payload_length, continuation_frame.write());
    continuation_frame.finalize(payload_length);
    // xmit event
    SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &continuation_frame);
    sent += payload_length;
  }
  ats_free(buf);

  Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  stream = this->create_stream(id, error);
  if (!stream) {
    h2_hdr.destroy();
    return;
  }

  SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
  if (Http2::stream_priority_enabled) {
    Http2DependencyTree::Node *node = this->dependency_tree->find(id);
    if (node != nullptr) {
      stream->priority_node = node;
    } else {
      Http2StreamDebug(this->ua_session, id, "PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d",
                       HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY, HTTP2_PRIORITY_DEFAULT_WEIGHT, false,
                       this->dependency_tree->size());

      stream->priority_node =
        this->dependency_tree->add(HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY, id, HTTP2_PRIORITY_DEFAULT_WEIGHT, false, stream);
    }
  }
  stream->change_state(HTTP2_FRAME_TYPE_PUSH_PROMISE, HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS);
  stream->set_request_headers(h2_hdr);
  stream->new_transaction();
  stream->send_request(*this);

  h2_hdr.destroy();
}

void
Http2ConnectionState::send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec)
{
  Http2StreamDebug(ua_session, id, "Send RST_STREAM frame");

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_STREAM_ERRORS_COUNT, this_ethread());
    ++stream_error_count;
  }

  Http2Frame rst_stream(HTTP2_FRAME_TYPE_RST_STREAM, id, 0);

  rst_stream.alloc(buffer_size_index[HTTP2_FRAME_TYPE_RST_STREAM]);
  http2_write_rst_stream(static_cast<uint32_t>(ec), rst_stream.write());
  rst_stream.finalize(HTTP2_RST_STREAM_LEN);

  // change state to closed
  Http2Stream *stream = find_stream(id);
  if (stream != nullptr) {
    stream->set_tx_error_code({ProxyErrorClass::TXN, static_cast<uint32_t>(ec)});
    if (!stream->change_state(HTTP2_FRAME_TYPE_RST_STREAM, 0)) {
      this->send_goaway_frame(this->latest_streamid_in, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR);
      this->ua_session->set_half_close_local_flag(true);
      if (fini_event == nullptr) {
        fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
      }

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

  Http2StreamDebug(ua_session, stream_id, "Send SETTINGS frame");

  Http2Frame settings(HTTP2_FRAME_TYPE_SETTINGS, stream_id, 0);
  settings.alloc(buffer_size_index[HTTP2_FRAME_TYPE_SETTINGS]);

  IOVec iov                = settings.write();
  uint32_t settings_length = 0;

  for (int i = HTTP2_SETTINGS_HEADER_TABLE_SIZE; i < HTTP2_SETTINGS_MAX; ++i) {
    Http2SettingsIdentifier id = static_cast<Http2SettingsIdentifier>(i);
    unsigned settings_value    = new_settings.get(id);

    // Send only difference
    if (settings_value != server_settings.get(id)) {
      const Http2SettingsParameter param = {static_cast<uint16_t>(id), settings_value};

      // Write settings to send buffer
      if (!http2_write_settings(param, iov)) {
        this->send_goaway_frame(this->latest_streamid_in, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR);
        this->ua_session->set_half_close_local_flag(true);
        if (fini_event == nullptr) {
          fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
        }

        return;
      }
      iov.iov_base = reinterpret_cast<uint8_t *>(iov.iov_base) + HTTP2_SETTINGS_PARAMETER_LEN;
      iov.iov_len -= HTTP2_SETTINGS_PARAMETER_LEN;
      settings_length += HTTP2_SETTINGS_PARAMETER_LEN;

      // Update current settings
      server_settings.set(id, new_settings.get(id));

      Http2StreamDebug(ua_session, stream_id, "  %s : %u", Http2DebugNames::get_settings_param_name(param.id), param.value);
    }
  }

  settings.finalize(settings_length);
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &settings);
}

void
Http2ConnectionState::send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data)
{
  Http2StreamDebug(ua_session, id, "Send PING frame");

  Http2Frame ping(HTTP2_FRAME_TYPE_PING, id, flag);

  ping.alloc(buffer_size_index[HTTP2_FRAME_TYPE_PING]);
  http2_write_ping(opaque_data, ping.write());
  ping.finalize(HTTP2_PING_LEN);

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ping);
}

// As for gracefull shutdown, TS should process outstanding stream as long as possible.
// As for signal connection error, TS should close connection immediately.
void
Http2ConnectionState::send_goaway_frame(Http2StreamId id, Http2ErrorCode ec)
{
  ink_assert(this->ua_session != nullptr);

  Http2ConDebug(ua_session, "Send GOAWAY frame, last_stream_id: %d", id);

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CONNECTION_ERRORS_COUNT, this_ethread());
  }

  Http2Frame frame(HTTP2_FRAME_TYPE_GOAWAY, 0, 0);
  Http2Goaway goaway;

  goaway.last_streamid = id;
  goaway.error_code    = ec;

  frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_GOAWAY]);
  http2_write_goaway(goaway, frame.write());
  frame.finalize(HTTP2_GOAWAY_LEN);

  this->tx_error_code = {ProxyErrorClass::SSN, static_cast<uint32_t>(ec)};

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &frame);
}

void
Http2ConnectionState::send_window_update_frame(Http2StreamId id, uint32_t size)
{
  Http2StreamDebug(ua_session, id, "Send WINDOW_UPDATE frame");

  // Create WINDOW_UPDATE frame
  Http2Frame window_update(HTTP2_FRAME_TYPE_WINDOW_UPDATE, id, 0x0);
  window_update.alloc(buffer_size_index[HTTP2_FRAME_TYPE_WINDOW_UPDATE]);
  http2_write_window_update(static_cast<uint32_t>(size), window_update.write());
  window_update.finalize(sizeof(uint32_t));

  // xmit event
  SCOPED_MUTEX_LOCK(lock, this->ua_session->mutex, this_ethread());
  this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &window_update);
}

void
Http2ConnectionState::increment_received_settings_count(uint32_t count)
{
  ink_hrtime hrtime_sec = Thread::get_hrtime() / HRTIME_SECOND;
  uint8_t counter_index = ((hrtime_sec % 60) >= 30);

  if ((hrtime_sec - 60) > this->settings_count_last_update) {
    this->settings_count[0] = 0;
    this->settings_count[1] = 0;
  } else if (counter_index != ((this->settings_count_last_update % 60) >= 30)) {
    this->settings_count[counter_index] = 0;
  }
  this->settings_count[counter_index] += count;
  this->settings_count_last_update = hrtime_sec;
}

uint32_t
Http2ConnectionState::get_received_settings_count()
{
  return this->settings_count[0] + this->settings_count[1];
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

  Http2ConDebug(ua_session, "current client streams: %" PRId64, current_client_streams);

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
