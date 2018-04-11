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

#include "I_Event.h"
#include "P_VConnection.h"
#include "QUICStreamManager.h"
#include "QUICDebugNames.h"

#define QUICStreamDebug(fmt, ...)                                                                                       \
  Debug("quic_stream", "[%" PRIx64 "] [%" PRIx64 "] [%s] " fmt, static_cast<uint64_t>(this->_connection_id), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)
#define QUICStreamFCDebug(fmt, ...)                                                                                        \
  Debug("quic_flow_ctrl", "[%" PRIx64 "] [%" PRIx64 "] [%s] " fmt, static_cast<uint64_t>(this->_connection_id), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)

QUICStream::QUICStream(QUICRTTProvider *rtt_provider, QUICConnectionId cid, QUICStreamId sid, uint64_t recv_max_stream_data,
                       uint64_t send_max_stream_data)
  : VConnection(nullptr),
    _connection_id(cid),
    _id(sid),
    _remote_flow_controller(send_max_stream_data, _id),
    _local_flow_controller(rtt_provider, recv_max_stream_data, _id),
    _received_stream_frame_buffer(this)
{
  SET_HANDLER(&QUICStream::state_stream_open);
  mutex = new_ProxyMutex();

  this->init_flow_control_params(recv_max_stream_data, send_max_stream_data);
}

QUICStream::~QUICStream()
{
  if (this->_read_event) {
    this->_read_event->cancel();
    this->_read_event = nullptr;
  }

  if (this->_write_event) {
    this->_write_event->cancel();
    this->_write_event = nullptr;
  }
}

void
QUICStream::init_flow_control_params(uint64_t recv_max_stream_data, uint64_t send_max_stream_data)
{
  this->_flow_control_buffer_size = recv_max_stream_data;
  this->_local_flow_controller.forward_limit(recv_max_stream_data);
  this->_remote_flow_controller.forward_limit(send_max_stream_data);
  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());
}

QUICStreamId
QUICStream::id() const
{
  return this->_id;
}

QUICOffset
QUICStream::final_offset()
{
  // TODO Return final offset
  return 0;
}

int
QUICStream::state_stream_open(int event, void *data)
{
  QUICStreamDebug("%s (%d)", get_vc_event_name(event), event);
  QUICErrorUPtr error = std::unique_ptr<QUICError>(new QUICNoError());

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    int64_t len = this->_process_read_vio();
    if (len > 0) {
      this->_signal_read_event();
    }

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    int64_t len = this->_process_write_vio();
    if (len > 0) {
      this->_signal_write_event();
    }

    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    // TODO
    ink_assert(false);
    break;
  }
  default:
    QUICStreamDebug("unknown event");
    ink_assert(false);
  }

  if (error->cls != QUICErrorClass::NONE) {
    if (error->cls == QUICErrorClass::TRANSPORT) {
      QUICStreamDebug("QUICError: %s (%u), %s (0x%x)", QUICDebugNames::error_class(error->cls),
                      static_cast<unsigned int>(error->cls), QUICDebugNames::error_code(error->trans_error_code),
                      static_cast<unsigned int>(error->trans_error_code));
    } else {
      QUICStreamDebug("QUICError: %s (%u), APPLICATION ERROR (0x%x)", QUICDebugNames::error_class(error->cls),
                      static_cast<unsigned int>(error->cls), static_cast<unsigned int>(error->app_error_code));
    }
    if (dynamic_cast<QUICStreamError *>(error.get()) != nullptr) {
      // Stream Error
      QUICStreamErrorUPtr serror = QUICStreamErrorUPtr(static_cast<QUICStreamError *>(error.get()));
      this->reset(std::move(serror));
    } else {
      // Connection Error
      // TODO Close connection (Does this really happen?)
    }
  }

  return EVENT_DONE;
}

int
QUICStream::state_stream_closed(int event, void *data)
{
  QUICStreamDebug("%s (%d)", get_vc_event_name(event), event);

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    // ignore
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    // ignore
    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    // TODO
    ink_assert(false);
    break;
  }
  default:
    ink_assert(false);
  }

  return EVENT_DONE;
}

VIO *
QUICStream::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (buf) {
    this->_read_vio.buffer.writer_for(buf);
  } else {
    this->_read_vio.buffer.clear();
  }

  this->_read_vio.mutex     = c ? c->mutex : this->mutex;
  this->_read_vio._cont     = c;
  this->_read_vio.nbytes    = nbytes;
  this->_read_vio.ndone     = 0;
  this->_read_vio.vc_server = this;
  this->_read_vio.op        = VIO::READ;

  this->_process_read_vio();
  this->_send_tracked_event(this->_read_event, VC_EVENT_READ_READY, &this->_read_vio);

  return &this->_read_vio;
}

VIO *
QUICStream::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  if (buf) {
    this->_write_vio.buffer.reader_for(buf);
  } else {
    this->_write_vio.buffer.clear();
  }

  this->_write_vio.mutex     = c ? c->mutex : this->mutex;
  this->_write_vio._cont     = c;
  this->_write_vio.nbytes    = nbytes;
  this->_write_vio.ndone     = 0;
  this->_write_vio.vc_server = this;
  this->_write_vio.op        = VIO::WRITE;

  this->_process_write_vio();
  this->_send_tracked_event(this->_write_event, VC_EVENT_WRITE_READY, &this->_write_vio);

  return &this->_write_vio;
}

void
QUICStream::do_io_close(int lerrno)
{
  SET_HANDLER(&QUICStream::state_stream_closed);

  this->_read_vio.buffer.clear();
  this->_read_vio.nbytes = 0;
  this->_read_vio.op     = VIO::NONE;
  this->_read_vio._cont  = nullptr;

  this->_write_vio.buffer.clear();
  this->_write_vio.nbytes = 0;
  this->_write_vio.op     = VIO::NONE;
  this->_write_vio._cont  = nullptr;
}

void
QUICStream::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(false); // unimplemented yet
  return;
}

void
QUICStream::reenable(VIO *vio)
{
  if (vio->op == VIO::READ) {
    QUICStreamDebug("read_vio reenabled");

    int64_t len = this->_process_read_vio();
    if (len > 0) {
      this->_signal_read_event();
    }
  } else if (vio->op == VIO::WRITE) {
    QUICStreamDebug("write_vio reenabled");

    int64_t len = this->_process_write_vio();
    if (len > 0) {
      this->_signal_write_event();
    }
  }
}

void
QUICStream::set_read_vio_nbytes(int64_t nbytes)
{
  this->_read_vio.nbytes = nbytes;
}

void
QUICStream::set_write_vio_nbytes(int64_t nbytes)
{
  this->_write_vio.nbytes = nbytes;
}

void
QUICStream::_write_to_read_vio(const std::shared_ptr<const QUICStreamFrame> &frame)
{
  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int bytes_added = this->_read_vio.buffer.writer()->write(frame->data(), frame->data_length());
  this->_read_vio.nbytes += bytes_added;
  // frame->offset() + frame->data_length() == this->_recv_offset
  this->_local_flow_controller.forward_limit(frame->offset() + frame->data_length() + this->_flow_control_buffer_size);
  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());

  this->_state.update_with_received_frame(*frame);
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writting operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICStreamFrame> frame)
{
  ink_assert(_id == frame->stream_id());
  ink_assert(this->_read_vio.op == VIO::READ);

  // Check stream state - Do this first before accept the frame
  if (!this->_state.is_allowed_to_receive(*frame)) {
    return QUICErrorUPtr(new QUICStreamError(this, QUICTransErrorCode::STREAM_STATE_ERROR));
  }

  // Flow Control - Even if it's allowed to receive on the state, it may exceed the limit
  int ret = this->_local_flow_controller.update(frame->offset() + frame->data_length());
  QUICStreamFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller.current_offset(),
                    this->_local_flow_controller.current_limit());
  if (ret != 0) {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::FLOW_CONTROL_ERROR));
  }

  QUICErrorUPtr error = this->_received_stream_frame_buffer.insert(frame);
  if (error->cls != QUICErrorClass::NONE) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame = this->_received_stream_frame_buffer.pop();
  while (new_frame != nullptr) {
    this->_write_to_read_vio(new_frame);
    new_frame = this->_received_stream_frame_buffer.pop();
  }

  this->_signal_read_event();

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICMaxStreamDataFrame> frame)
{
  this->_remote_flow_controller.forward_limit(frame->maximum_stream_data());
  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());

  QUICStreamDebug("restart sending");
  int64_t len = this->_process_write_vio();
  if (len > 0) {
    this->_signal_write_event();
  }

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICStreamBlockedFrame> frame)
{
  // STREAM_BLOCKED frames are for debugging. Nothing to do here.
  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICStopSendingFrame> frame)
{
  this->_state.update_with_received_frame(*frame);
  this->_reset_reason = QUICStreamErrorUPtr(new QUICStreamError(this, QUIC_APP_ERROR_CODE_STOPPING));
  // We received and processed STOP_SENDING frame, so return NO_ERROR here
  return QUICErrorUPtr(new QUICNoError());
}

bool
QUICStream::will_generate_frame()
{
  return this->_write_vio.get_reader()->read_avail() > 0;
}

QUICFrameUPtr
QUICStream::generate_frame(uint16_t connection_credit, uint16_t maximum_frame_size)
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  QUICErrorUPtr error = std::unique_ptr<QUICError>(new QUICNoError());

  if (this->_reset_reason) {
    return QUICFrameFactory::create_rst_stream_frame(std::move(this->_reset_reason));
  }

  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  frame               = this->_local_flow_controller.generate_frame();
  if (frame) {
    return frame;
  }

  IOBufferReader *reader = this->_write_vio.get_reader();
  int64_t bytes_avail    = reader->read_avail();
  if (bytes_avail == 0) {
    return frame;
  }

  int64_t data_len = reader->block_read_avail();
  int64_t len      = 0;
  bool fin         = false;

  len = std::min(data_len, static_cast<int64_t>(
                             std::min(static_cast<uint64_t>(maximum_frame_size),
                                      std::min(this->_remote_flow_controller.credit(), static_cast<uint64_t>(connection_credit)))));
  if (len >= bytes_avail) {
    fin = this->_fin;
  }

  if (len > 0) {
    bool protection = this->_id != STREAM_ID_FOR_HANDSHAKE;
    frame           = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>(reader->start()), len, this->_id,
                                                  this->_send_offset, fin, protection);
    if (!this->_state.is_allowed_to_send(*frame)) {
      QUICStreamDebug("Canceled sending %s frame due to the stream state", QUICDebugNames::frame_type(frame->type()));
      return frame;
    }
  } else {
    len = bytes_avail;
  }

  int ret = this->_remote_flow_controller.update(this->_send_offset + len);
  // We cannot cancel sending the frame after updating the flow controller

  QUICStreamFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller.current_offset(),
                    this->_remote_flow_controller.current_limit());
  if (frame && ret == 0) {
    this->_send_offset += len;
    reader->consume(len);
    this->_write_vio.ndone += len;
    this->_signal_write_event();
    this->_state.update_with_sent_frame(*frame);
  } else if (ret != 0) {
    QUICStreamDebug("Flow Controller blocked sending a STREAM frame");
    frame = this->_remote_flow_controller.generate_frame();
  }
  return frame;
}

/**
 * Reset send/recv offset of stream. This is only for stream 0.
 */
void
QUICStream::reset_send_offset()
{
  if (this->_id == STREAM_ID_FOR_HANDSHAKE) {
    this->_send_offset = 0;
  }
}

void
QUICStream::reset_recv_offset()
{
  if (this->_id == STREAM_ID_FOR_HANDSHAKE) {
    this->_received_stream_frame_buffer.clear();
  }
}

/**
 * Replace existing event only if the new event is different than the inprogress event
 */
Event *
QUICStream::_send_tracked_event(Event *event, int send_event, VIO *vio)
{
  if (event != nullptr) {
    if (event->callback_event != send_event) {
      event->cancel();
      event = nullptr;
    }
  }

  if (event == nullptr) {
    event = this_ethread()->schedule_imm(this, send_event, vio);
  }

  return event;
}

/**
 * @brief Signal event to this->_read_vio._cont
 */
void
QUICStream::_signal_read_event()
{
  if (this->_read_vio._cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int event = this->_read_vio.ntodo() ? VC_EVENT_READ_READY : VC_EVENT_READ_COMPLETE;

  if (lock.is_locked()) {
    this->_read_vio._cont->handleEvent(event, &this->_read_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio._cont, event, &this->_read_vio);
  }

  QUICStreamDebug("%s (%d)", get_vc_event_name(event), event);
}

/**
 * @brief Signal event to this->_write_vio._cont
 */
void
QUICStream::_signal_write_event()
{
  if (this->_write_vio._cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return;
  }
  MUTEX_TRY_LOCK(lock, this->_write_vio.mutex, this_ethread());

  int event = this->_write_vio.ntodo() ? VC_EVENT_WRITE_READY : VC_EVENT_WRITE_COMPLETE;

  if (lock.is_locked()) {
    this->_write_vio._cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_write_vio._cont, event, &this->_write_vio);
  }

  QUICStreamDebug("%s (%d)", get_vc_event_name(event), event);
}

int64_t
QUICStream::_process_read_vio()
{
  if (this->_read_vio._cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return 0;
  }

  // Pass through. Read operation is done by QUICStream::recv(const std::shared_ptr<const QUICStreamFrame> frame)
  // TODO: 1. pop frame from _received_stream_frame_buffer
  //       2. write data to _read_vio

  return 0;
}

/**
 * @brief Send STREAM DATA from _response_buffer
 * @detail Call _signal_write_event() to indicate event upper layer
 */
int64_t
QUICStream::_process_write_vio()
{
  if (this->_write_vio._cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return 0;
  }

  return 0;
}

void
QUICStream::reset(QUICStreamErrorUPtr error)
{
  this->_reset_reason = std::move(error);
}

void
QUICStream::shutdown()
{
  this->_fin = true;
}

size_t
QUICStream::nbytes_to_read()
{
  return this->_read_vio.ntodo();
}

QUICOffset
QUICStream::largest_offset_received()
{
  return this->_local_flow_controller.current_offset();
}

QUICOffset
QUICStream::largest_offset_sent()
{
  return this->_remote_flow_controller.current_offset();
}
