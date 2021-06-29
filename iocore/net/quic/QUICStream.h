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

class QUICStreamAdapter;
class QUICStreamStateListener;

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
  QUICStreamDirection direction() const;
  const QUICConnectionInfoProvider *connection_info() const;
  bool is_bidirectional() const;
  QUICOffset final_offset() const;

  /**
   * Set an adapter to read/write data from/to this stream
   *
   * This is an interface for QUICApplication. An application can set an adapter
   * to access data in the  way the applications wants.
   */
  void set_io_adapter(QUICStreamAdapter *adapter);

  /*
   * QUICApplication need to call one of these functions when it process VC_EVENT_*
   */
  virtual void on_read();
  virtual void on_eos();

  virtual QUICConnectionErrorUPtr recv(const QUICStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStreamDataBlockedFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICRstStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICCryptoFrame &frame);

  QUICOffset reordered_bytes() const;
  virtual QUICOffset largest_offset_received() const;
  virtual QUICOffset largest_offset_sent() const;

  virtual void stop_sending(QUICStreamErrorUPtr error);
  virtual void reset(QUICStreamErrorUPtr error);

  void set_state_listener(QUICStreamStateListener *listener);

  LINK(QUICStream, link);

protected:
  QUICConnectionInfoProvider *_connection_info = nullptr;
  QUICStreamId _id                             = 0;
  QUICOffset _send_offset                      = 0;
  QUICOffset _reordered_bytes                  = 0;

  QUICStreamAdapter *_adapter              = nullptr;
  QUICStreamStateListener *_state_listener = nullptr;

  virtual void _on_adapter_updated(){};

  void _notify_state_change();

  void _records_rst_stream_frame(QUICEncryptionLevel level, const QUICRstStreamFrame &frame);
  void _records_stream_frame(QUICEncryptionLevel level, const QUICStreamFrame &frame);
  void _records_stop_sending_frame(QUICEncryptionLevel level, const QUICStopSendingFrame &frame);
  void _records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame);
};

class QUICStreamStateListener
{
public:
  virtual void on_stream_state_close(const QUICStream *stream) = 0;
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
#define QUICVStreamFCDebug(fmt, ...)                                                                          \
  Debug("v_quic_flow_ctrl", "[%s] [%" PRIu64 "] [%s] " fmt, this->_connection_info->cids().data(), this->_id, \
        QUICDebugNames::stream_state(this->_state.get()), ##__VA_ARGS__)

extern const uint32_t MAX_STREAM_FRAME_OVERHEAD;
