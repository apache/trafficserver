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
#include "QUICConfig.h"

static constexpr char tag[] = "quic_stream_manager";

ClassAllocator<QUICStreamManager> quicStreamManagerAllocator("quicStreamManagerAllocator");
ClassAllocator<QUICStream> quicStreamAllocator("quicStreamAllocator");

QUICStreamManager::QUICStreamManager(QUICConnectionInfoProvider *info, QUICRTTProvider *rtt_provider, QUICApplicationMap *app_map)
  : _info(info), _rtt_provider(rtt_provider), _app_map(app_map)
{
  if (this->_info->direction() == NET_VCONNECTION_OUT) {
    this->_next_stream_id_bidi = static_cast<uint32_t>(QUICStreamType::CLIENT_BIDI);
    this->_next_stream_id_uni  = static_cast<uint32_t>(QUICStreamType::CLIENT_UNI);
  } else {
    this->_next_stream_id_bidi = static_cast<uint32_t>(QUICStreamType::SERVER_BIDI);
    this->_next_stream_id_uni  = static_cast<uint32_t>(QUICStreamType::SERVER_UNI);
  }
}

std::vector<QUICFrameType>
QUICStreamManager::interests()
{
  return {
    QUICFrameType::STREAM,          QUICFrameType::RST_STREAM,    QUICFrameType::STOP_SENDING,
    QUICFrameType::MAX_STREAM_DATA, QUICFrameType::MAX_STREAM_ID,
  };
}

void
QUICStreamManager::init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                            const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
  this->_local_tp  = local_tp;
  this->_remote_tp = remote_tp;

  if (this->_local_tp) {
    this->_local_maximum_stream_id_bidi = this->_local_tp->getAsUInt16(QUICTransportParameterId::INITIAL_MAX_BIDI_STREAMS);
    this->_local_maximum_stream_id_uni  = this->_local_tp->getAsUInt16(QUICTransportParameterId::INITIAL_MAX_UNI_STREAMS);
  }
  if (this->_remote_tp) {
    this->_remote_maximum_stream_id_bidi = this->_remote_tp->getAsUInt16(QUICTransportParameterId::INITIAL_MAX_BIDI_STREAMS);
    this->_remote_maximum_stream_id_uni  = this->_remote_tp->getAsUInt16(QUICTransportParameterId::INITIAL_MAX_UNI_STREAMS);
  }
}

void
QUICStreamManager::set_max_stream_id(QUICStreamId id)
{
  QUICStreamType type = QUICTypeUtil::detect_stream_type(id);
  if (type == QUICStreamType::SERVER_BIDI || type == QUICStreamType::CLIENT_BIDI) {
    if (this->_local_maximum_stream_id_bidi <= id) {
      this->_local_maximum_stream_id_bidi = id;
    }
  } else {
    if (this->_local_maximum_stream_id_uni <= id) {
      this->_local_maximum_stream_id_uni = id;
    }
  }
}

QUICConnectionErrorUPtr
QUICStreamManager::create_stream(QUICStreamId stream_id)
{
  // TODO: check stream_id
  QUICConnectionErrorUPtr error = nullptr;
  QUICStream *stream            = this->_find_or_create_stream(stream_id);
  if (!stream) {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }

  QUICApplication *application = this->_app_map->get(stream_id);

  if (!application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  return error;
}

QUICConnectionErrorUPtr
QUICStreamManager::create_uni_stream(QUICStreamId &new_stream_id)
{
  QUICConnectionErrorUPtr error = this->create_stream(this->_next_stream_id_uni);
  if (error == nullptr) {
    new_stream_id = this->_next_stream_id_uni;
    this->_next_stream_id_uni += 2;
  }

  return error;
}

QUICConnectionErrorUPtr
QUICStreamManager::create_bidi_stream(QUICStreamId &new_stream_id)
{
  QUICConnectionErrorUPtr error = this->create_stream(this->_next_stream_id_bidi);
  if (error == nullptr) {
    new_stream_id = this->_next_stream_id_bidi;
    this->_next_stream_id_bidi += 2;
  }

  return error;
}

void
QUICStreamManager::reset_stream(QUICStreamId stream_id, QUICStreamErrorUPtr error)
{
  auto stream = this->_find_stream(stream_id);
  stream->reset(std::move(error));
}

QUICConnectionErrorUPtr
QUICStreamManager::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  switch (frame.type()) {
  case QUICFrameType::MAX_STREAM_DATA:
    error = this->_handle_frame(static_cast<const QUICMaxStreamDataFrame &>(frame));
    break;
  case QUICFrameType::STREAM_BLOCKED:
    // STREAM_BLOCKED frame is for debugging. Just propagate to streams
    error = this->_handle_frame(static_cast<const QUICStreamBlockedFrame &>(frame));
    break;
  case QUICFrameType::STREAM:
    error = this->_handle_frame(static_cast<const QUICStreamFrame &>(frame));
    break;
  case QUICFrameType::STOP_SENDING:
    error = this->_handle_frame(static_cast<const QUICStopSendingFrame &>(frame));
    break;
  case QUICFrameType::RST_STREAM:
    error = this->_handle_frame(static_cast<const QUICRstStreamFrame &>(frame));
    break;
  case QUICFrameType::MAX_STREAM_ID:
    error = this->_handle_frame(static_cast<const QUICMaxStreamIdFrame &>(frame));
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICMaxStreamDataFrame &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame.stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICStreamBlockedFrame &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame.stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICStreamFrame &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame.stream_id());
  if (!stream) {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }

  QUICApplication *application = this->_app_map->get(frame.stream_id());

  if (application && !application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  return stream->recv(frame);
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICRstStreamFrame &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame.stream_id());
  if (stream) {
    // TODO Reset the stream
    return nullptr;
  } else {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICStopSendingFrame &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame.stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::STREAM_ID_ERROR);
  }
}

QUICConnectionErrorUPtr
QUICStreamManager::_handle_frame(const QUICMaxStreamIdFrame &frame)
{
  QUICStreamType type = QUICTypeUtil::detect_stream_type(frame.maximum_stream_id());
  if (type == QUICStreamType::SERVER_BIDI || type == QUICStreamType::CLIENT_BIDI) {
    this->_remote_maximum_stream_id_bidi = frame.maximum_stream_id();
  } else {
    this->_remote_maximum_stream_id_uni = frame.maximum_stream_id();
  }
  return nullptr;
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
    if (!this->_local_tp) {
      return nullptr;
    }

    ink_assert(this->_local_tp);
    ink_assert(this->_remote_tp);

    uint64_t local_max_stream_data  = 0;
    uint64_t remote_max_stream_data = 0;

    switch (QUICTypeUtil::detect_stream_type(stream_id)) {
    case QUICStreamType::CLIENT_BIDI:
      if (stream_id > this->_local_maximum_stream_id_bidi && this->_local_maximum_stream_id_bidi != 0) {
        return nullptr;
      }

      if (this->_info->direction() == NET_VCONNECTION_OUT) {
        // client
        local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
        remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
      } else {
        // server
        local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
        remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
      }

      break;
    case QUICStreamType::CLIENT_UNI:
      if (stream_id > this->_local_maximum_stream_id_uni && this->_local_maximum_stream_id_uni != 0) {
        return nullptr;
      }

      local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI);
      remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI);

      break;
    case QUICStreamType::SERVER_BIDI:
      if (stream_id > this->_remote_maximum_stream_id_bidi && this->_remote_maximum_stream_id_bidi != 0) {
        return nullptr;
      }

      if (this->_info->direction() == NET_VCONNECTION_OUT) {
        // client
        local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
        remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
      } else {
        // server
        local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL);
        remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE);
      }

      break;
    case QUICStreamType::SERVER_UNI:
      if (stream_id > this->_remote_maximum_stream_id_uni && this->_remote_maximum_stream_id_uni != 0) {
        return nullptr;
      }

      local_max_stream_data  = this->_local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI);
      remote_max_stream_data = this->_remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI);

      break;
    }

    // TODO Free the stream somewhere
    stream = THREAD_ALLOC(quicStreamAllocator, this_ethread());
    new (stream) QUICStream(this->_rtt_provider, this->_info, stream_id, local_max_stream_data, remote_max_stream_data);

    this->stream_list.push(stream);
  }

  return stream;
}

uint64_t
QUICStreamManager::total_reordered_bytes() const
{
  uint64_t total_bytes = 0;

  // FIXME Iterating all (open + closed) streams is expensive
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    total_bytes += s->reordered_bytes();
  }
  return total_bytes;
}

uint64_t
QUICStreamManager::total_offset_received() const
{
  uint64_t total_offset_received = 0;

  // FIXME Iterating all (open + closed) streams is expensive
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    total_offset_received += s->largest_offset_received();
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

bool
QUICStreamManager::will_generate_frame(QUICEncryptionLevel level)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  // workaround fix until support 0-RTT on client
  if (level == QUICEncryptionLevel::ZERO_RTT) {
    return false;
  }

  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->will_generate_frame(level)) {
      return true;
    }
  }

  return false;
}

QUICFrameUPtr
QUICStreamManager::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  // FIXME We should pick a stream based on priority
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  // workaround fix until support 0-RTT on client
  if (level == QUICEncryptionLevel::ZERO_RTT) {
    return frame;
  }

  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    frame = s->generate_frame(level, connection_credit, maximum_frame_size);
    if (frame) {
      break;
    }
  }

  if (frame != nullptr && frame->type() == QUICFrameType::STREAM) {
    this->add_total_offset_sent(static_cast<QUICStreamFrame *>(frame.get())->data_length());
  }

  return frame;
}
