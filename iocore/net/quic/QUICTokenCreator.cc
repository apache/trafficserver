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
#include "QUICTokenCreator.h"

bool
QUICTokenCreator::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return !this->_is_resumption_token_sent;
}

QUICFrame *
QUICTokenCreator::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                                 size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_is_resumption_token_sent) {
    return frame;
  }

  if (this->_context->connection_info()->direction() == NET_VCONNECTION_IN) {
    // TODO Make expiration period configurable
    QUICResumptionToken token(this->_context->connection_info()->five_tuple().source(),
                              this->_context->connection_info()->connection_id(), ink_get_hrtime() + HRTIME_HOURS(24));
    frame = QUICFrameFactory::create_new_token_frame(buf, token, this->_issue_frame_id(), this);
    if (frame) {
      if (frame->size() < maximum_frame_size) {
        this->_is_resumption_token_sent = true;
      } else {
        // Cancel generating frame
        frame = nullptr;
      }
    }
  }

  return frame;
}

void
QUICTokenCreator::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  this->_is_resumption_token_sent = false;
}
