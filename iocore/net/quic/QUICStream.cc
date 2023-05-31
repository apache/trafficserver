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

#include "QUICStream.h"

#include "QUICStreamManager.h"

constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD = 24;

QUICStream::QUICStream(QUICConnectionInfoProvider *cinfo, QUICStreamId sid) : _connection_info(cinfo), _id(sid) {}

QUICStream::~QUICStream() {}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

const QUICConnectionInfoProvider *
QUICStream::connection_info()
{
  return this->_connection_info;
}

QUICStreamDirection
QUICStream::direction() const
{
  return QUICTypeUtil::detect_stream_direction(this->_id, this->_connection_info->direction());
}

bool
QUICStream::is_bidirectional() const
{
  return ((this->_id & 0x03) < 0x02);
}

bool
QUICStream::is_closable() const
{
  return this->_is_finished_reading_from_net && this->_is_finished_writing_to_net;
}

void
QUICStream::set_io_adapter(QUICStreamAdapter *adapter)
{
  this->_adapter = adapter;
  this->_on_adapter_updated();
}
