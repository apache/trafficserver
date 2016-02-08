/** @file

  Http2Stream

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

#include "Http2Stream.h"
#include "Http2ConnectionState.h"
#include "Http2ClientSession.h"

// Currently use only HTTP/1.1 for requesting to origin server
const static char *HTTP2_FETCHING_HTTP_VERSION = "HTTP/1.1";

bool
Http2Stream::init_fetcher(Http2ConnectionState &cstate)
{
  extern ClassAllocator<FetchSM> FetchSMAllocator;

  // Convert header to HTTP/1.1 format
  if (http2_convert_header_from_2_to_1_1(&_req_header) == PARSE_ERROR) {
    return false;
  }

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
  return true;
}

void
Http2Stream::set_body_to_fetcher(const void *data, size_t len)
{
  ink_assert(_fetch_sm != NULL);

  _fetch_sm->ext_write_data(data, len);
}

bool
Http2Stream::change_state(uint8_t type, uint8_t flags)
{
  switch (_state) {
  case HTTP2_STREAM_STATE_IDLE:
    if (type == HTTP2_FRAME_TYPE_HEADERS) {
      if (end_stream && flags & HTTP2_FLAGS_HEADERS_END_HEADERS) {
        _state = HTTP2_STREAM_STATE_HALF_CLOSED_REMOTE;
      } else {
        _state = HTTP2_STREAM_STATE_OPEN;
      }
    } else if (type == HTTP2_FRAME_TYPE_CONTINUATION) {
      if (end_stream && flags & HTTP2_FLAGS_CONTINUATION_END_HEADERS) {
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
