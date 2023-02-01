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
#include "QUICStream_native.h"

QUICOffset
QUICStreamBase::final_offset() const
{
  // TODO Return final offset
  return 0;
}

QUICOffset
QUICStreamBase::reordered_bytes() const
{
  return this->_reordered_bytes;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICStreamFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICMaxStreamDataFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICStreamDataBlockedFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICStopSendingFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICRstStreamFrame &frame)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamBase::recv(const QUICCryptoFrame &frame)
{
  return nullptr;
}

void
QUICStreamBase::_records_stream_frame(QUICEncryptionLevel level, const QUICStreamFrame &frame)
{
  QUICFrameInformationUPtr info = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                    = frame.type();
  info->level                   = level;
  info->stream_id               = this->id();
  StreamFrameInfo *frame_info   = reinterpret_cast<StreamFrameInfo *>(info->data);
  frame_info->stream_id         = frame.stream_id();
  frame_info->offset            = frame.offset();
  frame_info->has_fin           = frame.has_fin_flag();
  frame_info->block             = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStreamBase::_records_rst_stream_frame(QUICEncryptionLevel level, const QUICRstStreamFrame &frame)
{
  QUICFrameInformationUPtr info  = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                     = frame.type();
  info->level                    = level;
  info->stream_id                = this->id();
  RstStreamFrameInfo *frame_info = reinterpret_cast<RstStreamFrameInfo *>(info->data);
  frame_info->error_code         = frame.error_code();
  frame_info->final_offset       = frame.final_offset();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStreamBase::_records_stop_sending_frame(QUICEncryptionLevel level, const QUICStopSendingFrame &frame)
{
  QUICFrameInformationUPtr info    = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                       = frame.type();
  info->level                      = level;
  info->stream_id                  = this->id();
  StopSendingFrameInfo *frame_info = reinterpret_cast<StopSendingFrameInfo *>(info->data);
  frame_info->error_code           = frame.error_code();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStreamBase::_records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame)
{
  QUICFrameInformationUPtr info      = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                         = QUICFrameType::CRYPTO;
  info->level                        = level;
  info->stream_id                    = this->id();
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->offset          = frame.offset();
  crypto_frame_info->block           = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

void
QUICStreamBase::set_state_listener(QUICStreamStateListener *listener)
{
  this->_state_listener = listener;
}

void
QUICStreamBase::_notify_state_change()
{
  if (this->_state_listener) {
    // TODO Check own state and call an appropriate callback function
  }
}

void
QUICStreamBase::reset(QUICStreamErrorUPtr error)
{
}

void
QUICStreamBase::stop_sending(QUICStreamErrorUPtr error)
{
}

QUICOffset
QUICStreamBase::largest_offset_received() const
{
  return 0;
}

QUICOffset
QUICStreamBase::largest_offset_sent() const
{
  return 0;
}

void
QUICStreamBase::on_eos()
{
}

void
QUICStreamBase::on_read()
{
}

void
QUICStreamBase::on_frame_acked(QUICFrameInformationUPtr &info)
{
  this->_on_frame_acked(info);
}

void
QUICStreamBase::on_frame_lost(QUICFrameInformationUPtr &info)
{
  this->_on_frame_lost(info);
}
