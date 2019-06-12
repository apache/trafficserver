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

  @section thoughts Transform thoughts

    - Must be able to handle a chain of transformations.
    - Any transformation in the chain may fail.
      Failure options:
        - abort the client (if transformed data already sent)
        - serve the client the untransformed document
        - remove the failing transformation from the chain and attempt the transformation again (difficult to do)
        - never send untransformed document to client if client would not understand it (e.g. a set top box)
    - Must be able to change response header fields up until the point that TRANSFORM_READ_READY is sent to the user.

  @section usage Transform usage

    -# transformProcessor.open (cont, hooks); - returns "tvc", a TransformVConnection if 'hooks != NULL'
    -# tvc->do_io_write (cont, nbytes, buffer1);
    -# cont->handleEvent (TRANSFORM_READ_READY, NULL);
    -# tvc->do_io_read (cont, nbytes, buffer2);
    -# tvc->do_io_close ();

  @section visualization Transform visualization

  @verbatim
         +----+     +----+     +----+     +----+
    -IB->| T1 |-B1->| T2 |-B2->| T3 |-B3->| T4 |-OB->
         +----+     +----+     +----+     +----+
  @endverbatim

  Data flows into the first transform in the form of the buffer
  passed to TransformVConnection::do_io_write (IB). Data flows
  out of the last transform in the form of the buffer passed to
  TransformVConnection::do_io_read (OB). Between each transformation is
  another buffer (B1, B2 and B3).

  A transformation is a Continuation. The continuation is called with the
  event TRANSFORM_IO_WRITE to initialize the write and TRANSFORM_IO_READ
  to initialize the read.

*/

#include "ProxyConfig.h"
#include "P_Net.h"
#include "TransformInternal.h"
#include "HdrUtils.h"
#include "Log.h"

TransformProcessor transformProcessor;

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformProcessor::start()
{
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VConnection *
TransformProcessor::open(Continuation *cont, APIHook *hooks)
{
  if (hooks) {
    return new TransformVConnection(cont, hooks);
  } else {
    return nullptr;
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

INKVConnInternal *
TransformProcessor::null_transform(ProxyMutex *mutex)
{
  return new NullTransform(mutex);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

INKVConnInternal *
TransformProcessor::range_transform(ProxyMutex *mut, RangeRecord *ranges, int num_fields, HTTPHdr *transform_resp,
                                    const char *content_type, int content_type_len, int64_t content_length)
{
  RangeTransform *range_transform =
    new RangeTransform(mut, ranges, num_fields, transform_resp, content_type, content_type_len, content_length);
  return range_transform;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformTerminus::TransformTerminus(TransformVConnection *tvc)
  : VConnection(tvc->mutex),
    m_tvc(tvc),
    m_read_vio(),
    m_write_vio(),
    m_event_count(0),
    m_deletable(0),
    m_closed(0),
    m_called_user(0)
{
  SET_HANDLER(&TransformTerminus::handle_event);
}

#define RETRY()                                                  \
  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {      \
    ink_assert(!"not reached");                                  \
  }                                                              \
  eventProcessor.schedule_in(this, HRTIME_MSECONDS(10), ET_NET); \
  return 0;

int
TransformTerminus::handle_event(int event, void * /* edata ATS_UNUSED */)
{
  int val;

  m_deletable = ((m_closed != 0) && (m_tvc->m_closed != 0));

  val = ink_atomic_increment((int *)&m_event_count, -1);

  Debug("transform", "[TransformTerminus::handle_event] event_count %d", m_event_count);

  if (val <= 0) {
    ink_assert(!"not reached");
  }

  m_deletable = m_deletable && (val == 1);

  if (m_closed != 0 && m_tvc->m_closed != 0) {
    if (m_deletable) {
      Debug("transform", "TransformVConnection destroy [0x%lx]", (long)m_tvc);
      delete m_tvc;
      return 0;
    }
  } else if (m_write_vio.op == VIO::WRITE) {
    if (m_read_vio.op == VIO::NONE) {
      if (!m_called_user) {
        Debug("transform", "TransformVConnection calling user: %d %d [0x%lx] [0x%lx]", m_event_count, event, (long)m_tvc,
              (long)m_tvc->m_cont);

        m_called_user = 1;
        // It is our belief this is safe to pass a reference, i.e. its scope
        // and locking ought to be safe across the lifetime of the continuation.
        m_tvc->m_cont->handleEvent(TRANSFORM_READ_READY, (void *)&m_write_vio.nbytes);
      }
    } else {
      int64_t towrite;

      MUTEX_TRY_LOCK(trylock1, m_write_vio.mutex, this_ethread());
      if (!trylock1.is_locked()) {
        RETRY();
      }

      MUTEX_TRY_LOCK(trylock2, m_read_vio.mutex, this_ethread());
      if (!trylock2.is_locked()) {
        RETRY();
      }

      if (m_closed != 0) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        return 0;
      }

      towrite = m_write_vio.ntodo();
      if (towrite > 0) {
        if (towrite > m_write_vio.get_reader()->read_avail()) {
          towrite = m_write_vio.get_reader()->read_avail();
        }
        if (towrite > m_read_vio.ntodo()) {
          towrite = m_read_vio.ntodo();
        }

        if (towrite > 0) {
          m_read_vio.get_writer()->write(m_write_vio.get_reader(), towrite);
          m_read_vio.ndone += towrite;

          m_write_vio.get_reader()->consume(towrite);
          m_write_vio.ndone += towrite;
        }
      }

      if (m_write_vio.ntodo() > 0) {
        if (towrite > 0) {
          m_write_vio.cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
        }
      } else {
        m_write_vio.cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
      }

      // We could have closed on the write callback
      if (m_closed != 0 && m_tvc->m_closed != 0) {
        return 0;
      }

      if (m_read_vio.ntodo() > 0) {
        if (m_write_vio.ntodo() <= 0) {
          m_read_vio.cont->handleEvent(VC_EVENT_EOS, &m_read_vio);
        } else if (towrite > 0) {
          m_read_vio.cont->handleEvent(VC_EVENT_READ_READY, &m_read_vio);
        }
      } else {
        m_read_vio.cont->handleEvent(VC_EVENT_READ_COMPLETE, &m_read_vio);
      }
    }
  } else {
    MUTEX_TRY_LOCK(trylock2, m_read_vio.mutex, this_ethread());
    if (!trylock2.is_locked()) {
      RETRY();
    }

    if (m_closed != 0) {
      // The terminus was closed, but the enclosing transform
      // vconnection wasn't. If the terminus was aborted then we
      // call the read_vio cont back with VC_EVENT_ERROR. If it
      // was closed normally then we call it back with
      // VC_EVENT_EOS. If a read operation hasn't been initiated
      // yet and we haven't called the user back then we call
      // the user back instead of the read_vio cont (which won't
      // exist).
      if (m_tvc->m_closed == 0) {
        int ev = (m_closed == TS_VC_CLOSE_ABORT) ? VC_EVENT_ERROR : VC_EVENT_EOS;

        if (!m_called_user) {
          m_called_user = 1;
          m_tvc->m_cont->handleEvent(ev, &m_read_vio);
        } else {
          ink_assert(m_read_vio.cont != nullptr);
          m_read_vio.cont->handleEvent(ev, &m_read_vio);
        }
      }

      return 0;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformTerminus::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
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
  Debug("transform", "[TransformTerminus::do_io_read] event_count %d", m_event_count);

  eventProcessor.schedule_imm(this, ET_NET);

  return &m_read_vio;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformTerminus::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  // In the process of eliminating 'owner' mode so asserting against it
  ink_assert(!owner);
  m_write_vio.buffer.reader_for(buf);
  m_write_vio.op = VIO::WRITE;
  m_write_vio.set_continuation(c);
  m_write_vio.nbytes    = nbytes;
  m_write_vio.ndone     = 0;
  m_write_vio.vc_server = this;

  if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
    ink_assert(!"not reached");
  }
  Debug("transform", "[TransformTerminus::do_io_write] event_count %d", m_event_count);

  eventProcessor.schedule_imm(this, ET_NET);

  return &m_write_vio;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::do_io_close(int error)
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

  eventProcessor.schedule_imm(this, ET_NET);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::do_io_shutdown(ShutdownHowTo_t howto)
{
  if ((howto == IO_SHUTDOWN_READ) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_read_vio.op = VIO::NONE;
    m_read_vio.buffer.clear();
  }

  if ((howto == IO_SHUTDOWN_WRITE) || (howto == IO_SHUTDOWN_READWRITE)) {
    m_write_vio.op = VIO::NONE;
    m_write_vio.buffer.clear();
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformTerminus::reenable(VIO *vio)
{
  ink_assert((vio == &m_read_vio) || (vio == &m_write_vio));

  if (m_event_count == 0) {
    if (ink_atomic_increment((int *)&m_event_count, 1) < 0) {
      ink_assert(!"not reached");
    }
    Debug("transform", "[TransformTerminus::reenable] event_count %d", m_event_count);
    eventProcessor.schedule_imm(this, ET_NET);
  } else {
    Debug("transform", "[TransformTerminus::reenable] skipping due to "
                       "pending events");
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformVConnection::TransformVConnection(Continuation *cont, APIHook *hooks)
  : TransformVCChain(cont->mutex.get()), m_cont(cont), m_terminus(this), m_closed(0)
{
  INKVConnInternal *xform;

  SET_HANDLER(&TransformVConnection::handle_event);

  ink_assert(hooks != nullptr);

  m_transform = hooks->m_cont;
  while (hooks->m_link.next) {
    xform = (INKVConnInternal *)hooks->m_cont;
    hooks = hooks->m_link.next;
    xform->do_io_transform(hooks->m_cont);
  }
  xform = (INKVConnInternal *)hooks->m_cont;
  xform->do_io_transform(&m_terminus);

  Debug("transform", "TransformVConnection create [0x%lx]", (long)this);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformVConnection::~TransformVConnection()
{
  // Clear the continuations in terminus VConnections so that
  //  mutex's get released (INKqa05596)
  m_terminus.m_read_vio.set_continuation(nullptr);
  m_terminus.m_write_vio.set_continuation(nullptr);
  m_terminus.mutex = nullptr;
  this->mutex      = nullptr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
TransformVConnection::handle_event(int /* event ATS_UNUSED */, void * /* edata ATS_UNUSED */)
{
  ink_assert(!"not reached");
  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformVConnection::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  Debug("transform", "TransformVConnection do_io_read: 0x%lx [0x%lx]", (long)c, (long)this);

  return m_terminus.do_io_read(c, nbytes, buf);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

VIO *
TransformVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool /* owner ATS_UNUSED */)
{
  Debug("transform", "TransformVConnection do_io_write: 0x%lx [0x%lx]", (long)c, (long)this);

  return m_transform->do_io_write(c, nbytes, buf);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::do_io_close(int error)
{
  Debug("transform", "TransformVConnection do_io_close: %d [0x%lx]", error, (long)this);

  if (m_closed != 0) {
    return;
  }

  if (error != -1) {
    m_closed = TS_VC_CLOSE_ABORT;
  } else {
    m_closed = TS_VC_CLOSE_NORMAL;
  }

  m_transform->do_io_close(error);
  m_transform = nullptr;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::do_io_shutdown(ShutdownHowTo_t howto)
{
  ink_assert(howto == IO_SHUTDOWN_WRITE);

  Debug("transform", "TransformVConnection do_io_shutdown: %d [0x%lx]", howto, (long)this);

  m_transform->do_io_shutdown(howto);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
TransformVConnection::reenable(VIO * /* vio ATS_UNUSED */)
{
  ink_assert(!"not reached");
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

uint64_t
TransformVConnection::backlog(uint64_t limit)
{
  uint64_t b          = 0; // backlog
  VConnection *raw_vc = m_transform;
  MIOBuffer *w;
  while (raw_vc && raw_vc != &m_terminus) {
    INKVConnInternal *vc = static_cast<INKVConnInternal *>(raw_vc);
    if (nullptr != (w = vc->m_read_vio.buffer.writer())) {
      b += w->max_read_avail();
    }
    if (b >= limit) {
      return b;
    }
    raw_vc = vc->m_output_vc;
  }
  if (nullptr != (w = m_terminus.m_read_vio.buffer.writer())) {
    b += w->max_read_avail();
  }
  if (b >= limit) {
    return b;
  }

  IOBufferReader *r = m_terminus.m_write_vio.get_reader();
  if (r) {
    b += r->read_avail();
  }
  return b;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

TransformControl::TransformControl() : Continuation(new_ProxyMutex()), m_hooks()
{
  SET_HANDLER(&TransformControl::handle_event);

  m_hooks.append(transformProcessor.null_transform(new_ProxyMutex()));
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
TransformControl::handle_event(int event, void * /* edata ATS_UNUSED */)
{
  switch (event) {
  case EVENT_IMMEDIATE: {
    char *s, *e;

    ink_assert(m_tvc == nullptr);
    if (http_global_hooks && http_global_hooks->get(TS_HTTP_RESPONSE_TRANSFORM_HOOK)) {
      m_tvc = transformProcessor.open(this, http_global_hooks->get(TS_HTTP_RESPONSE_TRANSFORM_HOOK));
    } else {
      m_tvc = transformProcessor.open(this, m_hooks.get());
    }
    ink_assert(m_tvc != nullptr);

    m_write_buf = new_MIOBuffer();
    s           = m_write_buf->end();
    e           = m_write_buf->buf_end();

    memset(s, 'a', e - s);
    m_write_buf->fill(e - s);

    m_tvc->do_io_write(this, 4 * 1024, m_write_buf->alloc_reader());
    break;
  }

  case TRANSFORM_READ_READY: {
    MIOBuffer *buf = new_empty_MIOBuffer();

    m_read_buf = buf->alloc_reader();
    m_tvc->do_io_read(this, INT64_MAX, buf);
    break;
  }

  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_EOS:
    m_tvc->do_io_close();

    free_MIOBuffer(m_read_buf->mbuf);
    m_read_buf = nullptr;

    free_MIOBuffer(m_write_buf);
    m_write_buf = nullptr;
    break;

  case VC_EVENT_WRITE_COMPLETE:
    break;

  default:
    ink_assert(!"not reached");
    break;
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

NullTransform::NullTransform(ProxyMutex *_mutex)
  : INKVConnInternal(nullptr, reinterpret_cast<TSMutex>(_mutex)),
    m_output_buf(nullptr),
    m_output_reader(nullptr),
    m_output_vio(nullptr)
{
  SET_HANDLER(&NullTransform::handle_event);

  Debug("transform", "NullTransform create [0x%lx]", (long)this);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

NullTransform::~NullTransform()
{
  if (m_output_buf) {
    free_MIOBuffer(m_output_buf);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
NullTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  Debug("transform", "[NullTransform::handle_event] event count %d", m_event_count);

  if (m_closed) {
    if (m_deletable) {
      Debug("transform", "NullTransform destroy: %" PRId64 " [%p]", m_output_vio ? m_output_vio->ndone : 0, this);
      delete this;
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio.cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;
    case VC_EVENT_WRITE_COMPLETE:
      ink_assert(m_output_vio == (VIO *)edata);

      // The write to the output vconnection completed. This
      // could only be the case if the data being fed into us
      // has also completed.
      ink_assert(m_write_vio.ntodo() == 0);

      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;
    case VC_EVENT_WRITE_READY:
    default: {
      int64_t towrite;
      int64_t avail;

      ink_assert(m_output_vc != nullptr);

      if (!m_output_vio) {
        m_output_buf    = new_empty_MIOBuffer();
        m_output_reader = m_output_buf->alloc_reader();
        m_output_vio    = m_output_vc->do_io_write(this, m_write_vio.nbytes, m_output_reader);
      }

      MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
      if (!trylock.is_locked()) {
        retry(10);
        return 0;
      }

      if (m_closed) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        m_output_vio->nbytes = m_write_vio.ndone;
        m_output_vio->reenable();
        return 0;
      }

      towrite = m_write_vio.ntodo();
      if (towrite > 0) {
        avail = m_write_vio.get_reader()->read_avail();
        if (towrite > avail) {
          towrite = avail;
        }

        if (towrite > 0) {
          Debug("transform",
                "[NullTransform::handle_event] "
                "writing %" PRId64 " bytes to output",
                towrite);
          m_output_buf->write(m_write_vio.get_reader(), towrite);

          m_write_vio.get_reader()->consume(towrite);
          m_write_vio.ndone += towrite;
        }
      }

      if (m_write_vio.ntodo() > 0) {
        if (towrite > 0) {
          m_output_vio->reenable();
          m_write_vio.cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
        }
      } else {
        m_output_vio->nbytes = m_write_vio.ndone;
        m_output_vio->reenable();
        m_write_vio.cont->handleEvent(VC_EVENT_WRITE_COMPLETE, &m_write_vio);
      }

      break;
    }
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  Reasons the JG transform cannot currently be a plugin:
    a) Uses the config system
       - Easily avoided by using the plugin.config file to pass the config
         values as parameters to the plugin initialization routine.
    b) Uses the stat system
       - FIXME: should probably solve this.
  -------------------------------------------------------------------------*/

/* the JG transform is now a plugin. All the JG code,
   config variables and stats are removed from Transform.cc */

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#if TS_HAS_TESTS
void
TransformTest::run()
{
  if (is_action_tag_set("transform_test")) {
    eventProcessor.schedule_imm(new TransformControl(), ET_NET);
  }
}
#endif

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

RangeTransform::RangeTransform(ProxyMutex *mut, RangeRecord *ranges, int num_fields, HTTPHdr *transform_resp,
                               const char *content_type, int content_type_len, int64_t content_length)
  : INKVConnInternal(nullptr, reinterpret_cast<TSMutex>(mut)),
    m_output_buf(nullptr),
    m_output_reader(nullptr),
    m_transform_resp(transform_resp),
    m_output_vio(nullptr),
    m_range_content_length(0),
    m_num_range_fields(num_fields),
    m_current_range(0),
    m_content_type(content_type),
    m_content_type_len(content_type_len),
    m_ranges(ranges),
    m_output_cl(content_length),
    m_done(0)
{
  SET_HANDLER(&RangeTransform::handle_event);

  m_num_chars_for_cl = num_chars_for_int(m_range_content_length);
  Debug("http_trans", "RangeTransform creation finishes");
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

RangeTransform::~RangeTransform()
{
  if (m_output_buf) {
    free_MIOBuffer(m_output_buf);
  }
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
RangeTransform::handle_event(int event, void *edata)
{
  handle_event_count(event);

  if (m_closed) {
    if (m_deletable) {
      if (m_output_vc != nullptr) {
        Debug("http_trans", "RangeTransform destroy: %p ndone=%" PRId64, this, m_output_vio ? m_output_vio->ndone : 0);
      } else {
        Debug("http_trans", "RangeTransform destroy");
      }
      delete this;
    }
  } else {
    switch (event) {
    case VC_EVENT_ERROR:
      m_write_vio.cont->handleEvent(VC_EVENT_ERROR, &m_write_vio);
      break;
    case VC_EVENT_WRITE_COMPLETE:
      ink_assert(m_output_vio == (VIO *)edata);
      m_output_vc->do_io_shutdown(IO_SHUTDOWN_WRITE);
      break;
    case VC_EVENT_WRITE_READY:
    default:
      ink_assert(m_output_vc != nullptr);

      if (!m_output_vio) {
        m_output_buf    = new_empty_MIOBuffer();
        m_output_reader = m_output_buf->alloc_reader();
        m_output_vio    = m_output_vc->do_io_write(this, m_output_cl, m_output_reader);

        change_response_header();

        if (m_num_range_fields > 1) {
          add_boundary(false);
          add_sub_header(m_current_range);
        }
      }

      MUTEX_TRY_LOCK(trylock, m_write_vio.mutex, this_ethread());
      if (!trylock.is_locked()) {
        retry(10);
        return 0;
      }

      if (m_closed) {
        return 0;
      }

      if (m_write_vio.op == VIO::NONE) {
        m_output_vio->nbytes = m_done;
        m_output_vio->reenable();
        return 0;
      }

      transform_to_range();
      break;
    }
  }

  return 0;
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

void
RangeTransform::transform_to_range()
{
  IOBufferReader *reader = m_write_vio.get_reader();
  int64_t toskip, tosend, avail;
  const int64_t *end, *start;
  int64_t prev_end = 0;
  int64_t *done_byte;

  end       = &m_ranges[m_current_range]._end;
  done_byte = &m_ranges[m_current_range]._done_byte;
  start     = &m_ranges[m_current_range]._start;
  avail     = reader->read_avail();

  while (true) {
    if (*done_byte < (*start - 1)) {
      toskip = *start - *done_byte - 1;

      if (toskip > avail) {
        toskip = avail;
      }

      if (toskip > 0) {
        reader->consume(toskip);
        *done_byte += toskip;
        avail = reader->read_avail();
      }
    }

    if (avail > 0) {
      tosend = *end - *done_byte;

      if (tosend > avail) {
        tosend = avail;
      }

      m_output_buf->write(reader, tosend);
      reader->consume(tosend);

      m_done += tosend;
      *done_byte += tosend;
    }

    if (*done_byte == *end) {
      prev_end = *end;
    }

    // move to next Range if done one
    // ignore bad Range: _done_byte -1, _end -1
    while (*done_byte == *end) {
      m_current_range++;

      if (m_current_range == m_num_range_fields) {
        if (m_num_range_fields > 1) {
          m_done += m_output_buf->write("\r\n", 2);
          add_boundary(true);
        }

        Debug("http_trans", "total bytes of Range response body is %" PRId64, m_done);
        m_output_vio->nbytes = m_done;
        m_output_vio->reenable();

        // if we are detaching before processing all the
        //   input data, send VC_EVENT_EOS to let the upstream know
        //   that it can rely on us consuming any more data
        int cb_event = (m_write_vio.ntodo() > 0) ? VC_EVENT_EOS : VC_EVENT_WRITE_COMPLETE;
        m_write_vio.cont->handleEvent(cb_event, &m_write_vio);
        return;
      }

      end       = &m_ranges[m_current_range]._end;
      done_byte = &m_ranges[m_current_range]._done_byte;
      start     = &m_ranges[m_current_range]._start;

      // if this is a good Range
      if (*end != -1) {
        m_done += m_output_buf->write("\r\n", 2);
        add_boundary(false);
        add_sub_header(m_current_range);

        // keep this part for future support of out-of-order Range
        // if this is NOT a sequential Range compared to the previous one -
        // start of current Range is larger than the end of last Range, do
        // not need to go back to the start of the IOBuffereReader.
        // Otherwise, reset the IOBufferReader.
        // if ( *start > prev_end )
        *done_byte = prev_end;
        // else
        //  reader->reset();

        break;
      }
    }

    // When we need to read and there is nothing available
    avail = reader->read_avail();
    if (avail == 0) {
      break;
    }
  }

  m_output_vio->reenable();
  m_write_vio.cont->handleEvent(VC_EVENT_WRITE_READY, &m_write_vio);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*
 * these two need be changed at the same time
 */

static char bound[]      = "RANGE_SEPARATOR";
static char range_type[] = "multipart/byteranges; boundary=RANGE_SEPARATOR";
static char cont_type[]  = "Content-type: ";
static char cont_range[] = "Content-range: bytes ";

void
RangeTransform::add_boundary(bool end)
{
  m_done += m_output_buf->write("--", 2);
  m_done += m_output_buf->write(bound, sizeof(bound) - 1);

  if (end) {
    m_done += m_output_buf->write("--", 2);
  }

  m_done += m_output_buf->write("\r\n", 2);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

#define RANGE_NUMBERS_LENGTH 60

void
RangeTransform::add_sub_header(int index)
{
  // this should be large enough to hold three integers!
  char numbers[RANGE_NUMBERS_LENGTH];
  int len;

  m_done += m_output_buf->write(cont_type, sizeof(cont_type) - 1);
  if (m_content_type) {
    m_done += m_output_buf->write(m_content_type, m_content_type_len);
  }
  m_done += m_output_buf->write("\r\n", 2);
  m_done += m_output_buf->write(cont_range, sizeof(cont_range) - 1);

  snprintf(numbers, sizeof(numbers), "%" PRId64 "-%" PRId64 "/%" PRId64 "", m_ranges[index]._start, m_ranges[index]._end,
           m_output_cl);
  len = strlen(numbers);
  if (len < RANGE_NUMBERS_LENGTH) {
    m_done += m_output_buf->write(numbers, len);
  }
  m_done += m_output_buf->write("\r\n\r\n", 4);
}

/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

/*
 * this function changes the response header to reflect this is
 * a Range response.
 */

void
RangeTransform::change_response_header()
{
  MIMEField *field;
  char *reason_phrase;
  HTTPStatus status_code;

  ink_release_assert(m_transform_resp);

  status_code = HTTP_STATUS_PARTIAL_CONTENT;
  m_transform_resp->status_set(status_code);
  reason_phrase = (char *)(http_hdr_reason_lookup(status_code));
  m_transform_resp->reason_set(reason_phrase, strlen(reason_phrase));

  if (m_num_range_fields > 1) {
    // set the right Content-Type for multiple entry Range
    field = m_transform_resp->field_find(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);

    if (field != nullptr) {
      m_transform_resp->field_delete(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    }

    field = m_transform_resp->field_create(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE);
    field->value_append(m_transform_resp->m_heap, m_transform_resp->m_mime, range_type, sizeof(range_type) - 1);

    m_transform_resp->field_attach(field);
  } else {
    char numbers[RANGE_NUMBERS_LENGTH];
    m_transform_resp->field_delete(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
    field = m_transform_resp->field_create(MIME_FIELD_CONTENT_RANGE, MIME_LEN_CONTENT_RANGE);
    snprintf(numbers, sizeof(numbers), "bytes %" PRId64 "-%" PRId64 "/%" PRId64, m_ranges[0]._start, m_ranges[0]._end, m_output_cl);
    field->value_set(m_transform_resp->m_heap, m_transform_resp->m_mime, numbers, strlen(numbers));
    m_transform_resp->field_attach(field);
  }
}

#undef RANGE_NUMBERS_LENGTH
