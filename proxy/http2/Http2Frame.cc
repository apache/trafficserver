/** @file

  Http2Frame

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

#include "Http2Frame.h"

//
// Http2Frame
//
IOBufferReader *
Http2Frame::reader() const
{
  return this->_ioreader;
}

const Http2FrameHeader &
Http2Frame::header() const
{
  return this->_hdr;
}

bool
Http2Frame::is_from_early_data() const
{
  return this->_from_early_data;
}

//
// DATA Frame
//
int64_t
Http2DataFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  if (this->_reader && this->_payload_len > 0) {
    int64_t written = 0;
    // Fill current IOBufferBlock as much as possible to reduce SSL_write() calls
    while (written < this->_payload_len) {
      int64_t read_len = std::min(this->_payload_len - written, this->_reader->block_read_avail());
      written += iobuffer->write(this->_reader->start(), read_len);
      this->_reader->consume(read_len);
    }
    len += written;
  }

  return len;
}

//
// HEADERS Frame
//
int64_t
Http2HeadersFrame::write_to(MIOBuffer *iobuffer) const
{
  // Validation
  if (this->_hdr_block_len > Http2::max_frame_size) {
    return -1;
  }

  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  if (this->_hdr_block && this->_hdr_block_len > 0) {
    len += iobuffer->write(this->_hdr_block, this->_hdr_block_len);
  }

  return len;
}

//
// PRIORITY Frame
//
int64_t
Http2PriorityFrame::write_to(MIOBuffer *iobuffer) const
{
  ink_abort("not supported yet");

  return 0;
}

//
// RST_STREM Frame
//
int64_t
Http2RstStreamFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  uint8_t payload[HTTP2_RST_STREAM_LEN];
  http2_write_rst_stream(this->_error_code, make_iovec(payload));
  len += iobuffer->write(payload, sizeof(payload));

  return len;
}

//
// SETTINGS Frame
//
int64_t
Http2SettingsFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  for (uint32_t i = 0; i < this->_psize; ++i) {
    Http2SettingsParameter *p = this->_params + i;

    uint8_t p_buf[HTTP2_SETTINGS_PARAMETER_LEN];
    http2_write_settings(*p, make_iovec(p_buf));
    len += iobuffer->write(p_buf, sizeof(p_buf));
  }

  return len;
}

//
// PUSH_PROMISE Frame
//
int64_t
Http2PushPromiseFrame::write_to(MIOBuffer *iobuffer) const
{
  // Validation
  if (this->_hdr_block_len > Http2::max_frame_size) {
    return -1;
  }

  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  uint8_t p_buf[HTTP2_MAX_FRAME_SIZE];
  http2_write_push_promise(this->_params, this->_hdr_block, this->_hdr_block_len, make_iovec(p_buf));
  len += iobuffer->write(p_buf, sizeof(Http2StreamId) + this->_hdr_block_len);

  return len;
}

//
// PING Frame
//
int64_t
Http2PingFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  uint8_t payload[HTTP2_PING_LEN] = {0};
  http2_write_ping(this->_opaque_data, make_iovec(payload));
  len += iobuffer->write(payload, sizeof(payload));

  return len;
}

//
// GOAWAY Frame
//
int64_t
Http2GoawayFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  uint8_t payload[HTTP2_GOAWAY_LEN];
  http2_write_goaway(this->_params, make_iovec(payload));
  len += iobuffer->write(payload, sizeof(payload));

  return len;
}

//
// WINDOW_UPDATE Frame
//
int64_t
Http2WindowUpdateFrame::write_to(MIOBuffer *iobuffer) const
{
  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  uint8_t payload[HTTP2_WINDOW_UPDATE_LEN];
  http2_write_window_update(this->_window, make_iovec(payload));
  len += iobuffer->write(payload, sizeof(payload));

  return len;
}

//
// CONTINUATION Frame
//
int64_t
Http2ContinuationFrame::write_to(MIOBuffer *iobuffer) const
{
  // Validation
  if (this->_hdr_block_len > Http2::max_frame_size) {
    return -1;
  }

  // Write frame header
  uint8_t buf[HTTP2_FRAME_HEADER_LEN];
  http2_write_frame_header(this->_hdr, make_iovec(buf));
  int64_t len = iobuffer->write(buf, sizeof(buf));

  // Write frame payload
  if (this->_hdr_block && this->_hdr_block_len > 0) {
    len += iobuffer->write(this->_hdr_block, this->_hdr_block_len);
  }

  return len;
}
