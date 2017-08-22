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

const static char *tag = "quic_stream";

void
QUICStream::init(QUICStreamManager *manager, QUICFrameTransmitter *tx, QUICStreamId id)
{
  this->_streamManager = manager;
  this->_tx            = tx;
  this->_id            = id;

  this->mutex = new_ProxyMutex();
}

void
QUICStream::start()
{
  SET_HANDLER(&QUICStream::main_event_handler);
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
QUICStream::_write_to_read_vio(std::shared_ptr<const QUICStreamFrame> frame)
{
  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  int bytes_added = this->_read_vio.buffer.writer()->write(frame->data(), frame->data_length());
  this->_read_vio.nbytes += bytes_added;
  this->_request_buffer_offset += frame->data_length();
}

void
QUICStream::_reorder_data()
{
  auto frame = _request_stream_frame_buffer.find(this->_request_buffer_offset);
  while (frame != this->_request_stream_frame_buffer.end()) {
    this->_write_to_read_vio(frame->second);
    frame = _request_stream_frame_buffer.find(this->_request_buffer_offset);
  }
}

/**
 * @brief Receive STREAM frame
 * @detail When receive STREAM frame, reorder frames and write to buffer of read_vio.
 * If the reordering or writting operation is heavy, split out them to read function,
 * which is called by application via do_io_read() or reenable().
 */
void
QUICStream::recv(std::shared_ptr<const QUICStreamFrame> frame)
{
  ink_assert(_id == frame->stream_id());
  ink_assert(this->_read_vio.op == VIO::READ);

  if (!this->_state.is_allowed_to_receive(*frame)) {
    this->reset();
    return;
  }
  this->_state.update_with_received_frame(*frame);

  if (this->_request_buffer_offset > frame->offset()) {
    // Do nothing. Just ignore STREAM frame.
    return;
  } else if (this->_request_buffer_offset == frame->offset()) {
    this->_write_to_read_vio(frame);
    this->_reorder_data();
  } else {
    // NOTE: push fragments in _request_stream_frame_buffer temporally.
    // They will be reordered when missing data is filled and offset is matched.
    this->_request_stream_frame_buffer.insert(std::make_pair(frame->offset(), frame));
  }

  return;
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

    std::unique_ptr<QUICStreamFrame, QUICFrameDeleterFunc> frame = QUICFrameFactory::create_stream_frame(
      reinterpret_cast<const uint8_t *>(reader->start()), len, this->_id, this->_response_buffer_offset);

    this->_response_buffer_offset += len;
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
