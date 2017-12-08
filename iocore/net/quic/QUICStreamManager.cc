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

#include "QUICStreamManager.h"

#include "QUICApplication.h"
#include "QUICTransportParameters.h"

static constexpr char tag[] = "quic_stream_manager";

ClassAllocator<QUICStreamManager> quicStreamManagerAllocator("quicStreamManagerAllocator");
ClassAllocator<QUICStream> quicStreamAllocator("quicStreamAllocator");

QUICStreamManager::QUICStreamManager(QUICConnectionId cid, QUICFrameTransmitter *tx, QUICApplicationMap *app_map)
  : _connection_id(cid), _tx(tx), _app_map(app_map)
{
}

std::vector<QUICFrameType>
QUICStreamManager::interests()
{
  return {
    QUICFrameType::STREAM, QUICFrameType::RST_STREAM, QUICFrameType::MAX_STREAM_DATA, QUICFrameType::MAX_STREAM_ID,
  };
}

void
QUICStreamManager::init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                            const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
  this->_local_tp  = local_tp;
  this->_remote_tp = remote_tp;

  // Setup a stream for Handshake
  QUICStream *stream = this->_find_stream(STREAM_ID_FOR_HANDSHAKE);
  if (stream) {
    uint32_t local_initial_max_stream_data  = 0;
    uint32_t remote_initial_max_stream_data = 0;
    if (this->_local_tp) {
      local_initial_max_stream_data = local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA);
    }
    if (this->_remote_tp) {
      remote_initial_max_stream_data = remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA);
    }
    stream->init_flow_control_params(local_initial_max_stream_data, remote_initial_max_stream_data);
  }

  if (this->_local_tp) {
    this->_local_maximum_stream_id = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_ID);
  }
  if (this->_remote_tp) {
    this->_remote_maximum_stream_id = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_ID);
  }
}

void
QUICStreamManager::set_max_stream_id(QUICStreamId id)
{
  if (this->_local_maximum_stream_id <= id) {
    this->_local_maximum_stream_id = id;
  }
}

QUICErrorUPtr
QUICStreamManager::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::MAX_STREAM_DATA:
    error = this->_handle_frame(std::static_pointer_cast<const QUICMaxStreamDataFrame>(frame));
    break;
  case QUICFrameType::STREAM_BLOCKED:
    // STREAM_BLOCKED frame is for debugging. Just propagate to streams
    error = this->_handle_frame(std::static_pointer_cast<const QUICStreamBlockedFrame>(frame));
    break;
  case QUICFrameType::STREAM:
    error = this->_handle_frame(std::static_pointer_cast<const QUICStreamFrame>(frame));
    break;
  case QUICFrameType::RST_STREAM:
    error = this->_handle_frame(std::static_pointer_cast<const QUICRstStreamFrame>(frame));
    break;
  case QUICFrameType::MAX_STREAM_ID:
    error = this->_handle_frame(std::static_pointer_cast<const QUICMaxStreamIdFrame>(frame));
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICErrorUPtr
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICMaxStreamDataFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::STREAM_ID_ERROR));
  }
}

QUICErrorUPtr
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICStreamBlockedFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::STREAM_ID_ERROR));
  }
}

QUICErrorUPtr
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICStreamFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (!stream) {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::STREAM_ID_ERROR));
  }

  QUICApplication *application = this->_app_map->get(frame->stream_id());

  if (!application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  size_t nbytes_to_read = stream->nbytes_to_read();
  QUICErrorUPtr error   = stream->recv(frame);
  // Prevent trigger read events multiple times
  if (nbytes_to_read == 0) {
    this_ethread()->schedule_imm(application, VC_EVENT_READ_READY, stream);
  }

  return error;
}

QUICErrorUPtr
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICRstStreamFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    // TODO Reset the stream
    return QUICErrorUPtr(new QUICNoError());
  } else {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::STREAM_ID_ERROR));
  }
}

QUICErrorUPtr
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICMaxStreamIdFrame> &frame)
{
  this->_remote_maximum_stream_id = frame->maximum_stream_id();
  return QUICErrorUPtr(new QUICNoError());
}

QUICStream *
QUICStreamManager::_find_stream(QUICStreamId id)
{
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->id() == id) {
      return s;
    }
  }
  return nullptr;
}

QUICStream *
QUICStreamManager::_find_or_create_stream(QUICStreamId stream_id)
{
  QUICStream *stream = this->_find_stream(stream_id);
  if (!stream) {
    QUICStreamType type = QUICTypeUtil::detect_stream_type(stream_id);
    if (type == QUICStreamType::CLIENT && stream_id > this->_local_maximum_stream_id && this->_local_maximum_stream_id != 0) {
      return nullptr;
    } else if (type == QUICStreamType::SERVER && stream_id > this->_remote_maximum_stream_id &&
               this->_remote_maximum_stream_id != 0) {
      return nullptr;
    }
    // TODO Free the stream somewhere
    stream = new (THREAD_ALLOC(quicStreamAllocator, this_ethread())) QUICStream();
    if (stream_id == STREAM_ID_FOR_HANDSHAKE) {
      // XXX rece/send max_stream_data are going to be set by init_flow_control_params()
      stream->init(this->_tx, this->_connection_id, stream_id,
                   this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA));
    } else {
      // TODO: check local_tp and remote_tp is initialized
      stream->init(this->_tx, this->_connection_id, stream_id,
                   this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA),
                   this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA));
    }

    stream->start();

    this->stream_list.push(stream);
  }
  return stream;
}

uint64_t
QUICStreamManager::total_offset_received() const
{
  uint64_t total_offset_received = 0;

  // FIXME Iterating all (open + closed) streams is expensive
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->id() != 0) {
      total_offset_received += s->largest_offset_received();
    }
  }
  return total_offset_received;
}

uint64_t
QUICStreamManager::total_offset_sent() const
{
  return this->_total_offset_sent;
}

void
QUICStreamManager::add_total_offset_sent(uint32_t sent_byte)
{
  // FIXME: use atomic increment
  this->_total_offset_sent += sent_byte;
}

uint32_t
QUICStreamManager::stream_count() const
{
  uint32_t count = 0;
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    ++count;
  }
  return count;
}

void
QUICStreamManager::set_default_application(QUICApplication *app)
{
  this->_app_map->set_default(app);
}
