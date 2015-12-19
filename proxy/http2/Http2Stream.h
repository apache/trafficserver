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

#ifndef __HTTP2_STREAM_H__
#define __HTTP2_STREAM_H__

#include "HTTP2.h"
#include "FetchSM.h"

class Http2ConnectionState;

class Http2Stream
{
public:
  Http2Stream(Http2StreamId sid = 0, ssize_t initial_rwnd = Http2::initial_window_size)
    : client_rwnd(initial_rwnd), server_rwnd(Http2::initial_window_size), header_blocks(NULL), header_blocks_length(0),
      request_header_length(0), end_stream(false), _id(sid), _state(HTTP2_STREAM_STATE_IDLE), _fetch_sm(NULL),
      trailing_header(false), body_done(false), data_length(0)
  {
    _thread = this_ethread();
    HTTP2_INCREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT, _thread);
    _start_time = Thread::get_hrtime();
    // FIXME: Are you sure? every "stream" needs _req_header?
    _req_header.create(HTTP_TYPE_REQUEST);
  }

  ~Http2Stream()
  {
    HTTP2_DECREMENT_THREAD_DYN_STAT(HTTP2_STAT_CURRENT_CLIENT_STREAM_COUNT, _thread);
    ink_hrtime end_time = Thread::get_hrtime();
    HTTP2_SUM_THREAD_DYN_STAT(HTTP2_STAT_TOTAL_TRANSACTIONS_TIME, _thread, end_time - _start_time);
    _req_header.destroy();

    if (_fetch_sm) {
      _fetch_sm->ext_destroy();
      _fetch_sm = NULL;
    }
    if (header_blocks) {
      ats_free(header_blocks);
    }
  }

  // Operate FetchSM
  bool init_fetcher(Http2ConnectionState &cstate);
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
  bool
  has_trailing_header() const
  {
    return trailing_header;
  }

  int64_t
  decode_header_blocks(Http2IndexingTable &indexing_table)
  {
    return http2_decode_header_blocks(&_req_header, (const uint8_t *)header_blocks,
                                      (const uint8_t *)header_blocks + header_blocks_length, indexing_table, trailing_header);
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

  uint8_t *header_blocks;
  uint32_t header_blocks_length;  // total length of header blocks (not include
                                  // Padding or other fields)
  uint32_t request_header_length; // total length of payload (include Padding
                                  // and other fields)
  bool end_stream;

private:
  ink_hrtime _start_time;
  EThread *_thread;
  Http2StreamId _id;
  Http2StreamState _state;

  HTTPHdr _req_header;
  FetchSM *_fetch_sm;
  bool trailing_header;
  bool body_done;
  uint64_t data_length;
};

#endif // __HTTP2_STREAM_H__
