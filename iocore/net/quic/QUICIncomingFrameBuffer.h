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

#include <map>
#include <queue>

#include "QUICTypes.h"
#include "QUICFrame.h"

class QUICIncomingFrameBuffer
{
public:
  QUICIncomingFrameBuffer(QUICStream *stream) : _stream(stream) {}

  ~QUICIncomingFrameBuffer();

  std::shared_ptr<const QUICStreamFrame> pop();

  QUICErrorUPtr insert(const std::shared_ptr<const QUICStreamFrame>);

  void clear();

  bool empty();

private:
  QUICOffset _recv_offset = 0;
  QUICOffset _max_offset  = 0;
  QUICOffset _fin_offset  = UINT64_MAX;

  QUICErrorUPtr _check_and_set_fin_flag(QUICOffset offset, size_t len = 0, bool fin_flag = false);

  std::queue<std::shared_ptr<const QUICStreamFrame>> _recv_buffer;
  std::map<QUICOffset, std::shared_ptr<const QUICStreamFrame>> _out_of_order_queue;

  QUICStream *_stream = nullptr;
};
