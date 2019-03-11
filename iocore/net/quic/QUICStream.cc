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
constexpr uint32_t MAX_CRYPTO_FRAME_OVERHEAD = 16;

QUICStream::QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid)
  : _connection_info(cinfo), _id(sid), _received_stream_frame_buffer()
{
}

QUICStream::~QUICStream() {}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

const QUICConnectionInfoProvider *
QUICStream::connection_info() const
{
  return this->_connection_info;
}

bool
QUICStream::is_bidirectional() const
{
  return (this->_id & 0x03) < 0x02;
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

//
// QUICCryptoStream
//
QUICCryptoStream::QUICCryptoStream() : _received_stream_frame_buffer()
{
  this->_read_buffer  = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);
  this->_write_buffer = new_MIOBuffer(BUFFER_SIZE_INDEX_8K);

  this->_read_buffer_reader  = this->_read_buffer->alloc_reader();
  this->_write_buffer_reader = this->_write_buffer->alloc_reader();
}

QUICCryptoStream::~QUICCryptoStream()
{
  // All readers will be deallocated
  free_MIOBuffer(this->_read_buffer);
  free_MIOBuffer(this->_write_buffer);
}

/**
 * Reset send/recv offset of stream
 */
void
QUICCryptoStream::reset_send_offset()
{
  this->_send_offset = 0;
}

void
QUICCryptoStream::reset_recv_offset()
{
  this->_received_stream_frame_buffer.clear();
}

QUICConnectionErrorUPtr
QUICCryptoStream::recv(const QUICCryptoFrame &frame)
{
  // Make a copy and insert it into the receive buffer because the frame passed is temporal
  QUICFrame *cloned             = new QUICCryptoFrame(frame);
  QUICConnectionErrorUPtr error = this->_received_stream_frame_buffer.insert(cloned);
  if (error != nullptr) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame = this->_received_stream_frame_buffer.pop();
  while (new_frame != nullptr) {
    const QUICCryptoFrame *crypto_frame = static_cast<const QUICCryptoFrame *>(new_frame);

    this->_read_buffer->write(reinterpret_cast<uint8_t *>(crypto_frame->data()->start()), crypto_frame->data_length());

    delete new_frame;
    new_frame = this->_received_stream_frame_buffer.pop();
  }

  return nullptr;
}

int64_t
QUICCryptoStream::read_avail()
{
  return this->_read_buffer_reader->read_avail();
}

int64_t
QUICCryptoStream::read(uint8_t *buf, int64_t len)
{
  return this->_read_buffer_reader->read(buf, len);
}

int64_t
QUICCryptoStream::write(const uint8_t *buf, int64_t len)
{
  return this->_write_buffer->write(buf, len);
}

bool
QUICCryptoStream::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  return this->_write_buffer_reader->is_read_avail_more_than(0) || !this->is_retransmited_frame_queue_empty();
}

/**
 * @param connection_credit This is not used. Because CRYPTO frame is not flow-controlled
 */
QUICFrame *
QUICCryptoStream::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                 uint16_t maximum_frame_size, ink_hrtime timestamp)
{
  QUICConnectionErrorUPtr error = nullptr;

  if (this->_reset_reason) {
    return QUICFrameFactory::create_rst_stream_frame(buf, *this->_reset_reason);
  }

  QUICFrame *frame = this->create_retransmitted_frame(buf, level, maximum_frame_size, this->_issue_frame_id(), this);
  if (frame != nullptr) {
    ink_assert(frame->type() == QUICFrameType::CRYPTO);
    this->_records_crypto_frame(level, *static_cast<QUICCryptoFrame *>(frame));
    return frame;
  }

  if (maximum_frame_size <= MAX_CRYPTO_FRAME_OVERHEAD) {
    return frame;
  }

  uint64_t frame_payload_size = maximum_frame_size - MAX_CRYPTO_FRAME_OVERHEAD;
  uint64_t bytes_avail        = this->_write_buffer_reader->read_avail();
  frame_payload_size          = std::min(bytes_avail, frame_payload_size);
  if (frame_payload_size == 0) {
    return frame;
  }

  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(this->_write_buffer_reader->get_current_block()->clone());
  block->consume(this->_write_buffer_reader->start_offset);
  block->_end = std::min(block->start() + frame_payload_size, block->_buf_end);
  ink_assert(static_cast<uint64_t>(block->read_avail()) == frame_payload_size);

  frame = QUICFrameFactory::create_crypto_frame(buf, block, this->_send_offset, this->_issue_frame_id(), this);
  this->_send_offset += frame_payload_size;
  this->_write_buffer_reader->consume(frame_payload_size);
  this->_records_crypto_frame(level, *static_cast<QUICCryptoFrame *>(frame));

  return frame;
}

void
QUICCryptoStream::_on_frame_acked(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::CRYPTO);
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->block           = nullptr;
}

void
QUICCryptoStream::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::CRYPTO);
  this->save_frame_info(std::move(info));
}

void
QUICCryptoStream::_records_crypto_frame(QUICEncryptionLevel level, const QUICCryptoFrame &frame)
{
  QUICFrameInformationUPtr info      = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
  info->type                         = QUICFrameType::CRYPTO;
  info->level                        = level;
  CryptoFrameInfo *crypto_frame_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  crypto_frame_info->offset          = frame.offset();
  crypto_frame_info->block           = frame.data();
  this->_records_frame(frame.id(), std::move(info));
}

QUICOffset
QUICCryptoStream::largest_offset_received() const
{
  // TODO:
  ink_assert(!"unsupported");
  return 0;
}

QUICOffset
QUICCryptoStream::largest_offset_sent() const
{
  // TODO
  ink_assert(!"unsupported");
  return 0;
}

void
QUICCryptoStream::stop_sending(QUICStreamErrorUPtr error)
{
  // TODO
  ink_assert(!"unsupported");
  return;
}

void
QUICCryptoStream::reset(QUICStreamErrorUPtr error)
{
  // TODO
  ink_assert(!"unsupported");
  return;
}

void
QUICCryptoStream::on_eos()
{
  return;
}

void
QUICCryptoStream::on_read()
{
  return;
}
