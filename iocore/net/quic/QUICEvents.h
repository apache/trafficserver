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

#include "I_EventSystem.h"
#include "I_Event.h"

enum {
  QUIC_EVENT_PACKET_READ_READY = QUIC_EVENT_EVENTS_START,
  QUIC_EVENT_PACKET_WRITE_READY,
  QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE,
  QUIC_EVENT_CLOSING_TIMEOUT,
  QUIC_EVENT_PATH_VALIDATION_TIMEOUT,
  QUIC_EVENT_ACK_PERIODIC,
  QUIC_EVENT_SHUTDOWN,
  QUIC_EVENT_LD_SHUTDOWN,
  QUIC_EVENT_STATELESS_RESET,
};
