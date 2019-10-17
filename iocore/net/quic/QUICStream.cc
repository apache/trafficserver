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

//
// QUICStreamVConnection
//
QUICStreamVConnection::~QUICStreamVConnection()
{
  if (this->_read_event) {
    this->_read_event->cancel();
    this->_read_event = nullptr;
  }

  if (this->_write_event) {
    this->_write_event->cancel();
    this->_write_event = nullptr;
  }
}

void
QUICStreamVConnection::_write_to_read_vio(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin)
{
  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  uint64_t bytes_added = this->_read_vio.buffer.writer()->write(data, data_length);

  // Until receive FIN flag, keep nbytes INT64_MAX
  if (fin && bytes_added == data_length) {
    this->_read_vio.nbytes = offset + data_length;
  }
}

/**
 * Replace existing event only if the new event is different than the inprogress event
 */
Event *
QUICStreamVConnection::_send_tracked_event(Event *event, int send_event, VIO *vio)
{
  if (event != nullptr) {
    if (event->callback_event != send_event) {
      event->cancel();
      event = nullptr;
    }
  }

  if (event == nullptr) {
    event = this_ethread()->schedule_imm(this, send_event, vio);
  }

  return event;
}

/**
 * @brief Signal event to this->_read_vio.cont
 */
void
QUICStreamVConnection::_signal_read_event()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int event = this->_read_vio.ntodo() ? VC_EVENT_READ_READY : VC_EVENT_READ_COMPLETE;

  if (lock.is_locked()) {
    this->_read_vio.cont->handleEvent(event, &this->_read_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio.cont, event, &this->_read_vio);
  }
}

/**
 * @brief Signal event to this->_write_vio.cont
 */
void
QUICStreamVConnection::_signal_write_event()
{
  if (this->_write_vio.cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_write_vio.mutex, this_ethread());

  int event = this->_write_vio.ntodo() ? VC_EVENT_WRITE_READY : VC_EVENT_WRITE_COMPLETE;

  if (lock.is_locked()) {
    this->_write_vio.cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_write_vio.cont, event, &this->_write_vio);
  }
}

/**
 * @brief Signal event to this->_write_vio.cont
 */
void
QUICStreamVConnection::_signal_read_eos_event()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int event = VC_EVENT_EOS;

  if (lock.is_locked()) {
    this->_write_vio.cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio.cont, event, &this->_read_vio);
  }
}

int64_t
QUICStreamVConnection::_process_read_vio()
{
  if (this->_read_vio.cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return 0;
  }

  // Pass through. Read operation is done by QUICStream::recv(const std::shared_ptr<const QUICStreamFrame> frame)
  // TODO: 1. pop frame from _received_stream_frame_buffer
  //       2. write data to _read_vio

  return 0;
}

/**
 * @brief Send STREAM DATA from _response_buffer
 * @detail Call _signal_write_event() to indicate event upper layer
 */
int64_t
QUICStreamVConnection::_process_write_vio()
{
  if (this->_write_vio.cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return 0;
  }

  return 0;
}
