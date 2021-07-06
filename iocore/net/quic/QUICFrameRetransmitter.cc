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

QUICFrame *
QUICFrameRetransmitter::create_retransmitted_frame(uint8_t *buf, QUICEncryptionLevel level, uint16_t maximum_frame_size,
                                                   QUICFrameId id, QUICFrameGenerator *owner)
{
  QUICFrame *frame = nullptr;
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
      frame = this->_create_stream_frame(buf, info, maximum_frame_size, tmp_queue, id, owner);
      break;
    case QUICFrameType::CRYPTO:
      frame = this->_create_crypto_frame(buf, info, maximum_frame_size, tmp_queue, id, owner);
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
QUICFrameRetransmitter::save_frame_info(QUICFrameInformationUPtr info)
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

QUICFrame *
QUICFrameRetransmitter::_create_stream_frame(uint8_t *buf, QUICFrameInformationUPtr &info, uint16_t maximum_frame_size,
                                             std::deque<QUICFrameInformationUPtr> &tmp_queue, QUICFrameId id,
                                             QUICFrameGenerator *owner)
{
  QUICFrame *frame             = nullptr;
  StreamFrameInfo *stream_info = reinterpret_cast<StreamFrameInfo *>(info->data);

  static constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;
  if (maximum_frame_size <= MAX_STREAM_FRAME_OVERHEAD) {
    tmp_queue.push_back(std::move(info));
    return frame;
  }

  // FIXME MAX_STREAM_FRAME_OVERHEAD is here and there
  // These size calculation should not exist multiple places
  uint64_t maximum_data_size = maximum_frame_size - MAX_STREAM_FRAME_OVERHEAD;
  if (maximum_data_size >= static_cast<uint64_t>(stream_info->block->size())) {
    frame = QUICFrameFactory::create_stream_frame(buf, stream_info->block, stream_info->stream_id, stream_info->offset,
                                                  stream_info->has_fin, true, true, id, owner);
    ink_assert(frame->size() <= maximum_frame_size);
    stream_info->block = nullptr;
  } else {
    frame = QUICFrameFactory::create_stream_frame(buf, stream_info->block, stream_info->stream_id, stream_info->offset, false, true,
                                                  true, id, owner);
    QUICStreamFrame *stream_frame = static_cast<QUICStreamFrame *>(frame);
    IOBufferBlock *block          = stream_frame->data();
    size_t over_length            = stream_frame->data_length() - maximum_data_size;
    block->_end                   = std::max(block->start(), block->_end - over_length);
    if (block->read_avail() == 0) {
      // no payload
      tmp_queue.push_back(std::move(info));
      return nullptr;
    }
    stream_info->block->consume(stream_frame->data_length());
    stream_info->offset += stream_frame->data_length();
    ink_assert(frame->size() <= maximum_frame_size);
    tmp_queue.push_back(std::move(info));
    return frame;
  }

  ink_assert(frame != nullptr);
  return frame;
}

QUICFrame *
QUICFrameRetransmitter::_create_crypto_frame(uint8_t *buf, QUICFrameInformationUPtr &info, uint16_t maximum_frame_size,
                                             std::deque<QUICFrameInformationUPtr> &tmp_queue, QUICFrameId id,
                                             QUICFrameGenerator *owner)
{
  CryptoFrameInfo *crypto_info = reinterpret_cast<CryptoFrameInfo *>(info->data);
  // FIXME: has_offset and has_length should be configurable.
  auto frame = QUICFrameFactory::create_crypto_frame(buf, crypto_info->block, crypto_info->offset, id, owner);
  if (frame->size() > maximum_frame_size) {
    QUICCryptoFrame *crypto_frame = static_cast<QUICCryptoFrame *>(frame);
    if (crypto_frame->size() - crypto_frame->data_length() > maximum_frame_size) {
      // header length is larger than maximum_frame_size.
      tmp_queue.push_back(std::move(info));
      return nullptr;
    }

    IOBufferBlock *block = crypto_frame->data();
    size_t over_length   = crypto_frame->size() - maximum_frame_size;
    block->_end          = std::max(block->start(), block->_end - over_length);
    if (block->read_avail() == 0) {
      // no payload
      tmp_queue.push_back(std::move(info));
      return nullptr;
    }

    crypto_info->block->consume(crypto_frame->data_length());
    crypto_info->offset += crypto_frame->data_length();
    ink_assert(frame->size() <= maximum_frame_size);
    tmp_queue.push_back(std::move(info));
    return frame;
  }

  crypto_info->block = nullptr;
  ink_assert(frame != nullptr);
  return frame;
}

bool
QUICFrameRetransmitter::is_retransmited_frame_queue_empty() const
{
  return this->_lost_frame_info_queue.empty();
}
