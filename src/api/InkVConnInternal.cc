/** @file

  Internal SDK stuff

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

#include "ts/apidefs.h"
#include "ts/InkAPIPrivateIOCore.h"

using namespace tsapi::c;

ClassAllocator<INKVConnInternal> INKVConnAllocator("INKVConnAllocator");

INKVConnInternal::INKVConnInternal() : INKContInternal(), m_read_vio(), m_write_vio(), m_output_vc(nullptr)
{
  m_closed = 0;
}

INKVConnInternal::INKVConnInternal(TSEventFunc funcp, TSMutex mutexp)
  : INKContInternal(funcp, mutexp), m_read_vio(), m_write_vio(), m_output_vc(nullptr)
{
  m_closed = 0;
}

void
INKVConnInternal::clear()
{
  m_read_vio.set_continuation(nullptr);
  m_write_vio.set_continuation(nullptr);
  INKContInternal::clear();
}

void
INKVConnInternal::free()
{
  clear();
  this->mutex.clear();
  m_free_magic = INKCONT_INTERN_MAGIC_DEAD;
  THREAD_FREE(this, INKVConnAllocator, this_thread());
}

void
INKVConnInternal::destroy()
{
  if (m_free_magic == INKCONT_INTERN_MAGIC_DEAD) {
    ink_release_assert(!"Plugin tries to use a vconnection which is deleted");
  }

  m_deleted = 1;
  if (m_deletable) {
    free();
  }
}

VIO *
INKVConnInternal::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  m_read_vio.buffer.writer_for(buf);
  m_read_vio.op = VIO::READ;
  m_read_vio.set_continuation(c);
  m_read_vio.nbytes    = nbytes;
  m_read_vio.ndone     = 0;
  m_read_vio.vc_server = this;

  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);

  return &m_read_vio;
}

VIO *
INKVConnInternal::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(!owner);
  m_write_vio.buffer.reader_for(buf);
  m_write_vio.op = VIO::WRITE;
  m_write_vio.set_continuation(c);
  m_write_vio.nbytes    = nbytes;
  m_write_vio.ndone     = 0;
  m_write_vio.vc_server = this;

  if (m_write_vio.buffer.reader()->read_avail() > 0) {
    if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
    eventProcessor.schedule_imm(this, ET_NET);
  }

  return &m_write_vio;
}

void
INKVConnInternal::do_io_transform(VConnection *vc)
{
  m_output_vc = vc;
}

void
INKVConnInternal::do_io_close(int error)
{
  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }

  INK_WRITE_MEMORY_BARRIER;

  if (error != -1) {
    lerrno   = error;
    m_closed = TS_VC_CLOSE_ABORT;
  } else {
    m_closed = TS_VC_CLOSE_NORMAL;
  }

  m_read_vio.op = VIO::NONE;
  m_read_vio.buffer.clear();

  m_write_vio.op = VIO::NONE;
  m_write_vio.buffer.clear();

  if (m_output_vc) {
    m_output_vc->do_io_close(error);
    m_output_vc = nullptr;
  }

  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::do_io_shutdown(ShutdownHowTo_t howto)
{
  if ((howto == IO_SHUTDOWN_READ) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_read_vio.op = VIO::NONE;
    m_read_vio.buffer.clear();
  }

  if ((howto == IO_SHUTDOWN_WRITE) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_write_vio.op = VIO::NONE;
    m_write_vio.buffer.clear();
  }

  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::reenable(VIO * /* vio ATS_UNUSED */)
{
  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  eventProcessor.schedule_imm(this, ET_NET);
}

void
INKVConnInternal::retry(unsigned int delay)
{
  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  mutex->thread_holding->schedule_in(this, HRTIME_MSECONDS(delay));
}

bool
INKVConnInternal::get_data(int id, void *data)
{
  switch (id) {
  case TS_API_DATA_READ_VIO:
    *((TSVIO *)data) = reinterpret_cast<TSVIO>(&m_read_vio);
    return true;
  case TS_API_DATA_WRITE_VIO:
    *((TSVIO *)data) = reinterpret_cast<TSVIO>(&m_write_vio);
    return true;
  case TS_API_DATA_OUTPUT_VC:
    *((TSVConn *)data) = reinterpret_cast<TSVConn>(m_output_vc);
    return true;
  case TS_API_DATA_CLOSED:
    *((int *)data) = m_closed;
    return true;
  default:
    return INKContInternal::get_data(id, data);
  }
}

bool
INKVConnInternal::set_data(int id, void *data)
{
  switch (id) {
  case TS_API_DATA_OUTPUT_VC:
    m_output_vc = (VConnection *)data;
    return true;
  default:
    return INKContInternal::set_data(id, data);
  }
}
