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
#include "QUICConnectionManager.h"
#include "QUICStreamManager.h"
#include "QUICFlowController.h"
#include "QUICCongestionController.h"
#include "QUICLossDetector.h"
#include "QUICEvents.h"

const static char *tag = "quic_frame_handler";

//
// Frame Dispatcher
//
QUICFrameDispatcher::QUICFrameDispatcher(const std::shared_ptr<QUICConnectionManager> cmgr,
                                         const std::shared_ptr<QUICStreamManager> smgr,
                                         const std::shared_ptr<QUICFlowController> fctlr,
                                         const std::shared_ptr<QUICCongestionController> cctlr,
                                         const std::shared_ptr<QUICLossDetector> ld)
{
  connectionManager    = cmgr;
  streamManager        = smgr;
  flowController       = fctlr;
  congestionController = cctlr;
  lossDetector         = ld;
}

bool
QUICFrameDispatcher::receive_frames(const uint8_t *payload, uint16_t size)
{
  std::shared_ptr<const QUICFrame> frame(nullptr);
  uint16_t cursor      = 0;
  bool should_send_ack = false;

  while (cursor < size) {
    frame = this->_frame_factory.fast_create(payload + cursor, size - cursor);
    if (frame == nullptr) {
      Debug(tag, "Failed to create a frame (%u bytes skipped)", size - cursor);
      break;
    }
    cursor += frame->size();

    // TODO: check debug build
    if (frame->type() != QUICFrameType::PADDING) {
      Debug(tag, "frame type %d, size %zu", static_cast<int>(frame->type()), frame->size());
    }

    // FIXME We should probably use a mapping table. All the objects has the common interface (QUICFrameHandler).
    switch (frame->type()) {
    case QUICFrameType::PADDING: {
      // NOTE: do nothing
      break;
    }
    case QUICFrameType::RST_STREAM: {
      streamManager->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::CONNECTION_CLOSE: {
      connectionManager->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::GOAWAY: {
      connectionManager->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::MAX_DATA: {
      flowController->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::MAX_STREAM_DATA: {
      flowController->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::MAX_STREAM_ID: {
      should_send_ack = true;
      break;
    }
    case QUICFrameType::PING: {
      connectionManager->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::BLOCKED: {
      flowController->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    case QUICFrameType::STREAM_BLOCKED: {
      should_send_ack = true;
      break;
    }
    case QUICFrameType::STREAM_ID_NEEDED: {
      should_send_ack = true;
      break;
    }
    case QUICFrameType::NEW_CONNECTION_ID: {
      should_send_ack = true;
      break;
    }
    case QUICFrameType::ACK: {
      congestionController->handle_frame(frame);
      this->lossDetector->handle_frame(frame);
      break;
    }
    case QUICFrameType::STREAM: {
      streamManager->handle_frame(frame);
      flowController->handle_frame(frame);
      congestionController->handle_frame(frame);
      should_send_ack = true;
      break;
    }
    default:
      // Unknown frame
      Debug(tag, "Unknown frame type: %02x", static_cast<unsigned int>(frame->type()));
      ink_assert(false);

      break;
    }
  }
  return should_send_ack;
}
