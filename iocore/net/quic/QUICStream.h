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
#include "QUICFrameRetransmitter.h"

/**
 * @brief QUIC Stream
 * TODO: This is similar to Http2Stream. Need to think some integration.
 */
class QUICStream : public QUICFrameGenerator, public QUICFrameRetransmitter
{
public:
  QUICStream() {}
  QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid);
  virtual ~QUICStream();

  QUICStreamId id() const;
  const QUICConnectionInfoProvider *connection_info() const;
  bool is_bidirectional() const;
  QUICOffset final_offset() const;

  /*
   * QUICApplication need to call one of these functions when it process VC_EVENT_*
   */
  virtual void on_read() = 0;
  virtual void on_eos()  = 0;

  virtual QUICConnectionErrorUPtr recv(const QUICStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStreamDataBlockedFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICRstStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICCryptoFrame &frame);

  QUICOffset reordered_bytes() const;
  virtual QUICOffset largest_offset_received() const = 0;
  virtual QUICOffset largest_offset_sent() const     = 0;

  virtual void stop_sending(QUICStreamErrorUPtr error) = 0;
  virtual void reset(QUICStreamErrorUPtr error)        = 0;

  LINK(QUICStream, link);

protected:
  QUICConnectionInfoProvider *_connection_info = nullptr;
  QUICStreamId _id                             = 0;
  QUICOffset _send_offset                      = 0;
  QUICOffset _reordered_bytes                  = 0;

  // Fragments of received STREAM frame (offset is unmatched)
  // TODO: Consider to replace with ts/RbTree.h or other data structure
  QUICIncomingStreamFrameBuffer _received_stream_frame_buffer;
};

// This is VConnection class for VIO operation.
class QUICStreamVConnection : public VConnection, public QUICStream
{
public:
  QUICStreamVConnection(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : VConnection(nullptr), QUICStream(cinfo, sid)
  {
    mutex = new_ProxyMutex();
  }

  QUICStreamVConnection() : VConnection(nullptr) {}
  virtual ~QUICStreamVConnection();

  LINK(QUICStreamVConnection, link);

protected:
  virtual int64_t _process_read_vio();
  virtual int64_t _process_write_vio();
  void _signal_read_event();
  void _signal_write_event();
  void _signal_read_eos_event();
  Event *_send_tracked_event(Event *, int, VIO *);

  void _write_to_read_vio(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin);

  VIO _read_vio;
  VIO _write_vio;

  Event *_read_event  = nullptr;
  Event *_write_event = nullptr;
};

class QUICBidirectionalStream : public QUICStreamVConnection, public QUICTransferProgressProvider
{
public:
  QUICBidirectionalStream(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *cinfo, QUICStreamId sid,
                          uint64_t recv_max_stream_data, uint64_t send_max_stream_data);
  QUICBidirectionalStream()
    : QUICStreamVConnection(),
      _remote_flow_controller(0, 0),
      _local_flow_controller(nullptr, 0, 0),
      _state(nullptr, nullptr, nullptr, nullptr)
  {
  }

  int state_stream_open(int event, void *data);
  int state_stream_closed(int event, void *data);

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

  virtual QUICConnectionErrorUPtr recv(const QUICStreamFrame &frame) override;
  virtual QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame) override;
  virtual QUICConnectionErrorUPtr recv(const QUICStreamDataBlockedFrame &frame) override;
  virtual QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame) override;
  virtual QUICConnectionErrorUPtr recv(const QUICRstStreamFrame &frame) override;

  // Implement VConnection Interface.
  VIO *do_io_read(Continuation *c, int64_t nbytes = INT64_MAX, MIOBuffer *buf = 0) override;
  VIO *do_io_write(Continuation *c = nullptr, int64_t nbytes = INT64_MAX, IOBufferReader *buf = 0, bool owner = false) override;
  void do_io_close(int lerrno = -1) override;
  void do_io_shutdown(ShutdownHowTo_t howto) override;
  void reenable(VIO *vio) override;

  void stop_sending(QUICStreamErrorUPtr error) override;
  void reset(QUICStreamErrorUPtr error) override;

  // QUICTransferProgressProvider
  bool is_transfer_goal_set() const override;
  uint64_t transfer_progress() const override;
  uint64_t transfer_goal() const override;
  bool is_cancelled() const override;

  /*
   * QUICApplication need to call one of these functions when it process VC_EVENT_*
   */
  virtual void on_read() override;
  virtual void on_eos() override;

  QUICOffset largest_offset_received() const override;
  QUICOffset largest_offset_sent() const override;

private:
  QUICStreamErrorUPtr _reset_reason        = nullptr;
  bool _is_reset_sent                      = false;
  QUICStreamErrorUPtr _stop_sending_reason = nullptr;
  bool _is_stop_sending_sent               = false;

  bool _is_transfer_complete = false;
  bool _is_reset_complete    = false;

  QUICTransferProgressProviderVIO _progress_vio = {this->_read_vio};

  QUICRemoteStreamFlowController _remote_flow_controller;
  QUICLocalStreamFlowController _local_flow_controller;
  uint64_t _flow_control_buffer_size = 1024;

  // FIXME Unidirectional streams should use either ReceiveStreamState or SendStreamState
  QUICBidirectionalStreamStateMachine _state;

  void _records_rst_stream_frame(QUICEncryptionLevel level, const QUICRstStreamFrame &frame);
  void _records_stream_frame(QUICEncryptionLevel level, const QUICStreamFrame &frame);
  void _records_stop_sending_frame(QUICEncryptionLevel level, const QUICStopSendingFrame &frame);

  // QUICFrameGenerator
  void _on_frame_acked(QUICFrameInformationUPtr &info) override;
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;
};

/**
 * @brief QUIC Crypto stream
 * Differences from QUICStream are below
 * - this doesn't have VConnection interface
 * - no stream id
 * - no flow control
 * - no state (never closed)
 */
class QUICCryptoStream : public QUICStream
{
public:
  QUICCryptoStream();
  ~QUICCryptoStream();

  int state_stream_open(int event, void *data);

  const QUICConnectionInfoProvider *info() const;
  QUICOffset final_offset() const;
  void reset_send_offset();
  void reset_recv_offset();

  QUICConnectionErrorUPtr recv(const QUICCryptoFrame &frame) override;

  int64_t read_avail();
  int64_t read(uint8_t *buf, int64_t len);
  int64_t write(const uint8_t *buf, int64_t len);

  void stop_sending(QUICStreamErrorUPtr error) override;
  void reset(QUICStreamErrorUPtr error) override;

  QUICOffset largest_offset_received() const override;
  QUICOffset largest_offset_sent() const override;

  void on_eos() override;
  void on_read() override;

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

private:
  void _on_frame_acked(QUICFrameInformationUPtr &info) override;
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;

  void _records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame);

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
