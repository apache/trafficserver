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

// ---------QUICReceiveStreamState -----------

bool
QUICReceiveStreamStateMachine::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICReceiveStreamStateMachine::is_allowed_to_send(QUICFrameType type) const
{
  if (type != QUICFrameType::STOP_SENDING && type != QUICFrameType::MAX_STREAM_DATA) {
    return false;
  }

  QUICReceiveStreamState state = this->get();
  // The receiver only sends MAX_STREAM_DATA in the "Recv" state.
  if (type == QUICFrameType::MAX_STREAM_DATA && state == QUICReceiveStreamState::Recv) {
    return true;
  }

  // A receiver can send STOP_SENDING in any state where it has not received a RESET_STREAM frame; that is states other than "Reset
  // Recvd" or "Reset Read".
  if (type == QUICFrameType::STOP_SENDING && state != QUICReceiveStreamState::ResetRecvd &&
      state != QUICReceiveStreamState::ResetRead) {
    return true;
  }

  return false;
}

bool
QUICReceiveStreamStateMachine::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICReceiveStreamStateMachine::is_allowed_to_receive(QUICFrameType type) const
{
  // always allow receive these frames.
  if (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_DATA_BLOCKED || type == QUICFrameType::RESET_STREAM) {
    return true;
  }

  return false;
}

void
QUICReceiveStreamStateMachine::update_with_sending_frame(const QUICFrame &frame)
{
}

void
QUICReceiveStreamStateMachine::update_with_receiving_frame(const QUICFrame &frame)
{
  // The receiving part of a stream initiated by a peer (types 1 and 3 for a client, or 0 and 2 for a server) is created when the
  // first STREAM, STREAM_DATA_BLOCKED, or RESET_STREAM is received for that stream.
  QUICReceiveStreamState state = this->get();
  QUICFrameType type           = frame.type();

  if (state == QUICReceiveStreamState::Init &&
      (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_DATA_BLOCKED || type == QUICFrameType::RESET_STREAM)) {
    this->_set_state(QUICReceiveStreamState::Recv);
  }

  switch (this->get()) {
  case QUICReceiveStreamState::Recv:
    if (type == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(QUICReceiveStreamState::SizeKnown);
        if (this->_in_progress->is_transfer_complete()) {
          this->_set_state(QUICReceiveStreamState::DataRecvd);
        }
      }
    } else if (type == QUICFrameType::RESET_STREAM) {
      this->_set_state(QUICReceiveStreamState::ResetRecvd);
    }
    break;
  case QUICReceiveStreamState::SizeKnown:
    if (type == QUICFrameType::STREAM && this->_in_progress->is_transfer_complete()) {
      this->_set_state(QUICReceiveStreamState::DataRecvd);
    } else if (type == QUICFrameType::RESET_STREAM) {
      this->_set_state(QUICReceiveStreamState::ResetRecvd);
    }
    break;
  case QUICReceiveStreamState::DataRecvd:
    if (type == QUICFrameType::STREAM && this->_in_progress->is_transfer_complete()) {
      this->_set_state(QUICReceiveStreamState::ResetRecvd);
    }
    break;
  case QUICReceiveStreamState::Init:
  case QUICReceiveStreamState::ResetRecvd:
  case QUICReceiveStreamState::DataRead:
  case QUICReceiveStreamState::ResetRead:
    break;
  default:
    ink_assert(!"Unknown state");
    break;
  }
}

void
QUICReceiveStreamStateMachine::update_on_read()
{
  if (this->_in_progress->is_transfer_complete()) {
    this->_set_state(QUICReceiveStreamState::DataRead);
  }
}

void
QUICReceiveStreamStateMachine::update_on_eos()
{
  this->_set_state(QUICReceiveStreamState::ResetRead);
}

void
QUICReceiveStreamStateMachine::update(const QUICSendStreamState state)
{
  // The receiving part of a stream enters the "Recv" state when the sending part of a bidirectional stream initiated by the
  // endpoint (type 0 for a client, type 1 for a server) enters the "Ready" state.
  switch (this->get()) {
  case QUICReceiveStreamState::Init:
    if (state == QUICSendStreamState::Ready) {
      this->_set_state(QUICReceiveStreamState::Recv);
    }
    break;
  default:
    break;
  }
}

// ---------- QUICSendStreamState -------------

bool
QUICSendStreamStateMachine::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICSendStreamStateMachine::is_allowed_to_send(QUICFrameType type) const
{
  if (type != QUICFrameType::STREAM && type != QUICFrameType::STREAM_DATA_BLOCKED && type != QUICFrameType::RESET_STREAM) {
    return false;
  }

  switch (this->get()) {
  case QUICSendStreamState::Ready:
    if (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_DATA_BLOCKED || type == QUICFrameType::RESET_STREAM) {
      return true;
    }
    break;
  case QUICSendStreamState::Send:
    if (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_DATA_BLOCKED || type == QUICFrameType::RESET_STREAM) {
      return true;
    }
    break;
  case QUICSendStreamState::DataSent:
    if (type == QUICFrameType::RESET_STREAM) {
      return true;
    }
    break;
  // A sender MUST NOT send any of these frames from a terminal state ("Data Recvd" or "Reset Recvd").
  case QUICSendStreamState::DataRecvd:
  case QUICSendStreamState::ResetRecvd:
    break;
  // A sender MUST NOT send STREAM or STREAM_DATA_BLOCKED after sending a RESET_STREAM; that is, in the terminal states and in the
  // "Reset Sent" state.
  case QUICSendStreamState::ResetSent:
    if (type == QUICFrameType::RESET_STREAM) {
      return true;
    }
    break;
  default:
    ink_assert("This shouldn't be happen");
    break;
  }

  return false;
}

bool
QUICSendStreamStateMachine::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICSendStreamStateMachine::is_allowed_to_receive(QUICFrameType type) const
{
  if (type != QUICFrameType::STOP_SENDING && type != QUICFrameType::MAX_STREAM_DATA) {
    return false;
  }

  // A sender could receive either of these two frames(MAX_STREAM_DATA and STOP_SENDING) in any state as a result of delayed
  // delivery of packets.
  // PS: Because we need to reply a RESET_STREAM frame. STOP_SENDING frame is accepted in all states. But we
  // don't need to do anything for MAX_STREAM_DATA frame when we are in terminal state.
  if (type == QUICFrameType::STOP_SENDING) {
    return true;
  }

  switch (this->get()) {
  case QUICSendStreamState::Ready:
  case QUICSendStreamState::Send:
    if (type == QUICFrameType::MAX_STREAM_DATA) {
      return true;
    }
    break;
  // "MAX_STREAM_DATA frames might be received until the peer receives the final stream offset. The endpoint can safely ignore
  // any MAX_STREAM_DATA frames it receives from its peer for a stream in this state."
  case QUICSendStreamState::DataSent:
  case QUICSendStreamState::ResetSent:
  case QUICSendStreamState::DataRecvd:
  case QUICSendStreamState::ResetRecvd:
    if (type == QUICFrameType::MAX_STREAM_DATA) {
      return true;
    }
    break;
  default:
    break;
  }

  return false;
}

void
QUICSendStreamStateMachine::update_with_sending_frame(const QUICFrame &frame)
{
  QUICSendStreamState state = this->get();
  QUICFrameType type        = frame.type();
  if (state == QUICSendStreamState::Ready &&
      (type == QUICFrameType::STREAM || type == QUICFrameType::STREAM_DATA_BLOCKED || type == QUICFrameType::RESET_STREAM)) {
    this->_set_state(QUICSendStreamState::Send);
  }

  switch (this->get()) {
  case QUICSendStreamState::Send:
    if (type == QUICFrameType::STREAM) {
      if (static_cast<const QUICStreamFrame &>(frame).has_fin_flag()) {
        this->_set_state(QUICSendStreamState::DataSent);
      }
    } else if (type == QUICFrameType::RESET_STREAM) {
      this->_set_state(QUICSendStreamState::ResetSent);
    }
    break;
  case QUICSendStreamState::DataSent:
    if (type == QUICFrameType::RESET_STREAM) {
      this->_set_state(QUICSendStreamState::ResetSent);
    }
    break;
  case QUICSendStreamState::Init:
  case QUICSendStreamState::Ready:
  case QUICSendStreamState::DataRecvd:
  case QUICSendStreamState::ResetSent:
  case QUICSendStreamState::ResetRecvd:
    break;
  default:
    ink_assert(!"Unknown state");
    break;
  }
}

void
QUICSendStreamStateMachine::update_with_receiving_frame(const QUICFrame &frame)
{
}

void
QUICSendStreamStateMachine::update_on_ack()
{
  if (this->_out_progress->is_transfer_complete()) {
    this->_set_state(QUICSendStreamState::DataRecvd);
  } else if (this->_out_progress->is_cancelled()) {
    this->_set_state(QUICSendStreamState::ResetRecvd);
  }
}

void
QUICSendStreamStateMachine::update(const QUICReceiveStreamState state)
{
  // The sending part of a bidirectional stream initiated by a peer (type 0 for a server, type 1 for a client) enters the "Ready"
  // state then immediately transitions to the "Send" state if the receiving part enters the "Recv" state (Section 3.2).
  switch (this->get()) {
  case QUICSendStreamState::Ready:
    if (state == QUICReceiveStreamState::Recv) {
      this->_set_state(QUICSendStreamState::Send);
    }
    break;
  default:
    break;
  }
}

// ---------QUICBidirectionalStreamState -----------

QUICBidirectionalStreamState
QUICBidirectionalStreamStateMachine::get() const
{
  QUICSendStreamState s_state    = this->_send_stream_state.get();
  QUICReceiveStreamState r_state = this->_recv_stream_state.get();

  if (s_state == QUICSendStreamState::Ready || r_state == QUICReceiveStreamState::Init) {
    return QUICBidirectionalStreamState::Idle;
  } else if (s_state == QUICSendStreamState::Ready || s_state == QUICSendStreamState::Send ||
             s_state == QUICSendStreamState::DataSent) {
    if (r_state == QUICReceiveStreamState::Recv || r_state == QUICReceiveStreamState::SizeKnown) {
      return QUICBidirectionalStreamState::Open;
    } else if (r_state == QUICReceiveStreamState::DataRecvd || r_state == QUICReceiveStreamState::DataRead) {
      return QUICBidirectionalStreamState::HC_R;
    } else if (r_state == QUICReceiveStreamState::ResetRecvd || r_state == QUICReceiveStreamState::ResetRead) {
      return QUICBidirectionalStreamState::HC_R;
    } else {
      ink_assert(false);
      return QUICBidirectionalStreamState::Invalid;
    }
  } else if (s_state == QUICSendStreamState::DataRecvd) {
    if (r_state == QUICReceiveStreamState::Recv || r_state == QUICReceiveStreamState::SizeKnown) {
      return QUICBidirectionalStreamState::HC_L;
    } else if (r_state == QUICReceiveStreamState::DataRecvd || r_state == QUICReceiveStreamState::DataRead) {
      return QUICBidirectionalStreamState::Closed;
    } else if (r_state == QUICReceiveStreamState::ResetRecvd || r_state == QUICReceiveStreamState::ResetRead) {
      return QUICBidirectionalStreamState::Closed;
    } else {
      ink_assert(false);
      return QUICBidirectionalStreamState::Invalid;
    }
  } else if (s_state == QUICSendStreamState::ResetSent || s_state == QUICSendStreamState::ResetRecvd) {
    if (r_state == QUICReceiveStreamState::Recv || r_state == QUICReceiveStreamState::SizeKnown) {
      return QUICBidirectionalStreamState::HC_L;
    } else if (r_state == QUICReceiveStreamState::DataRecvd || r_state == QUICReceiveStreamState::DataRead) {
      return QUICBidirectionalStreamState::Closed;
    } else if (r_state == QUICReceiveStreamState::ResetRecvd || r_state == QUICReceiveStreamState::ResetRead) {
      return QUICBidirectionalStreamState::Closed;
    } else {
      ink_assert(false);
      return QUICBidirectionalStreamState::Invalid;
    }
  } else {
    ink_assert(false);
    return QUICBidirectionalStreamState::Invalid;
  }
}

void
QUICBidirectionalStreamStateMachine::update_with_sending_frame(const QUICFrame &frame)
{
  // The receiving part of a stream enters the "Recv" state when the sending part of a bidirectional stream initiated by the
  // endpoint (type 0 for a client, type 1 for a server) enters the "Ready" state.
  this->_send_stream_state.update_with_sending_frame(frame);
  // PS: It should not happen because we initialize the send side and read side together. And the SendState has the default state
  // "Ready". But to obey the specs, we do this as follow.
  if (this->_send_stream_state.get() == QUICSendStreamState::Ready &&
      this->_recv_stream_state.get() == QUICReceiveStreamState::Init) {
    this->_recv_stream_state.update(this->_send_stream_state.get());
  }
}

void
QUICBidirectionalStreamStateMachine::update_with_receiving_frame(const QUICFrame &frame)
{
  // The sending part of a bidirectional stream initiated by a peer (type 0 for a server, type 1 for a client) enters the "Ready"
  // state then immediately transitions to the "Send" state if the receiving part enters the "Recv" state (Section 3.2).
  this->_recv_stream_state.update_with_receiving_frame(frame);
  if (this->_send_stream_state.get() == QUICSendStreamState::Ready &&
      this->_recv_stream_state.get() == QUICReceiveStreamState::Recv) {
    this->_send_stream_state.update(this->_recv_stream_state.get());
  }
}

void
QUICBidirectionalStreamStateMachine::update_on_ack()
{
  this->_send_stream_state.update_on_ack();
}

void
QUICBidirectionalStreamStateMachine::update_on_read()
{
  this->_recv_stream_state.update_on_read();
}

void
QUICBidirectionalStreamStateMachine::update_on_eos()
{
  this->_recv_stream_state.update_on_eos();
}

bool
QUICBidirectionalStreamStateMachine::is_allowed_to_send(const QUICFrame &frame) const
{
  return this->is_allowed_to_send(frame.type());
}

bool
QUICBidirectionalStreamStateMachine::is_allowed_to_send(QUICFrameType type) const
{
  return this->_send_stream_state.is_allowed_to_send(type) || this->_recv_stream_state.is_allowed_to_send(type);
}

bool
QUICBidirectionalStreamStateMachine::is_allowed_to_receive(const QUICFrame &frame) const
{
  return this->is_allowed_to_receive(frame.type());
}

bool
QUICBidirectionalStreamStateMachine::is_allowed_to_receive(QUICFrameType type) const
{
  return this->_send_stream_state.is_allowed_to_receive(type) || this->_recv_stream_state.is_allowed_to_receive(type);
}
