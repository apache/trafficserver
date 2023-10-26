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

#include "iocore/net/quic/QUICStream.h"

class QUICStreamBase : public QUICStream, public QUICFrameGenerator, public QUICFrameRetransmitter
{
public:
  QUICStreamBase() : QUICStream() {}
  QUICStreamBase(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : QUICStream(cinfo, sid) {}
  virtual ~QUICStreamBase() {}

  QUICOffset final_offset() const override;

  virtual void stop_sending(QUICStreamErrorUPtr error) override;
  virtual void reset(QUICStreamErrorUPtr error) override;

  virtual void on_read() override;
  virtual void on_eos() override;

  void on_frame_acked(QUICFrameInformationUPtr &info);
  void on_frame_lost(QUICFrameInformationUPtr &info);

  virtual QUICConnectionErrorUPtr recv(const QUICStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICMaxStreamDataFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStreamDataBlockedFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICStopSendingFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICRstStreamFrame &frame);
  virtual QUICConnectionErrorUPtr recv(const QUICCryptoFrame &frame);

  QUICOffset reordered_bytes() const;
  virtual QUICOffset largest_offset_received() const;
  virtual QUICOffset largest_offset_sent() const;

  void set_state_listener(QUICStreamStateListener *listener);

  LINK(QUICStreamBase, link);

protected:
  QUICOffset _send_offset     = 0;
  QUICOffset _reordered_bytes = 0;

  QUICStreamStateListener *_state_listener = nullptr;

  void _notify_state_change();

  void _records_rst_stream_frame(QUICEncryptionLevel level, const QUICRstStreamFrame &frame);
  void _records_stream_frame(QUICEncryptionLevel level, const QUICStreamFrame &frame);
  void _records_stop_sending_frame(QUICEncryptionLevel level, const QUICStopSendingFrame &frame);
  void _records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame);
};
