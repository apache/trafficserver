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

const static char *tag = "quic_stream_manager";

ClassAllocator<QUICStreamManager> quicStreamManagerAllocator("quicStreamManagerAllocator");
ClassAllocator<QUICStream> quicStreamAllocator("quicStreamAllocator");

QUICStreamManager::QUICStreamManager(QUICFrameTransmitter *tx, QUICApplicationMap *app_map) : _tx(tx), _app_map(app_map)
{
}

std::vector<QUICFrameType>
QUICStreamManager::interests()
{
  return {QUICFrameType::STREAM, QUICFrameType::RST_STREAM, QUICFrameType::MAX_DATA, QUICFrameType::MAX_STREAM_DATA,
          QUICFrameType::BLOCKED};
}

void
QUICStreamManager::init_flow_control_params(std::shared_ptr<const QUICTransportParameters> local_tp,
                                            std::shared_ptr<const QUICTransportParameters> remote_tp)
{
  this->_local_tp  = local_tp;
  this->_remote_tp = remote_tp;

  // Connection level
  this->_recv_max_data = QUICMaximumData(local_tp->initial_max_data());
  this->_send_max_data = QUICMaximumData(remote_tp->initial_max_data());

  // Setup a stream for Handshake
  QUICStream *stream = this->_find_stream(STREAM_ID_FOR_HANDSHAKE);
  stream->init_flow_control_params(local_tp->initial_max_stream_data(), remote_tp->initial_max_stream_data());
}

QUICError
QUICStreamManager::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICError error = QUICError(QUICErrorClass::NONE);

  switch (frame->type()) {
  case QUICFrameType::MAX_DATA: {
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICMaxDataFrame>(frame));
    break;
  }
  case QUICFrameType::BLOCKED: {
    this->slide_recv_max_data();
    break;
  }
  case QUICFrameType::MAX_STREAM_DATA: {
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICMaxStreamDataFrame>(frame));
    break;
  }
  case QUICFrameType::STREAM_BLOCKED: {
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICStreamBlockedFrame>(frame));
    break;
  }
  case QUICFrameType::STREAM:
    error = this->_handle_frame(std::dynamic_pointer_cast<const QUICStreamFrame>(frame));
    break;
  default:
    Debug(tag, "Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  return error;
}

QUICError
QUICStreamManager::_handle_frame(std::shared_ptr<const QUICMaxDataFrame> frame)
{
  this->_send_max_data = frame->maximum_data();
  return QUICError(QUICErrorClass::NONE);
}

void
QUICStreamManager::slide_recv_max_data()
{
  // TODO: How much should this be increased?
  this->_recv_max_data += this->_local_tp->initial_max_data();
  this->send_frame(QUICFrameFactory::create_max_data_frame(this->_recv_max_data));
}

QUICError
QUICStreamManager::_handle_frame(std::shared_ptr<const QUICMaxStreamDataFrame> frame)
{
  QUICStream *stream = this->_find_stream(frame->stream_id());
  if (stream) {
    stream->recv(frame);
  } else {
    // TODO: connection error?
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStreamManager::_handle_frame(std::shared_ptr<const QUICStreamBlockedFrame> frame)
{
  QUICStream *stream = this->_find_stream(frame->stream_id());
  if (stream) {
    stream->recv(frame);
  } else {
    // TODO: connection error?
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStreamManager::_handle_frame(std::shared_ptr<const QUICStreamFrame> frame)
{
  QUICStream *stream           = this->_find_or_create_stream(frame->stream_id());
  QUICApplication *application = this->_app_map->get(frame->stream_id());

  if (!application->is_stream_set(stream)) {
    application->set_stream(stream);
  }

  QUICError error = stream->recv(frame);

  // FIXME: schedule VC_EVENT_READ_READY to application every single frame?
  // If application reading buffer continuously, do not schedule event.
  this_ethread()->schedule_imm(application, VC_EVENT_READ_READY, stream);

  return error;
}

/**
 * @brief Send stream frame
 */
void
QUICStreamManager::send_frame(std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame)
{
  // XXX The offset of sending frame is always largest offset by sending side
  if (frame->stream_id() != STREAM_ID_FOR_HANDSHAKE) {
    this->_send_total_offset += frame->size();
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

bool
QUICStreamManager::is_send_avail_more_than(uint64_t size)
{
  return this->_send_max_data > (this->_send_total_offset + size);
}

bool
QUICStreamManager::is_recv_avail_more_than(uint64_t size)
{
  return this->_recv_max_data > (this->_recv_total_offset + size);
}

void
QUICStreamManager::add_recv_total_offset(uint64_t delta)
{
  this->_recv_total_offset += delta;
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
    stream = THREAD_ALLOC_INIT(quicStreamAllocator, this_ethread());
    if (stream_id == STREAM_ID_FOR_HANDSHAKE) {
      // XXX rece/send max_stream_data are going to be set by init_flow_control_params()
      stream->init(this, this->_tx, stream_id);
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
QUICStreamManager::recv_max_data() const
{
  return this->_recv_max_data;
}

uint64_t
QUICStreamManager::send_max_data() const
{
  return this->_send_max_data;
}

uint64_t
QUICStreamManager::recv_total_offset() const
{
  return this->_recv_total_offset;
}

uint64_t
QUICStreamManager::send_total_offset() const
{
  return this->_send_total_offset;
}
