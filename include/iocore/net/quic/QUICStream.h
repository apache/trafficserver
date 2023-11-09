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

#include "../../../../src/iocore/eventsystem/P_VConnection.h"
#include "iocore/eventsystem/Event.h"

#include "iocore/net/quic/QUICConnection.h"
#include "iocore/net/quic/QUICDebugNames.h"

#include <quiche.h>

class QUICStreamAdapter;
class QUICStreamStateListener;

/**
 * @brief QUIC Stream
 * TODO: This is similar to Http2Stream. Need to think some integration.
 */
class QUICStream
{
public:
  QUICStream() {}
  QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid);
  ~QUICStream();

  QUICStreamId id() const;
  const QUICConnectionInfoProvider *connection_info();
  QUICStreamDirection direction() const;
  bool is_bidirectional() const;

  QUICOffset final_offset() const;

  void stop_sending(QUICStreamErrorUPtr error);
  void reset(QUICStreamErrorUPtr error);

  void receive_data(quiche_conn *quiche_con);
  void send_data(quiche_conn *quiche_con);

  /*
   * QUICApplication need to call one of these functions when it process VC_EVENT_*
   */
  void on_read();
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
  QUICConnectionInfoProvider *_connection_info = nullptr;
  QUICStreamId _id                             = 0;
  QUICStreamAdapter *_adapter                  = nullptr;
  uint64_t _received_bytes                     = 0;
  uint64_t _sent_bytes                         = 0;
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
