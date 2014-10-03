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

#define DebugHttp2Ssn(fmt, ...) \
  DebugSsn("http2_cs",  "[%" PRId64 "] " fmt, this->con_id, __VA_ARGS__)

typedef Http2ErrorCode (*http2_frame_dispatch)(Http2ClientSession&, Http2ConnectionState&, const Http2Frame&);

static const int buffer_size_index[HTTP2_FRAME_TYPE_MAX] =
{
  -1,   // HTTP2_FRAME_TYPE_DATA
  -1,   // HTTP2_FRAME_TYPE_HEADERS
  -1,   // HTTP2_FRAME_TYPE_PRIORITY
  -1,   // HTTP2_FRAME_TYPE_RST_STREAM
  BUFFER_SIZE_INDEX_128,   // HTTP2_FRAME_TYPE_SETTINGS
  -1,   // HTTP2_FRAME_TYPE_PUSH_PROMISE
  -1,   // HTTP2_FRAME_TYPE_PING
  BUFFER_SIZE_INDEX_128,   // HTTP2_FRAME_TYPE_GOAWAY
  -1,   // HTTP2_FRAME_TYPE_WINDOW_UPDATE
  -1,   // HTTP2_FRAME_TYPE_CONTINUATION
  -1,   // HTTP2_FRAME_TYPE_ALTSVC
  -1,   // HTTP2_FRAME_TYPE_BLOCKED
};

static Http2ErrorCode
rcv_settings_frame(Http2ClientSession& cs, Http2ConnectionState& cstate, const Http2Frame& frame)
{
  Http2SettingsParameter  param;
  char      buf[HTTP2_SETTINGS_PARAMETER_LEN];
  unsigned  nbytes = 0;
  char *    end;

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
    end = frame.reader()->memcpy(buf, sizeof(buf), nbytes);
    nbytes += (end - buf);

    if (!http2_parse_settings_parameter(make_iovec(buf, end - buf), param)) {
      return HTTP2_ERROR_PROTOCOL_ERROR;
    }

    if (!http2_settings_parameter_is_valid(param)) {
      return param.id == HTTP2_SETTINGS_INITIAL_WINDOW_SIZE
        ? HTTP2_ERROR_FLOW_CONTROL_ERROR : HTTP2_ERROR_PROTOCOL_ERROR;
    }

    DebugSsn(&cs, "http2_cs",  "[%" PRId64 "] setting param=%d value=%u",
        cs.connection_id(), param.id, param.value);

    cstate.client_settings.set((Http2SettingsIdentifier)param.id, param.value);
  }

  // 6.5 Once all values have been applied, the recipient MUST immediately emit a
  // SETTINGS frame with the ACK flag set.
  Http2Frame ackFrame(HTTP2_FRAME_TYPE_SETTINGS, 0, HTTP2_FLAGS_SETTINGS_ACK);
  cstate.ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &ackFrame);

  return HTTP2_ERROR_NO_ERROR;
}

static const http2_frame_dispatch frame_handlers[HTTP2_FRAME_TYPE_MAX] =
{
  NULL,   // HTTP2_FRAME_TYPE_DATA
  NULL,   // HTTP2_FRAME_TYPE_HEADERS
  NULL,   // HTTP2_FRAME_TYPE_PRIORITY
  NULL,   // HTTP2_FRAME_TYPE_RST_STREAM
  rcv_settings_frame,   // HTTP2_FRAME_TYPE_SETTINGS
  NULL,   // HTTP2_FRAME_TYPE_PUSH_PROMISE
  NULL,   // HTTP2_FRAME_TYPE_PING
  NULL,   // HTTP2_FRAME_TYPE_GOAWAY
  NULL,   // HTTP2_FRAME_TYPE_WINDOW_UPDATE
  NULL,   // HTTP2_FRAME_TYPE_CONTINUATION
  NULL,   // HTTP2_FRAME_TYPE_ALTSVC
  NULL,   // HTTP2_FRAME_TYPE_BLOCKED
};

int
Http2ConnectionState::main_event_handler(int event, void * edata)
{
  if (event == HTTP2_SESSION_EVENT_INIT) {
    ink_assert(this->ua_session == NULL);
    this->ua_session = (Http2ClientSession *)edata;

    // 3.5 HTTP/2 Connection Preface. Upon establishment of a TCP connection and
    // determination that HTTP/2 will be used by both peers, each endpoint MUST
    // send a connection preface as a final confirmation ... The server connection
    // preface consists of a potentially empty SETTINGS frame.
    Http2Frame settings(HTTP2_FRAME_TYPE_SETTINGS, 0, 0);
    this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &settings);

    return 0;
  }

  if (event == HTTP2_SESSION_EVENT_FINI) {
    this->ua_session = NULL;
    SET_HANDLER(&Http2ConnectionState::state_closed);
    return 0;
  }

  if (event == HTTP2_SESSION_EVENT_RECV) {
    Http2Frame * frame = (Http2Frame *)edata;
    Http2ErrorCode error;

    // The session layer should have validated the frame already.
    ink_assert(frame->header().type < countof(frame_handlers));

    if (frame_handlers[frame->header().type]) {
      error = frame_handlers[frame->header().type](*this->ua_session, *this, *frame);
    } else {
      error = HTTP2_ERROR_INTERNAL_ERROR;
    }

    if (error != HTTP2_ERROR_NO_ERROR) {
      Http2Frame frame(HTTP2_FRAME_TYPE_GOAWAY, 0, 0);
      Http2Goaway goaway;

      goaway.last_streamid = 0;
      goaway.error_code = error;

      frame.alloc(buffer_size_index[HTTP2_FRAME_TYPE_GOAWAY]);
      http2_write_goaway(goaway, frame.write());
      frame.finalize(HTTP2_GOAWAY_LEN);

      this->ua_session->handleEvent(HTTP2_SESSION_EVENT_XMIT, &frame);
      eventProcessor.schedule_imm(this->ua_session, ET_NET, VC_EVENT_ERROR);

      // XXX We need to think a bit harder about how to coordinate the client session and the
      // protocol connection. At this point, the protocol is shutting down, but there's no way
      // to tell that to the client session. Perhaps this could be solved by implementing the
      // half-closed state ...
      SET_HANDLER(&Http2ConnectionState::state_closed);
    }

    return 0;
  }

  return 0;
}

int
Http2ConnectionState::state_closed(int /* event */, void * /* edata */)
{
  return 0;
}
