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

#include "QUICIncomingFrameBuffer.h"

//
// QUICIncomingFrameBuffer
//
void
QUICIncomingFrameBuffer::clear()
{
  this->_out_of_order_queue.clear();

  while (!this->_recv_buffer.empty()) {
    this->_recv_buffer.pop();
  }

  this->_recv_offset = 0;
}

bool
QUICIncomingFrameBuffer::empty()
{
  return this->_out_of_order_queue.empty() && this->_recv_buffer.empty();
}

//
// QUICIncomingStreamFrameBuffer
//
QUICIncomingStreamFrameBuffer::~QUICIncomingStreamFrameBuffer()
{
  this->clear();
}

QUICFrameSPtr
QUICIncomingStreamFrameBuffer::pop()
{
  if (this->_recv_buffer.empty()) {
    auto elem = this->_out_of_order_queue.find(this->_recv_offset);
    while (elem != this->_out_of_order_queue.end()) {
      QUICStreamFrameSPtr frame = std::static_pointer_cast<const QUICStreamFrame>(elem->second);

      this->_recv_buffer.push(frame);
      this->_recv_offset += frame->data_length();
      this->_out_of_order_queue.erase(elem);
      elem = this->_out_of_order_queue.find(this->_recv_offset);
    }
  }

  if (!this->_recv_buffer.empty()) {
    auto frame = this->_recv_buffer.front();
    this->_recv_buffer.pop();
    return frame;
  }
  return nullptr;
}

QUICErrorUPtr
QUICIncomingStreamFrameBuffer::insert(const QUICFrame &frame)
{
  const QUICStreamFrame *stream_frame = static_cast<const QUICStreamFrame *>(&frame);

  QUICOffset offset = stream_frame->offset();
  size_t len        = stream_frame->data_length();

  QUICErrorUPtr err = this->_check_and_set_fin_flag(offset, len, stream_frame->has_fin_flag());
  if (err->cls != QUICErrorClass::NONE) {
    return err;
  }

  // Ignore empty stream frame except pure fin stream frame
  if (len == 0 && !stream_frame->has_fin_flag()) {
    return QUICErrorUPtr(new QUICNoError());
  }

  if (this->_recv_offset > offset) {
    // dup frame;
    return QUICErrorUPtr(new QUICNoError());
  } else if (this->_recv_offset == offset) {
    this->_recv_offset   = offset + len;
    QUICFrameSPtr cloned = frame.clone();
    this->_recv_buffer.push(cloned);
  } else {
    QUICFrameSPtr cloned = frame.clone();
    this->_out_of_order_queue.insert(std::make_pair(offset, cloned));
  }

  return QUICErrorUPtr(new QUICNoError());
}

void
QUICIncomingStreamFrameBuffer::clear()
{
  this->_fin_offset = UINT64_MAX;
  this->_max_offset = 0;

  super::clear();
}

QUICErrorUPtr
QUICIncomingStreamFrameBuffer::_check_and_set_fin_flag(QUICOffset offset, size_t len, bool fin_flag)
{
  // stream with fin flag {11.3. Stream Final Offset}
  // Once a final offset for a stream is known, it cannot change.
  // If a RST_STREAM or STREAM frame causes the final offset to change for a stream,
  // an endpoint SHOULD respond with a FINAL_OFFSET_ERROR error (see Section 12).
  // A receiver SHOULD treat receipt of data at or beyond the final offset as a
  // FINAL_OFFSET_ERROR error, even after a stream is closed.

  // {11.3. Stream Final Offset}
  // A receiver SHOULD treat receipt of data at or beyond the final offset as a
  // FINAL_OFFSET_ERROR error, even after a stream is closed.
  if (fin_flag) {
    if (this->_fin_offset != UINT64_MAX) {
      if (this->_fin_offset == offset + len) {
        // dup fin frame
        return QUICErrorUPtr(new QUICNoError());
      }
      return QUICErrorUPtr(new QUICStreamError(this->_stream, QUICTransErrorCode::FINAL_OFFSET_ERROR));
    }

    this->_fin_offset = offset + len;

    if (this->_max_offset > this->_fin_offset) {
      return QUICErrorUPtr(new QUICStreamError(this->_stream, QUICTransErrorCode::FINAL_OFFSET_ERROR));
    }

  } else if (this->_fin_offset != UINT64_MAX && this->_fin_offset <= offset) {
    return QUICErrorUPtr(new QUICStreamError(this->_stream, QUICTransErrorCode::FINAL_OFFSET_ERROR));
  }
  this->_max_offset = std::max(offset + len, this->_max_offset);

  return QUICErrorUPtr(new QUICNoError());
}

bool
QUICIncomingStreamFrameBuffer::is_transfer_goal_set() const
{
  return this->_fin_offset != UINT64_MAX;
}

bool
QUICIncomingStreamFrameBuffer::is_transfer_complete() const
{
  return this->is_transfer_goal_set() && this->transfer_progress() == this->transfer_goal();
}

uint64_t
QUICIncomingStreamFrameBuffer::transfer_progress() const
{
  return this->_max_offset;
}

uint64_t
QUICIncomingStreamFrameBuffer::transfer_goal() const
{
  return this->_fin_offset;
}

//
// QUICIncomingCryptoFrameBuffer
//
QUICIncomingCryptoFrameBuffer::~QUICIncomingCryptoFrameBuffer()
{
  super::clear();
}

QUICFrameSPtr
QUICIncomingCryptoFrameBuffer::pop()
{
  if (this->_recv_buffer.empty()) {
    auto elem = this->_out_of_order_queue.find(this->_recv_offset);
    while (elem != this->_out_of_order_queue.end()) {
      QUICCryptoFrameSPtr frame = std::static_pointer_cast<const QUICCryptoFrame>(elem->second);

      this->_recv_buffer.push(frame);
      this->_recv_offset += frame->data_length();
      this->_out_of_order_queue.erase(elem);
      elem = this->_out_of_order_queue.find(this->_recv_offset);
    }
  }

  if (!this->_recv_buffer.empty()) {
    auto frame = this->_recv_buffer.front();
    this->_recv_buffer.pop();
    return frame;
  }

  return nullptr;
}

QUICErrorUPtr
QUICIncomingCryptoFrameBuffer::insert(const QUICFrame &frame)
{
  const QUICCryptoFrame *crypto_frame = static_cast<const QUICCryptoFrame *>(&frame);

  QUICOffset offset = crypto_frame->offset();
  size_t len        = crypto_frame->data_length();

  // Ignore empty stream frame
  if (len == 0) {
    return QUICErrorUPtr(new QUICNoError());
  }

  if (this->_recv_offset > offset) {
    // dup frame;
    return QUICErrorUPtr(new QUICNoError());
  } else if (this->_recv_offset == offset) {
    this->_recv_offset   = offset + len;
    QUICFrameSPtr cloned = frame.clone();
    this->_recv_buffer.push(cloned);
  } else {
    QUICFrameSPtr cloned = frame.clone();
    this->_out_of_order_queue.insert(std::make_pair(offset, cloned));
  }

  return QUICErrorUPtr(new QUICNoError());
}
