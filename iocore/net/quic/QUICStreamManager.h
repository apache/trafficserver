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

#include "QUICTypes.h"
#include "QUICStream.h"
#include "QUICFrameHandler.h"
#include "QUICFrame.h"
#include "QUICFrameTransmitter.h"

class QUICNetVConnection;

class QUICStreamManager : public QUICFrameHandler
{
public:
  QUICStreamManager(){};

  int init(QUICFrameTransmitter *tx);
  void set_connection(QUICNetVConnection *vc); // FIXME Want to remove.
  virtual void handle_frame(std::shared_ptr<const QUICFrame>) override;
  void send_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame);

  DLL<QUICStream> stream_list;

private:
  QUICStream *_find_or_create_stream(QUICStreamId stream_id);
  QUICStream *_find_stream(QUICStreamId id);

  QUICNetVConnection *_vc   = nullptr;
  QUICFrameTransmitter *_tx = nullptr;

private:
  void _handle_stream_frame(std::shared_ptr<const QUICStreamFrame>);
};
