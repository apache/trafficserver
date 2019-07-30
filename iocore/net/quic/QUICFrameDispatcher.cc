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

#include "QUICFrameDispatcher.h"
#include "QUICDebugNames.h"

static constexpr char tag[] = "quic_net";

#define QUICDebug(fmt, ...) Debug(tag, "[%s] " fmt, this->_info->cids().data(), ##__VA_ARGS__)

//
// Frame Dispatcher
//
QUICFrameDispatcher::QUICFrameDispatcher(QUICConnectionInfoProvider *info) : _info(info) {}

void
QUICFrameDispatcher::add_handler(QUICFrameHandler *handler)
{
  for (QUICFrameType t : handler->interests()) {
    this->_handlers[static_cast<uint8_t>(t)].push_back(handler);
  }
}

QUICConnectionErrorUPtr
QUICFrameDispatcher::receive_frames(QUICEncryptionLevel level, const uint8_t *payload, uint16_t size, bool &ack_only,
                                    bool &is_flow_controlled, bool *has_non_probing_frame)
{
  uint16_t cursor               = 0;
  ack_only                      = true;
  is_flow_controlled            = false;
  QUICConnectionErrorUPtr error = nullptr;

  while (cursor < size) {
    const QUICFrame &frame = this->_frame_factory.fast_create(payload + cursor, size - cursor);
    if (frame.type() == QUICFrameType::UNKNOWN) {
      QUICDebug("Failed to create a frame (%u bytes skipped)", size - cursor);
      break;
    }
    if (has_non_probing_frame) {
      *has_non_probing_frame |= !frame.is_probing_frame();
    }
    cursor += frame.size();

    QUICFrameType type = frame.type();

    if (type == QUICFrameType::STREAM) {
      is_flow_controlled = true;
    }

    if (is_debug_tag_set(tag) && type != QUICFrameType::PADDING) {
      char msg[1024];
      frame.debug_msg(msg, sizeof(msg));
      QUICDebug("[RX] | %s", msg);
    }

    if (type != QUICFrameType::PADDING && type != QUICFrameType::ACK) {
      ack_only = false;
    }

    std::vector<QUICFrameHandler *> handlers = this->_handlers[static_cast<uint8_t>(type)];
    for (auto h : handlers) {
      error = h->handle_frame(level, frame);
      // TODO: is there any case to continue this loop even if error?
      if (error != nullptr) {
        return error;
      }
    }
  }

  return error;
}
