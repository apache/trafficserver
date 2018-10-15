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

#include "I_VConnection.h"

#include "QUICFrame.h"
#include "QUICStreamState.h"
#include "QUICFlowController.h"
#include "QUICIncomingFrameBuffer.h"
#include "QUICFrameGenerator.h"
#include "QUICConnection.h"

/**
 * @brief QUIC Stream
 * TODO: This is similar to Http2Stream. Need to think some integration.
 */
class QUICStream : public VConnection, public QUICFrameGenerator
{
public:
  QUICStream()
    : VConnection(nullptr),
      _remote_flow_controller(0, 0),
      _local_flow_controller(nullptr, 0, 0),
      _received_stream_frame_buffer(this),
      _state(nullptr, nullptr, nullptr, nullptr)
  {
  }
  QUICStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *cinfo, QUICStreamId sid, uint64_t recv_max_stream_data,
             uint64_t send_max_stream_data);
  ~QUICStream();

  int state_stream_open(int event, void *data);
  int state_stream_closed(int event, void *data);

  QUICStreamId id() const;
  const QUICConnectionInfoProvider *connection_info() const;
  QUICOffset final_offset() const;

  // Implement VConnection Interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  QUICConnectionErrorUPtr recv(const QUICStreamFrame &frame);
  QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame);
  QUICConnectionErrorUPtr recv(const QUICStreamBlockedFrame &frame);
  QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame);

  void reset(QUICStreamErrorUPtr error);

  QUICOffset reordered_bytes() const;
  QUICOffset largest_offset_received() const;
  QUICOffset largest_offset_sent() const;

  LINK(QUICStream, link);

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level) override;
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

protected:
  virtual int64_t _process_read_vio();
  virtual int64_t _process_write_vio();
  void _signal_read_event();
  void _signal_write_event();
  Event *_send_tracked_event(Event *, int, VIO *);

  void _write_to_read_vio(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin);

  QUICStreamErrorUPtr _reset_reason            = nullptr;
  QUICConnectionInfoProvider *_connection_info = nullptr;
  QUICStreamId _id                             = 0;
  QUICOffset _send_offset                      = 0;
  QUICOffset _reordered_bytes                  = 0;

  QUICRemoteStreamFlowController _remote_flow_controller;
  QUICLocalStreamFlowController _local_flow_controller;
  uint64_t _flow_control_buffer_size = 1024;

  VIO _read_vio;
  VIO _write_vio;

  Event *_read_event  = nullptr;
  Event *_write_event = nullptr;

  // Fragments of received STREAM frame (offset is unmatched)
  // TODO: Consider to replace with ts/RbTree.h or other data structure
  QUICIncomingStreamFrameBuffer _received_stream_frame_buffer;

  // FIXME Unidirectional streams should use either ReceiveStreamState or SendStreamState
  QUICBidirectionalStreamState _state;
};

/**
 * @brief QUIC Crypto stream
 * Differences from QUICStream are below
 * - this doesn't have VConnection interface
 * - no stream id
 * - no flow control
 * - no state (never closed)
 */
class QUICCryptoStream : public QUICFrameGenerator
{
public:
  QUICCryptoStream();
  ~QUICCryptoStream();

  int state_stream_open(int event, void *data);

  const QUICConnectionInfoProvider *info() const;
  QUICOffset final_offset() const;
  void reset_send_offset();
  void reset_recv_offset();

  QUICConnectionErrorUPtr recv(const QUICCryptoFrame &frame);
  int64_t read_avail();
  int64_t read(uint8_t *buf, int64_t len);
  int64_t write(const uint8_t *buf, int64_t len);

  void reset(QUICStreamErrorUPtr error);

  QUICOffset largest_offset_received();
  QUICOffset largest_offset_sent();

  LINK(QUICStream, link);

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level) override;
  QUICFrameUPtr generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size) override;

private:
  QUICStreamErrorUPtr _reset_reason = nullptr;
  QUICOffset _send_offset           = 0;

  // Fragments of received STREAM frame (offset is unmatched)
  // TODO: Consider to replace with ts/RbTree.h or other data structure
  QUICIncomingCryptoFrameBuffer _received_stream_frame_buffer;

  MIOBuffer *_read_buffer  = nullptr;
  MIOBuffer *_write_buffer = nullptr;

  IOBufferReader *_read_buffer_reader  = nullptr;
  IOBufferReader *_write_buffer_reader = nullptr;
};
