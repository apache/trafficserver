/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "QUICPinger.h"

void
QUICPinger::request(QUICEncryptionLevel level)
{
  if (!this->_is_level_matched(level)) {
    return;
  }
  ++this->_need_to_fire[static_cast<int>(level)];
}

void
QUICPinger::cancel(QUICEncryptionLevel level)
{
  if (!this->_is_level_matched(level)) {
    return;
  }

  if (this->_need_to_fire[static_cast<int>(level)] > 0) {
    --this->_need_to_fire[static_cast<int>(level)];
  }
}

bool
QUICPinger::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return this->_need_to_fire[static_cast<int>(QUICTypeUtil::pn_space(level))] > 0;
}

/**
 * @param connection_credit This is not used. Because PING frame is not flow-controlled
 */
QUICFrame *
QUICPinger::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t /* connection_credit */, uint16_t maximum_frame_size,
                           ink_hrtime timestamp)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_need_to_fire[static_cast<int>(level)] > 0 && maximum_frame_size > 0) {
    // don't care ping frame lost or acked
    frame                                        = QUICFrameFactory::create_ping_frame(buf, 0, nullptr);
    this->_need_to_fire[static_cast<int>(level)] = 0;
  }

  return frame;
}
