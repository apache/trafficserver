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

#include <vector>

#include "QUICConnection.h"
#include "QUICFrame.h"
#include "QUICFrameHandler.h"
#include "QUICContext.h"

class QUICFrameDispatcher
{
public:
  QUICFrameDispatcher(QUICConnectionInfoProvider *info);

  QUICConnectionErrorUPtr receive_frames(QUICContext &context, QUICEncryptionLevel level, const uint8_t *payload, uint16_t size,
                                         bool &should_send_ackbool, bool &is_flow_controlled, bool *has_non_probing_frame,
                                         const QUICPacketR *packet);

  void add_handler(QUICFrameHandler *handler);

private:
  QUICConnectionInfoProvider *_info = nullptr;
  QUICFrameFactory _frame_factory;
  std::vector<QUICFrameHandler *> _handlers[256];
};
