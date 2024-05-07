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

#include "iocore/net/quic/QUICStreamManager.h"

#include "iocore/net/quic/QUICApplication.h"
#include "iocore/net/quic/QUICTransportParameters.h"
#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICStreamManager.h"

QUICStreamManager::QUICStreamManager(QUICContext *context, QUICApplicationMap *app_map) : _context(context), _app_map(app_map) {}

QUICStreamManager::~QUICStreamManager() {}

void
QUICStreamManager::set_default_application(QUICApplication *app)
{
  this->_app_map->set_default(app);
}

void
QUICStreamManager::init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                            const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
}

void
QUICStreamManager::set_max_streams_bidi(uint64_t max_streams)
{
}

void
QUICStreamManager::set_max_streams_uni(uint64_t max_streams)
{
}

uint64_t
QUICStreamManager::total_reordered_bytes() const
{
  return 0;
}

uint64_t
QUICStreamManager::total_offset_received() const
{
  return 0;
}

uint64_t
QUICStreamManager::total_offset_sent() const
{
  return 0;
}

uint32_t
QUICStreamManager::stream_count() const
{
  return 0;
}

QUICStream *
QUICStreamManager::find_stream(QUICStreamId stream_id)
{
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->id() == stream_id) {
      return s;
    }
  }
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamManager::create_stream(QUICStreamId stream_id)
{
  QUICStream *stream = new QUICStream(this->_context->connection_info(), stream_id);
  this->stream_list.push(stream);

  QUICApplication *application = this->_app_map->get(stream_id);
  application->on_stream_open(*stream);
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamManager::create_uni_stream(QUICStreamId new_stream_id)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamManager::create_bidi_stream(QUICStreamId new_stream_id)
{
  return nullptr;
}

QUICConnectionErrorUPtr
QUICStreamManager::delete_stream(QUICStreamId stream_id)
{
  QUICStream *stream = static_cast<QUICStream *>(this->find_stream(stream_id));

  QUICApplication *application = this->_app_map->get(stream_id);
  application->on_stream_close(*stream);

  stream_list.remove(stream);

  delete stream;

  return nullptr;
}

void
QUICStreamManager::reset_stream(QUICStreamId stream_id, QUICStreamErrorUPtr error)
{
}

void
QUICStreamManager::on_stream_state_close(const QUICStream *stream)
{
}
