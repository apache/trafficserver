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
#include "QUICFrameTransmitter.h"

//
// QUICFlowController
//
QUICOffset
QUICFlowController::current_offset()
{
  return this->_offset;
}

QUICOffset
QUICFlowController::current_limit()
{
  return this->_limit;
}

int
QUICFlowController::update(QUICOffset offset)
{
  if (this->_offset <= offset) {
    // Assume flow control is not initialized if the limit was 0
    if (this->_limit != 0 && offset > this->_limit) {
      return -1;
    }
    this->_offset = offset;
  }

  return 0;
}

void
QUICFlowController::forward_limit(QUICOffset limit)
{
  // MAX_(STREAM_)DATA might be unorderd due to delay
  // Just ignore if the size was smaller than the last one
  if (this->_limit > limit) {
    return;
  }
  this->_limit = limit;
}

void
QUICFlowController::set_threshold(uint64_t threshold)
{
  this->_threshold = threshold;
}

//
// QUICRemoteFlowController
//
void
QUICRemoteFlowController::forward_limit(QUICOffset offset)
{
  QUICFlowController::forward_limit(offset);
  this->_blocked = false;
}

int
QUICRemoteFlowController::update(QUICOffset offset)
{
  int ret = QUICFlowController::update(offset);

  // Assume flow control is not initialized if the limit was 0
  if (this->_limit == 0) {
    return ret;
  }

  // Send BLOCKED(_STREAM) frame
  if (!this->_blocked && offset > this->_limit) {
    this->_tx->transmit_frame(this->_create_frame());
    this->_blocked = true;
  }

  return ret;
}

//
// QUICLocalFlowController
//
void
QUICLocalFlowController::forward_limit(QUICOffset offset)
{
  QUICFlowController::forward_limit(offset);

  // Send MAX_(STREAM_)DATA frame
  if (this->_limit - this->_offset <= this->_threshold) {
    this->_tx->transmit_frame(this->_create_frame());
  }
}

//
// QUIC[Remote|Local][Connection|Stream]FlowController
//
QUICFrameUPtr
QUICRemoteConnectionFlowController::_create_frame()
{
  return QUICFrameFactory::create_blocked_frame(this->_offset);
}

QUICFrameUPtr
QUICLocalConnectionFlowController::_create_frame()
{
  return QUICFrameFactory::create_max_data_frame(this->_limit);
}

QUICFrameUPtr
QUICRemoteStreamFlowController::_create_frame()
{
  return QUICFrameFactory::create_stream_blocked_frame(this->_stream_id, this->_offset);
}

QUICFrameUPtr
QUICLocalStreamFlowController::_create_frame()
{
  return QUICFrameFactory::create_max_stream_data_frame(this->_stream_id, this->_limit);
}
