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

#include "QUICApplication.h"
#include "Http3Frame.h"
#include "Http3FrameHandler.h"
#include <vector>

class QUICStreamVCAdapter;

class Http3FrameDispatcher
{
public:
  Http3ErrorUPtr on_read_ready(QUICStreamId stream_id, IOBufferReader &reader, uint64_t &nread);

  void add_handler(Http3FrameHandler *handler);

private:
  enum READING_STATE {
    READING_TYPE_LEN,
    READING_LENGTH_LEN,
    READING_PAYLOAD_LEN,
    READING_PAYLOAD,
  } _reading_state = READING_TYPE_LEN;
  int64_t _reading_frame_type_len;
  int64_t _reading_frame_length_len;
  uint64_t _reading_frame_payload_len;
  Http3FrameFactory _frame_factory;
  std::vector<Http3FrameHandler *> _handlers[256];
};
