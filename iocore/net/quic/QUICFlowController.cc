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

#include "QUICFlowController.h"
#include "QUICFrame.h"

//
// QUICRateAnalyzer
//
void
QUICRateAnalyzer::update(QUICOffset offset)
{
  ink_hrtime now = ink_get_hrtime();
  if (offset > 0 && now > this->_start_time) {
    this->_rate = static_cast<double>(offset) / (now - this->_start_time);
  }
}

uint64_t
QUICRateAnalyzer::expect_recv_bytes(ink_hrtime time) const
{
  return static_cast<uint64_t>(time * this->_rate);
}

//
// QUICFlowController
//
uint64_t
QUICFlowController::credit() const
{
  return this->current_limit() - this->current_offset();
}

QUICOffset
QUICFlowController::current_offset() const
{
  return this->_offset;
}

QUICOffset
QUICFlowController::current_limit() const
{
  return this->_limit;
}

int
QUICFlowController::update(QUICOffset offset)
{
  if (this->_offset <= offset) {
    if (offset > this->_limit) {
      return -1;
    }
    this->_offset = offset;
  }

  return 0;
}

void
QUICFlowController::forward_limit(QUICOffset limit)
{
  // MAX_(STREAM_)DATA might be unordered due to delay
  // Just ignore if the size was smaller than the last one
  if (this->_limit > limit) {
    return;
  }
  this->_limit = limit;
}

void
QUICFlowController::set_limit(QUICOffset limit)
{
  ink_assert(this->_limit == UINT64_MAX || this->_limit == limit);
  this->_limit = limit;
}

// For RemoteFlowController, caller of this function should also check QUICStreamManager::will_generate_frame()
bool
QUICFlowController::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return this->_should_create_frame;
}

/**
 * @param connection_credit This is not used. Because MAX_(STREAM_)DATA frame are not flow-controlled
 */
QUICFrame *
QUICFlowController::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */,
                                   uint16_t maximum_frame_size, size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_should_create_frame) {
    frame = this->_create_frame(buf);
    if (frame) {
      if (frame->size() <= maximum_frame_size) {
        this->_should_create_frame                    = false;
        QUICFrameInformationUPtr info                 = QUICFrameInformationUPtr(quicFrameInformationAllocator.alloc());
        info->type                                    = frame->type();
        info->level                                   = QUICEncryptionLevel::NONE;
        *(reinterpret_cast<QUICOffset *>(info->data)) = this->_limit;
        this->_records_frame(frame->id(), std::move(info));
      } else {
        frame->~QUICFrame();
        frame = nullptr;
      }
    }
  }

  return frame;
}

//
// QUICRemoteFlowController
//
void
QUICRemoteFlowController::forward_limit(QUICOffset new_limit)
{
  QUICFlowController::forward_limit(new_limit);
  this->_blocked             = false;
  this->_should_create_frame = false;
}

int
QUICRemoteFlowController::update(QUICOffset offset)
{
  int ret = QUICFlowController::update(offset);

  // Create BLOCKED(_STREAM) frame
  // The frame will be sent if stream has something to send.
  if (offset >= this->_limit) {
    this->_should_create_frame = true;
    this->_blocked             = true;
  }

  return ret;
}

void
QUICRemoteFlowController::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::DATA_BLOCKED || info->type == QUICFrameType::STREAM_DATA_BLOCKED);
  if (this->_offset == *reinterpret_cast<QUICOffset *>(info->data)) {
    this->_should_create_frame = true;
  }
}

//
// QUICLocalFlowController
//
QUICOffset
QUICLocalFlowController::current_limit() const
{
  return this->_limit;
}

void
QUICLocalFlowController::forward_limit(QUICOffset new_limit)
{
  // Create MAX_(STREAM_)DATA frame. The frame will be sent on next WRITE_READY event on QUICNetVC
  if (this->_need_to_forward_limit()) {
    QUICFlowController::forward_limit(new_limit);
    this->_should_create_frame = true;
  }
}

int
QUICLocalFlowController::update(QUICOffset offset)
{
  if (this->_offset <= offset) {
    this->_analyzer.update(offset);
  }
  return QUICFlowController::update(offset);
}

void
QUICLocalFlowController::set_limit(QUICOffset limit)
{
  QUICFlowController::set_limit(limit);
}

void
QUICLocalFlowController::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::MAX_DATA || info->type == QUICFrameType::MAX_STREAM_DATA);
  if (this->_limit == *reinterpret_cast<QUICOffset *>(info->data)) {
    this->_should_create_frame = true;
  }
}

bool
QUICLocalFlowController::_need_to_forward_limit()
{
  QUICOffset threshold = this->_analyzer.expect_recv_bytes(2 * this->_rtt_provider->smoothed_rtt());
  if (this->_offset + threshold >= this->_limit) {
    return true;
  }

  return false;
}

//
// QUIC[Remote|Local][Connection|Stream]FlowController
//
QUICFrame *
QUICRemoteConnectionFlowController::_create_frame(uint8_t *buf)
{
  return QUICFrameFactory::create_data_blocked_frame(buf, this->_offset, this->_issue_frame_id(), this);
}

QUICFrame *
QUICLocalConnectionFlowController::_create_frame(uint8_t *buf)
{
  return QUICFrameFactory::create_max_data_frame(buf, this->_limit, this->_issue_frame_id(), this);
}

QUICFrame *
QUICRemoteStreamFlowController::_create_frame(uint8_t *buf)
{
  return QUICFrameFactory::create_stream_data_blocked_frame(buf, this->_stream_id, this->_offset, this->_issue_frame_id(), this);
}

QUICFrame *
QUICLocalStreamFlowController::_create_frame(uint8_t *buf)
{
  return QUICFrameFactory::create_max_stream_data_frame(buf, this->_stream_id, this->_limit, this->_issue_frame_id(), this);
}
