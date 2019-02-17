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

enum class QUICSendStreamState {
  Init       = 0,
  Ready      = 1,
  Send       = 2,
  DataSent   = 3,
  DataRecvd  = 4,
  ResetSent  = 5,
  ResetRecvd = 6,
};

enum class QUICReceiveStreamState {
  Init       = 7,
  Recv       = 8,
  SizeKnown  = 9,
  DataRecvd  = 10,
  ResetRecvd = 11,
  DataRead   = 12,
  ResetRead  = 13,
};

enum class QUICBidirectionalStreamState { Init = 14, Idle = 15, Open = 16, HC_R = 17, HC_L = 18, Closed = 19, Invalid = 20 };

template <class T> class QUICStreamStateMachine
{
public:
  virtual ~QUICStreamStateMachine() {}

  virtual T
  get() const
  {
    return this->_state;
  }

  virtual void update_with_sending_frame(const QUICFrame &frame)   = 0;
  virtual void update_with_receiving_frame(const QUICFrame &frame) = 0;

  virtual void
  update_on_transport_send_event()
  {
  }
  virtual void
  update_on_transport_recv_event()
  {
  }
  virtual void
  update_on_user_read_event()
  {
  }

  virtual bool is_allowed_to_send(QUICFrameType type) const        = 0;
  virtual bool is_allowed_to_send(const QUICFrame &frame) const    = 0;
  virtual bool is_allowed_to_receive(QUICFrameType type) const     = 0;
  virtual bool is_allowed_to_receive(const QUICFrame &frame) const = 0;

protected:
  void
  _set_state(T s)
  {
    ink_assert(s != T::Init);
    this->_state = s;
  }

private:
  T _state = T::Init;
};

class QUICSendStreamStateMachine : public QUICStreamStateMachine<QUICSendStreamState>
{
public:
  QUICSendStreamStateMachine() { this->_set_state(QUICSendStreamState::Ready); };

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;

  void update_on_transport_send_event() override;

  bool is_allowed_to_send(QUICFrameType type) const override;
  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(QUICFrameType type) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;

  void update(const QUICReceiveStreamState opposite_side);
};

class QUICReceiveStreamStateMachine : public QUICStreamStateMachine<QUICReceiveStreamState>
{
public:
  QUICReceiveStreamStateMachine() = default;

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;

  void update_on_transport_recv_event() override;
  void update_on_user_read_event() override;

  bool is_allowed_to_send(QUICFrameType type) const override;
  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(QUICFrameType type) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;

  void update(const QUICSendStreamState opposite_side);
};

class QUICBidirectionalStreamStateMachine : public QUICStreamStateMachine<QUICBidirectionalStreamState>
{
public:
  QUICBidirectionalStreamStateMachine() { this->_recv_stream_state.update(this->_send_stream_state.get()); };

  QUICBidirectionalStreamState get() const override;

  void update_with_sending_frame(const QUICFrame &frame) override;
  void update_with_receiving_frame(const QUICFrame &frame) override;

  void update_on_transport_send_event() override;
  void update_on_transport_recv_event() override;
  void update_on_user_read_event() override;

  bool is_allowed_to_send(QUICFrameType type) const override;
  bool is_allowed_to_send(const QUICFrame &frame) const override;
  bool is_allowed_to_receive(QUICFrameType type) const override;
  bool is_allowed_to_receive(const QUICFrame &frame) const override;

private:
  QUICSendStreamStateMachine _send_stream_state;
  QUICReceiveStreamStateMachine _recv_stream_state;
};
