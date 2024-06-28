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

#include "iocore/eventsystem/VConnection.h"
#include "iocore/net/quic/QUICStreamVCAdapter.h"

QUICStreamVCAdapter::QUICStreamVCAdapter(QUICStream &stream) : VConnection(new_ProxyMutex()), QUICStreamAdapter(stream)
{
  SET_HANDLER(&QUICStreamVCAdapter::state_stream_open);
}

QUICStreamVCAdapter::~QUICStreamVCAdapter()
{
  if (this->_read_ready_event) {
    this->_read_ready_event->cancel();
    this->_read_ready_event = nullptr;
  }

  if (this->_read_complete_event) {
    this->_read_complete_event->cancel();
    this->_read_complete_event = nullptr;
  }

  if (this->_write_ready_event) {
    this->_write_ready_event->cancel();
    this->_write_ready_event = nullptr;
  }

  if (this->_write_complete_event) {
    this->_write_complete_event->cancel();
    this->_write_complete_event = nullptr;
  }

  if (this->_eos_event) {
    this->_eos_event->cancel();
    this->_eos_event = nullptr;
  }
}

int64_t
QUICStreamVCAdapter::write(QUICOffset offset, const uint8_t *data, uint64_t data_length, bool fin)
{
  uint64_t bytes_added = -1;
  if (this->_read_vio.op == VIO::READ) {
    SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

    bytes_added = this->_read_vio.get_writer()->write(data, data_length);

    // Until receive FIN flag, keep nbytes INT64_MAX
    if (fin && bytes_added == data_length) {
      this->_read_vio.nbytes = offset + data_length;
    }
  }

  return bytes_added;
}

Ptr<IOBufferBlock>
QUICStreamVCAdapter::_read(size_t len)
{
  Ptr<IOBufferBlock> block;

  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

    IOBufferReader *reader = this->_write_vio.get_reader();
    block                  = make_ptr<IOBufferBlock>(reader->get_current_block()->clone());
    if (block->size()) {
      block->consume(reader->start_offset);
      block->_end             = std::min(block->start() + len, block->_buf_end);
      this->_write_vio.ndone += block->size();
    }
    reader->consume(block->size());
  }

  return block;
}

bool
QUICStreamVCAdapter::is_eos()
{
  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

    if (this->_write_vio.nbytes == INT64_MAX) {
      return false;
    }
    if (this->_write_vio.ntodo() != 0) {
      return false;
    }
    return true;
  } else {
    return false;
  }
}

uint64_t
QUICStreamVCAdapter::unread_len()
{
  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());
    return this->_write_vio.get_reader()->block_read_avail();
  } else {
    return 0;
  }
}

uint64_t
QUICStreamVCAdapter::read_len()
{
  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());
    return this->_write_vio.ndone;
  } else {
    return 0;
  }
}

uint64_t
QUICStreamVCAdapter::total_len()
{
  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());
    return this->_write_vio.nbytes;
  } else {
    return 0;
  }
}

/**
 * @brief Signal event to this->_read_vio.cont
 */
void
QUICStreamVCAdapter::encourge_read()
{
  if (this->_read_vio.op == VIO::READ) {
    SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

    if (this->_read_vio.cont == nullptr) {
      return;
    }

    if (this->_read_vio.nbytes == INT64_MAX) {
      if (!this->_read_ready_event) {
        this->_read_ready_event = this_ethread()->schedule_imm(this->_read_vio.cont, VC_EVENT_READ_READY, &this->_read_vio);
      }
    } else {
      if (!this->_read_complete_event) {
        this->_read_complete_event = this_ethread()->schedule_imm(this->_read_vio.cont, VC_EVENT_READ_COMPLETE, &this->_read_vio);
      }
    }
  }
}

/**
 * @brief Signal event to this->_write_vio.cont
 */
void
QUICStreamVCAdapter::encourge_write()
{
  if (this->_write_vio.op == VIO::WRITE) {
    SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

    if (this->_write_vio.cont == nullptr) {
      return;
    }

    if (this->_write_vio.ntodo()) {
      if (!this->_write_ready_event) {
        this->_write_ready_event = this_ethread()->schedule_imm(this->_write_vio.cont, VC_EVENT_WRITE_READY, &this->_write_vio);
      }
    } else {
      if (!this->_write_complete_event) {
        this->_write_complete_event =
          this_ethread()->schedule_imm(this->_write_vio.cont, VC_EVENT_WRITE_COMPLETE, &this->_write_vio);
      }
    }
  }
}

/**
 * @brief Signal event to this->_read_vio.cont
 */
void
QUICStreamVCAdapter::notify_eos()
{
  if (this->_read_vio.op == VIO::READ) {
    if (this->_read_vio.cont == nullptr) {
      return;
    }
    int event = VC_EVENT_EOS;

    MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());
    if (lock.is_locked()) {
      this->_read_vio.cont->handleEvent(event, &this->_read_vio);
    } else {
      if (!this->_eos_event) {
        this->_eos_event = this_ethread()->schedule_imm(this->_read_vio.cont, event, &this->_read_vio);
      }
    }
  }
}

void
QUICStreamVCAdapter::clear_read_ready_event(Event *e)
{
  ink_assert(e == this->_read_ready_event);
  this->_read_ready_event = nullptr;
}

void
QUICStreamVCAdapter::clear_read_complete_event(Event *e)
{
  ink_assert(e == this->_read_complete_event);
  this->_read_complete_event = nullptr;
}

void
QUICStreamVCAdapter::clear_write_ready_event(Event *e)
{
  ink_assert(e == this->_write_ready_event);
  this->_write_ready_event = nullptr;
}

void
QUICStreamVCAdapter::clear_write_complete_event(Event *e)
{
  ink_assert(e == this->_write_complete_event);
  this->_write_complete_event = nullptr;
}

void
QUICStreamVCAdapter::clear_eos_event(Event *e)
{
  ink_assert(e == this->_eos_event);
  this->_eos_event = nullptr;
}

// this->_read_vio.nbytes should be INT64_MAX until receive FIN flag
VIO *
QUICStreamVCAdapter::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (buf) {
    this->_read_vio.buffer.writer_for(buf);
  } else {
    this->_read_vio.buffer.clear();
  }

  this->_read_vio.mutex     = c ? c->mutex : this->mutex;
  this->_read_vio.cont      = c;
  this->_read_vio.nbytes    = nbytes;
  this->_read_vio.ndone     = 0;
  this->_read_vio.vc_server = this;
  this->_read_vio.op        = VIO::READ;

  return &this->_read_vio;
}

VIO *
QUICStreamVCAdapter::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool /* owner ATS_UNUSED */)
{
  if (buf) {
    this->_write_vio.buffer.reader_for(buf);
  } else {
    this->_write_vio.buffer.clear();
  }

  this->_write_vio.mutex     = c ? c->mutex : this->mutex;
  this->_write_vio.cont      = c;
  this->_write_vio.nbytes    = nbytes;
  this->_write_vio.ndone     = 0;
  this->_write_vio.vc_server = this;
  this->_write_vio.op        = VIO::WRITE;

  return &this->_write_vio;
}

void
QUICStreamVCAdapter::do_io_close(int /* lerrno ATS_UNUSED */)
{
  SET_HANDLER(&QUICStreamVCAdapter::state_stream_closed);

  this->_read_vio.buffer.clear();
  this->_read_vio.nbytes    = 0;
  this->_read_vio.op        = VIO::NONE;
  this->_read_vio.cont      = nullptr;
  this->_read_vio.vc_server = nullptr;

  this->_write_vio.buffer.clear();
  this->_write_vio.nbytes    = 0;
  this->_write_vio.op        = VIO::NONE;
  this->_write_vio.cont      = nullptr;
  this->_write_vio.vc_server = nullptr;
}

void
QUICStreamVCAdapter::do_io_shutdown(ShutdownHowTo_t /* howto ATS_UNUSED */)
{
  ink_assert(false); // unimplemented yet
  return;
}

void
QUICStreamVCAdapter::reenable(VIO * /* vio ATS_UNUSED */)
{
  // TODO We probably need to tell QUICStream that the application consumed received data
  // to update receive window here. In other words, we should not update receive window
  // until the application consume data.
}

bool
QUICStreamVCAdapter::is_readable()
{
  return this->stream().direction() != QUICStreamDirection::SEND && _read_vio.nbytes != _read_vio.ndone;
}

bool
QUICStreamVCAdapter::is_writable()
{
  return this->stream().direction() != QUICStreamDirection::RECEIVE && _write_vio.nbytes != -1;
}

int
QUICStreamVCAdapter::state_stream_open(int event, void * /* data ATS_UNUSED */)
{
  QUICErrorUPtr error = nullptr;

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    this->encourge_read();
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    this->encourge_write();
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

int
QUICStreamVCAdapter::state_stream_closed(int event, void * /* data ATS_UNUSED */)
{
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
