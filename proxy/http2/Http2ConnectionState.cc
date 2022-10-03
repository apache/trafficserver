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
#include "Http2Frame.h"
#include "Http2DebugNames.h"
#include "HttpDebugNames.h"

#include "tscpp/util/PostScript.h"
#include "tscpp/util/LocalBuffer.h"

#include <sstream>
#include <numeric>

#define REMEMBER(e, r)                                     \
  {                                                        \
    if (this->session) {                                   \
      this->session->remember(MakeSourceLocation(), e, r); \
    }                                                      \
  }

#define Http2ConDebug(session, fmt, ...) \
  SsnDebug(session->get_proxy_session(), "http2_con", "[%" PRId64 "] " fmt, session->get_connection_id(), ##__VA_ARGS__);

#define Http2StreamDebug(session, stream_id, fmt, ...)                                                                    \
  SsnDebug(session->get_proxy_session(), "http2_con", "[%" PRId64 "] [%u] " fmt, session->get_connection_id(), stream_id, \
           ##__VA_ARGS__);

static const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] = {
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_DATA
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_HEADERS
  -1,                    // HTTP2_FRAME_TYPE_PRIORITY
  -1,                    // HTTP2_FRAME_TYPE_RST_STREAM
  -1,                    // HTTP2_FRAME_TYPE_SETTINGS
  BUFFER_SIZE_INDEX_16K, // HTTP2_FRAME_TYPE_PUSH_PROMISE
  -1,                    // HTTP2_FRAME_TYPE_PING
  -1,                    // HTTP2_FRAME_TYPE_GOAWAY
  -1,                    // HTTP2_FRAME_TYPE_WINDOW_UPDATE
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

Http2Error
Http2ConnectionState::rcv_data_frame(const Http2Frame &frame)
{
  unsigned nbytes               = 0;
  Http2StreamId id              = frame.header().streamid;
  uint8_t pad_length            = 0;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(this->session, id, "Received DATA frame");

  if (this->get_zombie_event()) {
    Warning("Data frame for zombied session %" PRId64, this->session->get_connection_id());
  }

  // If a DATA frame is received whose stream identifier field is 0x0, the
  // recipient MUST
  // respond with a connection error of type PROTOCOL_ERROR.
  if (!http2_is_client_streamid(id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv data bad frame client id");
  }

  Http2Stream *stream = this->find_stream(id);
  if (stream == nullptr) {
    if (this->is_valid_streamid(id)) {
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
      this->send_rst_stream_frame(id, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED);
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
    if (!stream->payload_length_is_valid()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data bad payload length");
    }

    // Pure END_STREAM
    if (payload_length == 0) {
      stream->signal_read_event(VC_EVENT_READ_COMPLETE);
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
  } else {
    // If payload length is 0 without END_STREAM flag, do nothing
    if (payload_length == 0) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
  }

  // Check whether Window Size is acceptable
  if (!this->_server_rwnd_is_shrinking && this->server_rwnd() < payload_length) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data cstate.server_rwnd < payload_length");
  }
  if (stream->server_rwnd() < payload_length) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data stream->server_rwnd < payload_length");
  }

  // Update Window size
  this->decrement_server_rwnd(payload_length);
  stream->decrement_server_rwnd(payload_length);

  if (is_debug_tag_set("http2_con")) {
    uint32_t rwnd = this->server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
    Http2StreamDebug(this->session, id, "Received DATA frame: rwnd con=%zd/%" PRId32 " stream=%zd/%" PRId32, this->server_rwnd(),
                     rwnd, stream->server_rwnd(), rwnd);
  }

  const uint32_t unpadded_length = payload_length - pad_length;
  MIOBuffer *writer              = stream->read_vio_writer();
  if (writer == nullptr) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR);
  }

  // If we call write() multiple times, we must keep the same reader, so we can
  // update its offset via consume.  Otherwise, we will read the same data on the
  // second time through
  IOBufferReader *myreader = frame.reader()->clone();
  // Skip pad length field
  if (frame.header().flags & HTTP2_FLAGS_DATA_PADDED) {
    myreader->consume(HTTP2_DATA_PADLEN_LEN);
  }

  if (nbytes < unpadded_length) {
    size_t read_len = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
    if (nbytes + read_len > unpadded_length) {
      read_len -= nbytes + read_len - unpadded_length;
    }
    unsigned int num_written = writer->write(myreader, read_len);
    if (num_written != read_len) {
      myreader->writer()->dealloc_reader(myreader);
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR);
    }
    myreader->consume(num_written);
  }
  myreader->writer()->dealloc_reader(myreader);

  if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
    // TODO: set total written size to read_vio.nbytes
    stream->signal_read_event(VC_EVENT_READ_COMPLETE);
  } else {
    stream->signal_read_event(VC_EVENT_READ_READY);
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
Http2Error
Http2ConnectionState::rcv_headers_frame(const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received HEADERS frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv headers bad client id");
  }

  Http2Stream *stream = nullptr;
  bool new_stream     = false;

  if (this->is_valid_streamid(stream_id)) {
    stream = this->find_stream(stream_id);
    if (stream == nullptr) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "recv headers cannot find existing stream_id");
    } else if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "recv_header to closed stream");
    } else if (!stream->has_trailing_header()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "stream not expecting trailer header");
    }
  } else {
    // Create new stream
    Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    stream     = this->create_stream(stream_id, error);
    new_stream = true;
    if (!stream) {
      return error;
    }
  }

  // Ignoring HEADERS frame on a closed stream.  The HdrHeap has gone away and it will core.
  if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
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

    // Payload length can't be smaller than the pad length
    if ((params.pad_length + HTTP2_HEADERS_PADLEN_LEN) > header_block_fragment_length) {
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
                        "recv headers priority parameters failed parse");
    }
    // Protocol error if the stream depends on itself
    if (stream_id == params.priority.stream_dependency) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers self dependency");
    }

    // Payload length can't be smaller than the priority length
    if (HTTP2_PRIORITY_LEN > header_block_fragment_length) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv priority length > payload length");
    }

    header_block_fragment_offset += HTTP2_PRIORITY_LEN;
    header_block_fragment_length -= HTTP2_PRIORITY_LEN;
  }

  if (new_stream && Http2::stream_priority_enabled) {
    Http2DependencyTree::Node *node = this->dependency_tree->find(stream_id);
    if (node != nullptr) {
      stream->priority_node = node;
      node->t               = stream;
    } else {
      Http2StreamDebug(this->session, stream_id, "HEADER PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d",
                       params.priority.stream_dependency, params.priority.weight, params.priority.exclusive_flag,
                       this->dependency_tree->size());

      stream->priority_node = this->dependency_tree->add(params.priority.stream_dependency, stream_id, params.priority.weight,
                                                         params.priority.exclusive_flag, stream);
    }
  }

  stream->header_blocks_length = header_block_fragment_length;

  // ATS advertises SETTINGS_MAX_HEADER_LIST_SIZE as a limit of total header blocks length. (Details in [RFC 7560] 10.5.1.)
  // Make it double to relax the limit in cases of 1) HPACK is used naively, or 2) Huffman Encoding generates large header blocks.
  // The total "decoded" header length is strictly checked by hpack_decode_header_block().
  if (stream->header_blocks_length > std::max(Http2::max_header_list_size, Http2::max_header_list_size * 2)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "header blocks too large");
  }

  stream->header_blocks = static_cast<uint8_t *>(ats_malloc(header_block_fragment_length));
  frame.reader()->memcpy(stream->header_blocks, header_block_fragment_length, header_block_fragment_offset);

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
      // Decoding header blocks is still needed to maintain a HPACK dynamic table.
      // TODO: TS-3812
      empty_request = true;
    }

    stream->mark_milestone(Http2StreamMilestone::START_DECODE_HEADERS);
    Http2ErrorCode result =
      stream->decode_header_blocks(*this->local_hpack_handle, this->server_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

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

    // Check Content-Length & payload length when END_STREAM flag is true
    if (stream->recv_end_stream && !stream->payload_length_is_valid()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data bad payload length");
    }

    // Set up the State Machine
    if (!empty_request) {
      SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
      stream->mark_milestone(Http2StreamMilestone::START_TXN);
      stream->new_transaction(frame.is_from_early_data());
      // Send request header to SM
      stream->send_request(*this);
    } else {
      // Signal VC_EVENT_READ_COMPLETE because received trailing header fields with END_STREAM flag
      stream->signal_read_event(VC_EVENT_READ_COMPLETE);
    }
  } else {
    // NOTE: Expect CONTINUATION Frame. Do NOT change state of stream or decode
    // Header Blocks.
    Http2StreamDebug(this->session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
    this->set_continued_stream_id(stream_id);
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

/*
 * [RFC 7540] 6.3 PRIORITY
 *
 */
Http2Error
Http2ConnectionState::rcv_priority_frame(const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received PRIORITY frame");

  if (this->get_zombie_event()) {
    Warning("Priority frame for zombied session %" PRId64, this->session->get_connection_id());
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

  // Update PRIORITY frame count per minute
  this->increment_received_priority_frame_count();
  // Close this connection if its priority frame count received exceeds a limit
  if (Http2::max_priority_frames_per_minute != 0 &&
      this->get_received_priority_frame_count() > Http2::max_priority_frames_per_minute) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_PRIORITY_FRAMES_PER_MINUTE_EXCEEDED, this_ethread());
    Http2StreamDebug(this->session, stream_id, "Observed too frequent priority changes: %u priority changes within a last minute",
                     this->get_received_priority_frame_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv priority too frequent priority changes");
  }

  Http2StreamDebug(this->session, stream_id, "PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d", priority.stream_dependency,
                   priority.weight, priority.exclusive_flag, this->dependency_tree->size());

  Http2DependencyTree::Node *node = this->dependency_tree->find(stream_id);

  if (node != nullptr) {
    // [RFC 7540] 5.3.3 Reprioritization
    Http2StreamDebug(this->session, stream_id, "Reprioritize");
    this->dependency_tree->reprioritize(node, priority.stream_dependency, priority.exclusive_flag);
    if (is_debug_tag_set("http2_priority")) {
      std::stringstream output;
      this->dependency_tree->dump_tree(output);
      Debug("http2_priority", "[%" PRId64 "] reprioritize %s", this->session->get_connection_id(), output.str().c_str());
    }
  } else {
    // PRIORITY frame is received before HEADERS frame.

    // Restrict number of inactive node in dependency tree smaller than max_concurrent_streams.
    // Current number of inactive node is size of tree minus active node count.
    if (Http2::max_concurrent_streams_in > this->dependency_tree->size() - this->get_client_stream_count() + 1) {
      this->dependency_tree->add(priority.stream_dependency, stream_id, priority.weight, priority.exclusive_flag, nullptr);
    }
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_rst_stream_frame(const Http2Frame &frame)
{
  Http2RstStream rst_stream;
  char buf[HTTP2_RST_STREAM_LEN];
  char *end;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, frame.header().streamid, "Received RST_STREAM frame");

  // RST_STREAM frames MUST be associated with a stream.  If a RST_STREAM
  // frame is received with a stream identifier of 0x0, the recipient MUST
  // treat this as a connection error (Section 5.4.1) of type
  // PROTOCOL_ERROR.
  if (stream_id == 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "reset access stream with invalid id");
  }

  Http2Stream *stream = this->find_stream(stream_id);
  if (stream == nullptr) {
    if (this->is_valid_streamid(stream_id)) {
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
    Http2StreamDebug(this->session, stream_id, "RST_STREAM: Error Code: %u", rst_stream.error_code);

    stream->set_rx_error_code({ProxyErrorClass::TXN, static_cast<uint32_t>(rst_stream.error_code)});
    stream->initiating_close();
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_settings_frame(const Http2Frame &frame)
{
  Http2SettingsParameter param;
  char buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned nbytes               = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, stream_id, "Received SETTINGS frame");

  if (this->get_zombie_event()) {
    Warning("Setting frame for zombied session %" PRId64, this->session->get_connection_id());
  }

  // Update SETTIGNS frame count per minute
  this->increment_received_settings_frame_count();
  // Close this connection if its SETTINGS frame count exceeds a limit
  if (this->get_received_settings_frame_count() > Http2::max_settings_frames_per_minute) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_SETTINGS_FRAMES_PER_MINUTE_EXCEEDED, this_ethread());
    Http2StreamDebug(this->session, stream_id, "Observed too frequent SETTINGS frames: %u frames within a last minute",
                     this->get_received_settings_frame_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv settings too frequent SETTINGS frames");
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
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_SETTINGS_PER_FRAME_EXCEEDED, this_ethread());
      Http2StreamDebug(this->session, stream_id, "Observed too many settings in a frame");
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

    Http2StreamDebug(this->session, stream_id, "   %s : %u", Http2DebugNames::get_settings_param_name(param.id), param.value);

    // [RFC 7540] 6.9.2. When the value of SETTINGS_INITIAL_WINDOW_SIZE
    // changes, a receiver MUST adjust the size of all stream flow control
    // windows that it maintains by the difference between the new value and
    // the old value.
    if (param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
      this->update_initial_rwnd(param.value);
    }

    this->client_settings.set(static_cast<Http2SettingsIdentifier>(param.id), param.value);
    ++n_settings;
  }

  // Update settings count per minute
  this->increment_received_settings_count(n_settings);
  // Close this connection if its settings count received exceeds a limit
  if (this->get_received_settings_count() > Http2::max_settings_per_minute) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_SETTINGS_PER_MINUTE_EXCEEDED, this_ethread());
    Http2StreamDebug(this->session, stream_id, "Observed too frequent setting changes: %u settings within a last minute",
                     this->get_received_settings_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv settings too frequent setting changes");
  }

  // [RFC 7540] 6.5. Once all values have been applied, the recipient MUST
  // immediately emit a SETTINGS frame with the ACK flag set.
  Http2SettingsFrame ack_frame(0, HTTP2_FLAGS_SETTINGS_ACK);
  this->session->xmit(ack_frame);

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_push_promise_frame(const Http2Frame &frame)
{
  Http2StreamDebug(this->session, frame.header().streamid, "Received PUSH_PROMISE frame");

  // [RFC 7540] 8.2. A client cannot push. Thus, servers MUST treat the receipt of a
  // PUSH_PROMISE frame as a connection error of type PROTOCOL_ERROR.
  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                    "promise not allowed");
}

Http2Error
Http2ConnectionState::rcv_ping_frame(const Http2Frame &frame)
{
  uint8_t opaque_data[HTTP2_PING_LEN];
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, stream_id, "Received PING frame");

  this->schedule_zombie_event();

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

  // Update PING frame count per minute
  this->increment_received_ping_frame_count();
  // Close this connection if its ping count received exceeds a limit
  if (this->get_received_ping_frame_count() > Http2::max_ping_frames_per_minute) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_PING_FRAMES_PER_MINUTE_EXCEEDED, this_ethread());
    Http2StreamDebug(this->session, stream_id, "Observed too frequent PING frames: %u PING frames within a last minute",
                     this->get_received_ping_frame_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv ping too frequent PING frame");
  }

  // An endpoint MUST NOT respond to PING frames containing this flag.
  if (frame.header().flags & HTTP2_FLAGS_PING_ACK) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  frame.reader()->memcpy(opaque_data, HTTP2_PING_LEN, 0);

  // ACK (0x1): An endpoint MUST set this flag in PING responses.
  this->send_ping_frame(stream_id, HTTP2_FLAGS_PING_ACK, opaque_data);

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_goaway_frame(const Http2Frame &frame)
{
  Http2Goaway goaway;
  char buf[HTTP2_GOAWAY_LEN];
  unsigned nbytes               = 0;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, stream_id, "Received GOAWAY frame");

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

  Http2StreamDebug(this->session, stream_id, "GOAWAY: last stream id=%d, error code=%d", goaway.last_streamid,
                   static_cast<int>(goaway.error_code));

  this->rx_error_code = {ProxyErrorClass::SSN, static_cast<uint32_t>(goaway.error_code)};
  this->session->get_proxy_session()->do_io_close();

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_window_update_frame(const Http2Frame &frame)
{
  char buf[HTTP2_WINDOW_UPDATE_LEN];
  uint32_t size;
  const Http2StreamId stream_id = frame.header().streamid;

  //  A WINDOW_UPDATE frame with a length other than 4 octets MUST be
  //  treated as a connection error of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_WINDOW_UPDATE_LEN) {
    Http2StreamDebug(this->session, stream_id, "Received WINDOW_UPDATE frame - length incorrect");
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
    Http2StreamDebug(this->session, stream_id, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (this->client_rwnd() + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - this->client_rwnd()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big");
    }

    auto error = this->increment_client_rwnd(size);
    if (error != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, error, "Erroneous client window update");
    }

    this->restart_streams();
  } else {
    // Stream level window update
    Http2Stream *stream = this->find_stream(stream_id);

    if (stream == nullptr) {
      if (this->is_valid_streamid(stream_id)) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "window update stream invalid id");
      }
    }

    Http2StreamDebug(this->session, stream_id, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (stream->client_rwnd() + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - stream->client_rwnd()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big 2");
    }

    auto error = stream->increment_client_rwnd(size);
    if (error != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, error);
    }

    ssize_t wnd = std::min(this->client_rwnd(), stream->client_rwnd());
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
Http2Error
Http2ConnectionState::rcv_continuation_frame(const Http2Frame &frame)
{
  const Http2StreamId stream_id = frame.header().streamid;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received CONTINUATION frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "continuation bad client id");
  }

  // Find opened stream
  // CONTINUATION frames MUST be associated with a stream.  If a
  // CONTINUATION frame is received whose stream identifier field is 0x0,
  // the recipient MUST respond with a connection error ([RFC 7540] Section
  // 5.4.1) of type PROTOCOL_ERROR.
  Http2Stream *stream = this->find_stream(stream_id);
  if (stream == nullptr) {
    if (this->is_valid_streamid(stream_id)) {
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

  uint32_t header_blocks_offset = stream->header_blocks_length;
  stream->header_blocks_length += payload_length;

  // ATS advertises SETTINGS_MAX_HEADER_LIST_SIZE as a limit of total header blocks length. (Details in [RFC 7560] 10.5.1.)
  // Make it double to relax the limit in cases of 1) HPACK is used naively, or 2) Huffman Encoding generates large header blocks.
  // The total "decoded" header length is strictly checked by hpack_decode_header_block().
  if (stream->header_blocks_length > std::max(Http2::max_header_list_size, Http2::max_header_list_size * 2)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "header blocks too large");
  }

  stream->header_blocks = static_cast<uint8_t *>(ats_realloc(stream->header_blocks, stream->header_blocks_length));
  frame.reader()->memcpy(stream->header_blocks + header_blocks_offset, payload_length);

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    this->clear_continued_stream_id();

    if (!stream->change_state(HTTP2_FRAME_TYPE_CONTINUATION, frame.header().flags)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "continuation no state change");
    }

    Http2ErrorCode result =
      stream->decode_header_blocks(*this->local_hpack_handle, this->server_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

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

    // Check Content-Length & payload length when END_STREAM flag is true
    if (stream->recv_end_stream && !stream->payload_length_is_valid()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data bad payload length");
    }

    // Set up the State Machine
    SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
    stream->mark_milestone(Http2StreamMilestone::START_TXN);
    // This should be fine, need to verify whether we need to replace this with the
    // "from_early_data" flag from the associated HEADERS frame.
    stream->new_transaction(frame.is_from_early_data());
    // Send request header to SM
    stream->send_request(*this);
  } else {
    // NOTE: Expect another CONTINUATION Frame. Do nothing.
    Http2StreamDebug(this->session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

////////
// Http2ConnectionSettings
//
Http2ConnectionSettings::Http2ConnectionSettings()
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
Http2ConnectionSettings::settings_from_configs()
{
  settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = Http2::max_concurrent_streams_in;
  settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)]    = Http2::initial_window_size;
  settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)]         = Http2::max_frame_size;
  settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)]      = Http2::header_table_size;
  settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)]   = Http2::max_header_list_size;
}

unsigned
Http2ConnectionSettings::get(Http2SettingsIdentifier id) const
{
  if (0 < id && id < HTTP2_SETTINGS_MAX) {
    return this->settings[indexof(id)];
  } else {
    ink_assert(!"Bad Settings Identifier");
  }

  return 0;
}

unsigned
Http2ConnectionSettings::set(Http2SettingsIdentifier id, unsigned value)
{
  if (0 < id && id < HTTP2_SETTINGS_MAX) {
    return this->settings[indexof(id)] = value;
  } else {
    // Do nothing - 6.5.2 Unsupported parameters MUST be ignored
  }

  return 0;
}

unsigned
Http2ConnectionSettings::indexof(Http2SettingsIdentifier id)
{
  ink_assert(0 < id && id < HTTP2_SETTINGS_MAX);

  return id - 1;
}

////////
// Http2ConnectionState
//
Http2ConnectionState::Http2ConnectionState() : stream_list()
{
  SET_HANDLER(&Http2ConnectionState::main_event_handler);
}

void
Http2ConnectionState::init(Http2CommonSession *ssn)
{
  session = ssn;

  if (Http2::initial_window_size < HTTP2_INITIAL_WINDOW_SIZE) {
    // There is no HTTP/2 specified way to shrink the connection window size
    // other than to receive data and not send WINDOW_UPDATE frames for a
    // while.
    this->_server_rwnd              = HTTP2_INITIAL_WINDOW_SIZE;
    this->_server_rwnd_is_shrinking = true;
  } else {
    this->_server_rwnd              = Http2::initial_window_size;
    this->_server_rwnd_is_shrinking = false;
  }

  local_hpack_handle  = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
  remote_hpack_handle = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
  if (Http2::stream_priority_enabled) {
    dependency_tree = new DependencyTree(Http2::max_concurrent_streams_in);
  }

  _cop = ActivityCop<Http2Stream>(this->mutex, &stream_list, 1);
  _cop.start();
}

/**
   Send connection preface

   The client connection preface is HTTP2_CONNECTION_PREFACE.
   The server connection preface consists of a potentially empty SETTINGS frame.

   Details in [RFC 7540] 3.5. HTTP/2 Connection Preface

   TODO: send client connection preface if the connection is outbound
 */
void
Http2ConnectionState::send_connection_preface()
{
  REMEMBER(NO_EVENT, this->recursion)

  Http2ConnectionSettings configured_settings;
  configured_settings.settings_from_configs();
  configured_settings.set(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, _adjust_concurrent_stream());

  send_settings_frame(configured_settings);

  if (server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) > HTTP2_INITIAL_WINDOW_SIZE) {
    send_window_update_frame(0, server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - HTTP2_INITIAL_WINDOW_SIZE);
  }
}

void
Http2ConnectionState::destroy()
{
  if (in_destroy) {
    schedule_zombie_event();
    return;
  }
  in_destroy = true;

  _cop.stop();

  if (shutdown_cont_event) {
    shutdown_cont_event->cancel();
    shutdown_cont_event = nullptr;
  }
  cleanup_streams();

  delete local_hpack_handle;
  local_hpack_handle = nullptr;
  delete remote_hpack_handle;
  remote_hpack_handle = nullptr;
  delete dependency_tree;
  dependency_tree = nullptr;
  this->session   = nullptr;

  if (fini_event) {
    fini_event->cancel();
  }
  if (zombie_event) {
    zombie_event->cancel();
  }
  // release the mutex after the events are cancelled and sessions are destroyed.
  mutex = nullptr; // magic happens - assigning to nullptr frees the ProxyMutex
}

void
Http2ConnectionState::rcv_frame(const Http2Frame *frame)
{
  REMEMBER(NO_EVENT, this->recursion);
  const Http2StreamId stream_id = frame->header().streamid;
  Http2Error error;

  // [RFC 7540] 5.5. Extending HTTP/2
  //   Implementations MUST discard frames that have unknown or unsupported types.
  if (frame->header().type >= HTTP2_FRAME_TYPE_MAX) {
    Http2StreamDebug(session, stream_id, "Discard a frame which has unknown type, type=%x", frame->header().type);
    return;
  }

  // We need to be careful here, certain frame types are not safe over 0-rtt, tentative for now.
  // DATA:          NO
  // HEADERS:       YES (safe http methods only, can only be checked after parsing the payload).
  // PRIORITY:      YES
  // RST_STREAM:    NO
  // SETTINGS:      YES
  // PUSH_PROMISE:  NO
  // PING:          YES
  // GOAWAY:        NO
  // WINDOW_UPDATE: YES
  // CONTINUATION:  YES (safe http methods only, same as HEADERS frame).
  if (frame->is_from_early_data() &&
      (frame->header().type == HTTP2_FRAME_TYPE_DATA || frame->header().type == HTTP2_FRAME_TYPE_RST_STREAM ||
       frame->header().type == HTTP2_FRAME_TYPE_PUSH_PROMISE || frame->header().type == HTTP2_FRAME_TYPE_GOAWAY)) {
    Http2StreamDebug(session, stream_id, "Discard a frame which is received from early data and has type=%x", frame->header().type);
    return;
  }

  if (this->_frame_handlers[frame->header().type]) {
    error = (this->*_frame_handlers[frame->header().type])(*frame);
  } else {
    error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR, "no handler");
  }

  if (error.cls != Http2ErrorClass::HTTP2_ERROR_CLASS_NONE) {
    ip_port_text_buffer ipb;
    const char *client_ip = ats_ip_ntop(session->get_proxy_session()->get_remote_addr(), ipb, sizeof(ipb));
    if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION) {
      if (error.msg) {
        Error("HTTP/2 connection error code=0x%02x client_ip=%s session_id=%" PRId64 " stream_id=%u %s",
              static_cast<int>(error.code), client_ip, session->get_connection_id(), stream_id, error.msg);
      }
      this->send_goaway_frame(this->latest_streamid_in, error.code);
      this->session->set_half_close_local_flag(true);
      if (fini_event == nullptr) {
        fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
      }

      // The streams will be cleaned up by the HTTP2_SESSION_EVENT_FINI event
      // The Http2ClientSession will shutdown because connection_state.is_state_closed() will be true
    } else if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM) {
      if (error.msg) {
        Error("HTTP/2 stream error code=0x%02x client_ip=%s session_id=%" PRId64 " stream_id=%u %s", static_cast<int>(error.code),
              client_ip, session->get_connection_id(), stream_id, error.msg);
      }
      this->send_rst_stream_frame(stream_id, error.code);
    }
  }
}

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
  // Finalize HTTP/2 Connection
  case HTTP2_SESSION_EVENT_FINI: {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    REMEMBER(event, this->recursion);

    ink_assert(this->fini_received == false);
    this->fini_received = true;
    cleanup_streams();
    release_stream();
    SET_HANDLER(&Http2ConnectionState::state_closed);
  } break;

  case HTTP2_SESSION_EVENT_XMIT: {
    REMEMBER(event, this->recursion);
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    send_data_frames_depends_on_priority();
    _scheduled = false;
  } break;

  // Initiate a graceful shutdown
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

  // Continue a graceful shutdown
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
    SCOPED_MUTEX_LOCK(lock, this->session->get_mutex(), this_ethread());
    this->session->set_half_close_local_flag(true);
  } break;

  default:
    Http2ConDebug(session, "unexpected event=%d edata=%p", event, edata);
    ink_release_assert(0);
    break;
  }

  --recursion;
  if (recursion == 0 && session && !session->is_recursing()) {
    if (this->session->ready_to_free()) {
      MUTEX_TRY_LOCK(lock, this->session->get_mutex(), this_ethread());
      if (lock.is_locked()) {
        this->session->get_proxy_session()->free();
        // After the free, the Http2ConnectionState object is also freed.
        // The Http2ConnectionState object is allocated within the Http2ClientSession object
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
  // first check if we've hit the active connection limit
  if (!session->get_netvc()->add_to_active_queue()) {
    error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_NO_ERROR,
                       "refused to create new stream, maxed out active connections");
    return nullptr;
  }

  // In half_close state, TS doesn't create new stream. Because GOAWAY frame is sent to client
  if (session->get_half_close_local_flag()) {
    error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                       "refused to create new stream, because session is in half_close state");
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
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_IN, this_ethread());
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating inbound stream beyond max_concurrent limit");
      return nullptr;
    }
  } else {
    if (client_streams_out_count >= client_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) {
      HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_MAX_CONCURRENT_STREAMS_EXCEEDED_OUT, this_ethread());
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating outbound stream beyond max_concurrent limit");
      return nullptr;
    }
  }

  Http2Stream *new_stream = THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread(), session->get_proxy_session(), new_id,
                                              client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));

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

  new_stream->mutex                     = new_ProxyMutex();
  new_stream->is_first_transaction_flag = get_stream_requests() == 0;
  increment_stream_requests();

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
          std::min(this->client_rwnd(), s->client_rwnd()) > 0) {
        SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
        s->restart_sending();
      }
      ink_assert(s != next);
      s = next;
    }
    if (!s->is_closed() && s->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE &&
        std::min(this->client_rwnd(), s->client_rwnd()) > 0) {
      SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
      s->restart_sending();
    }

    ++starting_point;
  }
}

void
Http2ConnectionState::restart_receiving(Http2Stream *stream)
{
  uint32_t initial_rwnd = this->server_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  uint32_t min_rwnd     = std::min(initial_rwnd, this->server_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE));

  // Connection level WINDOW UPDATE
  if (this->server_rwnd() < min_rwnd) {
    Http2WindowSize diff_size = initial_rwnd - this->server_rwnd();
    this->increment_server_rwnd(diff_size);
    this->_server_rwnd_is_shrinking = false;
    this->send_window_update_frame(0, diff_size);
  }

  // Stream level WINDOW UPDATE
  if (stream == nullptr || stream->server_rwnd() >= min_rwnd) {
    return;
  }

  // If read_vio is buffering data, do not fully update window
  int64_t data_size = stream->read_vio_read_avail();
  if (data_size >= initial_rwnd) {
    return;
  }

  Http2WindowSize diff_size = initial_rwnd - std::max(static_cast<int64_t>(stream->server_rwnd()), data_size);
  stream->increment_server_rwnd(diff_size);
  this->send_window_update_frame(stream->get_id(), diff_size);
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
    s->initiating_close();
    ink_assert(s != next);
    s = next;
  }

  if (!is_state_closed()) {
    SCOPED_MUTEX_LOCK(lock, this->session->get_mutex(), this_ethread());

    UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(session->get_netvc());
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

  Http2StreamDebug(session, stream->get_id(), "Delete stream");
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
        Debug("http2_priority", "[%" PRId64 "] %s", session->get_connection_id(), output.str().c_str());
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
Http2ConnectionState::release_stream()
{
  REMEMBER(NO_EVENT, this->recursion)

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  if (this->session) {
    ink_assert(this->mutex == session->get_mutex());

    if (total_client_streams_count == 0) {
      if (fini_received) {
        session->do_clear_session_active();

        // We were shutting down, go ahead and terminate the session
        // this is a member of Http2ConnectionState and will be freed
        // when session is destroyed
        session->get_proxy_session()->destroy();

        // Can't do this because we just destroyed right here ^,
        // or we can use a local variable to do it.
        // session = nullptr;
      } else if (session->get_proxy_session()->is_active()) {
        // If the number of clients is 0, HTTP2_SESSION_EVENT_FINI is not received or sent, and session is active,
        // then mark the connection as inactive
        session->do_clear_session_active();
        UnixNetVConnection *vc = static_cast<UnixNetVConnection *>(session->get_netvc());
        if (vc && vc->active_timeout_in == 0) {
          // With heavy traffic, session could be destroyed. Do not touch session after this.
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
    s->update_initial_rwnd(new_size - (client_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - s->client_rwnd()));
  }
}

void
Http2ConnectionState::schedule_stream(Http2Stream *stream)
{
  Http2StreamDebug(session, stream->get_id(), "Scheduled");

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
  if (node == nullptr || _client_rwnd <= 0) {
    return;
  }

  Http2Stream *stream = static_cast<Http2Stream *>(node->t);
  ink_release_assert(stream != nullptr);
  Http2StreamDebug(session, stream->get_id(), "top node, point=%d", node->point);

  size_t len                      = 0;
  Http2SendDataFrameResult result = send_a_data_frame(stream, len);

  switch (result) {
  case Http2SendDataFrameResult::NO_ERROR: {
    // No response body to send
    if (len == 0 && !stream->is_write_vio_done()) {
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
    stream->initiating_close();
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
  const ssize_t window_size         = std::min(this->client_rwnd(), stream->client_rwnd());
  const size_t buf_len              = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
  const size_t write_available_size = std::min(buf_len, static_cast<size_t>(window_size));
  payload_length                    = 0;

  uint8_t flags               = 0x00;
  IOBufferReader *resp_reader = stream->response_get_data_reader();

  SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());

  if (!resp_reader) {
    Http2StreamDebug(this->session, stream->get_id(), "couldn't get data reader");
    return Http2SendDataFrameResult::ERROR;
  }

  // Select appropriate payload length
  if (resp_reader->is_read_avail_more_than(0)) {
    // We only need to check for window size when there is a payload
    if (window_size <= 0) {
      Http2StreamDebug(this->session, stream->get_id(), "No window");
      this->session->flush();
      return Http2SendDataFrameResult::NO_WINDOW;
    }

    if (resp_reader->is_read_avail_more_than(write_available_size)) {
      payload_length = write_available_size;
    } else {
      payload_length = resp_reader->read_avail();
    }
  } else {
    payload_length = 0;
  }

  if (payload_length > 0 && this->session->is_write_high_water()) {
    Http2StreamDebug(this->session, stream->get_id(), "Not write avail");
    this->session->flush();
    return Http2SendDataFrameResult::NOT_WRITE_AVAIL;
  }

  stream->update_sent_count(payload_length);

  // Are we at the end?
  // If we return here, we never send the END_STREAM in the case of a early terminating OS.
  // OK if there is no body yet. Otherwise continue on to send a DATA frame and delete the stream
  if (!stream->is_write_vio_done() && payload_length == 0) {
    Http2StreamDebug(this->session, stream->get_id(), "No payload");
    this->session->flush();
    return Http2SendDataFrameResult::NO_PAYLOAD;
  }

  if (stream->is_write_vio_done()) {
    flags |= HTTP2_FLAGS_DATA_END_STREAM;
  }

  // Update window size
  this->decrement_client_rwnd(payload_length);
  stream->decrement_client_rwnd(payload_length);

  // Create frame
  Http2StreamDebug(session, stream->get_id(), "Send a DATA frame - client window con: %5zd stream: %5zd payload: %5zd",
                   _client_rwnd, stream->client_rwnd(), payload_length);

  Http2DataFrame data(stream->get_id(), flags, resp_reader, payload_length);
  this->session->xmit(data, flags & HTTP2_FLAGS_DATA_END_STREAM);

  if (flags & HTTP2_FLAGS_DATA_END_STREAM) {
    Http2StreamDebug(session, stream->get_id(), "END_STREAM");
    stream->send_end_stream = true;
    // Setting to the same state shouldn't be erroneous
    stream->change_state(HTTP2_FRAME_TYPE_DATA, flags);

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
    Http2StreamDebug(this->session, stream->get_id(), "Shutdown half closed local stream");
    stream->initiating_close();
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
      Http2StreamDebug(this->session, stream->get_id(), "Shutdown stream");
      stream->initiating_close();
    }
  }

  return;
}

void
Http2ConnectionState::send_headers_frame(Http2Stream *stream)
{
  uint32_t header_blocks_size = 0;
  int payload_length          = 0;
  uint8_t flags               = 0x00;

  Http2StreamDebug(session, stream->get_id(), "Send HEADERS frame");

  HTTPHdr *resp_hdr = &stream->response_header;
  http2_convert_header_from_1_1_to_2(resp_hdr);

  uint32_t buf_len = resp_hdr->length_get() * 2; // Make it double just in case
  ts::LocalBuffer local_buffer(buf_len);
  uint8_t *buf = local_buffer.data();

  stream->mark_milestone(Http2StreamMilestone::START_ENCODE_HEADERS);
  Http2ErrorCode result = http2_encode_header_blocks(resp_hdr, buf, buf_len, &header_blocks_size, *(this->remote_hpack_handle),
                                                     client_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    return;
  }

  // Send a HEADERS frame
  if (header_blocks_size <= static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]))) {
    payload_length = header_blocks_size;
    flags |= HTTP2_FLAGS_HEADERS_END_HEADERS;
    if ((resp_hdr->presence(MIME_PRESENCE_CONTENT_LENGTH) && resp_hdr->get_content_length() == 0) ||
        (!resp_hdr->expect_final_response() && stream->is_write_vio_done())) {
      Http2StreamDebug(session, stream->get_id(), "END_STREAM");
      flags |= HTTP2_FLAGS_HEADERS_END_STREAM;
      stream->send_end_stream = true;
    }
    stream->mark_milestone(Http2StreamMilestone::START_TX_HEADERS_FRAMES);
  } else {
    payload_length = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]);
  }

  // Change stream state
  if (!stream->change_state(HTTP2_FRAME_TYPE_HEADERS, flags)) {
    this->send_goaway_frame(this->latest_streamid_in, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR);
    this->session->set_half_close_local_flag(true);
    if (fini_event == nullptr) {
      fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
    }

    return;
  }

  Http2HeadersFrame headers(stream->get_id(), flags, buf, payload_length);
  this->session->xmit(headers);
  uint64_t sent = payload_length;

  // Send CONTINUATION frames
  flags = 0;
  while (sent < header_blocks_size) {
    Http2StreamDebug(session, stream->get_id(), "Send CONTINUATION frame");
    payload_length = std::min(static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION])),
                              static_cast<uint32_t>(header_blocks_size - sent));
    if (sent + payload_length == header_blocks_size) {
      flags |= HTTP2_FLAGS_CONTINUATION_END_HEADERS;
    }
    stream->change_state(HTTP2_FRAME_TYPE_CONTINUATION, flags);

    Http2ContinuationFrame continuation_frame(stream->get_id(), flags, buf + sent, payload_length);
    this->session->xmit(continuation_frame);
    sent += payload_length;
  }
}

bool
Http2ConnectionState::send_push_promise_frame(Http2Stream *stream, URL &url, const MIMEField *accept_encoding)
{
  uint32_t header_blocks_size = 0;
  int payload_length          = 0;
  uint8_t flags               = 0x00;

  if (client_settings.get(HTTP2_SETTINGS_ENABLE_PUSH) == 0) {
    return false;
  }

  Http2StreamDebug(session, stream->get_id(), "Send PUSH_PROMISE frame");

  HTTPHdr hdr;
  ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
  hdr.create(HTTP_TYPE_REQUEST, HTTP_2_0);
  hdr.url_set(&url);
  hdr.method_set(HTTP_METHOD_GET, HTTP_LEN_GET);

  if (accept_encoding != nullptr) {
    int name_len;
    const char *name = accept_encoding->name_get(&name_len);
    MIMEField *f     = hdr.field_create(name, name_len);

    int value_len;
    const char *value = accept_encoding->value_get(&value_len);
    f->value_set(hdr.m_heap, hdr.m_mime, value, value_len);

    hdr.field_attach(f);
  }

  http2_convert_header_from_1_1_to_2(&hdr);

  uint32_t buf_len = hdr.length_get() * 2; // Make it double just in case
  ts::LocalBuffer local_buffer(buf_len);
  uint8_t *buf = local_buffer.data();

  Http2ErrorCode result = http2_encode_header_blocks(&hdr, buf, buf_len, &header_blocks_size, *(this->remote_hpack_handle),
                                                     client_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    return false;
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

  Http2StreamId id               = this->get_latest_stream_id_out() + 2;
  push_promise.promised_streamid = id;

  Http2PushPromiseFrame push_promise_frame(stream->get_id(), flags, push_promise, buf, payload_length);
  this->session->xmit(push_promise_frame);
  uint64_t sent = payload_length;

  // Send CONTINUATION frames
  flags = 0;
  while (sent < header_blocks_size) {
    Http2StreamDebug(session, stream->get_id(), "Send CONTINUATION frame");
    payload_length = std::min(static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_CONTINUATION])),
                              static_cast<uint32_t>(header_blocks_size - sent));
    if (sent + payload_length == header_blocks_size) {
      flags |= HTTP2_FLAGS_CONTINUATION_END_HEADERS;
    }

    Http2ContinuationFrame continuation(stream->get_id(), flags, buf + sent, payload_length);
    this->session->xmit(continuation);
    sent += payload_length;
  }

  Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  stream = this->create_stream(id, error);
  if (!stream) {
    return false;
  }

  SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
  if (Http2::stream_priority_enabled) {
    Http2DependencyTree::Node *node = this->dependency_tree->find(id);
    if (node != nullptr) {
      stream->priority_node = node;
    } else {
      Http2StreamDebug(this->session, id, "PRIORITY - dep: %d, weight: %d, excl: %d, tree size: %d",
                       HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY, HTTP2_PRIORITY_DEFAULT_WEIGHT, false,
                       this->dependency_tree->size());

      stream->priority_node =
        this->dependency_tree->add(HTTP2_PRIORITY_DEFAULT_STREAM_DEPENDENCY, id, HTTP2_PRIORITY_DEFAULT_WEIGHT, false, stream);
    }
  }
  stream->change_state(HTTP2_FRAME_TYPE_PUSH_PROMISE, HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS);
  stream->set_request_headers(hdr);
  stream->new_transaction();
  stream->recv_end_stream = true; // No more data with the request
  stream->send_request(*this);

  return true;
}

void
Http2ConnectionState::send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec)
{
  Http2StreamDebug(session, id, "Send RST_STREAM frame");

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_STREAM_ERRORS_COUNT, this_ethread());
    ++stream_error_count;
  }

  // change state to closed
  Http2Stream *stream = find_stream(id);
  if (stream != nullptr) {
    stream->set_tx_error_code({ProxyErrorClass::TXN, static_cast<uint32_t>(ec)});
    if (!stream->change_state(HTTP2_FRAME_TYPE_RST_STREAM, 0)) {
      this->send_goaway_frame(this->latest_streamid_in, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR);
      this->session->set_half_close_local_flag(true);
      if (fini_event == nullptr) {
        fini_event = this_ethread()->schedule_imm_local((Continuation *)this, HTTP2_SESSION_EVENT_FINI);
      }

      return;
    }
  }

  Http2RstStreamFrame rst_stream(id, static_cast<uint32_t>(ec));
  this->session->xmit(rst_stream);
}

void
Http2ConnectionState::send_settings_frame(const Http2ConnectionSettings &new_settings)
{
  const Http2StreamId stream_id = 0;

  Http2StreamDebug(session, stream_id, "Send SETTINGS frame");

  Http2SettingsParameter params[HTTP2_SETTINGS_MAX];
  size_t params_size = 0;

  for (int i = HTTP2_SETTINGS_HEADER_TABLE_SIZE; i < HTTP2_SETTINGS_MAX; ++i) {
    Http2SettingsIdentifier id = static_cast<Http2SettingsIdentifier>(i);
    unsigned settings_value    = new_settings.get(id);

    // Send only difference
    if (settings_value != server_settings.get(id)) {
      Http2StreamDebug(session, stream_id, "  %s : %u", Http2DebugNames::get_settings_param_name(id), settings_value);

      params[params_size++] = {static_cast<uint16_t>(id), settings_value};

      // Update current settings
      server_settings.set(id, new_settings.get(id));
    }
  }

  Http2SettingsFrame settings(stream_id, HTTP2_FRAME_NO_FLAG, params, params_size);
  this->session->xmit(settings);
}

void
Http2ConnectionState::send_ping_frame(Http2StreamId id, uint8_t flag, const uint8_t *opaque_data)
{
  Http2StreamDebug(session, id, "Send PING frame");

  Http2PingFrame ping(id, flag, opaque_data);
  this->session->xmit(ping);
}

// As for graceful shutdown, TS should process outstanding stream as long as possible.
// As for signal connection error, TS should close connection immediately.
void
Http2ConnectionState::send_goaway_frame(Http2StreamId id, Http2ErrorCode ec)
{
  ink_assert(this->session != nullptr);

  Http2ConDebug(session, "Send GOAWAY frame, last_stream_id: %d", id);

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CONNECTION_ERRORS_COUNT, this_ethread());
  }

  this->tx_error_code = {ProxyErrorClass::SSN, static_cast<uint32_t>(ec)};

  Http2Goaway goaway;
  goaway.last_streamid = id;
  goaway.error_code    = ec;

  Http2GoawayFrame frame(goaway);
  this->session->xmit(frame);
}

void
Http2ConnectionState::send_window_update_frame(Http2StreamId id, uint32_t size)
{
  Http2StreamDebug(session, id, "Send WINDOW_UPDATE frame: size=%" PRIu32, size);

  // Create WINDOW_UPDATE frame
  Http2WindowUpdateFrame window_update(id, size);
  this->session->xmit(window_update);
}

void
Http2ConnectionState::increment_received_settings_count(uint32_t count)
{
  this->_received_settings_counter.increment(count);
}

uint32_t
Http2ConnectionState::get_received_settings_count()
{
  return this->_received_settings_counter.get_count();
}

void
Http2ConnectionState::increment_received_settings_frame_count()
{
  this->_received_settings_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_settings_frame_count()
{
  return this->_received_settings_frame_counter.get_count();
}

void
Http2ConnectionState::increment_received_ping_frame_count()
{
  this->_received_ping_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_ping_frame_count()
{
  return this->_received_ping_frame_counter.get_count();
}

void
Http2ConnectionState::increment_received_priority_frame_count()
{
  this->_received_priority_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_priority_frame_count()
{
  return this->_received_priority_frame_counter.get_count();
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

  Http2ConDebug(session, "current client streams: %" PRId64, current_client_streams);

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

ssize_t
Http2ConnectionState::client_rwnd() const
{
  return this->_client_rwnd;
}

Http2ErrorCode
Http2ConnectionState::increment_client_rwnd(size_t amount)
{
  this->_client_rwnd += amount;

  this->_recent_rwnd_increment[this->_recent_rwnd_increment_index] = amount;
  ++this->_recent_rwnd_increment_index;
  this->_recent_rwnd_increment_index %= this->_recent_rwnd_increment.size();
  double sum = std::accumulate(this->_recent_rwnd_increment.begin(), this->_recent_rwnd_increment.end(), 0.0);
  double avg = sum / this->_recent_rwnd_increment.size();
  if (avg < Http2::min_avg_window_update) {
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_INSUFFICIENT_AVG_WINDOW_UPDATE, this_ethread());
    return Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
  }
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2ConnectionState::decrement_client_rwnd(size_t amount)
{
  this->_client_rwnd -= amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

ssize_t
Http2ConnectionState::server_rwnd() const
{
  return this->_server_rwnd;
}

Http2ErrorCode
Http2ConnectionState::increment_server_rwnd(size_t amount)
{
  this->_server_rwnd += amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2ConnectionState::decrement_server_rwnd(size_t amount)
{
  this->_server_rwnd -= amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}
