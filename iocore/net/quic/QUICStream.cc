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
#include "QUICStreamManager.h"
#include "QUICDebugNames.h"
#include "QUICConfig.h"

#define DebugQUICStream(fmt, ...)                                                                                       \
  Debug("quic_stream", "[%" PRIx64 "] [%" PRIx32 "] [%s] " fmt, static_cast<uint64_t>(this->_connection_id), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)
#define DebugQUICStreamFC(fmt, ...)                                                                                        \
  Debug("quic_flow_ctrl", "[%" PRIx64 "] [%" PRIx32 "] [%s] " fmt, static_cast<uint64_t>(this->_connection_id), this->_id, \
        QUICDebugNames::stream_state(this->_state), ##__VA_ARGS__)

void
QUICStream::init(QUICFrameTransmitter *tx, QUICConnectionId cid, QUICStreamId sid, uint64_t recv_max_stream_data,
                 uint64_t send_max_stream_data)
{
  this->mutex                   = new_ProxyMutex();
  this->_tx                     = tx;
  this->_connection_id          = cid;
  this->_id                     = sid;
  this->_remote_flow_controller = new QUICRemoteStreamFlowController(send_max_stream_data, _tx, _id);
  this->_local_flow_controller  = new QUICLocalStreamFlowController(recv_max_stream_data, _tx, _id);
  this->init_flow_control_params(recv_max_stream_data, send_max_stream_data);

  DebugQUICStream("Initialized");
}

void
QUICStream::start()
{
  SET_HANDLER(&QUICStream::main_event_handler);
}

void
QUICStream::init_flow_control_params(uint32_t recv_max_stream_data, uint32_t send_max_stream_data)
{
  this->_flow_control_buffer_size = recv_max_stream_data;
  this->_local_flow_controller->forward_limit(recv_max_stream_data);
  this->_remote_flow_controller->forward_limit(send_max_stream_data);
  DebugQUICStreamFC("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
                    this->_local_flow_controller->current_limit());
  DebugQUICStreamFC("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                    this->_remote_flow_controller->current_limit());
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
QUICStream::main_event_handler(int event, void *data)
{
  DebugQUICStream("%s", QUICDebugNames::vc_event(event));
  QUICErrorUPtr error = std::unique_ptr<QUICError>(new QUICNoError());

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    this->_signal_read_event(true);
    this->_read_event = nullptr;

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    error = this->_send();
    this->_signal_write_event(true);
    this->_write_event = nullptr;

    break;
  }
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_INACTIVITY_TIMEOUT:
  case VC_EVENT_ACTIVE_TIMEOUT: {
    ink_assert(false);
    break;
  }
  default:
    DebugQUICStream("unknown event");
    ink_assert(false);
  }

  if (error->cls != QUICErrorClass::NONE) {
    DebugQUICStream("QUICError: %s (%u), %s (0x%x)", QUICDebugNames::error_class(error->cls), static_cast<unsigned int>(error->cls),
                    QUICDebugNames::error_code(error->code), static_cast<unsigned int>(error->code));
    if (dynamic_cast<QUICStreamError *>(error.get()) != nullptr) {
      // Stream Error
      QUICStreamErrorUPtr serror = QUICStreamErrorUPtr(static_cast<QUICStreamError *>(error.get()));
      this->reset(std::move(serror));
    } else {
      // Connection Error
      // TODO Close connection (Does this really happen?)
    }
  }

  return EVENT_CONT;
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

  // TODO: If read function is added, call reenable here
  this->_read_vio.reenable();

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

  this->_write_vio.reenable();

  return &this->_write_vio;
}

void
QUICStream::do_io_close(int lerrno)
{
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
}

void
QUICStream::reenable(VIO *vio)
{
  if (vio->op == VIO::READ) {
    SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

    if (this->_read_vio.nbytes > 0) {
      int event = (this->_read_vio.ntodo() == 0) ? VC_EVENT_READ_COMPLETE : VC_EVENT_READ_READY;

      if (this->_read_event == nullptr) {
        this->_read_event = this_ethread()->schedule_imm_local(this, event);
      }
    }
  } else if (vio->op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

    if (this->_write_vio.nbytes > 0) {
      int event = (this->_write_vio.ntodo() == 0) ? VC_EVENT_WRITE_COMPLETE : VC_EVENT_WRITE_READY;

      if (this->_write_event == nullptr) {
        this->_write_event = this_ethread()->schedule_imm_local(this, event);
      }
    }
  }
}

/**
 * @brief Signal event to this->_read_vio._cont
 * @param (call_update)  If true, safe to call vio handler directly.
 *   Or called from do_io_read. Still setting things up. Send event to handle this after the dust settles
 */
void
QUICStream::_signal_read_event(bool direct)
{
  int event          = (this->_read_vio.ntodo() == 0) ? VC_EVENT_READ_COMPLETE : VC_EVENT_READ_READY;
  Continuation *cont = this->_read_vio._cont;

  if (direct) {
    Event *e          = eventAllocator.alloc();
    e->callback_event = event;
    e->cookie         = this;
    e->init(cont, 0, 0);

    cont->handleEvent(event, e);
  } else {
    this_ethread()->schedule_imm(cont, event, this);
  }
}

/**
 * @brief Signal event to this->_write_vio._cont
 * @param (call_update)  If true, safe to call vio handler directly.
 *   Or called from do_io_write. Still setting things up. Send event to handle this after the dust settles
 */
void
QUICStream::_signal_write_event(bool direct)
{
  int event          = (this->_write_vio.ntodo() == 0) ? VC_EVENT_WRITE_COMPLETE : VC_EVENT_WRITE_READY;
  Continuation *cont = this->_write_vio._cont;

  if (direct) {
    Event *e          = eventAllocator.alloc();
    e->callback_event = event;
    e->cookie         = this;
    e->init(cont, 0, 0);

    cont->handleEvent(event, e);
  } else {
    this_ethread()->schedule_imm(cont, event, this);
  }
}

void
QUICStream::_write_to_read_vio(const std::shared_ptr<const QUICStreamFrame> &frame)
{
  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int bytes_added = this->_read_vio.buffer.writer()->write(frame->data(), frame->data_length());
  this->_read_vio.nbytes += bytes_added;
  // frame->offset() + frame->data_length() == this->_recv_offset
  this->_local_flow_controller->forward_limit(frame->offset() + frame->data_length() + this->_flow_control_buffer_size);
  DebugQUICStreamFC("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
                    this->_local_flow_controller->current_limit());

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
    return QUICErrorUPtr(new QUICStreamError(this, QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::STREAM_STATE_ERROR));
  }

  // Flow Control - Even if it's allowed to receive on the state, it may exceed the limit
  QUICErrorUPtr error = this->_local_flow_controller->update(frame->offset() + frame->data_length());
  DebugQUICStreamFC("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
                    this->_local_flow_controller->current_limit());
  if (error->cls != QUICErrorClass::NONE) {
    return error;
  }

  error = this->_received_stream_frame_buffer.insert(frame);
  if (error->cls != QUICErrorClass::NONE) {
    this->_received_stream_frame_buffer.clear();
    return error;
  }

  auto new_frame = this->_received_stream_frame_buffer.pop();
  while (new_frame != nullptr) {
    this->_write_to_read_vio(new_frame);
    new_frame = this->_received_stream_frame_buffer.pop();
  }

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICMaxStreamDataFrame> frame)
{
  this->_remote_flow_controller->forward_limit(frame->maximum_stream_data());
  DebugQUICStreamFC("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                    this->_remote_flow_controller->current_limit());

  this->reenable(&this->_write_vio);

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICStream::recv(const std::shared_ptr<const QUICStreamBlockedFrame> frame)
{
  // STREAM_BLOCKED frames are for debugging. Nothing to do here.
  return QUICErrorUPtr(new QUICNoError());
}

/**
 * @brief Send STREAM DATA from _response_buffer
 */
QUICErrorUPtr
QUICStream::_send()
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  QUICErrorUPtr error = std::unique_ptr<QUICError>(new QUICNoError());

  IOBufferReader *reader = this->_write_vio.get_reader();
  int64_t bytes_avail    = reader->read_avail();
  int64_t total_len      = 0;
  uint32_t max_size      = this->_tx->maximum_stream_frame_data_size();

  while (total_len < bytes_avail) {
    int64_t data_len = reader->block_read_avail();
    int64_t len      = 0;
    bool fin         = false;

    int64_t credit = this->_remote_flow_controller->current_limit() - this->_remote_flow_controller->current_offset();
    if (credit != 0 && max_size > credit) {
      max_size = credit;
    }
    if (data_len > max_size) {
      len = max_size;
    } else {
      len = data_len;
      if (total_len + len >= bytes_avail) {
        fin = this->_fin;
      }
    }

    error = this->_remote_flow_controller->update(this->_send_offset + len);
    DebugQUICStreamFC("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                      this->_remote_flow_controller->current_limit());
    if (error->cls != QUICErrorClass::NONE) {
      break;
    }

    QUICStreamFrameUPtr frame = QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>(reader->start()), len,
                                                                      this->_id, this->_send_offset, fin);

    this->_send_offset += len;
    reader->consume(len);
    this->_write_vio.ndone += len;
    total_len += len;

    if (!this->_state.is_allowed_to_send(*frame)) {
      DebugQUICStream("Canceled sending %s frame due to the stream state", QUICDebugNames::frame_type(frame->type()));
      break;
    }
    this->_state.update_with_sent_frame(*frame);
    this->_tx->transmit_frame(std::move(frame));
  }

  return error;
}

void
QUICStream::reset(QUICStreamErrorUPtr error)
{
  this->_tx->transmit_frame(QUICFrameFactory::create_rst_stream_frame(std::move(error)));
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
  return this->_local_flow_controller->current_offset();
}

QUICOffset
QUICStream::largest_offset_sent()
{
  return this->_remote_flow_controller->current_offset();
}
