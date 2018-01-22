/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "HQClientTransaction.h"

#include "QUICDebugNames.h"

#include "HQClientSession.h"
#include "HttpSM.h"

#define HQTransDebug(fmt, ...) \
  Debug("hq_trans", "[%" PRIx64 "] [%" PRIx32 "] " fmt, static_cast<uint64_t>(static_cast<QUICConnection *>(reinterpret_cast<QUICNetVConnection*>(this->parent->get_netvc()))->connection_id()), this->get_transaction_id(), ##__VA_ARGS__)

// static void
// dump_io_buffer(IOBufferReader *reader)
// {
//   IOBufferReader *debug_reader = reader->clone();
//   uint8_t msg[1024]            = {0};
//   int64_t msg_len              = 1024;
//   int64_t read_len             = debug_reader->read(msg, msg_len);
//   Debug("v_hq_trans", "len=%" PRId64 "\n%s\n", read_len, msg);
// }

HQClientTransaction::HQClientTransaction(HQClientSession *session, QUICStreamIO *stream_io) : super(), _stream_io(stream_io)

{
  this->mutex = new_ProxyMutex();
  this->set_parent(session);
  this->sm_reader = this->_read_vio_buf.alloc_reader();
  static_cast<HQClientSession *>(this->parent)->add_transaction(this);

  SET_HANDLER(&HQClientTransaction::state_stream_open);
}

void
HQClientTransaction::set_active_timeout(ink_hrtime timeout_in)
{
  if (parent) {
    parent->set_active_timeout(timeout_in);
  }
}

void
HQClientTransaction::set_inactivity_timeout(ink_hrtime timeout_in)
{
  if (parent) {
    parent->set_inactivity_timeout(timeout_in);
  }
}

void
HQClientTransaction::cancel_inactivity_timeout()
{
  if (parent) {
    parent->cancel_inactivity_timeout();
  }
}

void
HQClientTransaction::release(IOBufferReader *r)
{
  super::release(r);
  this->current_reader = nullptr;
}

bool
HQClientTransaction::allow_half_open() const
{
  return false;
}

int
HQClientTransaction::state_stream_open(int event, void *edata)
{
  // TODO: should check recursive call?
  HQTransDebug("%s (%d)", get_vc_event_name(event), event);

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    int64_t len = this->_process_read_vio();
    // if no progress, don't need to signal
    if (len > 0) {
      this->_signal_read_event();
    }
    this->_stream_io->read_reenable();

    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    int64_t len = this->_process_write_vio();
    if (len > 0) {
      this->_signal_write_event();
    }
    this->_stream_io->write_reenable();

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
    HQTransDebug("Unknown event %d", event);
    ink_assert(false);
  }

  return EVENT_DONE;
}

int
HQClientTransaction::state_stream_closed(int event, void *data)
{
  HQTransDebug("%s (%d)", get_vc_event_name(event), event);

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
HQClientTransaction::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
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
HQClientTransaction::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
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
HQClientTransaction::do_io_close(int lerrno)
{
  SET_HANDLER(&HQClientTransaction::state_stream_closed);

  if (this->_read_event) {
    this->_read_event->cancel();
    this->_read_event = nullptr;
  }

  if (this->_write_event) {
    this->_write_event->cancel();
    this->_write_event = nullptr;
  }

  this->_read_vio.buffer.clear();
  this->_read_vio.nbytes = 0;
  this->_read_vio.op     = VIO::NONE;
  this->_read_vio._cont  = nullptr;

  this->_write_vio.buffer.clear();
  this->_write_vio.nbytes = 0;
  this->_write_vio.op     = VIO::NONE;
  this->_write_vio._cont  = nullptr;

  parent->do_io_close(lerrno);
}

void
HQClientTransaction::do_io_shutdown(ShutdownHowTo_t howto)
{
  return;
}

void
HQClientTransaction::reenable(VIO *vio)
{
  if (vio->op == VIO::READ) {
    int64_t len = this->_process_read_vio();
    this->_stream_io->read_reenable();

    if (len > 0) {
      this->_signal_read_event();
    }
  } else if (vio->op == VIO::WRITE) {
    int64_t len = this->_process_write_vio();
    this->_stream_io->write_reenable();

    if (len > 0) {
      this->_signal_write_event();
    }
  }
}

/**
 * @brief Replace existing event only if the new event is different than the inprogress event
 */
Event *
HQClientTransaction::_send_tracked_event(Event *event, int send_event, VIO *vio)
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

void
HQClientTransaction::set_read_vio_nbytes(int64_t nbytes)
{
  this->_read_vio.nbytes = nbytes;
}

void
HQClientTransaction::set_write_vio_nbytes(int64_t nbytes)
{
  this->_write_vio.nbytes = nbytes;
}

void
HQClientTransaction::destroy()
{
  current_reader = nullptr;
}

/**
 * @brief Signal event to this->_read_vio._cont
 */
void
HQClientTransaction::_signal_read_event()
{
  if (this->_read_vio._cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return;
  }
  int event = this->_read_vio.ntodo() ? VC_EVENT_READ_READY : VC_EVENT_READ_COMPLETE;

  MUTEX_TRY_LOCK(lock, this->_read_vio.mutex, this_ethread());
  if (lock.is_locked()) {
    this->_read_vio._cont->handleEvent(event, &this->_read_vio);
  } else {
    this_ethread()->schedule_imm(this->_read_vio._cont, event, &this->_read_vio);
  }

  HQTransDebug("%s (%d)", get_vc_event_name(event), event);
}

/**
 * @brief Signal event to this->_write_vio._cont
 */
void
HQClientTransaction::_signal_write_event()
{
  if (this->_write_vio._cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return;
  }
  int event = this->_write_vio.ntodo() ? VC_EVENT_WRITE_READY : VC_EVENT_WRITE_COMPLETE;

  MUTEX_TRY_LOCK(lock, this->_write_vio.mutex, this_ethread());
  if (lock.is_locked()) {
    this->_write_vio._cont->handleEvent(event, &this->_write_vio);
  } else {
    this_ethread()->schedule_imm(this->_write_vio._cont, event, &this->_write_vio);
  }

  HQTransDebug("%s (%d)", get_vc_event_name(event), event);
}

// Convert HTTP/0.9 to HTTP/1.1
int64_t
HQClientTransaction::_process_read_vio()
{
  if (this->_read_vio._cont == nullptr || this->_read_vio.op == VIO::NONE) {
    return 0;
  }

  SCOPED_MUTEX_LOCK(lock, this->_read_vio.mutex, this_ethread());

  IOBufferReader *client_vio_reader = this->_stream_io->get_read_buffer_reader();
  int64_t bytes_avail               = client_vio_reader->read_avail();
  MIOBuffer *writer                 = this->_read_vio.get_writer();

  if (!this->_client_req_header_complete) {
    int n = 2;
    // Check client request is complete or not
    if (bytes_avail < 2 || client_vio_reader->start()[bytes_avail - 1] != '\n') {
      return 0;
    }
    this->_client_req_header_complete = true;

    // Check "CRLF" or "LF"
    if (client_vio_reader->start()[bytes_avail - 2] != '\r') {
      n = 1;
    }

    writer->write(client_vio_reader, bytes_avail - n);
    client_vio_reader->consume(bytes_avail);

    // FIXME: Get hostname from SNI?
    const char version[] = " HTTP/1.1\r\nHost: localhost\r\n\r\n";
    writer->write(version, sizeof(version));
  } else {
    writer->write(client_vio_reader, bytes_avail);
    client_vio_reader->consume(bytes_avail);
  }

  return bytes_avail;
}

// FIXME: already defined somewhere?
static constexpr char http_1_1_version[] = "HTTP/1.1";

// Convert HTTP/1.1 to HTTP/0.9
int64_t
HQClientTransaction::_process_write_vio()
{
  if (this->_write_vio._cont == nullptr || this->_write_vio.op == VIO::NONE) {
    return 0;
  }

  SCOPED_MUTEX_LOCK(lock, this->_write_vio.mutex, this_ethread());

  IOBufferReader *reader = this->_write_vio.get_reader();

  int64_t http_1_1_version_len = sizeof(http_1_1_version) - 1;

  if (reader->is_read_avail_more_than(http_1_1_version_len) &&
      memcmp(reader->start(), http_1_1_version, http_1_1_version_len) == 0) {
    // Skip HTTP/1.1 response headers
    IOBufferBlock *headers = reader->get_current_block();
    int64_t headers_size   = headers->read_avail();
    reader->consume(headers_size);
    this->_write_vio.ndone += headers_size;

    // The size of respons to client
    this->_stream_io->set_write_vio_nbytes(this->_write_vio.nbytes - headers_size);
  }

  // Write HTTP/1.1 response body
  int64_t bytes_avail   = reader->read_avail();
  int64_t total_written = 0;

  HQTransDebug("%" PRId64, bytes_avail);

  while (total_written < bytes_avail) {
    int64_t data_len      = reader->block_read_avail();
    int64_t bytes_written = this->_stream_io->write(reader, data_len);
    if (bytes_written <= 0) {
      break;
    }

    reader->consume(bytes_written);
    this->_write_vio.ndone += bytes_written;
    total_written += bytes_written;
  }

  // NOTE: When Chunked Transfer Coding is supported, check ChunkedState of ChunkedHandler
  // is CHUNK_READ_DONE and set FIN flag
  if (this->_write_vio.ntodo() == 0) {
    this->_stream_io->shutdown();
  }

  return total_written;
}

void
HQClientTransaction::transaction_done()
{
  // TODO: start closing transaction
  return;
}

int
HQClientTransaction::get_transaction_id() const
{
  return this->_stream_io->get_transaction_id();
}
