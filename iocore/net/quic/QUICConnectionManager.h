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

#include <QUICFrameHandler.h>
#include <QUICFrame.h>
#include "QUICFrameTransmitter.h"

class QUICConnectionManager : public QUICFrameHandler
{
public:
  QUICConnectionManager(QUICFrameTransmitter *tx) : _tx(tx){};
  virtual void handle_frame(std::shared_ptr<const QUICFrame> frame) override;

private:
  QUICFrameTransmitter *_tx = nullptr;

  void _handle_ping_frame(const QUICPingFrame *);
};
