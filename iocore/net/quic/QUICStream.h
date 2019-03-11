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

#include "P_VConnection.h"
#include "I_Event.h"

#include "QUICFrame.h"
#include "QUICStreamState.h"
#include "QUICFlowController.h"
#include "QUICIncomingFrameBuffer.h"
#include "QUICFrameGenerator.h"
#include "QUICConnection.h"
#include "QUICFrameRetransmitter.h"
#include "QUICDebugNames.h"

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

#define QUICStreamDebug(fmt, ...)                                                                        \
  Debug("quic_stream", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

#define QUICVStreamDebug(fmt, ...)                                                                         \
  Debug("v_quic_stream", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

#define QUICStreamFCDebug(fmt, ...)                                                                         \
  Debug("quic_flow_ctrl", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

extern const uint32_t MAX_STREAM_FRAME_OVERHEAD;
extern const uint32_t MAX_CRYPTO_FRAME_OVERHEAD;
