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

static void
dump_io_buffer(IOBufferReader *reader)
{
  IOBufferReader *debug_reader = reader->clone();
  uint8_t msg[1024]            = {0};
  int64_t msg_len              = 1024;
  int64_t read_len             = debug_reader->read(msg, msg_len);
  Debug("hq_trans", "len=%" PRId64 "\n%s\n", read_len, msg);
}

HQClientTransaction::HQClientTransaction(HQClientSession *session, QUICStreamIO *stream_io) : super(), _stream_io(stream_io)

{
  this->mutex     = new_ProxyMutex();
  this->parent    = session; //       trans.set_parent();
  this->sm_reader = this->_read_vio_buf.alloc_reader();
  static_cast<HQClientSession *>(this->parent)->add_transaction(this);

  SET_HANDLER(&HQClientTransaction::main_event_handler);
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
HQClientTransaction::main_event_handler(int event, void *edata)
{
  Debug("hq_trans", "%s", QUICDebugNames::vc_event(event));

  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    if (edata == this->_read_event) {
      this->_read_event = nullptr;
    }
    if (this->_stream_io->read_avail()) {
      this->_read_request();
    }
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    if (edata == this->_write_event) {
      this->_write_event = nullptr;
    }
    if (this->_write_vio.get_reader()->read_avail()) {
      this->_write_response();
    }
    break;
  }
  default:
    Debug("hq_trans", "Unknown event %d", event);
    ink_assert(false);
  }

  return EVENT_CONT;
}

void
HQClientTransaction::reenable(VIO *vio)
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

  this->_read_vio.reenable();

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

  this->_write_vio.reenable();

  return &this->_write_vio;
}

void
HQClientTransaction::do_io_close(int lerrno)
{
  parent->do_io_close(lerrno);
}

void
HQClientTransaction::destroy()
{
  current_reader = nullptr;
}

void
HQClientTransaction::do_io_shutdown(ShutdownHowTo_t howto)
{
  if (parent) {
    parent->do_io_shutdown(howto);
  }
}

// Convert HTTP/0.9 to HTTP/1.1
void
HQClientTransaction::_read_request()
{
  IOBufferReader *client_vio_reader = this->_stream_io->get_read_buffer_reader();
  int64_t bytes_avail               = client_vio_reader->read_avail();

  // Copy only "GET /path/"
  // Check CRLF or LF
  int n = 2;
  if (bytes_avail > 2 && client_vio_reader->start()[bytes_avail - 2] != '\r') {
    n = 1;
  }

  MIOBuffer *writer = this->_read_vio.get_writer();
  writer->write(client_vio_reader, bytes_avail - n);

  // FIXME: Get hostname from SNI?
  const char version[] = " HTTP/1.1\r\nHost: localhost\r\n\r\n";
  writer->write(version, sizeof(version));

  dump_io_buffer(this->sm_reader);

  this->_read_vio._cont->handleEvent(VC_EVENT_READ_READY, &this->_read_vio);
}

// FIXME: already defined somewhere?
static constexpr char http_1_1_version[] = "HTTP/1.1";

// Convert HTTP/1.1 to HTTP/0.9
void
HQClientTransaction::_write_response()
{
  IOBufferReader *reader = this->_write_vio.get_reader();

  if (memcmp(reader->start(), http_1_1_version, sizeof(http_1_1_version)) == 0) {
    // Skip HTTP/1.1 response headers
    IOBufferBlock *headers = reader->get_current_block();
    int64_t headers_size   = headers->read_avail();
    reader->consume(headers_size);
  }

  // Write HTTP/1.1 response body
  int64_t bytes_avail   = reader->read_avail();
  int64_t total_written = 0;
  while (total_written < bytes_avail) {
    int64_t bytes_written = this->_stream_io->write(reader, bytes_avail);
    reader->consume(bytes_written);
    this->_write_vio.ndone += bytes_written;
    total_written += bytes_written;
  }

  this->_stream_io->write_reenable();
}

void
HQClientTransaction::transaction_done()
{
  // TODO: set FIN flag
  return;
}
