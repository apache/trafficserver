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
#include "tscore/ink_assert.h"

void
QUICStreamState::_set_state(State s)
{
  ink_assert(s != State::_Init);
  ink_assert(s != State::_Invalid);
  this->_state = s;
}

// ---------QUICReceiveStreamState -----------

bool
QUICReceiveStreamState::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICReceiveStreamState::is_allowed_to_send(QUICFrameType type) const
{
  // Return true or break out the switch to return false
  switch (this->get()) {
  case State::_Init:
    if (type == QUICFrameType::STREAM || type == QUICFrameType::RST_STREAM || type == QUICFrameType::MAX_STREAM_DATA ||
        type == QUICFrameType::STREAM_BLOCKED) {
      return true;
    }
    break;
  case State::_Invalid:
    // Everthing is invalid on this state
    break;
  case State::Recv:
    if (type == QUICFrameType::NEW_CONNECTION_ID || type == QUICFrameType::PATH_CHALLENGE) {
      return true;
    }
    if (type == QUICFrameType::ACK) {
      return true;
    }
    if (type == QUICFrameType::MAX_STREAM_DATA || type == QUICFrameType::STOP_SENDING) {
      return true;
    }
    break;
  case State::SizeKnown:
    if (type == QUICFrameType::ACK) {
      return true;
    }
    if (type == QUICFrameType::STOP_SENDING) {
      return true;
    }
    break;
  case State::DataRecvd:
    if (type != QUICFrameType::STREAM && type != QUICFrameType::RST_STREAM && type != QUICFrameType::STREAM_BLOCKED) {
      return true;
    }
    break;
  case State::DataRead:
    if (type == QUICFrameType::STOP_SENDING) {
      return true;
    }
    break;
  case State::ResetRecvd:
    // It should not send any frame after receiving RST_STREAM
    break;
  case State::ResetRead:
    // It should not send any frame after receiving RST_STREAM
    break;
  default:
    ink_assert(!"Unknown state");
    break;
  }

  return false;
}

bool
QUICReceiveStreamState::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICReceiveStreamState::is_allowed_to_receive(QUICFrameType type) const
{
  // Return true or break out the switch to return false
  switch (this->get()) {
  case State::_Init:
    if (type == QUICFrameType::STREAM || type == QUICFrameType::RST_STREAM || type == QUICFrameType::MAX_STREAM_DATA ||
        type == QUICFrameType::STREAM_BLOCKED) {
      return true;
    }
    break;
  case State::_Invalid:
    // Everthing is invalid on this state
    break;
  case State::Recv:
    return true;
  case State::SizeKnown:
    return true;
  case State::DataRecvd:
    return true;
  case State::DataRead:
    return true;
  case State::ResetRecvd:
    return true;
  case State::ResetRead:
    return true;
  default:
    ink_assert(!"Unknown state");
    break;
  }

  return false;
}

void
QUICReceiveStreamState::update_with_sending_frame(const QUICFrame &frame)
{
}

void
QUICReceiveStreamState::update_with_receiving_frame(const QUICFrame &frame)
{
  switch (this->get()) {
  case State::_Init:
    if (frame.type() == QUICFrameType::STREAM) {
      this->_set_state(State::Recv);
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::SizeKnown);
        if (this->_in_progress->is_transfer_complete()) {
          this->_set_state(State::DataRecvd);
        }
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::Recv);
      this->_set_state(State::ResetRecvd);
    } else if (frame.type() == QUICFrameType::MAX_STREAM_DATA || frame.type() == QUICFrameType::STREAM_BLOCKED) {
      this->_set_state(State::Recv);
    } else {
      this->_set_state(State::_Invalid);
    }
    break;
  case State::Recv:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::SizeKnown);
        if (this->_in_progress->transfer_progress() == this->_in_progress->transfer_goal()) {
          this->_set_state(State::DataRecvd);
        }
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetRecvd);
    }
    break;
  case State::SizeKnown:
    if (frame.type() == QUICFrameType::STREAM) {
      if (this->_in_progress->transfer_progress() == this->_in_progress->transfer_goal()) {
        this->_set_state(State::DataRecvd);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetRecvd);
    }
    break;
  case State::DataRecvd:
    if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetRecvd);
    }
    break;
  case State::DataRead:
    break;
  case State::ResetRecvd:
    if (frame.type() == QUICFrameType::STREAM) {
      if (this->_in_progress->transfer_progress() == this->_in_progress->transfer_goal()) {
        this->_set_state(State::DataRecvd);
      }
    }
    break;
  case State::ResetRead:
    break;
  case State::_Invalid:
    // Once we get illegal state, no way to recover it
    break;
  default:
    ink_assert(!"Unknown state");
    break;
  }
}

void
QUICReceiveStreamState::update(const QUICStreamState &opposite_side)
{
  switch (this->get()) {
  case State::_Init:
    ink_assert(opposite_side.get() != State::_Init);
    this->_set_state(State::Recv);
    break;
  default:
    ink_assert(!"This shouldn't be happen");
    break;
  }
}

void
QUICReceiveStreamState::update_on_read()
{
  if (this->_in_progress->is_transfer_complete()) {
    this->_set_state(State::DataRead);
  }
}

void
QUICReceiveStreamState::update_on_eos()
{
  this->_set_state(State::ResetRead);
}

// ---------- QUICSendStreamState -------------

bool
QUICSendStreamState::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICSendStreamState::is_allowed_to_send(QUICFrameType type) const
{
  switch (this->get()) {
  case State::Ready:
  case State::Send:
    if (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_BLOCKED || type == QUICFrameType::RST_STREAM) {
      return true;
    }
    break;
  /*A sender MUST NOT send STREAM or STREAM_DATA_BLOCKED after sending a RESET_STREAM; that is, in the terminal states and in the
   * “Reset Sent” state.*/
  case State::ResetSent:
  case State::DataSent:
    if (type == QUICFrameType::RST_STREAM) {
      return true;
    }
    break;
  case State::DataRecvd:
    break;
  case State::ResetRecvd:
    break;
  case State::_Invalid:
  case State::_Init:
  default:
    ink_assert("This shouuldn't be happen");
    break;
  }

  return false;
}

bool
QUICSendStreamState::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICSendStreamState::is_allowed_to_receive(QUICFrameType type) const
{
  // An endpoint that receives a RESET_STREAM frame for a send-only stream MUST terminate the connection with error
  // PROTOCOL_VIOLATION. 
  // PS: RST_STREAM should be processed before Stream transition.
  ink_assert(type != QUICFrameType::RST_STREAM);

  switch (type) {
  case QUICFrameType::STOP_SENDING:
  case QUICFrameType::MAX_STREAM_DATA:
    return true;
  default:
    break;
  }

  return false;
}

void
QUICSendStreamState::update_with_sending_frame(const QUICFrame &frame)
{
  switch (this->get()) {
  case State::Ready:
    if (frame.type() == QUICFrameType::STREAM) {
      this->_set_state(State::Send);
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::DataSent);
      }
    } else if (frame.type() == QUICFrameType::STREAM_BLOCKED) {
      this->_set_state(State::Send);
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetSent);
    }
    break;
  case State::Send:
    if (frame.type() == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(State::DataSent);
      }
    } else if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetSent);
    }
    break;
  case State::DataSent:
    if (frame.type() == QUICFrameType::RST_STREAM) {
      this->_set_state(State::ResetSent);
    }
    break;
  case State::DataRecvd:
    break;
  case State::ResetSent:
    break;
  case State::ResetRecvd:
    break;
  case State::_Init:
  case State::_Invalid:
  default:
    ink_assert(!"Unknown state");
    break;
  }
}

void
QUICSendStreamState::update_with_receiving_frame(const QUICFrame &frame)
{
}

void
QUICSendStreamState::update(const QUICStreamState &opposite_side)
{
  switch (this->get()) {
  case State::_Init:
    ink_assert(opposite_side.get() != State::_Init);
    this->_set_state(State::Ready);
    break;
  default:
    ink_assert(!"This shouldn't be happen");
    break;
  }
}

void
QUICSendStreamState::update_on_ack()
{
  if (this->_out_progress->is_transfer_complete()) {
    this->_set_state(State::DataRecvd);
  } else if (this->_out_progress->is_cancelled()) {
    this->_set_state(State::ResetRecvd);
  }
}

// ---------QUICBidirectionalStreamState -----------

QUICStreamState::State
QUICBidirectionalStreamState::get() const
{
  QUICStreamState::State s_state = this->_send_stream_state.get();
  QUICStreamState::State r_state = this->_recv_stream_state.get();

  if (s_state == State::Ready || s_state == State::Send || s_state == State::DataSent) {
    if (r_state == State::Recv || r_state == State::SizeKnown) {
      return State::Open;
    } else if (r_state == State::DataRecvd || r_state == State::DataRead) {
      return State::HC_R;
    } else if (r_state == State::ResetRecvd || r_state == State::ResetRead) {
      return State::HC_R;
    } else {
      ink_assert(false);
      return State::_Invalid;
    }
  } else if (s_state == State::DataRecvd) {
    if (r_state == State::Recv || r_state == State::SizeKnown) {
      return State::HC_L;
    } else if (r_state == State::DataRecvd || r_state == State::DataRead) {
      return State::Closed;
    } else if (r_state == State::ResetRecvd || r_state == State::ResetRead) {
      return State::Closed;
    } else {
      ink_assert(false);
      return State::_Invalid;
    }
  } else if (s_state == State::ResetSent || s_state == State::ResetRecvd) {
    if (r_state == State::Recv || r_state == State::SizeKnown) {
      return State::HC_L;
    } else if (r_state == State::DataRecvd || r_state == State::DataRead) {
      return State::Closed;
    } else if (r_state == State::ResetRecvd || r_state == State::ResetRead) {
      return State::Closed;
    } else {
      ink_assert(false);
      return State::_Invalid;
    }
  } else if (s_state == State::_Init && r_state == State::_Init) {
    return State::_Init;
  } else {
    ink_assert(false);
    return State::_Invalid;
  }
}

void
QUICBidirectionalStreamState::update_with_sending_frame(const QUICFrame &frame)
{
  this->_send_stream_state.update_with_sending_frame(frame);
  if (this->_recv_stream_state.get() == State::_Init) {
    this->_recv_stream_state.update(this->_send_stream_state);
  }
}

void
QUICBidirectionalStreamState::update_with_receiving_frame(const QUICFrame &frame)
{
  this->_recv_stream_state.update_with_receiving_frame(frame);
  if (this->_send_stream_state.get() == State::_Init) {
    this->_send_stream_state.update(this->_recv_stream_state);
  }
}

void
QUICBidirectionalStreamState::update_on_ack()
{
  this->_send_stream_state.update_on_ack();
}

void
QUICBidirectionalStreamState::update_on_read()
{
  this->_recv_stream_state.update_on_read();
}

void
QUICBidirectionalStreamState::update_on_eos()
{
  this->_recv_stream_state.update_on_eos();
}

bool
QUICBidirectionalStreamState::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICBidirectionalStreamState::is_allowed_to_send(QUICFrameType type) const
{
  return this->_send_stream_state.is_allowed_to_send(type) || this->_recv_stream_state.is_allowed_to_send(type);
}

bool
QUICBidirectionalStreamState::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICBidirectionalStreamState::is_allowed_to_receive(QUICFrameType type) const
{
  return this->_send_stream_state.is_allowed_to_receive(type) || this->_recv_stream_state.is_allowed_to_receive(type);
}
