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

#include "QUICStream.h"

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

  // QUICFrameGenerator
  bool will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp) override;
  QUICFrame *generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                            ink_hrtime timestamp) override;

private:
  void _on_frame_acked(QUICFrameInformationUPtr &info) override;
  void _on_frame_lost(QUICFrameInformationUPtr &info) override;

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
