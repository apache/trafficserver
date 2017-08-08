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

class QUICStreamState
{
public:
  // 10.1.  Life of a Stream
  enum class State {
    idle,
    open,
    half_closed_remote,
    half_closed_local,
    closed,
    illegal // not on the specification, just for internal use
  };
  const State get() const;
  bool is_allowed_to_send(const QUICFrame &frame) const;
  bool is_allowed_to_receive(const QUICFrame &frame) const;

  /*
   * Updates its internal state
   * Internal state will be "illegal" state if inappropriate frame was passed
   */
  void update_with_received_frame(const QUICFrame &frame);
  void update_with_sent_frame(const QUICFrame &frame);

private:
  void _set_state(State s);
  State _state = State::idle;
};
