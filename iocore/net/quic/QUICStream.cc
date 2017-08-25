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

const static char *tag = "quic_stream";

constexpr uint64_t MAX_DATA_HEADSPACE        = 10240; // in uints of octets
constexpr uint64_t MAX_STREAM_DATA_HEADSPACE = 1024;

void
QUICStream::init(QUICStreamManager *manager, QUICFrameTransmitter *tx, QUICStreamId id, uint64_t recv_max_stream_data,
                 uint64_t send_max_stream_data)
{
  this->_streamManager = manager;
  this->_tx            = tx;
  this->_id            = id;
  init_flow_control_params(recv_max_stream_data, send_max_stream_data);

  this->mutex = new_ProxyMutex();
}

void
QUICStream::start()
{
  SET_HANDLER(&QUICStream::main_event_handler);
}

void
QUICStream::init_flow_control_params(uint32_t recv_max_stream_data, uint32_t send_max_stream_data)
{
  this->_recv_max_stream_data        = recv_max_stream_data;
  this->_recv_max_stream_data_deleta = recv_max_stream_data;
  this->_send_max_stream_data        = send_max_stream_data;
}

uint32_t
QUICStream::id()
{
  return this->_id;
}

int
QUICStream::main_event_handler(int event, void *data)
{
  Debug(tag, "%s", QUICDebugNames::vc_event(event));

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    this->_signal_read_event(true);
    this->_read_event = nullptr;

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    this->_send();
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
    Debug(tag, "unknown event");
    ink_assert(false);
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
  this->_recv_offset += frame->data_length();
}

void
QUICStream::_reorder_data()
{
  auto frame = _request_stream_frame_buffer.find(this->_recv_offset);
  while (frame != this->_request_stream_frame_buffer.end()) {
    this->_write_to_read_vio(frame->second);
    frame = _request_stream_frame_buffer.find(this->_recv_offset);
  }
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writting operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
QUICError
QUICStream::recv(std::shared_ptr<const QUICStreamFrame> frame)
{
  ink_assert(_id == frame->stream_id());
  ink_assert(this->_read_vio.op == VIO::READ);

  if (!this->_state.is_allowed_to_receive(*frame)) {
    this->reset();
    return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
  }
  this->_state.update_with_received_frame(*frame);

  // Flow Control
  QUICError error = this->_recv_flow_control(frame->offset());
  if (error.cls != QUICErrorClass::NONE) {
    return error;
  }

  // Reordering
  if (this->_recv_offset > frame->offset()) {
    // Do nothing. Just ignore STREAM frame.
    return QUICError(QUICErrorClass::NONE);
  } else if (this->_recv_offset == frame->offset()) {
    this->_write_to_read_vio(frame);
    this->_reorder_data();
  } else {
    // NOTE: push fragments in _request_stream_frame_buffer temporally.
    // They will be reordered when missing data is filled and offset is matched.
    this->_request_stream_frame_buffer.insert(std::make_pair(frame->offset(), frame));
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStream::recv(const std::shared_ptr<const QUICMaxStreamDataFrame> &frame)
{
  this->_send_max_stream_data += frame->maximum_stream_data();
  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICStream::recv(const std::shared_ptr<const QUICStreamBlockedFrame> &frame)
{
  this->_slide_recv_max_stream_data();
  return QUICError(QUICErrorClass::NONE);
}

void
QUICStream::_slide_recv_max_stream_data()
{
  // TODO: How much should this be increased?
  this->_recv_max_stream_data += this->_recv_max_stream_data_deleta;
  this->_streamManager->send_frame(QUICFrameFactory::create_max_stream_data_frame(this->_id, this->_recv_max_stream_data));
}

QUICError
QUICStream::_recv_flow_control(uint64_t new_offset)
{
  if (this->_recv_largest_offset > new_offset) {
    return QUICError(QUICErrorClass::NONE);
  }

  uint64_t delta = new_offset - this->_recv_largest_offset;

  Debug("quic_flow_ctrl", "Con: %" PRIu64 "/%" PRIu64 " Stream: %" PRIu64 "/%" PRIu64,
        (this->_streamManager->recv_total_offset() + delta) / 1024, this->_streamManager->recv_max_data(), new_offset,
        this->_recv_max_stream_data);

  // Connection Level Flow Control
  if (this->_id != STREAM_ID_FOR_HANDSHAKE) {
    if (!this->_streamManager->is_recv_avail_more_than(delta)) {
      return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA);
    }

    if (!this->_streamManager->is_recv_avail_more_than(delta + MAX_DATA_HEADSPACE)) {
      this->_streamManager->slide_recv_max_data();
    }

    this->_streamManager->add_recv_total_offset(delta);
  }

  // Stream Level Flow Control
  if (this->_recv_max_stream_data > 0) {
    if (this->_recv_max_stream_data < new_offset) {
      return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_FLOW_CONTROL_RECEIVED_TOO_MUCH_DATA);
    }

    if (this->_recv_max_stream_data < new_offset + MAX_STREAM_DATA_HEADSPACE) {
      this->_slide_recv_max_stream_data();
    }
  }

  this->_recv_largest_offset = new_offset;

  return QUICError(QUICErrorClass::NONE);
}

/**
 * @brief Send STREAM DATA from _response_buffer
 */
void
QUICStream::_send()
{
  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  IOBufferReader *reader = this->_write_vio.get_reader();
  int64_t bytes_avail    = reader->read_avail();
  int64_t total_len      = 0;
  uint32_t max_size      = this->_tx->maximum_stream_frame_data_size();

  while (total_len < bytes_avail) {
    int64_t data_len = reader->block_read_avail();
    size_t len       = 0;

    if (data_len > max_size) {
      len = max_size;
    } else {
      len = data_len;
    }

    if (!this->_send_flow_control(len)) {
      break;
    }

    std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame =
      QUICFrameFactory::create_stream_frame(reinterpret_cast<const uint8_t *>(reader->start()), len, this->_id, this->_send_offset);

    this->_send_offset += len;
    reader->consume(len);
    this->_write_vio.ndone += len;
    total_len += len;

    if (!this->_state.is_allowed_to_send(*frame)) {
      // FIXME: What should we do?
      break;
    }
    this->_state.update_with_sent_frame(*frame);
    this->_streamManager->send_frame(std::move(frame));
  }

  return;
}

bool
QUICStream::_send_flow_control(uint64_t len)
{
  Debug("quic_flow_ctrl", "Con: %" PRIu64 "/%" PRIu64 " Stream: %" PRIu64 "/%" PRIu64,
        (this->_streamManager->send_total_offset() + len) / 1024, this->_streamManager->send_max_data(), this->_send_offset + len,
        this->_send_max_stream_data);

  // Stream Level Flow Control
  // TODO: remove check of _send_max_stream_data when moved to Second Implementation completely
  if (this->_send_max_stream_data > 0 && len > this->_send_max_stream_data) {
    this->_streamManager->send_frame(QUICFrameFactory::create_stream_blocked_frame(this->_id));

    return false;
  }

  // Connection Level Flow Control
  if (this->_id != STREAM_ID_FOR_HANDSHAKE) {
    if (!this->_streamManager->is_send_avail_more_than(len)) {
      this->_streamManager->send_frame(QUICFrameFactory::create_blocked_frame());

      return false;
    }
  }

  return true;
}

void
QUICStream::reset()
{
  // TODO: Create a RST_STREAM frame and pass it to Stream Manager
}

bool
QUICStream::is_read_ready()
{
  return this->_read_vio.nbytes > 0;
}
