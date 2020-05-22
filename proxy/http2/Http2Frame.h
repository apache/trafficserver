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

#pragma once

#include "P_Net.h"

#include "HTTP2.h"

/**
   Incoming HTTP/2 Frame
 */
class Http2Frame
{
public:
  Http2Frame(const Http2FrameHeader &h, IOBufferReader *r, bool e = false) : _hdr(h), _ioreader(r), _from_early_data(e) {}

  // Accessor
  IOBufferReader *reader() const;
  const Http2FrameHeader &header() const;
  bool is_from_early_data() const;

private:
  Http2FrameHeader _hdr;
  IOBufferReader *_ioreader = nullptr;
  bool _from_early_data     = false;
};

/**
   Outgoing HTTP/2 Frame
 */
class Http2TxFrame
{
public:
  Http2TxFrame(const Http2FrameHeader &h) : _hdr(h) {}
  virtual ~Http2TxFrame() {}

  // Don't allocate on heap
  void *operator new(std::size_t)   = delete;
  void *operator new[](std::size_t) = delete;

  virtual int64_t write_to(MIOBuffer *iobuffer) const = 0;

protected:
  Http2FrameHeader _hdr;
};

/**
   DATA Frame
 */
class Http2DataFrame : public Http2TxFrame
{
public:
  Http2DataFrame(Http2StreamId stream_id, uint8_t flags, IOBufferReader *r, uint32_t l)
    : Http2TxFrame({l, HTTP2_FRAME_TYPE_DATA, flags, stream_id}), _reader(r), _payload_len(l)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  IOBufferReader *_reader = nullptr;
  uint32_t _payload_len   = 0;
};

/**
   HEADERS Frame

   TODO: support priority info & padding using Http2HeadersParameter
 */
class Http2HeadersFrame : public Http2TxFrame
{
public:
  Http2HeadersFrame(Http2StreamId stream_id, uint8_t flags, uint8_t *h, uint32_t l)
    : Http2TxFrame({l, HTTP2_FRAME_TYPE_HEADERS, flags, stream_id}), _hdr_block(h), _hdr_block_len(l)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  uint8_t *_hdr_block     = nullptr;
  uint32_t _hdr_block_len = 0;
};

/**
   PRIORITY Frame

   TODO: implement xmit function
 */
class Http2PriorityFrame : public Http2TxFrame
{
public:
  Http2PriorityFrame(Http2StreamId stream_id, uint8_t flags, Http2Priority p)
    : Http2TxFrame({HTTP2_PRIORITY_LEN, HTTP2_FRAME_TYPE_PRIORITY, flags, stream_id}), _params(p)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  Http2Priority _params;
};

/**
   RST_STREAM Frame
 */
class Http2RstStreamFrame : public Http2TxFrame
{
public:
  Http2RstStreamFrame(Http2StreamId stream_id, uint32_t e)
    : Http2TxFrame({HTTP2_RST_STREAM_LEN, HTTP2_FRAME_TYPE_RST_STREAM, HTTP2_FRAME_NO_FLAG, stream_id}), _error_code(e)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  uint32_t _error_code;
};

/**
   SETTINGS Frame
 */
class Http2SettingsFrame : public Http2TxFrame
{
public:
  Http2SettingsFrame(Http2StreamId stream_id, uint8_t flags) : Http2TxFrame({0, HTTP2_FRAME_TYPE_SETTINGS, flags, stream_id}) {}
  Http2SettingsFrame(Http2StreamId stream_id, uint8_t flags, Http2SettingsParameter *p, uint32_t s)
    : Http2TxFrame({static_cast<uint32_t>(HTTP2_SETTINGS_PARAMETER_LEN) * s, HTTP2_FRAME_TYPE_SETTINGS, flags, stream_id}),
      _params(p),
      _psize(s)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  Http2SettingsParameter *_params = nullptr;
  uint32_t _psize                 = 0;
};

/**
   PUSH_PROMISE Frame

   TODO: support padding
 */
class Http2PushPromiseFrame : public Http2TxFrame
{
public:
  Http2PushPromiseFrame(Http2StreamId stream_id, uint8_t flags, Http2PushPromise p, uint8_t *h, uint32_t l)
    : Http2TxFrame({l + static_cast<uint32_t>(sizeof(Http2StreamId)), HTTP2_FRAME_TYPE_PUSH_PROMISE, flags, stream_id}),
      _params(p),
      _hdr_block(h),
      _hdr_block_len(l)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  Http2PushPromise _params;
  uint8_t *_hdr_block     = nullptr;
  uint32_t _hdr_block_len = 0;
};

/**
   PING Frame
 */
class Http2PingFrame : public Http2TxFrame
{
public:
  Http2PingFrame(Http2StreamId stream_id, uint8_t flags, const uint8_t *data)
    : Http2TxFrame({HTTP2_PING_LEN, HTTP2_FRAME_TYPE_PING, flags, stream_id}), _opaque_data(data)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  const uint8_t *_opaque_data;
};

/**
   GOAWAY Frame

   TODO: support Additional Debug Data
 */
class Http2GoawayFrame : public Http2TxFrame
{
public:
  Http2GoawayFrame(Http2Goaway p)
    : Http2TxFrame({HTTP2_GOAWAY_LEN, HTTP2_FRAME_TYPE_GOAWAY, HTTP2_FRAME_NO_FLAG, HTTP2_CONNECTION_CONTROL_STRTEAM}), _params(p)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  Http2Goaway _params;
};

/**
   WINDOW_UPDATE Frame
 */
class Http2WindowUpdateFrame : public Http2TxFrame
{
public:
  Http2WindowUpdateFrame(Http2StreamId stream_id, uint32_t w)
    : Http2TxFrame({HTTP2_WINDOW_UPDATE_LEN, HTTP2_FRAME_TYPE_WINDOW_UPDATE, HTTP2_FRAME_NO_FLAG, stream_id}), _window(w)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  uint32_t _window = 0;
};

/**
   CONTINUATION Frame
 */
class Http2ContinuationFrame : public Http2TxFrame
{
public:
  Http2ContinuationFrame(Http2StreamId stream_id, uint8_t flags, uint8_t *h, uint32_t l)
    : Http2TxFrame({l, HTTP2_FRAME_TYPE_CONTINUATION, flags, stream_id}), _hdr_block(h), _hdr_block_len(l)
  {
  }

  int64_t write_to(MIOBuffer *iobuffer) const override;

private:
  uint8_t *_hdr_block     = nullptr;
  uint32_t _hdr_block_len = 0;
};
