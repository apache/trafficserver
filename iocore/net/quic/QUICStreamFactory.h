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

class QUICStreamVConnection;

// PS: this class function should not static because of  THREAD_ALLOC and THREAD_FREE
class QUICStreamFactory
{
public:
  QUICStreamFactory(QUICRTTProvider *rtt_provider, QUICConnectionInfoProvider *info) : _rtt_provider(rtt_provider), _info(info) {}
  ~QUICStreamFactory() {}

  // create a bidistream, send only stream or receive only stream
  QUICStreamVConnection *create(QUICStreamId sid, uint64_t recv_max_stream_data, uint64_t send_max_stream_data);

  // delete stream by stream type
  void delete_stream(QUICStreamVConnection *stream);

private:
  QUICRTTProvider *_rtt_provider    = nullptr;
  QUICConnectionInfoProvider *_info = nullptr;
};
