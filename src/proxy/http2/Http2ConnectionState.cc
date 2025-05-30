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

#include "../../iocore/net/P_Net.h"
#include "../../iocore/net/P_UnixNetVConnection.h"
#include "iocore/eventsystem/Lock.h"
#include "proxy/http2/HTTP2.h"
#include "proxy/http2/Http2ConnectionState.h"
#include "proxy/http2/Http2ClientSession.h"
#include "proxy/http2/Http2ServerSession.h"
#include "proxy/http2/Http2Stream.h"
#include "proxy/http2/Http2Frame.h"
#include "proxy/http2/Http2DebugNames.h"
#include "proxy/http/HttpDebugNames.h"
#include "proxy/http/HttpSM.h"

#include "iocore/net/TLSSNISupport.h"

#include "tscore/ink_assert.h"
#include "tscore/ink_hrtime.h"
#include "tscore/ink_memory.h"
#include "tsutil/PostScript.h"
#include "tsutil/LocalBuffer.h"

#include <cstdint>
#include <sstream>
#include <numeric>

namespace
{
DbgCtl dbg_ctl_http2_con{"http2_con"};
DbgCtl dbg_ctl_http2_priority{"http2_priority"};

#define REMEMBER(e, r)                                     \
  {                                                        \
    if (this->session) {                                   \
      this->session->remember(MakeSourceLocation(), e, r); \
    }                                                      \
  }

#define Http2ConDebug(session, fmt, ...) Dbg(dbg_ctl_http2_con, "[%" PRId64 "] " fmt, session->get_connection_id(), ##__VA_ARGS__);

#define Http2StreamDebug(session, stream_id, fmt, ...) \
  Dbg(dbg_ctl_http2_con, "[%" PRId64 "] [%u] " fmt, session->get_connection_id(), stream_id, ##__VA_ARGS__);

const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] = {
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

inline unsigned
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

} // end anonymous namespace

Http2Error
Http2ConnectionState::rcv_data_frame(const Http2Frame &frame)
{
  unsigned       nbytes         = 0;
  Http2StreamId  id             = frame.header().streamid;
  uint8_t        pad_length     = 0;
  const uint32_t payload_length = frame.header().length;

  Http2StreamDebug(this->session, id, "Received DATA frame, flags: %d", frame.header().flags);

  // Update connection window size, before any stream specific handling
  this->decrement_local_rwnd(payload_length);

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
      if (this->session->is_outbound()) {
        this->send_rst_stream_frame(id, Http2ErrorCode::HTTP2_ERROR_NO_ERROR);
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED, nullptr);
      }
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data stream freed with invalid id");
    }
  }

  if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED ||
      stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE) {
    this->send_rst_stream_frame(id, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED);
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
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
    stream->receive_end_stream = true;
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
      if (stream->is_read_enabled()) {
        stream->signal_read_event(VC_EVENT_READ_COMPLETE);
      }
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
  } else {
    // Any headers that show up after we received data are by definition trailing headers
    stream->set_trailing_header_is_possible();
  }

  // If payload length is 0 without END_STREAM flag, just count it
  const uint32_t unpadded_length = payload_length - pad_length;
  if (unpadded_length == 0 && !stream->receive_end_stream) {
    this->increment_received_empty_frame_count();
    if (configured_max_empty_frames_per_minute >= 0 &&
        this->get_received_empty_frame_count() > static_cast<uint32_t>(configured_max_empty_frames_per_minute)) {
      Metrics::Counter::increment(http2_rsb.max_empty_frames_per_minute_exceeded);
      Http2StreamDebug(this->session, id, "Observed too many empty DATA frames: %u within the last minute",
                       this->get_received_empty_frame_count());
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                        "recv data too frequent empty frame");
    }

    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
  }

  // Check whether Window Size is acceptable
  // compare to 0 because we already decreased the connection rwnd with payload_length
  if (!this->_local_rwnd_is_shrinking && this->get_local_rwnd() < 0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data this->local_rwnd < payload_length");
  }
  if (stream->get_local_rwnd() < payload_length) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                      "recv data stream->local_rwnd < payload_length");
  }

  // Update stream window size
  stream->decrement_local_rwnd(payload_length);

  if (dbg_ctl_http2_con.on()) {
    uint32_t const stream_window  = this->acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
    uint32_t const session_window = this->_get_configured_receive_session_window_size();
    Http2StreamDebug(this->session, id,
                     "Received DATA frame: payload_length=%" PRId32 " rwnd con=%zd/%" PRId32 " stream=%zd/%" PRId32, payload_length,
                     this->get_local_rwnd(), session_window, stream->get_local_rwnd(), stream_window);
  }

  MIOBuffer *writer = stream->read_vio_writer();
  if (writer == nullptr) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR, "no writer");
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
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR, "Write mismatch");
    }
    myreader->consume(num_written);
    stream->update_read_length(num_written);
  }
  myreader->writer()->dealloc_reader(myreader);

  if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
    // TODO: set total written size to read_vio.nbytes
    stream->set_read_done();
  }

  if (stream->is_read_enabled()) {
    if (frame.header().flags & HTTP2_FLAGS_DATA_END_STREAM) {
      if (this->get_peer_stream_count() > 1 && this->get_local_rwnd() == 0) {
        // This final DATA frame for this stream consumed all the bytes for the
        // session window. Send a WINDOW_UPDATE frame in order to open up the
        // session window for other streams.
        restart_receiving(nullptr);
      }
      stream->signal_read_event(VC_EVENT_READ_COMPLETE);
    } else {
      stream->signal_read_event(VC_EVENT_READ_READY);
    }
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
  const Http2StreamId stream_id      = frame.header().streamid;
  const uint32_t      payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received HEADERS frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv headers bad client id");
  }

  Http2Stream *stream                      = nullptr;
  bool         new_stream                  = false;
  bool         reset_header_after_decoding = false;
  bool         free_stream_after_decoding  = false;

  if (this->is_valid_streamid(stream_id)) {
    stream = this->find_stream(stream_id);
    if (!this->session->is_outbound() && (stream == nullptr || !stream->trailing_header_is_possible())) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                        "stream not expecting trailer header");
    } else if (stream == nullptr || stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
      if (this->session->is_outbound()) {
        reset_header_after_decoding = true;
        // return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
        // return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
        //                  "recv_header to closed stream");
      } else {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_STREAM_CLOSED,
                          "recv_header to closed stream");
      }
    }
  }

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "recv headers bad client id");
  }

  if (!stream) {
    if (reset_header_after_decoding) {
      free_stream_after_decoding                 = true;
      uint32_t const initial_local_stream_window = this->acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
      ink_assert(dynamic_cast<Http2CommonSession *>(this->session->get_proxy_session()));
      ink_assert(this->session->is_outbound() == true);
      stream = THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread(), this->session->get_proxy_session(), stream_id,
                                 this->peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE), initial_local_stream_window,
                                 !STREAM_IS_REGISTERED);
    } else {
      // Create new stream
      Http2Error error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
      stream     = this->create_stream(stream_id, error);
      new_stream = true;
      if (!stream) {
        // Terminate the connection with COMPRESSION_ERROR because we don't decompress the field block in this HEADERS frame.
        // TODO: try to decompress to keep HPACK Dynamic Table in sync.
        if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM) {
          return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR,
                            error.msg);
        }

        return error;
      }
    }
  }

  // HEADERS frame on a closed stream.  The HdrHeap has gone away and it will core.
  if (stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED) {
    Http2StreamDebug(session, stream_id, "Replaced closed stream");
    free_stream_after_decoding = true;
    stream                     = THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread(), session->get_proxy_session(), stream_id,
                                                   peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE), true, false);
    if (!stream) {
      // This happening is possibly catastrophic, the HPACK tables can be out of sync
      // Maybe this is a connection level error?
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }
  }

  Http2HeadersParameter params;
  uint32_t              header_block_fragment_offset = 0;
  uint32_t              header_block_fragment_length = payload_length;

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM) {
    stream->receive_end_stream = true;
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
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_COMPRESSION_ERROR,
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

  if (stream->trailing_header_is_possible()) {
    // Don't leak the header_blocks from the initial, non-trailing headers.
    ats_free(stream->header_blocks);
  }
  stream->header_blocks = static_cast<uint8_t *>(ats_malloc(header_block_fragment_length));
  frame.reader()->memcpy(stream->header_blocks, header_block_fragment_length, header_block_fragment_offset);

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    if (!stream->change_state(HTTP2_FRAME_TYPE_HEADERS, frame.header().flags)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv headers end headers and not trailing header");
    }

    if (stream->trailing_header_is_possible()) {
      if (!(frame.header().flags & HTTP2_FLAGS_HEADERS_END_STREAM)) {
        return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                          "recv headers tailing header without endstream");
      }
    }

    if (stream->trailing_header_is_possible()) {
      stream->reset_receive_headers();
    } else {
      stream->mark_milestone(Http2StreamMilestone::START_DECODE_HEADERS);
    }
    Http2ErrorCode result = stream->decode_header_blocks(*this->local_hpack_handle,
                                                         this->acknowledged_local_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

    // If this was an outbound connection and the state was already closed, just clear the
    // headers after processing.  We just processed the heaer blocks to keep the dynamic table in
    // sync with peer to avoid future HPACK compression errors
    if (reset_header_after_decoding) {
      stream->reset_receive_headers();
      if (free_stream_after_decoding) {
        THREAD_FREE(stream, http2StreamAllocator, this_ethread());
      }
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
    }

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
    if (stream->receive_end_stream && !stream->payload_length_is_valid()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "recv data bad payload length");
    }

    // Set up the State Machine
    if (!stream->is_outbound_connection() && !stream->trailing_header_is_possible()) {
      SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
      stream->mark_milestone(Http2StreamMilestone::START_TXN);
      stream->cancel_active_timeout();
      stream->new_transaction(frame.is_from_early_data());
      // Send request header to SM
      stream->send_headers(*this);
    } else {
      // If this is a trailer, first signal to the SM that the body is done
      if (stream->trailing_header_is_possible()) {
        stream->set_expect_receive_trailer();
        // Propagate the  trailer header
        stream->send_headers(*this);
      } else {
        // Propagate the response
        stream->send_headers(*this);
      }
    }
    // Give a chance to send response before reading next frame.
    this->session->interrupt_reading_frames();
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
  const Http2StreamId stream_id      = frame.header().streamid;
  const uint32_t      payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received PRIORITY frame");

  if (this->get_zombie_event()) {
    Warning("Priority frame for zombied session %" PRId64, this->session->get_connection_id());
  }

  // If a PRIORITY frame is received with a stream identifier of 0x0, the
  // recipient MUST respond with a connection error of type PROTOCOL_ERROR.
  if (stream_id == HTTP2_CONNECTION_CONTROL_STREAM) {
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
  if (configured_max_priority_frames_per_minute >= 0 &&
      this->get_received_priority_frame_count() > static_cast<uint32_t>(configured_max_priority_frames_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_priority_frames_per_minute_exceeded);
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
    if (dbg_ctl_http2_priority.on()) {
      std::stringstream output;
      this->dependency_tree->dump_tree(output);
      Dbg(dbg_ctl_http2_priority, "[%" PRId64 "] reprioritize %s", this->session->get_connection_id(), output.str().c_str());
    }
  } else {
    // PRIORITY frame is received before HEADERS frame.

    // Restrict number of inactive node in dependency tree smaller than max_concurrent_streams.
    // Current number of inactive node is size of tree minus active node count.
    if (this->_get_configured_max_concurrent_streams() > this->dependency_tree->size() - this->get_peer_stream_count() + 1) {
      this->dependency_tree->add(priority.stream_dependency, stream_id, priority.weight, priority.exclusive_flag, nullptr);
    }
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_rst_stream_frame(const Http2Frame &frame)
{
  Http2RstStream      rst_stream;
  char                buf[HTTP2_RST_STREAM_LEN];
  char               *end;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, frame.header().streamid, "Received RST_STREAM frame");

  // RST_STREAM frames MUST be associated with a stream.  If a RST_STREAM
  // frame is received with a stream identifier of 0x0, the recipient MUST
  // treat this as a connection error (Section 5.4.1) of type
  // PROTOCOL_ERROR.
  if (stream_id == HTTP2_CONNECTION_CONTROL_STREAM) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "reset access stream with invalid id");
  }

  // A RST_STREAM frame with a length other than 4 octets MUST be treated
  // as a connection error (Section 5.4.1) of type FRAME_SIZE_ERROR.
  if (frame.header().length != HTTP2_RST_STREAM_LEN) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FRAME_SIZE_ERROR,
                      "reset frame wrong length");
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

  // Update RST_STREAM frame count per minute
  this->increment_received_rst_stream_frame_count();
  // Close this connection if its RST_STREAM frame count exceeds a limit
  if (configured_max_rst_stream_frames_per_minute >= 0 &&
      this->get_received_rst_stream_frame_count() > static_cast<uint32_t>(configured_max_rst_stream_frames_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_rst_stream_frames_per_minute_exceeded);
    Http2StreamDebug(this->session, stream_id, "Observed too frequent RST_STREAM frames: %u frames within a last minute",
                     this->get_received_rst_stream_frame_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "reset too frequent RST_STREAM frames");
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
    Http2StreamDebug(this->session, stream_id, "Parsed RST_STREAM frame: Error Code: %u", rst_stream.error_code);
    stream->set_rx_error_code({ProxyErrorClass::TXN, static_cast<uint32_t>(rst_stream.error_code)});
    stream->initiating_close();
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

Http2Error
Http2ConnectionState::rcv_settings_frame(const Http2Frame &frame)
{
  Http2SettingsParameter param;
  char                   buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned               nbytes    = 0;
  const Http2StreamId    stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, stream_id, "Received SETTINGS frame");

  if (this->get_zombie_event()) {
    Warning("Setting frame for zombied session %" PRId64, this->session->get_connection_id());
  }

  // Update SETTINGS frame count per minute
  this->increment_received_settings_frame_count();
  // Close this connection if its SETTINGS frame count exceeds a limit
  if (configured_max_settings_frames_per_minute >= 0 &&
      this->get_received_settings_frame_count() > static_cast<uint32_t>(configured_max_settings_frames_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_settings_frames_per_minute_exceeded);
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
      this->_process_incoming_settings_ack_frame();
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
    if (Http2::max_settings_per_frame >= 0 && n_settings >= static_cast<uint32_t>(Http2::max_settings_per_frame)) {
      Metrics::Counter::increment(http2_rsb.max_settings_per_frame_exceeded);
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
      this->update_initial_peer_rwnd(param.value);
    }

    this->peer_settings.set(static_cast<Http2SettingsIdentifier>(param.id), param.value);
    ++n_settings;
  }

  // Update settings count per minute
  this->increment_received_settings_count(n_settings);
  // Close this connection if its settings count received exceeds a limit
  if (Http2::max_settings_per_frame >= 0 &&
      this->get_received_settings_count() > static_cast<uint32_t>(Http2::max_settings_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_settings_per_minute_exceeded);
    Http2StreamDebug(this->session, stream_id, "Observed too frequent setting changes: %u settings within a last minute",
                     this->get_received_settings_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "recv settings too frequent setting changes");
  }

  // [RFC 7540] 6.5. Once all values have been applied, the recipient MUST
  // immediately emit a SETTINGS frame with the ACK flag set.
  Http2SettingsFrame ack_frame(HTTP2_CONNECTION_CONTROL_STREAM, HTTP2_FLAGS_SETTINGS_ACK);
  Http2StreamDebug(this->session, stream_id, "Send SETTINGS ACK");
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
  uint8_t             opaque_data[HTTP2_PING_LEN];
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
  if (configured_max_ping_frames_per_minute >= 0 &&
      this->get_received_ping_frame_count() > static_cast<uint32_t>(configured_max_ping_frames_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_ping_frames_per_minute_exceeded);
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
  Http2Goaway         goaway;
  char                buf[HTTP2_GOAWAY_LEN];
  char               *end;
  const Http2StreamId stream_id = frame.header().streamid;

  Http2StreamDebug(this->session, stream_id, "Received GOAWAY frame");

  // An endpoint MUST treat a GOAWAY frame with a stream identifier other
  // than 0x0 as a connection error of type PROTOCOL_ERROR.
  if (stream_id != 0x0) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "goaway id non-zero");
  }

  end = frame.reader()->memcpy(buf, sizeof(buf), 0);

  if (!http2_parse_goaway(make_iovec(buf, end - buf), goaway)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "goaway failed parse");
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
  char                buf[HTTP2_WINDOW_UPDATE_LEN];
  uint32_t            size;
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
    if (stream_id == HTTP2_CONNECTION_CONTROL_STREAM) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "window update length=0 and id=0");
    } else {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "window update length=0");
    }
  }

  if (stream_id == HTTP2_CONNECTION_CONTROL_STREAM) {
    // Connection level window update
    Http2StreamDebug(this->session, stream_id, "Received WINDOW_UPDATE frame - updated to: %zd delta: %u",
                     (this->get_peer_rwnd() + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - this->get_peer_rwnd()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big");
    }

    auto error = this->increment_peer_rwnd(size);
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
                     (stream->get_peer_rwnd() + size), size);

    // A sender MUST NOT allow a flow-control window to exceed 2^31-1
    // octets.  If a sender receives a WINDOW_UPDATE that causes a flow-
    // control window to exceed this maximum, it MUST terminate either the
    // stream or the connection, as appropriate.  For streams, the sender
    // sends a RST_STREAM with an error code of FLOW_CONTROL_ERROR; for the
    // connection, a GOAWAY frame with an error code of FLOW_CONTROL_ERROR
    // is sent.
    if (size > HTTP2_MAX_WINDOW_SIZE - stream->get_peer_rwnd()) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_FLOW_CONTROL_ERROR,
                        "window update too big 2");
    }

    auto error = stream->increment_peer_rwnd(size);
    if (error != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, error, "Bad stream rwnd");
    }

    ssize_t wnd = std::min(this->get_peer_rwnd(), stream->get_peer_rwnd());
    if (wnd > 0) {
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
  const Http2StreamId stream_id      = frame.header().streamid;
  const uint32_t      payload_length = frame.header().length;

  Http2StreamDebug(this->session, stream_id, "Received CONTINUATION frame");

  if (!http2_is_client_streamid(stream_id)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                      "continuation bad client id");
  }

  if (payload_length == 0 && (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) == 0x0) {
    this->increment_received_empty_frame_count();
    if (configured_max_empty_frames_per_minute >= 0 &&
        this->get_received_empty_frame_count() > static_cast<uint32_t>(configured_max_empty_frames_per_minute)) {
      Metrics::Counter::increment(http2_rsb.max_empty_frames_per_minute_exceeded);
      Http2StreamDebug(this->session, stream_id, "Observed too many empty CONTINUATION frames: %u within the last minute",
                       this->get_received_empty_frame_count());
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                        "recv continuation too frequent empty frame");
    }
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

  // Update CONTINUATION frame count per minute.
  this->increment_received_continuation_frame_count();
  // Close this connection if its CONTINUATION frame count exceeds a limit.
  if (configured_max_continuation_frames_per_minute != 0 &&
      this->get_received_continuation_frame_count() > static_cast<uint32_t>(configured_max_continuation_frames_per_minute)) {
    Metrics::Counter::increment(http2_rsb.max_continuation_frames_per_minute_exceeded);
    Http2StreamDebug(this->session, stream_id, "Observed too frequent CONTINUATION frames: %u frames within a last minute",
                     this->get_received_continuation_frame_count());
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "reset too frequent CONTINUATION frames");
  }

  uint32_t header_blocks_offset  = stream->header_blocks_length;
  stream->header_blocks_length  += payload_length;

  // ATS advertises SETTINGS_MAX_HEADER_LIST_SIZE as a limit of total header blocks length. (Details in [RFC 7560] 10.5.1.)
  // Make it double to relax the limit in cases of 1) HPACK is used naively, or 2) Huffman Encoding generates large header blocks.
  // The total "decoded" header length is strictly checked by hpack_decode_header_block().
  if (stream->header_blocks_length > std::max(Http2::max_header_list_size, Http2::max_header_list_size * 2)) {
    return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM,
                      "header blocks too large");
  }

  if (payload_length > 0) {
    stream->header_blocks = static_cast<uint8_t *>(ats_realloc(stream->header_blocks, stream->header_blocks_length));
    frame.reader()->memcpy(stream->header_blocks + header_blocks_offset, payload_length);
  }

  if (frame.header().flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
    // NOTE: If there are END_HEADERS flag, decode stored Header Blocks.
    this->clear_continued_stream_id();

    if (!stream->change_state(HTTP2_FRAME_TYPE_CONTINUATION, frame.header().flags)) {
      return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION, Http2ErrorCode::HTTP2_ERROR_PROTOCOL_ERROR,
                        "continuation no state change");
    }

    Http2ErrorCode result = stream->decode_header_blocks(*this->local_hpack_handle,
                                                         this->acknowledged_local_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));

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
    if (stream->receive_end_stream && !stream->payload_length_is_valid()) {
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
    stream->send_headers(*this);
    // Give a chance to send response before reading next frame.
    this->session->interrupt_reading_frames();
  } else {
    // NOTE: Expect another CONTINUATION Frame. Do nothing.
    Http2StreamDebug(this->session, stream_id, "No END_HEADERS flag, expecting CONTINUATION frame");
  }

  return Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_NONE);
}

////////
// Configuration Getters.
//
uint32_t
Http2ConnectionState::_get_configured_max_concurrent_streams() const
{
  ink_assert(this->session != nullptr);
  if (this->session->is_outbound()) {
    return Http2::max_concurrent_streams_out;
  } else {
    return Http2::max_concurrent_streams_in;
  }
}

uint32_t
Http2ConnectionState::_get_configured_min_concurrent_streams() const
{
  ink_assert(this->session != nullptr);
  if (this->session->is_outbound()) {
    return Http2::min_concurrent_streams_out;
  } else {
    return Http2::min_concurrent_streams_in;
  }
}

uint32_t
Http2ConnectionState::_get_configured_max_active_streams() const
{
  ink_assert(this->session != nullptr);
  if (this->session->is_outbound()) {
    return Http2::max_active_streams_out;
  } else {
    return Http2::max_active_streams_in;
  }
}

uint32_t
Http2ConnectionState::_get_configured_initial_window_size() const
{
  ink_assert(this->session != nullptr);
  if (this->session->is_outbound()) {
    return Http2::initial_window_size_out;
  } else {
    uint32_t initial_window_size_in = Http2::initial_window_size_in;
    if (this->session) {
      if (auto snis = session->get_netvc()->get_service<TLSSNISupport>();
          snis && snis->hints_from_sni.http2_initial_window_size_in.has_value()) {
        initial_window_size_in = snis->hints_from_sni.http2_initial_window_size_in.value();
      }
    }

    return initial_window_size_in;
  }
}

Http2FlowControlPolicy
Http2ConnectionState::_get_configured_flow_control_policy() const
{
  ink_assert(this->session != nullptr);
  if (this->session->is_outbound()) {
    return Http2::flow_control_policy_out;
  } else {
    return Http2::flow_control_policy_in;
  }
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
Http2ConnectionSettings::settings_from_configs(bool is_outbound)
{
  if (is_outbound) {
    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = Http2::max_concurrent_streams_out;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)]    = Http2::initial_window_size_out;
  } else {
    settings[indexof(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)] = Http2::max_concurrent_streams_in;
    settings[indexof(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE)]    = Http2::initial_window_size_in;
  }
  settings[indexof(HTTP2_SETTINGS_MAX_FRAME_SIZE)]       = Http2::max_frame_size;
  settings[indexof(HTTP2_SETTINGS_HEADER_TABLE_SIZE)]    = Http2::header_table_size;
  settings[indexof(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE)] = Http2::max_header_list_size;
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
  session                                  = ssn;
  uint32_t const configured_session_window = this->_get_configured_receive_session_window_size();

  if (configured_session_window < HTTP2_INITIAL_WINDOW_SIZE) {
    // There is no HTTP/2 specified way to shrink the connection window size
    // other than to receive data and not send WINDOW_UPDATE frames for a
    // while.
    this->_local_rwnd              = HTTP2_INITIAL_WINDOW_SIZE;
    this->_local_rwnd_is_shrinking = true;
  } else {
    this->_local_rwnd              = configured_session_window;
    this->_local_rwnd_is_shrinking = false;
  }
  Http2ConDebug(session, "initial _local_rwnd: %zd", this->_local_rwnd);

  local_hpack_handle = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
  peer_hpack_handle  = new HpackHandle(HTTP2_HEADER_TABLE_SIZE);
  if (Http2::stream_priority_enabled) {
    dependency_tree = new DependencyTree(this->_get_configured_max_concurrent_streams());
  }

  // Generally speaking, before enforcing h2 settings we wait upon the client to
  // acknowledge the settings via a SETTINGS ACK. This is important for things
  // like correctly handling windows. Howerver, the RFC default values for
  // MAX_CONCURRENT_STREAMS and MAX_HEADER_SIZE are infinite, which is not
  // practical and a client can run ATS out of resources by simply opening up
  // more streams than is reasonable. We enforce our configured defaults before
  // the client ACKs our SETTINGS by setting the configured defaults in the
  // acknowledged settings.
  Http2ConnectionSettings configured_settings;
  configured_settings.settings_from_configs(session->is_outbound());
  acknowledged_local_settings.set(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS,
                                  configured_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS));
  acknowledged_local_settings.set(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE,
                                  configured_settings.get(HTTP2_SETTINGS_MAX_HEADER_LIST_SIZE));

  configured_max_settings_frames_per_minute     = Http2::max_settings_frames_per_minute;
  configured_max_ping_frames_per_minute         = Http2::max_ping_frames_per_minute;
  configured_max_priority_frames_per_minute     = Http2::max_priority_frames_per_minute;
  configured_max_rst_stream_frames_per_minute   = Http2::max_rst_stream_frames_per_minute;
  configured_max_continuation_frames_per_minute = Http2::max_continuation_frames_per_minute;
  configured_max_empty_frames_per_minute        = Http2::max_empty_frames_per_minute;

  if (auto snis = session->get_netvc()->get_service<TLSSNISupport>(); snis) {
    if (snis->hints_from_sni.http2_max_settings_frames_per_minute.has_value()) {
      configured_max_settings_frames_per_minute = snis->hints_from_sni.http2_max_settings_frames_per_minute.value();
    }
    if (snis->hints_from_sni.http2_max_ping_frames_per_minute.has_value()) {
      configured_max_ping_frames_per_minute = snis->hints_from_sni.http2_max_ping_frames_per_minute.value();
    }
    if (snis->hints_from_sni.http2_max_priority_frames_per_minute.has_value()) {
      configured_max_priority_frames_per_minute = snis->hints_from_sni.http2_max_priority_frames_per_minute.value();
    }
    if (snis->hints_from_sni.http2_max_rst_stream_frames_per_minute.has_value()) {
      configured_max_rst_stream_frames_per_minute = snis->hints_from_sni.http2_max_rst_stream_frames_per_minute.value();
    }
    if (snis->hints_from_sni.http2_max_continuation_frames_per_minute.has_value()) {
      configured_max_continuation_frames_per_minute = snis->hints_from_sni.http2_max_continuation_frames_per_minute.value();
    }
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
  configured_settings.settings_from_configs(session->is_outbound());

  // We do not have PUSH_PROMISE implemented, so we communicate to the peer
  // that they should not send such frames to us. RFC 9113 6.5.2 says that
  // servers can send this too, but they  must always set a value of 0. Thus we
  // send a value of 0 for both inbound and outbound connections.
  configured_settings.set(HTTP2_SETTINGS_ENABLE_PUSH, 0);

  configured_settings.set(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, _adjust_concurrent_stream());

  uint32_t const configured_initial_window_size = this->_get_configured_receive_session_window_size();
  if (this->_has_dynamic_stream_window()) {
    // Since this is the beginning of the connection and there are no streams
    // yet, we can just set the stream window size to fill the entire session
    // window size.
    configured_settings.set(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, configured_initial_window_size);
  }

  send_settings_frame(configured_settings);

  // If the session window size is non-default, send a WINDOW_UPDATE right
  // away. Note that there is no session window size setting in HTTP/2. The
  // session window size is controlled entirely by WINDOW_UPDATE frames.
  if (configured_initial_window_size > HTTP2_INITIAL_WINDOW_SIZE) {
    auto const diff = configured_initial_window_size - HTTP2_INITIAL_WINDOW_SIZE;
    Http2ConDebug(session, "Updating the session window with a WINDOW_UPDATE frame: %u", diff);
    send_window_update_frame(HTTP2_CONNECTION_CONTROL_STREAM, diff);
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
  delete peer_hpack_handle;
  peer_hpack_handle = nullptr;
  delete dependency_tree;
  dependency_tree = nullptr;
  this->session   = nullptr;

  if (fini_event) {
    fini_event->cancel();
  }
  if (zombie_event) {
    zombie_event->cancel();
  }

  if (_priority_event) {
    _priority_event->cancel();
  }

  if (_data_event) {
    _data_event->cancel();
  }

  if (retransmit_event) {
    retransmit_event->cancel();
  }
  // release the mutex after the events are cancelled and sessions are destroyed.
  mutex = nullptr; // magic happens - assigning to nullptr frees the ProxyMutex
}

void
Http2ConnectionState::rcv_frame(const Http2Frame *frame)
{
  REMEMBER(NO_EVENT, this->recursion);
  const Http2StreamId stream_id = frame->header().streamid;
  Http2Error          error;

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
    const char         *client_ip = ats_ip_ntop(session->get_proxy_session()->get_remote_addr(), ipb, sizeof(ipb));
    if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_CONNECTION) {
      if (error.msg) {
        Error("HTTP/2 connection error code=0x%02x client_ip=%s session_id=%" PRId64 " stream_id=%u %s",
              static_cast<int>(error.code), client_ip, session->get_connection_id(), stream_id, error.msg);
      }
      this->send_goaway_frame(this->latest_streamid_in, error.code);
      this->session->set_half_close_local_flag(true);
      if (fini_event == nullptr) {
        fini_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_FINI);
      }

      // The streams will be cleaned up by the HTTP2_SESSION_EVENT_FINI event
      // The Http2ClientSession will shutdown because connection_state.is_state_closed() will be true
    } else if (error.cls == Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM) {
      if (error.msg) {
        Error("HTTP/2 stream error code=0x%02x client_ip=%s session_id=%" PRId64 " stream_id=%u %s", static_cast<int>(error.code),
              client_ip, session->get_connection_id(), stream_id, error.msg);
      }
      this->send_rst_stream_frame(stream_id, error.code);

      // start closing stream on stream error
      if (Http2Stream *stream = find_stream(stream_id); stream != nullptr) {
        ink_assert(stream->get_state() == Http2StreamState::HTTP2_STREAM_STATE_CLOSED);
        stream->initiating_close();
      }
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
  } else if (edata == _priority_event) {
    _priority_event = nullptr;
  } else if (edata == _data_event) {
    _data_event = nullptr;
  } else if (edata == retransmit_event) {
    retransmit_event = nullptr;
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

  case HTTP2_SESSION_EVENT_PRIO: {
    REMEMBER(event, this->recursion);
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    send_data_frames_depends_on_priority();
  } break;

  case HTTP2_SESSION_EVENT_DATA: {
    REMEMBER(event, this->recursion);
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    // mark the retry flag here so if the writes fail again we will
    // backoff the next attempt
    _data_event_retry = true;
    this->restart_streams();
    _data_event_retry = false;
  } break;

  case HTTP2_SESSION_EVENT_XMIT: {
    REMEMBER(event, this->recursion);
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    this->session->flush();
  } break;

  case HTTP2_SESSION_EVENT_ERROR: {
    REMEMBER(event, this->recursion);

    Http2ErrorCode error_code = Http2ErrorCode::HTTP2_ERROR_INTERNAL_ERROR;
    if (edata != nullptr) {
      Http2Error *error = static_cast<Http2Error *>(edata);
      error_code        = error->code;
    }

    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    this->send_goaway_frame(this->latest_streamid_in, error_code);
    this->session->set_half_close_local_flag(true);
    if (fini_event == nullptr) {
      this->fini_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_FINI);
    }
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
    shutdown_cont_event =
      this_ethread()->schedule_in(static_cast<Continuation *>(this), HRTIME_SECONDS(2), HTTP2_SESSION_EVENT_SHUTDOWN_CONT);
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
  } else if (edata == _priority_event) {
    _priority_event = nullptr;
  } else if (edata == _data_event) {
    _data_event = nullptr;
  } else if (edata == shutdown_cont_event) {
    shutdown_cont_event = nullptr;
  } else if (edata == retransmit_event) {
    retransmit_event = nullptr;
  }
  return 0;
}

bool
Http2ConnectionState::is_peer_concurrent_stream_ub() const
{
  return peer_streams_count_in >= (peer_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) * 0.9;
}

bool
Http2ConnectionState::is_peer_concurrent_stream_lb() const
{
  return peer_streams_count_in <= (peer_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS)) / 2;
}

void
Http2ConnectionState::set_stream_id(Http2Stream *stream)
{
  if (stream->get_transaction_id() < 0) {
    Http2StreamId stream_id = (latest_streamid_in == 0) ? 3 : latest_streamid_in + 2;
    stream->set_transaction_id(stream_id);
    latest_streamid_in = stream_id;
  }
}

Http2Stream *
Http2ConnectionState::create_initiating_stream(Http2Error &error)
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

  // Endpoints MUST NOT exceed the limit set by their peer.  An endpoint
  // that receives a HEADERS frame that causes their advertised concurrent
  // stream limit to be exceeded MUST treat this as a stream error.
  int check_max_concurrent_limit;
  int check_count;
  check_count = peer_streams_count_in;
  // If this is an outbound client stream, must check against the peer's max_concurrent
  if (session->is_outbound()) {
    check_max_concurrent_limit = peer_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
  } else { // Inbound client streamm check against our own max_connecurent limits
    check_max_concurrent_limit = local_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
  }
  ink_release_assert(check_max_concurrent_limit != 0);

  // If we haven't got the peers settings yet, just hope for the best
  if (check_max_concurrent_limit >= 0) {
    if (session->is_outbound() && Http2ConnectionState::is_peer_concurrent_stream_ub()) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating stream beyond max_concurrent limit");
      return nullptr;
    } else if (check_count >= check_max_concurrent_limit) {
      error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                         "recv headers creating stream beyond max_concurrent limit");
      return nullptr;
    }
  }

  ink_assert(dynamic_cast<Http2CommonSession *>(this->session->get_proxy_session()));
  ink_assert(this->session->is_outbound() == true);
  uint32_t const initial_stream_window = this->acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  Http2Stream   *new_stream =
    THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread(), session->get_proxy_session(), -1,
                      peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE), initial_stream_window, STREAM_IS_REGISTERED);

  ink_assert(nullptr != new_stream);
  ink_assert(!stream_list.in(new_stream));

  stream_list.enqueue(new_stream);
  ink_assert(peer_streams_count_in < UINT32_MAX);
  ++peer_streams_count_in;
  ++total_peer_streams_count;

  if (zombie_event != nullptr) {
    zombie_event->cancel();
    zombie_event = nullptr;
  }

  new_stream->mutex                     = new_ProxyMutex();
  new_stream->is_first_transaction_flag = get_stream_requests() == 0;
  increment_stream_requests();

  // Clear the session timeout.  Let the transaction timeouts reign
  session->get_proxy_session()->cancel_inactivity_timeout();

  if (session->is_outbound() && this->_has_dynamic_stream_window()) {
    // See the comment in create_stream() concerning the difference between the
    // initial window size and the target window size for dynamic stream window
    // sizes.
    Http2ConnectionSettings new_settings = local_settings;
    uint32_t const          initial_stream_window_target =
      this->_get_configured_receive_session_window_size() / (peer_streams_count_in.load());
    new_settings.set(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, initial_stream_window_target);
    send_settings_frame(new_settings);
  }

  return new_stream;
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

  bool is_client_streamid = http2_is_client_streamid(new_id);

  // 5.1.1 The identifier of a newly established stream MUST be numerically
  // greater than all streams that the initiating endpoint has opened or
  // reserved.  This governs streams that are opened using a HEADERS frame
  // and streams that are reserved using PUSH_PROMISE.  An endpoint that
  // receives an unexpected stream identifier MUST respond with a
  // connection error (Section 5.4.1) of type PROTOCOL_ERROR.
  if (is_client_streamid) {
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
  int check_max_concurrent_limit = 0;
  int check_count                = 0;
  if (is_client_streamid) {
    check_count = peer_streams_count_in;
    // If this is an outbound client stream, must check against the peer's max_concurrent
    if (session->is_outbound()) {
      check_max_concurrent_limit = peer_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    } else { // Inbound client streamm check against our own max_connecurent limits
      check_max_concurrent_limit = acknowledged_local_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    }
  } else { // Not a client stream (i.e. a push)
    check_count = peer_streams_count_out;
    // If this is an outbound non-client stream, must check against the local max_concurrent
    if (session->is_outbound()) {
      check_max_concurrent_limit = acknowledged_local_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    } else { // Inbound non-client streamm check against the peer's max_connecurent limits
      check_max_concurrent_limit = peer_settings.get(HTTP2_SETTINGS_MAX_CONCURRENT_STREAMS);
    }
  }
  // If we haven't got the peers settings yet, just hope for the best
  if (check_max_concurrent_limit >= 0 && check_count >= check_max_concurrent_limit) {
    Metrics::Counter::increment(is_client_streamid ? http2_rsb.max_concurrent_streams_exceeded_in :
                                                     http2_rsb.max_concurrent_streams_exceeded_out);
    error = Http2Error(Http2ErrorClass::HTTP2_ERROR_CLASS_STREAM, Http2ErrorCode::HTTP2_ERROR_REFUSED_STREAM,
                       "recv headers creating stream beyond max_concurrent limit");
    return nullptr;
  }

  ink_assert(dynamic_cast<Http2CommonSession *>(this->session->get_proxy_session()));
  ink_assert(this->session->is_outbound() == false);
  uint32_t initial_stream_window        = this->acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  uint32_t initial_stream_window_target = initial_stream_window;
  if (is_client_streamid && this->_has_dynamic_stream_window()) {
    // For dynamic stream windows, the peer's idea of what the window size is
    // may be different than what we are configuring. Our calculated server
    // receive window is always maintained at what the peer has acknowledged so
    // far. This prevents us from enforcing window sizes that have been
    // adjusted by SETTINGS frames which the peer has not received yet. So we
    // initialize the receive window to what the peer has acknowledged while in
    // the meantime calculating initial_stream_window_target for the SETTINGS
    // frame which will shrink or enlarge it to our new desired size.
    //
    // The situation of dynamic stream window sizes is described in [RFC 9113]
    // 6.9.3.
    initial_stream_window_target = this->_get_configured_receive_session_window_size() / (peer_streams_count_in.load() + 1);
  }
  Http2Stream *new_stream =
    THREAD_ALLOC_INIT(http2StreamAllocator, this_ethread(), session->get_proxy_session(), new_id,
                      peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE), initial_stream_window, STREAM_IS_REGISTERED);

  ink_assert(nullptr != new_stream);
  ink_assert(!stream_list.in(new_stream));

  new_stream->mutex                     = new_ProxyMutex();
  new_stream->is_first_transaction_flag = get_stream_requests() == 0;

  stream_list.enqueue(new_stream);
  if (is_client_streamid) {
    latest_streamid_in = new_id;
    ink_assert(peer_streams_count_in < UINT32_MAX);
    ++peer_streams_count_in;
    if (this->_has_dynamic_stream_window()) {
      Http2ConnectionSettings new_settings = local_settings;
      new_settings.set(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE, initial_stream_window_target);
      send_settings_frame(new_settings);
    }
  } else {
    latest_streamid_out = new_id;
    ink_assert(peer_streams_count_out < UINT32_MAX);
    ++peer_streams_count_out;
  }
  ++total_peer_streams_count;

  if (zombie_event != nullptr) {
    zombie_event->cancel();
    zombie_event = nullptr;
  }
  increment_stream_requests();

  // Set incomplete header timeout
  //   Client should send END_HEADERS flag within the http2.incomplete_header_timeout_in.
  //   The active timeout of this stream will be reset by HttpSM with http.transction_active_timeout_in when a HTTP TXN is started
  new_stream->set_active_timeout(HRTIME_SECONDS(Http2::incomplete_header_timeout_in));

  // Clear the session timeout.  Let the transaction timeouts reign
  session->get_proxy_session()->cancel_inactivity_timeout();
  session->get_proxy_session()->set_session_active();

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
    for (int i = starting_point % total_peer_streams_count; i >= 0; --i) {
      end = static_cast<Http2Stream *>(end->link.next ? end->link.next : stream_list.head);
    }
    s = static_cast<Http2Stream *>(end->link.next ? end->link.next : stream_list.head);

    // Call send_response_body() for each streams
    while (s != end) {
      Http2Stream *next = static_cast<Http2Stream *>(s->link.next ? s->link.next : stream_list.head);
      if (this->get_peer_rwnd() > 0 && s->get_peer_rwnd() > 0) {
        SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
        s->restart_sending();
      }
      ink_assert(s != next);
      s = next;
    }

    // The above stopped at end, so we need to call send_response_body() one
    // last time for the stream pointed to by end.
    if (this->get_peer_rwnd() > 0 && s->get_peer_rwnd() > 0) {
      SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
      s->restart_sending();
    }

    ++starting_point;
  }
}

void
Http2ConnectionState::restart_receiving(Http2Stream *stream)
{
  if (this->session->get_proxy_session()->is_peer_closed()) {
    // Cannot restart a closed connection.
    return;
  }
  // Connection level WINDOW UPDATE
  uint32_t const configured_session_window = this->_get_configured_receive_session_window_size();
  uint32_t const min_session_window =
    std::min(configured_session_window, this->acknowledged_local_settings.get(HTTP2_SETTINGS_MAX_FRAME_SIZE));
  if (this->get_local_rwnd() < min_session_window) {
    Http2WindowSize diff_size = configured_session_window - this->get_local_rwnd();
    if (diff_size > 0) {
      this->increment_local_rwnd(diff_size);
      this->_local_rwnd_is_shrinking = false;
      this->send_window_update_frame(HTTP2_CONNECTION_CONTROL_STREAM, diff_size);
    }
  }

  // Stream level WINDOW UPDATE
  if (stream == nullptr || stream->get_local_rwnd() >= min_session_window) {
    // There's no need to increase the stream window size if it is already big
    // enough to hold what the stream/max frame size can receive.
    return;
  }

  uint32_t const initial_stream_window = this->acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE);
  int64_t        data_size             = stream->read_vio_read_avail();

  Http2WindowSize diff_size = 0;
  if (stream->get_local_rwnd() < 0) {
    // Receive windows can be negative if we sent a SETTINGS frame that
    // decreased the stream window size mid-stream. This is not a problem: we
    // simply compute a WINDOW_UPDATE value to bring the window up to the
    // target initial_stream_window size.
    diff_size = initial_stream_window - stream->get_local_rwnd();
  } else {
    diff_size = initial_stream_window - std::min(static_cast<int64_t>(stream->get_local_rwnd()), data_size);
  }

  // Dynamic stream window sizes may result in negative values. In this case,
  // we'll just be waiting for the peer to send more data until the receive
  // window decreases to be under the initial window size.
  if (diff_size > 0) {
    stream->increment_local_rwnd(diff_size);
    this->send_window_update_frame(stream->get_id(), diff_size);
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
    Http2DependencyTree::Node *node       = stream->priority_node;
    Http2DependencyTree::Node *node_by_id = this->dependency_tree->find(stream->get_id());
    ink_assert(node == node_by_id);
    if (node != nullptr) {
      if (node->active) {
        dependency_tree->deactivate(node, 0);
      }
      if (dbg_ctl_http2_priority.on()) {
        std::stringstream output;
        dependency_tree->dump_tree(output);
        Dbg(dbg_ctl_http2_priority, "[%" PRId64 "] %s", session->get_connection_id(), output.str().c_str());
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
    ink_release_assert(peer_streams_count_in > 0);
    --peer_streams_count_in;
    if (!fini_received && is_peer_concurrent_stream_lb()) {
      session->add_session();
    }
  } else {
    ink_assert(peer_streams_count_out > 0);
    --peer_streams_count_out;
  }
  // total_peer_streams_count will be decremented in release_stream(), because it's a counter include streams in the process of
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

    if (total_peer_streams_count == 0) {
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
        session->set_no_activity_timeout();
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
Http2ConnectionState::update_initial_peer_rwnd(Http2WindowSize new_size)
{
  // Update stream level window sizes
  for (Http2Stream *s = stream_list.head; s; s = static_cast<Http2Stream *>(s->link.next)) {
    SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
    // Set the new window size, but take into account the already adjusted
    // window based on previously sent bytes.
    //
    // For example:
    // 1. Client initializes the stream window to 10K bytes.
    // 2. ATS sends 3K bytes to the client. The stream window is now 7K bytes.
    // 3. The client sends a SETTINGS frame to update the initial window size to 20K bytes.
    // 4. ATS should update the stream window to 17K bytes: 20K - (10K - 7K).
    //
    // Note that if the client reduces the stream window, this may result in
    // negative receive window values.
    s->set_peer_rwnd(new_size - (peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - s->get_peer_rwnd()));
  }
}

void
Http2ConnectionState::update_initial_local_rwnd(Http2WindowSize new_size)
{
  // Update stream level window sizes
  for (Http2Stream *s = stream_list.head; s; s = static_cast<Http2Stream *>(s->link.next)) {
    SCOPED_MUTEX_LOCK(lock, s->mutex, this_ethread());
    // Set the new window size, but take into account the already adjusted
    // window based on previously sent bytes.
    //
    // For example:
    // 1. ATS initializes the stream window to 10K bytes.
    // 2. ATS receives 3K bytes from the client. The stream window is now 7K bytes.
    // 3. ATS sends a SETTINGS frame to the client to update the initial window size to 20K bytes.
    // 4. The stream window should be updated to 17K bytes: 20K - (10K - 7K).
    //
    // Note that if ATS reduces the stream window, this may result in negative
    // receive window values.
    s->set_local_rwnd(new_size - (acknowledged_local_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) - s->get_local_rwnd()));
  }
}

void
Http2ConnectionState::schedule_stream_to_send_priority_frames(Http2Stream *stream)
{
  Http2StreamDebug(session, stream->get_id(), "Scheduling sending priority frames");

  Http2DependencyTree::Node *node = stream->priority_node;
  ink_release_assert(node != nullptr);

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  dependency_tree->activate(node);

  if (_priority_event == nullptr) {
    SET_HANDLER(&Http2ConnectionState::main_event_handler);
    _priority_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_PRIO);
  }
}

void
Http2ConnectionState::schedule_stream_to_send_data_frames(Http2Stream *stream)
{
  Http2StreamDebug(session, stream->get_id(), "Scheduling sending data frames");

  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  if (_data_event == nullptr) {
    SET_HANDLER(&Http2ConnectionState::main_event_handler);
    // exponential backoff scheduling event if we are in a retry.
    // assume a slow reader will always be slow so don't reset the backoff
    if (_data_event_retry) {
      _data_event_backoff = std::min(DATA_EVENT_BACKOFF_MAX, _data_event_backoff << 1);
    }
    _data_event = this_ethread()->schedule_in(static_cast<Continuation *>(this), HRTIME_MSECONDS(_data_event_backoff),
                                              HTTP2_SESSION_EVENT_DATA);
  }
}

void
Http2ConnectionState::schedule_retransmit(ink_hrtime t)
{
  Http2StreamDebug(session, 0, "Scheduling retransmitting data frames");
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  if (retransmit_event == nullptr) {
    SET_HANDLER(&Http2ConnectionState::main_event_handler);
    retransmit_event = this_ethread()->schedule_in(static_cast<Continuation *>(this), t, HTTP2_SESSION_EVENT_XMIT);
  }
}

void
Http2ConnectionState::cancel_retransmit()
{
  Http2StreamDebug(session, 0, "Canceling retransmitting data frames");
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  if (retransmit_event != nullptr) {
    retransmit_event->cancel();
    retransmit_event = nullptr;
  }
}

void
Http2ConnectionState::send_data_frames_depends_on_priority()
{
  Http2DependencyTree::Node *node = dependency_tree->top();

  // No node to send or no connection level window left
  if (node == nullptr || _peer_rwnd <= 0) {
    return;
  }

  Http2Stream *stream = static_cast<Http2Stream *>(node->t);
  ink_release_assert(stream != nullptr);
  ink_release_assert(stream->priority_node == node);
  Http2StreamDebug(session, stream->get_id(), "top node, point=%d", node->point);

  size_t                   len    = 0;
  Http2SendDataFrameResult result = send_a_data_frame(stream, len);
  ink_release_assert(stream->priority_node != nullptr);

  switch (result) {
  case Http2SendDataFrameResult::NO_ERROR: {
    // No response body to send
    if (len == 0 && !stream->is_write_vio_done()) {
      dependency_tree->deactivate(node, len);
    } else {
      dependency_tree->update(node, len);
      SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());
      stream->signal_write_event(stream->is_write_vio_done() ? VC_EVENT_WRITE_COMPLETE : VC_EVENT_WRITE_READY);
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

  if (_priority_event == nullptr) {
    _priority_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_PRIO);
  }

  return;
}

Http2SendDataFrameResult
Http2ConnectionState::send_a_data_frame(Http2Stream *stream, size_t &payload_length)
{
  const ssize_t window_size          = std::min(this->get_peer_rwnd(), stream->get_peer_rwnd());
  const size_t  buf_len              = BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_DATA]);
  const size_t  write_available_size = std::min(buf_len, static_cast<size_t>(window_size));
  payload_length                     = 0;

  uint8_t         flags       = 0x00;
  IOBufferReader *resp_reader = stream->get_data_reader_for_send();

  SCOPED_MUTEX_LOCK(stream_lock, stream->mutex, this_ethread());

  if (!resp_reader) {
    Http2StreamDebug(this->session, stream->get_id(), "couldn't get data reader");
    return Http2SendDataFrameResult::ERROR;
  }

  // Select appropriate payload length
  if (resp_reader->is_read_avail_more_than(0)) {
    // We only need to check for window size when there is a payload
    if (window_size <= 0) {
      if (session->is_outbound()) {
        ip_port_text_buffer ipb;
        const char         *server_ip = ats_ip_ntop(session->get_proxy_session()->get_remote_addr(), ipb, sizeof(ipb));
        // Warn the user to give them visibility that their server-side
        // connection is being limited by their server's flow control. Maybe
        // they can make adjustments.
        Warning("No window server_ip=%s session_wnd=%zd stream_wnd=%zd peer_initial_window=%u", server_ip, get_peer_rwnd(),
                stream->get_peer_rwnd(), this->peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));
      }
      Http2StreamDebug(this->session, stream->get_id(), "No window session_wnd=%zd stream_wnd=%zd peer_initial_window=%u",
                       get_peer_rwnd(), stream->get_peer_rwnd(), this->peer_settings.get(HTTP2_SETTINGS_INITIAL_WINDOW_SIZE));
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

  // For HTTP/2 sessions, is_write_high_water() returning true correlates to
  // our write buffer exceeding HTTP2_SETTINGS_MAX_FRAME_SIZE. Thus we will
  // hold off on processing the payload until the write buffer is drained.
  if (payload_length > 0 && this->session->is_write_high_water()) {
    Http2StreamDebug(this->session, stream->get_id(), "Not write avail, payload_length=%zu", payload_length);
    this->session->flush();
    return Http2SendDataFrameResult::NOT_WRITE_AVAIL;
  }

  stream->update_sent_count(payload_length);

  // Are we at the end?
  // We have no payload to send but might expect data from either trailer or body
  // TODO(KS): does the expect send trailer and empty payload need a flush, or does it
  //           warrant a separate flow with NO_ERROR?
  // If we return here, we never send the END_STREAM in the case of a early terminating OS.
  // OK if there is no body yet. Otherwise continue on to send a DATA frame and delete the stream
  if ((!stream->is_write_vio_done() || stream->expect_send_trailer()) && payload_length == 0) {
    Http2StreamDebug(this->session, stream->get_id(), "No payload");
    this->session->flush();
    return Http2SendDataFrameResult::NO_PAYLOAD;
  }

  if (stream->is_write_vio_done() && !resp_reader->is_read_avail_more_than(payload_length) && !stream->expect_send_trailer()) {
    Http2StreamDebug(this->session, stream->get_id(), "End of Data Frame");
    flags |= HTTP2_FLAGS_DATA_END_STREAM;
  }

  // Update window size
  this->decrement_peer_rwnd(payload_length);
  stream->decrement_peer_rwnd(payload_length);

  // Create frame
  Http2StreamDebug(session, stream->get_id(), "Send a DATA frame - peer window con: %5zd stream: %5zd payload: %5zd flags: 0x%x",
                   _peer_rwnd, stream->get_peer_rwnd(), payload_length, flags);

  Http2DataFrame data(stream->get_id(), flags, resp_reader, payload_length);
  this->session->xmit(data, stream->is_tunneling() || flags & HTTP2_FLAGS_DATA_END_STREAM);

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

  if (zombie_event != nullptr) {
    zombie_event->cancel();
    zombie_event = nullptr;
  }

  size_t                   len         = 0;
  Http2SendDataFrameResult result      = Http2SendDataFrameResult::NO_ERROR;
  bool                     more_data   = true;
  IOBufferReader          *resp_reader = stream->get_data_reader_for_send();
  while (more_data && result == Http2SendDataFrameResult::NO_ERROR) {
    result    = send_a_data_frame(stream, len);
    more_data = resp_reader->is_read_avail_more_than(0);

    if (result == Http2SendDataFrameResult::DONE) {
      if (!stream->is_outbound_connection()) {
        // Delete a stream immediately
        // TODO its should not be deleted for a several time to handling
        // RST_STREAM and WINDOW_UPDATE.
        // See 'closed' state written at [RFC 7540] 5.1.
        Http2StreamDebug(this->session, stream->get_id(), "Shutdown stream");
        stream->signal_write_event(VC_EVENT_WRITE_COMPLETE);
        stream->do_io_close();
      } else if (stream->is_outbound_connection() && stream->is_write_vio_done()) {
        stream->signal_write_event(VC_EVENT_WRITE_COMPLETE);
      } else {
        ink_release_assert(!"What case is this?");
      }
    } else if (result == Http2SendDataFrameResult::NOT_WRITE_AVAIL) {
      // Schedule an even to wake up and try to resend the stream.
      schedule_stream_to_send_data_frames(stream);
    }
  }
  if (!more_data && result != Http2SendDataFrameResult::DONE) {
    stream->signal_write_event(VC_EVENT_WRITE_READY);
  }

  return;
}

void
Http2ConnectionState::send_headers_frame(Http2Stream *stream)
{
  uint32_t header_blocks_size = 0;
  int      payload_length     = 0;
  uint8_t  flags              = 0x00;

  // For outbound streams, set the ID if it has not yet already been set
  // Need to defer setting the stream ID to avoid another later created stream
  // sending out first.  This may cause the peer to issue a stream or connection
  // error (new stream less that the greatest we have seen so far)
  this->set_stream_id(stream);

  // Keep this debug below set_stream_id so that the id is correct.
  Http2StreamDebug(session, stream->get_id(), "Send HEADERS frame");

  HTTPHdr *send_hdr = stream->get_send_header();
  if (stream->expect_send_trailer()) {
    // Which is a no-op conversion
  } else {
    http2_convert_header_from_1_1_to_2(send_hdr);
  }

  uint32_t        buf_len = send_hdr->length_get() * 2; // Make it double just in case
  ts::LocalBuffer local_buffer(buf_len);
  uint8_t        *buf = local_buffer.data();

  stream->mark_milestone(Http2StreamMilestone::START_ENCODE_HEADERS);
  Http2ErrorCode result = http2_encode_header_blocks(send_hdr, buf, buf_len, &header_blocks_size, *(this->peer_hpack_handle),
                                                     peer_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    return;
  }

  // Send a HEADERS frame
  if (header_blocks_size <= static_cast<uint32_t>(BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_HEADERS]))) {
    payload_length  = header_blocks_size;
    flags          |= HTTP2_FLAGS_HEADERS_END_HEADERS;
    if (stream->is_outbound_connection()) { // Will be sending a request_header
      int method = send_hdr->method_get_wksidx();

      // Set END_STREAM on request headers for POST, etc. methods combined with
      // an explicit length 0. Some origins RST on request headers with
      // explicit zero length and no end stream flag, causing the request to
      // fail. We emulate chromium behaviour here prevent such RSTs. Transfer-encoding
      // implies there’s a body, regardless of whether it is chunked or not.
      bool    content_method       = method == HTTP_WKSIDX_POST || method == HTTP_WKSIDX_PUSH || method == HTTP_WKSIDX_PUT;
      bool    is_transfer_encoded  = send_hdr->presence(MIME_PRESENCE_TRANSFER_ENCODING);
      bool    has_content_header   = send_hdr->presence(MIME_PRESENCE_CONTENT_LENGTH);
      bool    explicit_zero_length = has_content_header && send_hdr->get_content_length() == 0;
      int64_t content_length       = has_content_header ? send_hdr->get_content_length() : 0L;
      bool is_chunked = is_transfer_encoded && send_hdr->value_get(static_cast<std::string_view>(MIME_FIELD_TRANSFER_ENCODING)) ==
                                                 static_cast<std::string_view>(HTTP_VALUE_CHUNKED);

      bool expect_content_stream =
        is_transfer_encoded ||                                                        // transfer encoded content length is unknown
        (!content_method && has_content_header && !explicit_zero_length) ||           // nonzero content with GET,etc
        (content_method && !explicit_zero_length) ||                                  // content-length >0 or empty with POST etc
        stream->get_sm()->get_ua_txn()->has_request_body(content_length, is_chunked); // request has a body

      // send END_STREAM if we don't expect any content
      if (!expect_content_stream) {
        // TODO deal with the chunked encoding case
        Http2StreamDebug(session, stream->get_id(), "request END_STREAM");
        flags                   |= HTTP2_FLAGS_HEADERS_END_STREAM;
        stream->send_end_stream  = true;
      }
    } else {
      if ((send_hdr->presence(MIME_PRESENCE_CONTENT_LENGTH) && send_hdr->get_content_length() == 0) ||
          (!send_hdr->expect_final_response() && stream->is_write_vio_done())) {
        Http2StreamDebug(session, stream->get_id(), "response END_STREAM");
        flags                   |= HTTP2_FLAGS_HEADERS_END_STREAM;
        stream->send_end_stream  = true;
      }
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
      fini_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_FINI);
    }

    return;
  }

  Http2StreamDebug(session, stream->get_id(), "Send HEADERS frame flags: 0x%x length: %d", flags, payload_length);
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
  int      payload_length     = 0;
  uint8_t  flags              = 0x00;

  // It makes no sense to send a PUSH_PROMISE toward the server.
  ink_release_assert(!this->session->is_outbound());
  if (peer_settings.get(HTTP2_SETTINGS_ENABLE_PUSH) == 0) {
    return false;
  }

  Http2StreamDebug(session, stream->get_id(), "Send PUSH_PROMISE frame");

  HTTPHdr        hdr;
  ts::PostScript hdr_defer([&]() -> void { hdr.destroy(); });
  hdr.create(HTTPType::REQUEST, HTTP_2_0);
  hdr.url_set(&url);
  hdr.method_set(static_cast<std::string_view>(HTTP_METHOD_GET));

  if (accept_encoding != nullptr) {
    auto       name{accept_encoding->name_get()};
    MIMEField *f = hdr.field_create(name);

    auto value{accept_encoding->value_get()};
    f->value_set(hdr.m_heap, hdr.m_mime, value);

    hdr.field_attach(f);
  }

  http2_convert_header_from_1_1_to_2(&hdr);

  uint32_t        buf_len = hdr.length_get() * 2; // Make it double just in case
  ts::LocalBuffer local_buffer(buf_len);
  uint8_t        *buf = local_buffer.data();

  Http2ErrorCode result = http2_encode_header_blocks(&hdr, buf, buf_len, &header_blocks_size, *(this->peer_hpack_handle),
                                                     peer_settings.get(HTTP2_SETTINGS_HEADER_TABLE_SIZE));
  if (result != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    return false;
  }

  // Send a PUSH_PROMISE frame
  Http2PushPromise push_promise;
  if (header_blocks_size <=
      BUFFER_SIZE_FOR_INDEX(buffer_size_index[HTTP2_FRAME_TYPE_PUSH_PROMISE]) - sizeof(push_promise.promised_streamid)) {
    payload_length  = header_blocks_size;
    flags          |= HTTP2_FLAGS_PUSH_PROMISE_END_HEADERS;
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
  stream->set_receive_headers(hdr);
  stream->new_transaction();
  stream->receive_end_stream = true; // No more data with the request
  stream->send_headers(*this);

  return true;
}

void
Http2ConnectionState::send_rst_stream_frame(Http2StreamId id, Http2ErrorCode ec)
{
  Http2StreamDebug(session, id, "Send RST_STREAM frame: Error Code: %u", static_cast<uint32_t>(ec));

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    Metrics::Counter::increment(http2_rsb.stream_errors_count);
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
        fini_event = this_ethread()->schedule_imm_local(static_cast<Continuation *>(this), HTTP2_SESSION_EVENT_FINI);
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
  constexpr Http2StreamId stream_id = HTTP2_CONNECTION_CONTROL_STREAM;

  Http2StreamDebug(session, stream_id, "Send SETTINGS frame");

  Http2SettingsParameter params[HTTP2_SETTINGS_MAX];
  size_t                 params_size = 0;

  for (int i = HTTP2_SETTINGS_HEADER_TABLE_SIZE; i < HTTP2_SETTINGS_MAX; ++i) {
    Http2SettingsIdentifier id        = static_cast<Http2SettingsIdentifier>(i);
    unsigned const          old_value = local_settings.get(id);
    unsigned const          new_value = new_settings.get(id);

    // Send only difference
    if (new_value != old_value) {
      Http2StreamDebug(session, stream_id, "  %s : %u -> %u", Http2DebugNames::get_settings_param_name(id), old_value, new_value);

      params[params_size++] = {static_cast<uint16_t>(id), new_value};

      // Update current settings
      local_settings.set(id, new_settings.get(id));
    }
  }

  Http2SettingsFrame settings(stream_id, HTTP2_FRAME_NO_FLAG, params, params_size);

  this->_outstanding_settings_frames.emplace(new_settings);
  this->session->xmit(settings, true);
}

void
Http2ConnectionState::_process_incoming_settings_ack_frame()
{
  constexpr Http2StreamId stream_id = HTTP2_CONNECTION_CONTROL_STREAM;
  Http2StreamDebug(session, stream_id, "Processing SETTINGS ACK frame with a queue size of %zu",
                   this->_outstanding_settings_frames.size());

  // Do not update this->acknowledged_local_settings yet as
  // update_initial_local_rwnd relies upon it still pointing to the old value.
  Http2ConnectionSettings const &old_settings = this->acknowledged_local_settings;
  Http2ConnectionSettings const &new_settings = this->_outstanding_settings_frames.front().get_outstanding_settings();

  for (int i = HTTP2_SETTINGS_HEADER_TABLE_SIZE; i < HTTP2_SETTINGS_MAX; ++i) {
    Http2SettingsIdentifier id        = static_cast<Http2SettingsIdentifier>(i);
    unsigned const          old_value = old_settings.get(id);
    unsigned const          new_value = new_settings.get(id);

    if (new_value == old_value) {
      continue;
    }

    Http2StreamDebug(session, stream_id, "SETTINGS ACK %s : %u -> %u", Http2DebugNames::get_settings_param_name(id), old_value,
                     new_value);

    if (id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE) {
      // Update all the streams for the newly acknowledged window size.
      this->update_initial_local_rwnd(new_value);
    }
  }
  this->acknowledged_local_settings = new_settings;
  this->_outstanding_settings_frames.pop();
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

  Http2ConDebug(session, "Send GOAWAY frame: Error Code: %u, Last Stream Id: %d", static_cast<uint32_t>(ec), id);

  if (ec != Http2ErrorCode::HTTP2_ERROR_NO_ERROR) {
    Metrics::Counter::increment(http2_rsb.connection_errors_count);
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

  // By specification, the window update increment must be greater than 0.
  ink_release_assert(size > 0);

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

void
Http2ConnectionState::increment_received_rst_stream_frame_count()
{
  this->_received_rst_stream_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_rst_stream_frame_count()
{
  return this->_received_rst_stream_frame_counter.get_count();
}

void
Http2ConnectionState::increment_received_continuation_frame_count()
{
  this->_received_continuation_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_continuation_frame_count()
{
  return this->_received_continuation_frame_counter.get_count();
}

void
Http2ConnectionState::increment_received_empty_frame_count()
{
  this->_received_empty_frame_counter.increment();
}

uint32_t
Http2ConnectionState::get_received_empty_frame_count()
{
  return this->_received_empty_frame_counter.get_count();
}

// Return min_concurrent_streams_in when current client streams number is larger than max_active_streams_in.
// Main purpose of this is preventing DDoS Attacks.
unsigned
Http2ConnectionState::_adjust_concurrent_stream()
{
  uint32_t const max_concurrent_streams = this->_get_configured_max_concurrent_streams();
  uint32_t const max_active_streams     = this->_get_configured_max_active_streams();
  uint32_t const min_concurrent_streams = this->_get_configured_min_concurrent_streams();

  if (max_active_streams == 0) {
    // Throttling down is disabled.
    return max_concurrent_streams;
  }

  int64_t current_client_streams = Metrics::Gauge::load(http2_rsb.current_client_stream_count);

  Http2ConDebug(session, "current client streams: %" PRId64, current_client_streams);

  if (current_client_streams >= max_active_streams) {
    if (!Http2::throttling) {
      Warning("too many streams: %" PRId64 ", reduce SETTINGS_MAX_CONCURRENT_STREAMS to %d", current_client_streams,
              min_concurrent_streams);
      Http2::throttling = true;
    }

    return min_concurrent_streams;
  } else {
    if (Http2::throttling) {
      Note("revert SETTINGS_MAX_CONCURRENT_STREAMS to %d", max_concurrent_streams);
      Http2::throttling = false;
    }
  }

  return max_concurrent_streams;
}

uint32_t
Http2ConnectionState::_get_configured_receive_session_window_size() const
{
  switch (this->_get_configured_flow_control_policy()) {
  case Http2FlowControlPolicy::STATIC_SESSION_AND_STATIC_STREAM:
    return this->_get_configured_initial_window_size();
  case Http2FlowControlPolicy::LARGE_SESSION_AND_STATIC_STREAM:
  case Http2FlowControlPolicy::LARGE_SESSION_AND_DYNAMIC_STREAM:
    return this->_get_configured_initial_window_size() * this->_get_configured_max_concurrent_streams();
  }

  // This is unreachable, but adding a return here quiets a compiler warning.
  return this->_get_configured_initial_window_size();
}

bool
Http2ConnectionState::_has_dynamic_stream_window() const
{
  switch (this->_get_configured_flow_control_policy()) {
  case Http2FlowControlPolicy::STATIC_SESSION_AND_STATIC_STREAM:
  case Http2FlowControlPolicy::LARGE_SESSION_AND_STATIC_STREAM:
    return false;
  case Http2FlowControlPolicy::LARGE_SESSION_AND_DYNAMIC_STREAM:
    return true;
  }

  // This is unreachable, but adding a return here quiets a compiler warning.
  return false;
}

ssize_t
Http2ConnectionState::get_peer_rwnd() const
{
  return this->_peer_rwnd;
}

Http2ErrorCode
Http2ConnectionState::increment_peer_rwnd(size_t amount)
{
  this->_peer_rwnd += amount;

  this->_recent_rwnd_increment[this->_recent_rwnd_increment_index] = amount;
  ++this->_recent_rwnd_increment_index;
  this->_recent_rwnd_increment_index %= this->_recent_rwnd_increment.size();
  double sum = std::accumulate(this->_recent_rwnd_increment.begin(), this->_recent_rwnd_increment.end(), 0.0);
  double avg = sum / this->_recent_rwnd_increment.size();
  if (avg < Http2::min_avg_window_update) {
    Metrics::Counter::increment(http2_rsb.insufficient_avg_window_update);
    return Http2ErrorCode::HTTP2_ERROR_ENHANCE_YOUR_CALM;
  }
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2ConnectionState::decrement_peer_rwnd(size_t amount)
{
  this->_peer_rwnd -= amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

ssize_t
Http2ConnectionState::get_local_rwnd() const
{
  return this->_local_rwnd;
}

Http2ErrorCode
Http2ConnectionState::increment_local_rwnd(size_t amount)
{
  this->_local_rwnd += amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}

Http2ErrorCode
Http2ConnectionState::decrement_local_rwnd(size_t amount)
{
  this->_local_rwnd -= amount;
  return Http2ErrorCode::HTTP2_ERROR_NO_ERROR;
}
