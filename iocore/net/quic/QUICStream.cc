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

#include "QUICStream.h"

#include "QUICStreamManager.h"

constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;

QUICStream::QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : _connection_info(cinfo), _id(sid) {}

QUICStream::~QUICStream() {}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

QUICStreamDirection
QUICStream::direction() const
{
  return QUICTypeUtil::detect_stream_direction(this->_id, this->_connection_info->direction());
}

const QUICConnectionInfoProvider *
QUICStream::connection_info() const
{
  return this->_connection_info;
}

bool
QUICStream::is_bidirectional() const
{
  return ((this->_id & 0x03) < 0x02);
}

QUICOffset
QUICStream::final_offset() const
{
  // TODO Return final offset
  return 0;
}

void
QUICStream::set_io_adapter(QUICStreamAdapter *adapter)
{
  this->_adapter = adapter;
  this->_on_adapter_updated();
}

QUICOffset
QUICStream::reordered_bytes() const
{
  return this->_reordered_bytes;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICStreamFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICMaxStreamDataFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICStreamDataBlockedFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICStopSendingFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICRstStreamFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStream::recv(const QUICCryptoFrame &frame)
{
  return nullptr;
}

void
QUICStream::_records_stream_frame(QUICEncryptionLevel level, const QUICStreamFrame &frame)
{
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = frame.type();
  info->level                   = level;
  StreamFrameInfo *frame_info   = reinterpret_cast<StreamFrameInfo *>(info->data);
  frame_info->stream_id         = frame.stream_id();
  frame_info->offset            = frame.offset();
  frame_info->has_fin           = frame.has_fin_flag();
  frame_info->block             = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_records_rst_stream_frame(QUICEncryptionLevel level, const QUICRstStreamFrame &frame)
{
  QUICFrameInformationUPtr info  = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                     = frame.type();
  info->level                    = level;
  RstStreamFrameInfo *frame_info = reinterpret_cast<RstStreamFrameInfo *>(info->data);
  frame_info->error_code         = frame.error_code();
  frame_info->final_offset       = frame.final_offset();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_records_stop_sending_frame(QUICEncryptionLevel level, const QUICStopSendingFrame &frame)
{
  QUICFrameInformationUPtr info    = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                       = frame.type();
  info->level                      = level;
  StopSendingFrameInfo *frame_info = reinterpret_cast<StopSendingFrameInfo *>(info->data);
  frame_info->error_code           = frame.error_code();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::_records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame)
{
  QUICFrameInformationUPtr info      = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                         = QUICFrameType::CRYPTO;
  info->level                        = level;
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->offset          = frame.offset();
  crypto_frame_info->block           = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStream::set_state_listener(QUICStreamStateListener *listener)
{
  this->_state_listener = listener;
}

void
QUICStream::_notify_state_change()
{
  if (this->_state_listener) {
    // TODO Check own state and call an appropriate callback function
  }
}

void
QUICStream::reset(QUICStreamErrorUPtr error)
{
}

void
QUICStream::stop_sending(QUICStreamErrorUPtr error)
{
}

QUICOffset
QUICStream::largest_offset_received() const
{
  return 0;
}

QUICOffset
QUICStream::largest_offset_sent() const
{
  return 0;
}

void
QUICStream::on_eos()
{
}

void
QUICStream::on_read()
{
}
