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

#pragma once

#include "QUICFrame.h"
#include "QUICTransferProgressProvider.h"

class QUICStreamState
{
public:
  enum class State {
    // Common
    _Init,
    _Invalid,
    // SendStreamState
    Ready,
    Send,
    DataSent,
    DataRecvd,
    ResetSent,
    ResetRecvd,
    // ReceiveStreamState
    Recv,
    SizeKnown,
    /* DataRecvd, */
    DataRead,
    /* ResetRecvd, */
    ResetRead,
    // Bidirectional
    Open,
    HC_R,
    HC_L,
    Closed
  };

  virtual ~QUICStreamState() {}

  virtual State
  get() const
  {
    return this->_state;
  }

  virtual void update_with_sending_frame(const QUICFrame &frame)   = 0;
  virtual void update_with_receiving_frame(const QUICFrame &frame) = 0;

  virtual bool is_allowed_to_send(const QUICFrame &frame) const    = 0;
  virtual bool is_allowed_to_receive(const QUICFrame &frame) const = 0;

protected:
  void _set_state(State s);

private:
  State _state = State::_Init;
};

class QUICUnidirectionalStreamState : public QUICStreamState
{
public:
  QUICUnidirectionalStreamState(QUICTransferProgressProvider *in, QUICTransferProgressProvider *out)
    : _in_progress(in), _out_progress(out)
  {
  }
  virtual void update(const QUICStreamState &opposite_side) = 0;

protected:
  QUICTransferProgressProvider *_in_progress  = nullptr;
  QUICTransferProgressProvider *_out_progress = nullptr;
};

class QUICSendStreamState : public QUICUnidirectionalStreamState
{
public:
  QUICSendStreamState(QUICTransferProgressProvider *in, QUICTransferProgressProvider *out) : QUICUnidirectionalStreamState(in, out)
  {
    this->_set_state(State::Ready);
  }

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;
  void update(const QUICStreamState &opposite_side) override;

  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;
};

class QUICReceiveStreamState : public QUICUnidirectionalStreamState
{
public:
  QUICReceiveStreamState(QUICTransferProgressProvider *in, QUICTransferProgressProvider *out)
    : QUICUnidirectionalStreamState(in, out)
  {
  }

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;
  void update(const QUICStreamState &opposite_side) override;

  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;
};

class QUICBidirectionalStreamState : public QUICStreamState
{
public:
  QUICBidirectionalStreamState(QUICTransferProgressProvider *send_in, QUICTransferProgressProvider *send_out,
                               QUICTransferProgressProvider *recv_in, QUICTransferProgressProvider *recv_out)
    : _send_stream_state(send_in, send_out), _recv_stream_state(recv_in, recv_out)
  {
    this->_recv_stream_state.update(this->_send_stream_state);
  }
  State get() const override;

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;

  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;

private:
  QUICSendStreamState _send_stream_state;
  QUICReceiveStreamState _recv_stream_state;
};
