/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include "tscore/List.h"

#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/IOBuffer.h"

#include "iocore/net/quic/QUICConnection.h"
#include "iocore/net/quic/QUICDebugNames.h"

#include <cstddef>
#include <cstdint>

class QUICStreamAdapter;
class QUICStreamStateListener;

class QUICStreamIO
{
public:
  using ErrorCode = uint64_t;

  virtual ~QUICStreamIO() = default;

  virtual int64_t read_stream(QUICStreamId stream_id, uint8_t *buf, size_t len, bool &fin, ErrorCode &error_code)       = 0;
  virtual bool    stream_read_finished(QUICStreamId stream_id)                                                          = 0;
  virtual int64_t stream_write_capacity(QUICStreamId stream_id)                                                         = 0;
  virtual int64_t write_stream(QUICStreamId stream_id, uint8_t const *buf, size_t len, bool fin, ErrorCode &error_code) = 0;
};

/**
 * @brief QUIC Stream
 * TODO: This is similar to Http2Stream. Need to think some integration.
 */
class QUICStream
{
public:
  using ErrorCode = uint64_t; //!<  recv/send stream application error codes.

  // Guaranteed per-stream send budget for one write event when many streams are
  // contending for the connection's write path this round.
  static constexpr size_t MIN_STREAM_SEND_BYTES_PER_EVENT = 16 * 1024;
  // Ceiling on how much a single stream can send in one write event; only reached
  // when few streams are contending, per compute_fair_send_budget().
  static constexpr size_t MAX_STREAM_SEND_BYTES_PER_EVENT = 256 * 1024;

  QUICStream() {}
  QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid);
  virtual ~QUICStream();

  QUICStreamId                      id() const;
  const QUICConnectionInfoProvider *connection_info();
  QUICStreamDirection               direction() const;
  bool                              is_bidirectional() const;
  bool                              has_no_more_data() const;
  bool                              has_data_to_send();

  QUICOffset final_offset() const;

  void stop_sending(QUICStreamErrorUPtr error);
  void reset(QUICStreamErrorUPtr error);

  void    receive_data(QUICStreamIO &stream_io);
  int64_t send_data(QUICStreamIO &stream_io, size_t max_bytes_this_event);

  // Computes the per-stream send budget for one write event given how many streams
  // were writable in the previous event on this connection. Scales down toward
  // MIN_STREAM_SEND_BYTES_PER_EVENT under contention, up toward
  // MAX_STREAM_SEND_BYTES_PER_EVENT when a stream has the write path to itself.
  static size_t compute_fair_send_budget(size_t num_writable_streams);

  /*
   * QUICApplication need to call one of these functions when it process VC_EVENT_*
   */
  void on_read();
  void on_write();
  void on_eos();

  /**
   * Set an adapter to read/write data from/to this stream
   *
   * This is an interface for QUICApplication. An application can set an adapter
   * to access data in the  way the applications wants.
   */
  void set_io_adapter(QUICStreamAdapter *adapter);

  LINK(QUICStream, link);

protected:
  QUICConnectionInfoProvider *_connection_info  = nullptr;
  QUICStreamId                _id               = 0;
  QUICStreamAdapter          *_adapter          = nullptr;
  uint64_t                    _received_bytes   = 0;
  uint64_t                    _sent_bytes       = 0;
  bool                        _has_no_more_data = false;
  Ptr<IOBufferBlock>          _pending_send_block;
  bool                        _pending_send_fin = false;
  bool                        _sent_fin         = false;
};

class QUICStreamStateListener
{
public:
  virtual void on_stream_state_close(const QUICStream *stream) = 0;
  virtual ~QUICStreamStateListener()                           = default; // Some compilers may warn about this.
};

namespace QUICStreamDbgCtl
{
inline DbgCtl &
quic_stream()
{
  static DbgCtl dc{"quic_stream"};
  return dc;
}
inline DbgCtl &
v_quic_stream()
{
  static DbgCtl dc{"v_quic_stream"};
  return dc;
}
inline DbgCtl &
quic_flow_ctrl()
{
  static DbgCtl dc{"quic_flow_ctrl"};
  return dc;
}
inline DbgCtl &
v_quic_flow_ctrl()
{
  static DbgCtl dc{"v_quic_flow_ctrl"};
  return dc;
}

} // namespace QUICStreamDbgCtl

#define QUICStreamDebug(fmt, ...)                                                                                        \
  Dbg(QUICStreamDbgCtl::quic_stream(), "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
      QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

#define QUICVStreamDebug(fmt, ...)                                                                                         \
  Dbg(QUICStreamDbgCtl::v_quic_stream(), "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
      QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

#define QUICStreamFCDebug(fmt, ...)                                                                                         \
  Dbg(QUICStreamDbgCtl::quic_flow_ctrl(), "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
      QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)
#define QUICVStreamFCDebug(fmt, ...)                                                                                          \
  Dbg(QUICStreamDbgCtl::v_quic_flow_ctrl(), "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
      QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

extern const uint32_t MAX_STREAM_FRAME_OVERHEAD;
