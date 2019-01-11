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

#include "tscore/Diags.h"

#include "QUICFrameRetransmitter.h"
#include "QUICFrameGenerator.h"
#include "QUICDebugNames.h"

ClassAllocator<QUICFrameInformation> quicFrameInformationAllocator("quicFrameInformationAllocator");

QUICFrameUPtr
QUICFrameRetransmitter::create_retransmitted_frame(QUICEncryptionLevel level, uint16_t maximum_frame_size, QUICFrameId id,
                                                   QUICFrameGenerator *owner)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  if (this->_lost_frame_info_queue.empty()) {
    return frame;
  }

  std::deque<QUICFrameInformationUPtr> tmp_queue;
  for (auto it = this->_lost_frame_info_queue.begin(); it != this->_lost_frame_info_queue.end();
       it      = this->_lost_frame_info_queue.begin()) {
    QUICFrameInformationUPtr &info = *it;

    if (info->level != QUICEncryptionLevel::NONE && info->level != level) {
      // skip unmapped info.
      tmp_queue.push_back(std::move(info));
      this->_lost_frame_info_queue.pop_front();
      continue;
    }

    switch (info->type) {
    case QUICFrameType::STREAM:
      frame = this->_create_stream_frame(info, maximum_frame_size, tmp_queue, id, owner);
      break;
    default:
      ink_assert("unknown frame type");
      Error("unknown frame type: %s", QUICDebugNames::frame_type(info->type));
    }

    this->_lost_frame_info_queue.pop_front();
    if (frame != nullptr) {
      break;
    }
  }

  this->_append_info_queue(tmp_queue);
  return frame;
}

void
QUICFrameRetransmitter::save_frame_info(QUICFrameInformationUPtr &info)
{
  for (auto type : RETRANSMITTED_FRAME_TYPE) {
    if (type == info->type) {
      this->_lost_frame_info_queue.push_back(std::move(info));
      break;
    }
  }
}

void
QUICFrameRetransmitter::_append_info_queue(std::deque<QUICFrameInformationUPtr> &tmp_queue)
{
  auto it = tmp_queue.begin();
  while (it != tmp_queue.end()) {
    this->_lost_frame_info_queue.push_back(std::move(*it));
    tmp_queue.pop_front();
    it = tmp_queue.begin();
  }
}

QUICFrameUPtr
QUICFrameRetransmitter::_create_stream_frame(QUICFrameInformationUPtr &info, uint16_t maximum_frame_size,
                                             std::deque<QUICFrameInformationUPtr> &tmp_queue, QUICFrameId id,
                                             QUICFrameGenerator *owner)
{
  StreamFrameInfo *stream_info = reinterpret_cast<StreamFrameInfo *>(info->data);
  // FIXME: has_offset and has_length should be configurable.
  auto frame = QUICFrameFactory::create_stream_frame(stream_info->block, stream_info->stream_id, stream_info->offset,
                                                     stream_info->has_fin, true, true, id, owner);
  if (frame->size() > maximum_frame_size) {
    auto new_frame = QUICFrameFactory::split_frame(frame.get(), maximum_frame_size);
    if (new_frame == nullptr) {
      // can not split stream frame. Because of too small maximum_frame_size.
      tmp_queue.push_back(std::move(info));
      return QUICFrameFactory::create_null_frame();
    } else {
      QUICStreamFrame *stream_frame = static_cast<QUICStreamFrame *>(frame.get());
      stream_info->block->consume(stream_frame->data_length());
      stream_info->offset += stream_frame->data_length();
      ink_assert(frame->size() <= maximum_frame_size);
      tmp_queue.push_back(std::move(info));
      return frame;
    }
  }

  stream_info->block = nullptr;
  ink_assert(frame != nullptr);
  return frame;
}
