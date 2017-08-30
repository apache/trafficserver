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

#include "QUICStreamState.h"
#include "ts/ink_assert.h"

const QUICStreamState::State
QUICStreamState::get() const
{
  return this->_state;
}

bool
QUICStreamState::is_allowed_to_send(const QUICFrame &frame) const
{
  switch (this->_state) {
  case State::idle:
    break;
  case State::open:
    break;
  case State::half_closed_local:
    if (frame.type() == QUICFrameType::STREAM) {
      return false;
    }
    break;
  case State::half_closed_remote:
    break;
  case State::closed:
    // Once a stream reaches this state, no frames can be sent that mention the stream
    if (frame.type() == QUICFrameType::STREAM) {
      return false;
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      return false;
    } else if (frame.type() == QUICFrameType::MAX_STREAM_DATA) {
      return false;
    }
    break;
  case State::illegal:
    return false;
  }
  return true;
}

bool
QUICStreamState::is_allowed_to_receive(const QUICFrame &frame) const
{
  switch (this->_state) {
  case State::idle:
    break;
  case State::open:
    break;
  case State::half_closed_local:
    break;
  case State::half_closed_remote:
    break;
  case State::closed:
    // Reordering might cause frames to be received after closing
    break;
  case State::illegal:
    return false;
  }
  return true;
}

void
QUICStreamState::update_with_received_frame(const QUICFrame &frame)
{
  switch (this->_state) {
  case State::idle:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::half_closed_remote);
      } else {
        this->_set_state(State::open);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::half_closed_remote);
    } else if (frame.type() == QUICFrameType::MAX_STREAM_DATA || frame.type() == QUICFrameType::STREAM_BLOCKED) {
      this->_set_state(State::open);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::open:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::half_closed_remote);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::half_closed_remote);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::half_closed_local:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::closed);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::closed);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::half_closed_remote:
    break;
  case State::closed:
    break;
  case State::illegal:
    // Once we get illegal state, no way to recover it
    break;
  default:
    break;
  }
}

void
QUICStreamState::update_with_sent_frame(const QUICFrame &frame)
{
  switch (this->_state) {
  case State::idle:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::half_closed_local);
      } else {
        this->_set_state(State::open);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::half_closed_local);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::open:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::half_closed_local);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::half_closed_local);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::half_closed_local:
    break;
  case State::half_closed_remote:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::closed);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::closed);
    } else {
      this->_set_state(State::illegal);
    }
    break;
  case State::closed:
    break;
  case State::illegal:
    // Once we get illegal state, no way to recover it
    break;
  default:
    break;
  }
}

void
QUICStreamState::_set_state(State s)
{
  ink_assert(s != State::idle);
  ink_assert(s != State::illegal);
  this->_state = s;
}
