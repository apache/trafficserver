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

#include "QUICCryptoStream.h"

constexpr uint32_t MAX_CRYPTO_FRAME_OVERHEAD = 16;

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
QUICCryptoStream::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num)
{
  return this->_write_buffer_reader->is_read_avail_more_than(0) || !this->is_retransmited_frame_queue_empty();
}

/**
 * @param connection_credit This is not used. Because CRYPTO frame is not flow-controlled
 */
QUICFrame *
QUICCryptoStream::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                 uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num)
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
