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
#include "QUICConnection.h"

static constexpr char tag[] = "quic_stream_manager";

ClassAllocator<QUICStreamManager> quicStreamManagerAllocator("quicStreamManagerAllocator");
ClassAllocator<QUICStream> quicStreamAllocator("quicStreamAllocator");

QUICStreamManager::QUICStreamManager(QUICFrameTransmitter *tx, QUICApplicationMap *app_map) : _tx(tx), _app_map(app_map)
{
}

std::vector<QUICFrameType>
QUICStreamManager::interests()
{
  return {
    QUICFrameType::STREAM, QUICFrameType::RST_STREAM, QUICFrameType::MAX_STREAM_DATA,
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
      local_initial_max_stream_data = local_tp->initial_max_stream_data();
    }
    if (this->_remote_tp) {
      remote_initial_max_stream_data = remote_tp->initial_max_stream_data();
    }
    stream->init_flow_control_params(local_initial_max_stream_data, remote_initial_max_stream_data);
  }
}

QUICError
QUICStreamManager::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICError error = QUICError(QUICErrorClass::NONE);

  switch (frame->type()) {
  case QUICFrameType::MAX_STREAM_DATA: {
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICMaxStreamDataFrame>(frame));
    break;
  }
  case QUICFrameType::STREAM_BLOCKED: {
    // STREAM_BLOCKED frame is for debugging. Just propagate to streams
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICStreamBlockedFrame>(frame));
    break;
  }
  case QUICFrameType::STREAM:
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICStreamFrame>(frame));
    break;
  case QUICFrameType::RST_STREAM:
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICRstStreamFrame>(frame));
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICError
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICMaxStreamDataFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    // TODO: connection error?
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICStreamBlockedFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    return stream->recv(frame);
  } else {
    // TODO: connection error?
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICStreamFrame> &frame)
{
  QUICStream *stream           = this->_find_or_create_stream(frame->stream_id());
  QUICApplication *application = this->_app_map->get(frame->stream_id());

  if (!application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  size_t nbytes_to_read = stream->nbytes_to_read();
  QUICError error       = stream->recv(frame);
  // Prevent trigger read events multiple times
  if (nbytes_to_read == 0) {
    this_ethread()->schedule_imm(application, VC_EVENT_READ_READY, stream);
  }

  return error;
}

QUICError
QUICStreamManager::_handle_frame(const std::shared_ptr<const QUICRstStreamFrame> &frame)
{
  QUICStream *stream = this->_find_or_create_stream(frame->stream_id());
  if (stream) {
    // TODO Reset the stream
  } else {
    // TODO: connection error?
  }

  return QUICError(QUICErrorClass::NONE);
}

/**
 * @brief Send stream frame
 */
void
QUICStreamManager::send_frame(std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame)
{
  if (frame->stream_id() != STREAM_ID_FOR_HANDSHAKE) {
  }
  this->_tx->transmit_frame(std::move(frame));

  return;
}

/**
 * @brief Send frame
 */
void
QUICStreamManager::send_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame)
{
  this->_tx->transmit_frame(std::move(frame));

  return;
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
    // TODO Free the stream somewhere
    stream = new (THREAD_ALLOC(quicStreamAllocator, this_ethread())) QUICStream();
    if (stream_id == STREAM_ID_FOR_HANDSHAKE) {
      // XXX rece/send max_stream_data are going to be set by init_flow_control_params()
      stream->init(this, this->_tx, stream_id, this->_local_tp->initial_max_stream_data());
    } else {
      const QUICTransportParameters &local_tp  = *this->_local_tp;
      const QUICTransportParameters &remote_tp = *this->_remote_tp;

      // TODO: check local_tp and remote_tp is initialized
      stream->init(this, this->_tx, stream_id, local_tp.initial_max_stream_data(), remote_tp.initial_max_stream_data());
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
      total_offset_received += s->largest_offset_received() / 1024;
    }
  }
  return total_offset_received;
}

uint64_t
QUICStreamManager::total_offset_sent() const
{
  uint64_t total_offset_sent = 0;

  // FIXME Iterating all (open + closed) streams is expensive
  for (QUICStream *s = this->stream_list.head; s; s = s->link.next) {
    if (s->id() != 0) {
      total_offset_sent += s->largest_offset_sent() / 1024;
    }
  }
  return total_offset_sent;
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
