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

QUICIncomingFrameBuffer::~QUICIncomingFrameBuffer()
{
  this->_out_of_order_queue.clear();

  while (!this->_recv_buffer.empty()) {
    this->_recv_buffer.pop();
  }
}

std::shared_ptr<const QUICStreamFrame>
QUICIncomingFrameBuffer::pop()
{
  if (this->_recv_buffer.empty()) {
    auto frame = this->_out_of_order_queue.find(this->_recv_offset);
    while (frame != this->_out_of_order_queue.end()) {
      this->_recv_buffer.push(frame->second);
      this->_recv_offset += frame->second->data_length();
      this->_out_of_order_queue.erase(frame);
      frame = this->_out_of_order_queue.find(this->_recv_offset);
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
QUICIncomingFrameBuffer::insert(const std::shared_ptr<const QUICStreamFrame> frame)
{
  QUICOffset offset = frame->offset();
  size_t len        = frame->data_length();

  QUICErrorUPtr err = this->_check_and_set_fin_flag(offset, len, frame->has_fin_flag());
  if (err->cls != QUICErrorClass::NONE) {
    return err;
  }

  if (this->_recv_offset > offset) {
    // dup frame;
    return QUICErrorUPtr(new QUICNoError());
  } else if (this->_recv_offset == offset) {
    this->_recv_offset = offset + len;
    this->_recv_buffer.push(this->_clone(frame));
  } else {
    this->_out_of_order_queue.insert(std::make_pair(offset, this->_clone(frame)));
  }

  return QUICErrorUPtr(new QUICNoError());
}

void
QUICIncomingFrameBuffer::clear()
{
  this->_out_of_order_queue.clear();

  while (!this->_recv_buffer.empty()) {
    this->_recv_buffer.pop();
  }

  this->_fin_offset  = UINT64_MAX;
  this->_max_offset  = 0;
  this->_recv_offset = 0;
}

bool
QUICIncomingFrameBuffer::empty()
{
  return this->_out_of_order_queue.empty() && this->_recv_buffer.empty();
}

std::shared_ptr<const QUICStreamFrame>
QUICIncomingFrameBuffer::_clone(std::shared_ptr<const QUICStreamFrame> frame)
{
  return QUICFrameFactory::create_stream_frame(frame->data(), frame->data_length(), frame->stream_id(), frame->offset(),
                                               frame->has_fin_flag());
}

QUICErrorUPtr
QUICIncomingFrameBuffer::_check_and_set_fin_flag(QUICOffset offset, size_t len, bool fin_flag)
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

    if (this->_max_offset >= this->_fin_offset) {
      return QUICErrorUPtr(new QUICStreamError(this->_stream, QUICTransErrorCode::FINAL_OFFSET_ERROR));
    }

  } else if (this->_fin_offset != UINT64_MAX && this->_fin_offset <= offset) {
    return QUICErrorUPtr(new QUICStreamError(this->_stream, QUICTransErrorCode::FINAL_OFFSET_ERROR));
  }
  this->_max_offset = std::max(offset + len, this->_max_offset);

  return QUICErrorUPtr(new QUICNoError());
}
