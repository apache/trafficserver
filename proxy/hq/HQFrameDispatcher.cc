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

#include "HQFrameDispatcher.h"
#include "HQDebugNames.h"
#include "ts/Diags.h"

static constexpr char tag[] = "hq_frame";

//
// Frame Dispatcher
//

void
HQFrameDispatcher::add_handler(HQFrameHandler *handler)
{
  for (HQFrameType t : handler->interests()) {
    this->_handlers[static_cast<uint8_t>(t)].push_back(handler);
  }
}

HQErrorUPtr
HQFrameDispatcher::on_read_ready(const uint8_t *src, uint16_t read_avail, uint16_t &nread)
{
  std::shared_ptr<const HQFrame> frame(nullptr);
  const uint8_t *cursor = src;
  HQErrorUPtr error     = HQErrorUPtr(new HQNoError());
  uint64_t frame_length = 0;

  while (HQFrame::length(cursor, read_avail, frame_length) != -1 && read_avail >= frame_length) {
    frame = this->_frame_factory.fast_create(cursor, read_avail);
    if (frame == nullptr) {
      Debug(tag, "Failed to create a frame");
      // error = HQErrorUPtr(new HQStreamError());
      break;
    }
    cursor += frame->total_length();
    read_avail -= frame->total_length();

    HQFrameType type                       = frame->type();
    std::vector<HQFrameHandler *> handlers = this->_handlers[static_cast<uint8_t>(type)];
    for (auto h : handlers) {
      error = h->handle_frame(frame);
      if (error->cls != HQErrorClass::NONE) {
        return error;
      }
    }
  }

  nread = cursor - src;

  return error;
}
