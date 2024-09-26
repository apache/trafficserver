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

#include "P_Net.h"

#include "P_NetAccept.h"
#include "iocore/net/ConnectionTracker.h"

#include "iocore/eventsystem/UnixSocket.h"

#include "tscore/ink_platform.h"
#include "tscore/InkErrno.h"

#include <termios.h>

#include <utility>

#define STATE_VIO_OFFSET   ((uintptr_t) & ((NetState *)0)->vio)
#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))

// Global
ClassAllocator<UnixNetVConnection> netVCAllocator("netVCAllocator");

namespace
{
DbgCtl dbg_ctl_socket{"socket"};
DbgCtl dbg_ctl_inactivity_cop{"inactivity_cop"};
DbgCtl dbg_ctl_iocore_net{"iocore_net"};

} // end anonymous namespace

//
// Reschedule a UnixNetVConnection by moving it
// onto or off of the ready_list
//
static inline void
read_reschedule(NetHandler *nh, UnixNetVConnection *vc)
{
  vc->ep.refresh(EVENTIO_READ);
  if (vc->read.triggered && vc->read.enabled) {
    nh->read_ready_list.in_or_enqueue(vc);
  } else {
    nh->read_ready_list.remove(vc);
  }
}

static inline void
write_reschedule(NetHandler *nh, UnixNetVConnection *vc)
{
  vc->ep.refresh(EVENTIO_WRITE);
  if (vc->write.triggered && vc->write.enabled) {
    nh->write_ready_list.in_or_enqueue(vc);
  } else {
    nh->write_ready_list.remove(vc);
  }
}

//
// Signal an event
//
static inline int
read_signal_and_update(int event, UnixNetVConnection *vc)
{
  vc->recursion++;
  if (vc->read.vio.cont && vc->read.vio.mutex == vc->read.vio.cont->mutex) {
    vc->read.vio.cont->handleEvent(event, &vc->read.vio);
  } else {
    if (vc->read.vio.cont) {
      Note("read_signal_and_update: mutexes are different? vc=%p, event=%d", vc, event);
    }
    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Dbg(dbg_ctl_inactivity_cop, "event %d: null read.vio cont, closing vc %p", event, vc);
      vc->closed = 1;
      break;
    default:
      Error("Unexpected event %d for vc %p", event, vc);
      ink_release_assert(0);
      break;
    }
  }
  if (!--vc->recursion && vc->closed) {
    /* BZ  31932 */
    ink_assert(vc->thread == this_ethread());
    vc->nh->free_netevent(vc);
    return EVENT_DONE;
  } else {
    return EVENT_CONT;
  }
}

static inline int
write_signal_and_update(int event, UnixNetVConnection *vc)
{
  vc->recursion++;
  if (vc->write.vio.cont && vc->write.vio.mutex == vc->write.vio.cont->mutex) {
    vc->write.vio.cont->handleEvent(event, &vc->write.vio);
  } else {
    if (vc->write.vio.cont) {
      Note("write_signal_and_update: mutexes are different? vc=%p, event=%d", vc, event);
    }
    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Dbg(dbg_ctl_inactivity_cop, "event %d: null write.vio cont, closing vc %p", event, vc);
      vc->closed = 1;
      break;
    default:
      Error("Unexpected event %d for vc %p", event, vc);
      ink_release_assert(0);
      break;
    }
  }
  if (!--vc->recursion && vc->closed) {
    /* BZ  31932 */
    ink_assert(vc->thread == this_ethread());
    vc->nh->free_netevent(vc);
    return EVENT_DONE;
  } else {
    return EVENT_CONT;
  }
}

static inline int
read_signal_done(int event, NetHandler *nh, UnixNetVConnection *vc)
{
  vc->read.enabled = 0;
  if (read_signal_and_update(event, vc) == EVENT_DONE) {
    return EVENT_DONE;
  } else {
    read_reschedule(nh, vc);
    return EVENT_CONT;
  }
}

static inline int
write_signal_done(int event, NetHandler *nh, UnixNetVConnection *vc)
{
  vc->write.enabled = 0;
  if (write_signal_and_update(event, vc) == EVENT_DONE) {
    return EVENT_DONE;
  } else {
    write_reschedule(nh, vc);
    return EVENT_CONT;
  }
}

static inline int
read_signal_error(NetHandler *nh, UnixNetVConnection *vc, int lerrno)
{
  vc->lerrno = lerrno;
  return read_signal_done(VC_EVENT_ERROR, nh, vc);
}

static inline int
write_signal_error(NetHandler *nh, UnixNetVConnection *vc, int lerrno)
{
  vc->lerrno = lerrno;
  return write_signal_done(VC_EVENT_ERROR, nh, vc);
}

bool
UnixNetVConnection::get_data(int id, void *data)
{
  union {
    TSVIO *vio;
    void  *data;
    int   *n;
  } ptr;

  ptr.data = data;

  switch (id) {
  case TS_API_DATA_READ_VIO:
    *ptr.vio = reinterpret_cast<TSVIO>(&this->read.vio);
    return true;
  case TS_API_DATA_WRITE_VIO:
    *ptr.vio = reinterpret_cast<TSVIO>(&this->write.vio);
    return true;
  case TS_API_DATA_CLOSED:
    *ptr.n = this->closed;
    return true;
  default:
    return false;
  }
}

VIO *
UnixNetVConnection::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  if (closed && !(c == nullptr && nbytes == 0 && buf == nullptr)) {
    Error("do_io_read invoked on closed vc %p, cont %p, nbytes %" PRId64 ", buf %p", this, c, nbytes, buf);
    return nullptr;
  }
  read.vio.op        = VIO::READ;
  read.vio.mutex     = c ? c->mutex : this->mutex;
  read.vio.cont      = c;
  read.vio.nbytes    = nbytes;
  read.vio.ndone     = 0;
  read.vio.vc_server = (VConnection *)this;
  if (buf) {
    read.vio.set_writer(buf);
    if (!read.enabled) {
      read.vio.reenable();
    }
  } else {
    read.vio.buffer.clear();
    read.enabled = 0;
  }
  return &read.vio;
}

VIO *
UnixNetVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *reader, bool owner)
{
  if (closed && !(c == nullptr && nbytes == 0 && reader == nullptr)) {
    Error("do_io_write invoked on closed vc %p, cont %p, nbytes %" PRId64 ", reader %p", this, c, nbytes, reader);
    return nullptr;
  }
  write.vio.op        = VIO::WRITE;
  write.vio.mutex     = c ? c->mutex : this->mutex;
  write.vio.cont      = c;
  write.vio.nbytes    = nbytes;
  write.vio.ndone     = 0;
  write.vio.vc_server = (VConnection *)this;
  if (reader) {
    ink_assert(!owner);
    write.vio.set_reader(reader);
    if (nbytes && !write.enabled) {
      write.vio.reenable();
    }
  } else {
    write.enabled = 0;
  }
  return &write.vio;
}

void
UnixNetVConnection::do_io_close(int alerrno /* = -1 */)
{
  // The vio continuations will be cleared in ::clear called from ::free_thread
  read.enabled    = 0;
  write.enabled   = 0;
  read.vio.nbytes = 0;
  read.vio.op     = VIO::NONE;

  if (netvc_context == NET_VCONNECTION_OUT) {
    // do not clear the iobufs yet to guard
    // against race condition with session pool closing
    Dbg(dbg_ctl_iocore_net, "delay vio buffer clear to protect against  race for vc %p", this);
  } else {
    // may be okay to delay for all VCs?
    read.vio.buffer.clear();
    write.vio.buffer.clear();
  }

  write.vio.nbytes = 0;
  write.vio.op     = VIO::NONE;

  EThread *t            = this_ethread();
  bool     close_inline = !recursion && (!nh || nh->mutex->thread_holding == t);

  INK_WRITE_MEMORY_BARRIER;
  if (alerrno && alerrno != -1) {
    this->lerrno = alerrno;
  }

  // Must mark for closed last in case this is a
  // cross thread migration scenario.
  if (alerrno == -1) {
    closed = 1;
  } else {
    closed = -1;
  }

  if (close_inline) {
    if (nh) {
      nh->free_netevent(this);
    } else {
      this->free_thread(t);
    }
  }
}

void
UnixNetVConnection::do_io_shutdown(ShutdownHowTo_t howto)
{
  switch (howto) {
  case IO_SHUTDOWN_READ:
    this->con.sock.shutdown(0);
    read.enabled = 0;
    read.vio.buffer.clear();
    read.vio.nbytes  = 0;
    read.vio.cont    = nullptr;
    f.shutdown      |= NetEvent::SHUTDOWN_READ;
    break;
  case IO_SHUTDOWN_WRITE:
    this->con.sock.shutdown(1);
    write.enabled = 0;
    write.vio.buffer.clear();
    write.vio.nbytes  = 0;
    write.vio.cont    = nullptr;
    f.shutdown       |= NetEvent::SHUTDOWN_WRITE;
    break;
  case IO_SHUTDOWN_READWRITE:
    this->con.sock.shutdown(2);
    read.enabled  = 0;
    write.enabled = 0;
    read.vio.buffer.clear();
    read.vio.nbytes = 0;
    write.vio.buffer.clear();
    write.vio.nbytes = 0;
    read.vio.cont    = nullptr;
    write.vio.cont   = nullptr;
    f.shutdown       = NetEvent::SHUTDOWN_READ | NetEvent::SHUTDOWN_WRITE;
    break;
  default:
    ink_assert(!"not reached");
  }
}

//
// Function used to reenable the VC for reading or
// writing.
//
void
UnixNetVConnection::reenable(VIO *vio)
{
  if (STATE_FROM_VIO(vio)->enabled) {
    return;
  }
  set_enabled(vio);
  if (!thread) {
    return;
  }
  EThread *t = vio->mutex->thread_holding;
  ink_assert(t == this_ethread());
  ink_release_assert(!closed);
  if (nh->mutex->thread_holding == t) {
    if (vio == &read.vio) {
      ep.modify(EVENTIO_READ);
      ep.refresh(EVENTIO_READ);
      if (read.triggered) {
        nh->read_ready_list.in_or_enqueue(this);
      } else {
        nh->read_ready_list.remove(this);
      }
    } else {
      ep.modify(EVENTIO_WRITE);
      ep.refresh(EVENTIO_WRITE);
      if (write.triggered) {
        nh->write_ready_list.in_or_enqueue(this);
      } else {
        nh->write_ready_list.remove(this);
      }
    }
  } else {
    MUTEX_TRY_LOCK(lock, nh->mutex, t);
    if (!lock.is_locked()) {
      if (vio == &read.vio) {
        int isin = ink_atomic_swap(&read.in_enabled_list, 1);
        if (!isin) {
          nh->read_enable_list.push(this);
        }
      } else {
        int isin = ink_atomic_swap(&write.in_enabled_list, 1);
        if (!isin) {
          nh->write_enable_list.push(this);
        }
      }
      if (likely(nh->thread)) {
        nh->thread->tail_cb->signalActivity();
      } else if (nh->trigger_event) {
        nh->trigger_event->ethread->tail_cb->signalActivity();
      }
    } else {
      if (vio == &read.vio) {
        ep.modify(EVENTIO_READ);
        ep.refresh(EVENTIO_READ);
        if (read.triggered) {
          nh->read_ready_list.in_or_enqueue(this);
        } else {
          nh->read_ready_list.remove(this);
        }
      } else {
        ep.modify(EVENTIO_WRITE);
        ep.refresh(EVENTIO_WRITE);
        if (write.triggered) {
          nh->write_ready_list.in_or_enqueue(this);
        } else {
          nh->write_ready_list.remove(this);
        }
      }
    }
  }
}

void
UnixNetVConnection::reenable_re(VIO *vio)
{
  if (!thread) {
    return;
  }
  EThread *t = vio->mutex->thread_holding;
  ink_assert(t == this_ethread());
  if (nh->mutex->thread_holding == t) {
    set_enabled(vio);
    if (vio == &read.vio) {
      ep.modify(EVENTIO_READ);
      ep.refresh(EVENTIO_READ);
      if (read.triggered) {
        net_read_io(nh);
      } else {
        nh->read_ready_list.remove(this);
      }
    } else {
      ep.modify(EVENTIO_WRITE);
      ep.refresh(EVENTIO_WRITE);
      if (write.triggered) {
        this->net_write_io(nh);
      } else {
        nh->write_ready_list.remove(this);
      }
    }
  } else {
    reenable(vio);
  }
}

UnixNetVConnection::UnixNetVConnection()
{
  SET_HANDLER(&UnixNetVConnection::startEvent);
}

// Private methods

void
UnixNetVConnection::set_enabled(VIO *vio)
{
  ink_assert(vio->mutex->thread_holding == this_ethread() && thread);
  ink_release_assert(!closed);
  STATE_FROM_VIO(vio)->enabled = 1;
  if (!next_inactivity_timeout_at && inactivity_timeout_in) {
    next_inactivity_timeout_at = ink_get_hrtime() + inactivity_timeout_in;
  }
}

// Read the data for a UnixNetVConnection.
// Rescheduling the UnixNetVConnection by moving the VC
// onto or off of the ready_list.
void
UnixNetVConnection::net_read_io(NetHandler *nh)
{
  NetState *s = &this->read;
  int64_t   r = 0;

  MUTEX_TRY_LOCK(lock, s->vio.mutex, thread);

  if (!lock.is_locked()) {
    read_reschedule(nh, this);
    return;
  }

  // It is possible that the closed flag got set from HttpSessionManager in the
  // global session pool case.  If so, the closed flag should be stable once we get the
  // s->vio.mutex (the global session pool mutex).
  if (this->closed) {
    this->nh->free_netevent(this);
    return;
  }
  // if it is not enabled.
  if (!s->enabled || s->vio.op != VIO::READ || s->vio.is_disabled()) {
    read_disable(nh, this);
    return;
  }

  MIOBufferAccessor &buf = s->vio.buffer;
  ink_assert(buf.writer());

  // if there is nothing to do, disable connection
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    read_disable(nh, this);
    return;
  }
  int64_t toread = buf.writer()->write_avail();
  if (toread > ntodo) {
    toread = ntodo;
  }

  // read data
  int64_t  rattempted = 0, total_read = 0;
  unsigned niov = 0;
  IOVec    tiovec[NET_MAX_IOV];
  if (toread) {
    IOBufferBlock *b = buf.writer()->first_write_block();
    do {
      niov       = 0;
      rattempted = 0;
      while (b && niov < NET_MAX_IOV) {
        int64_t a = b->write_avail();
        if (a > 0) {
          tiovec[niov].iov_base = b->_end;
          int64_t togo          = toread - total_read - rattempted;
          if (a > togo) {
            a = togo;
          }
          tiovec[niov].iov_len  = a;
          rattempted           += a;
          niov++;
          if (a >= togo) {
            break;
          }
        }
        b = b->next.get();
      }

      ink_assert(niov > 0);
      ink_assert(niov <= countof(tiovec));
      struct msghdr msg;

      ink_zero(msg);
      msg.msg_name    = const_cast<sockaddr *>(this->get_remote_addr());
      msg.msg_namelen = ats_ip_size(this->get_remote_addr());
      msg.msg_iov     = &tiovec[0];
      msg.msg_iovlen  = niov;
      r               = this->con.sock.recvmsg(&msg, 0);

      Metrics::Counter::increment(net_rsb.calls_to_read);

      total_read += rattempted;
    } while (rattempted && r == rattempted && total_read < toread);

    // if we have already moved some bytes successfully, summarize in r
    if (total_read != rattempted) {
      if (r <= 0) {
        r = total_read - rattempted;
      } else {
        r = total_read - rattempted + r;
      }
    }
    // check for errors
    if (r <= 0) {
      if (r == -EAGAIN || r == -ENOTCONN) {
        Metrics::Counter::increment(net_rsb.calls_to_read_nodata);
        this->read.triggered = 0;
        nh->read_ready_list.remove(this);
        return;
      }

      if (!r || r == -ECONNRESET) {
        this->read.triggered = 0;
        nh->read_ready_list.remove(this);
        read_signal_done(VC_EVENT_EOS, nh, this);
        return;
      }
      this->read.triggered = 0;
      read_signal_error(nh, this, static_cast<int>(-r));
      return;
    }
    Metrics::Counter::increment(net_rsb.read_bytes, r);
    Metrics::Counter::increment(net_rsb.read_bytes_count);

    // Add data to buffer and signal continuation.
    buf.writer()->fill(r);
#ifdef DEBUG
    if (buf.writer()->write_avail() <= 0) {
      Dbg(dbg_ctl_iocore_net, "read_from_net, read buffer full");
    }
#endif
    s->vio.ndone += r;
    this->netActivity();
  } else {
    r = 0;
  }

  // Signal read ready, check if user is not done
  if (r) {
    // If there are no more bytes to read, signal read complete
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      read_signal_done(VC_EVENT_READ_COMPLETE, nh, this);
      Dbg(dbg_ctl_iocore_net, "read_from_net, read finished - signal done");
      return;
    } else {
      if (read_signal_and_update(VC_EVENT_READ_READY, this) != EVENT_CONT) {
        return;
      }

      // change of lock... don't look at shared variables!
      if (lock.get_mutex() != s->vio.mutex.get()) {
        read_reschedule(nh, this);
        return;
      }
    }
  }

  // If here are is no more room, or nothing to do, disable the connection
  if (s->vio.ntodo() <= 0 || !s->enabled || !buf.writer()->write_avail()) {
    read_disable(nh, this);
    return;
  }

  read_reschedule(nh, this);
}

//
// Write the data for a UnixNetVConnection.
// Rescheduling the UnixNetVConnection when necessary.
//
void
UnixNetVConnection::net_write_io(NetHandler *nh)
{
  Metrics::Counter::increment(net_rsb.calls_to_writetonet);
  NetState     *s = &this->write;
  Continuation *c = this->write.vio.cont;

  MUTEX_TRY_LOCK(lock, s->vio.mutex, thread);

  if (!lock.is_locked() || lock.get_mutex() != s->vio.mutex.get()) {
    write_reschedule(nh, this);
    return;
  }

  if (this->has_error()) {
    this->lerrno = this->error;
    write_signal_and_update(VC_EVENT_ERROR, this);
    return;
  }

  // This function will always return true unless
  // this vc is an SSLNetVConnection.
  if (!this->getSSLHandShakeComplete()) {
    if (this->trackFirstHandshake()) {
      // Eat the first write-ready.  Until the TLS handshake is complete,
      // we should still be under the connect timeout and shouldn't bother
      // the state machine until the TLS handshake is complete
      this->write.triggered = 0;
      nh->write_ready_list.remove(this);
    }

    int err, ret;

    if (this->get_context() == NET_VCONNECTION_OUT) {
      ret = this->sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = this->sslStartHandShake(SSL_EVENT_SERVER, err);
    }

    if (ret == EVENT_ERROR) {
      this->write.triggered = 0;
      write_signal_error(nh, this, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT) {
      this->read.triggered = 0;
      nh->read_ready_list.remove(this);
      read_reschedule(nh, this);
    } else if (ret == SSL_HANDSHAKE_WANT_CONNECT || ret == SSL_HANDSHAKE_WANT_WRITE) {
      this->write.triggered = 0;
      nh->write_ready_list.remove(this);
      write_reschedule(nh, this);
    } else if (ret == EVENT_DONE) {
      this->write.triggered = 1;
      if (this->write.enabled) {
        nh->write_ready_list.in_or_enqueue(this);
      }
      // If this was driven by a zero length read, signal complete when
      // the handshake is complete. Otherwise set up for continuing read
      // operations.
      if (s->vio.ntodo() <= 0) {
        this->readSignalDone(VC_EVENT_WRITE_COMPLETE, nh);
      }
    } else {
      write_reschedule(nh, this);
    }

    return;
  }

  // If it is not enabled,add to WaitList.
  if (!s->enabled || s->vio.op != VIO::WRITE) {
    write_disable(nh, this);
    return;
  }

  // If there is nothing to do, disable
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    write_disable(nh, this);
    return;
  }

  MIOBufferAccessor &buf = s->vio.buffer;
  ink_assert(buf.writer());

  // Calculate the amount to write.
  int64_t towrite = buf.reader()->read_avail();
  if (towrite > ntodo) {
    towrite = ntodo;
  }

  int signalled = 0;

  // signal write ready to allow user to fill the buffer
  if (towrite != ntodo && !buf.writer()->high_water()) {
    if (write_signal_and_update(VC_EVENT_WRITE_READY, this) != EVENT_CONT) {
      return;
    } else if (c != s->vio.cont) { /* The write vio was updated in the handler */
      write_reschedule(nh, this);
      return;
    }

    ntodo = s->vio.ntodo();
    if (ntodo <= 0) {
      write_disable(nh, this);
      return;
    }

    signalled = 1;

    // Recalculate amount to write
    towrite = buf.reader()->read_avail();
    if (towrite > ntodo) {
      towrite = ntodo;
    }
  }

  // if there is nothing to do, disable
  ink_assert(towrite >= 0);
  if (towrite <= 0) {
    write_disable(nh, this);
    return;
  }

  int     needs         = 0;
  int64_t total_written = 0;
  int64_t r             = this->load_buffer_and_write(towrite, buf, total_written, needs);

  if (total_written > 0) {
    Metrics::Counter::increment(net_rsb.write_bytes, total_written);
    Metrics::Counter::increment(net_rsb.write_bytes_count);
    s->vio.ndone += total_written;
    this->netActivity();
  }

  // A write of 0 makes no sense since we tried to write more than 0.
  ink_assert(r != 0);
  // Either we wrote something or got an error.
  // check for errors
  if (r < 0) { // if the socket was not ready, add to WaitList
    if (r == -EAGAIN || r == -ENOTCONN || -r == EINPROGRESS) {
      Metrics::Counter::increment(net_rsb.calls_to_write_nodata);
      if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
        this->write.triggered = 0;
        nh->write_ready_list.remove(this);
        write_reschedule(nh, this);
      }

      if ((needs & EVENTIO_READ) == EVENTIO_READ) {
        this->read.triggered = 0;
        nh->read_ready_list.remove(this);
        read_reschedule(nh, this);
      }

      return;
    }

    this->write.triggered = 0;
    write_signal_error(nh, this, static_cast<int>(-r));
    return;
  } else {                                          // Wrote data.  Finished without error
    int wbe_event = this->write_buffer_empty_event; // save so we can clear if needed.

    // If the empty write buffer trap is set, clear it.
    if (!(buf.reader()->is_read_avail_more_than(0))) {
      this->write_buffer_empty_event = 0;
    }

    // If there are no more bytes to write, signal write complete,
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      write_signal_done(VC_EVENT_WRITE_COMPLETE, nh, this);
      return;
    }

    int e = 0;
    if (!signalled || (s->vio.ntodo() > 0 && !buf.writer()->high_water())) {
      e = VC_EVENT_WRITE_READY;
    } else if (wbe_event != this->write_buffer_empty_event) {
      // @a signalled means we won't send an event, and the event values differing means we
      // had a write buffer trap and cleared it, so we need to send it now.
      e = wbe_event;
    }

    if (e) {
      if (write_signal_and_update(e, this) != EVENT_CONT) {
        return;
      }

      // change of lock... don't look at shared variables!
      if (lock.get_mutex() != s->vio.mutex.get()) {
        write_reschedule(nh, this);
        return;
      }
    }

    if ((needs & EVENTIO_READ) == EVENTIO_READ) {
      read_reschedule(nh, this);
    }

    if (!(buf.reader()->is_read_avail_more_than(0))) {
      write_disable(nh, this);
      return;
    }

    if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
      write_reschedule(nh, this);
    }

    return;
  }
}

// This code was pulled out of write_to_net so
// I could overwrite it for the SSL implementation
// (SSL read does not support overlapped i/o)
// without duplicating all the code in write_to_net.
int64_t
UnixNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  int64_t         r            = 0;
  int64_t         try_to_write = 0;
  IOBufferReader *tmp_reader   = buf.reader()->clone();

  do {
    IOVec    tiovec[NET_MAX_IOV];
    unsigned niov = 0;
    try_to_write  = 0;

    while (niov < NET_MAX_IOV) {
      int64_t wavail = towrite - total_written - try_to_write;
      int64_t len    = tmp_reader->block_read_avail();

      // Check if we have done this block.
      if (len <= 0) {
        break;
      }

      // Check if the amount to write exceeds that in this buffer.
      if (len > wavail) {
        len = wavail;
      }

      if (len == 0) {
        break;
      }

      // build an iov entry
      tiovec[niov].iov_len  = len;
      tiovec[niov].iov_base = tmp_reader->start();
      niov++;

      try_to_write += len;
      tmp_reader->consume(len);
    }

    ink_assert(niov > 0);
    ink_assert(niov <= countof(tiovec));

    // If the platform doesn't support TCP Fast Open, verify that we
    // correctly disabled support in the socket option configuration.
    ink_assert(MSG_FASTOPEN != 0 || this->options.f_tcp_fastopen == false);
    struct msghdr msg;

    ink_zero(msg);
    msg.msg_name    = const_cast<sockaddr *>(this->get_remote_addr());
    msg.msg_namelen = ats_ip_size(this->get_remote_addr());
    msg.msg_iov     = &tiovec[0];
    msg.msg_iovlen  = niov;
    int flags       = 0;

    if (!this->con.is_connected && this->options.f_tcp_fastopen) {
      Metrics::Counter::increment(net_rsb.fastopen_attempts);
      flags = MSG_FASTOPEN;
    }
    r = con.sock.sendmsg(&msg, flags);
    if (!this->con.is_connected && this->options.f_tcp_fastopen) {
      if (r < 0) {
        if (r == -EINPROGRESS || r == -EWOULDBLOCK) {
          this->con.is_connected = true;
        }
      } else {
        Metrics::Counter::increment(net_rsb.fastopen_successes);
        this->con.is_connected = true;
      }
    }

    if (r > 0) {
      buf.reader()->consume(r);
      total_written += r;
    }

    Metrics::Counter::increment(net_rsb.calls_to_write);
  } while (r == try_to_write && total_written < towrite);

  tmp_reader->dealloc();

  needs |= EVENTIO_WRITE;

  return r;
}

void
UnixNetVConnection::readDisable(NetHandler *nh)
{
  read_disable(nh, this);
}

void
UnixNetVConnection::readSignalError(NetHandler *nh, int err)
{
  read_signal_error(nh, this, err);
}

int
UnixNetVConnection::readSignalDone(int event, NetHandler *nh)
{
  return (read_signal_done(event, nh, this));
}

int
UnixNetVConnection::readSignalAndUpdate(int event)
{
  return (read_signal_and_update(event, this));
}

// Interface so SSL inherited class can call some static in-line functions
// without affecting regular net stuff or copying a bunch of code into
// the header files.
void
UnixNetVConnection::readReschedule(NetHandler *nh)
{
  read_reschedule(nh, this);
}

void
UnixNetVConnection::writeReschedule(NetHandler *nh)
{
  write_reschedule(nh, this);
}

void
UnixNetVConnection::netActivity()
{
  Dbg(dbg_ctl_socket, "net_activity updating inactivity %" PRId64 ", NetVC=%p", this->inactivity_timeout_in, this);
  if (this->inactivity_timeout_in) {
    this->next_inactivity_timeout_at = ink_get_hrtime() + this->inactivity_timeout_in;
  } else {
    this->next_inactivity_timeout_at = 0;
  }
}

int
UnixNetVConnection::startEvent(int /* event ATS_UNUSED */, Event *e)
{
  MUTEX_TRY_LOCK(lock, get_NetHandler(e->ethread)->mutex, e->ethread);
  if (!lock.is_locked()) {
    e->schedule_in(HRTIME_MSECONDS(net_retry_delay));
    return EVENT_CONT;
  }
  if (!action_.cancelled) {
    connectUp(e->ethread, NO_FD);
  } else {
    get_NetHandler(e->ethread)->free_netevent(this);
  }
  return EVENT_DONE;
}

int
UnixNetVConnection::acceptEvent(int /* event ATS_UNUSED */, Event *e)
{
  EThread    *t = (e == nullptr) ? this_ethread() : e->ethread;
  NetHandler *h = get_NetHandler(t);

  thread = t;

  // Send this NetVC to NetHandler and start to polling read & write event.
  if (h->startIO(this) < 0) {
    this->free_thread(t);
    return EVENT_DONE;
  }

  // Switch vc->mutex from NetHandler->mutex to new mutex
  mutex = new_ProxyMutex();
  SCOPED_MUTEX_LOCK(lock2, mutex, t);

  // Setup a timeout callback handler.
  SET_HANDLER(&UnixNetVConnection::mainEvent);

  // Send this netvc to InactivityCop.
  nh->startCop(this);

  set_inactivity_timeout(inactivity_timeout_in);

  if (active_timeout_in) {
    UnixNetVConnection::set_active_timeout(active_timeout_in);
  }
  if (action_.continuation->mutex != nullptr) {
    MUTEX_TRY_LOCK(lock3, action_.continuation->mutex, t);
    if (!lock3.is_locked()) {
      ink_release_assert(0);
    }
    action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  } else {
    action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  }
  return EVENT_DONE;
}

//
// The main event for UnixNetVConnections.
// This is called by the Event subsystem to initialize the UnixNetVConnection
// and for active and inactivity timeouts.
//
int
UnixNetVConnection::mainEvent(int event, Event *e)
{
  ink_assert(event == VC_EVENT_ACTIVE_TIMEOUT || event == VC_EVENT_INACTIVITY_TIMEOUT);
  ink_assert(thread == this_ethread());

  MUTEX_TRY_LOCK(hlock, get_NetHandler(thread)->mutex, e->ethread);
  MUTEX_TRY_LOCK(rlock, read.vio.mutex ? read.vio.mutex : e->ethread->mutex, e->ethread);
  MUTEX_TRY_LOCK(wlock, write.vio.mutex ? write.vio.mutex : e->ethread->mutex, e->ethread);

  if (!hlock.is_locked() || !rlock.is_locked() || !wlock.is_locked() ||
      (read.vio.mutex && rlock.get_mutex() != read.vio.mutex.get()) ||
      (write.vio.mutex && wlock.get_mutex() != write.vio.mutex.get())) {
    return EVENT_CONT;
  }

  if (e->cancelled) {
    return EVENT_DONE;
  }

  int           signal_event;
  Continuation *reader_cont       = nullptr;
  Continuation *writer_cont       = nullptr;
  ink_hrtime   *signal_timeout_at = nullptr;

  switch (event) {
  // Treating immediate as inactivity timeout for any
  // deprecated remaining immediates. The previous code was using EVENT_INTERVAL
  // and EVENT_IMMEDIATE to distinguish active and inactive timeouts.
  // There appears to be some stray EVENT_IMMEDIATEs floating around
  case EVENT_IMMEDIATE:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    signal_event      = VC_EVENT_INACTIVITY_TIMEOUT;
    signal_timeout_at = &next_inactivity_timeout_at;
    break;
  case VC_EVENT_ACTIVE_TIMEOUT:
    signal_event      = VC_EVENT_ACTIVE_TIMEOUT;
    signal_timeout_at = &next_activity_timeout_at;
    break;
  default:
    ink_release_assert(!"BUG: unexpected event in UnixNetVConnection::mainEvent");
    break;
  }

  *signal_timeout_at = 0;
  writer_cont        = write.vio.cont;

  if (closed) {
    nh->free_netevent(this);
    return EVENT_DONE;
  }

  if (read.vio.op == VIO::READ && !(f.shutdown & NetEvent::SHUTDOWN_READ)) {
    reader_cont = read.vio.cont;
    if (read_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }

  if (!*signal_timeout_at && !closed && write.vio.op == VIO::WRITE && !(f.shutdown & NetEvent::SHUTDOWN_WRITE) &&
      reader_cont != write.vio.cont && writer_cont == write.vio.cont) {
    if (write_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }
  return EVENT_DONE;
}

int
UnixNetVConnection::populate(Connection &con_in, Continuation *c, void * /* arg ATS_UNUSED */)
{
  this->con.move(con_in);
  this->mutex  = c->mutex;
  this->thread = this_ethread();

  EThread    *t = this_ethread();
  NetHandler *h = get_NetHandler(t);

  MUTEX_TRY_LOCK(lock, h->mutex, t);
  if (!lock.is_locked()) {
    // Clean up and go home
    return EVENT_ERROR;
  }

  if (h->startIO(this) < 0) {
    Dbg(dbg_ctl_iocore_net, "populate : Failed to add to epoll list");
    return EVENT_ERROR;
  }

  ink_assert(this->nh != nullptr);
  SET_HANDLER(&UnixNetVConnection::mainEvent);
  this->nh->startCop(this);
  ink_assert(this->con.sock.is_ok());
  return EVENT_DONE;
}

int
UnixNetVConnection::connectUp(EThread *t, int fd)
{
  ink_assert(get_NetHandler(t)->mutex->thread_holding == this_ethread());
  int        res;
  UnixSocket sock{fd};

  thread = t;
  if (check_net_throttle(CONNECT)) {
    check_throttle_warning(CONNECT);
    res = -ENET_THROTTLING;
    Metrics::Counter::increment(net_rsb.connections_throttled_out);
    goto fail;
  }

  // Force family to agree with remote (server) address.
  options.ip_family = con.addr.sa.sa_family;

  //
  // Initialize this UnixNetVConnection
  //
  if (dbg_ctl_iocore_net.on()) {
    char addrbuf[INET6_ADDRSTRLEN];
    DbgPrint(dbg_ctl_iocore_net, "connectUp:: local_addr=%s:%d [%s]",
             options.local_ip.isValid() ? options.local_ip.toString(addrbuf, sizeof(addrbuf)) : "*", options.local_port,
             NetVCOptions::toString(options.addr_binding));
  }

  // If this is getting called from the TS API, then we are wiring up a file descriptor
  // provided by the caller. In that case, we know that the socket is already connected.
  if (!sock.is_ok()) {
    // Due to multi-threads system, the fd returned from con.open() may exceed the limitation of check_net_throttle().
    res = con.open(options);
    if (res != 0) {
      goto fail;
    }
  } else {
    int len = sizeof(con.sock_type);

    // This call will fail if fd is not a socket (e.g. it is a
    // eventfd or a regular file fd.  That is ok, because sock_type
    // is only used when setting up the socket.
    safe_getsockopt(fd, SOL_SOCKET, SO_TYPE, reinterpret_cast<char *>(&con.sock_type), &len);
    sock.set_nonblocking();
    con.sock         = sock;
    con.is_connected = true;
    con.is_bound     = true;
  }

  // Must connect after EventIO::Start() to avoid a race condition
  // when edge triggering is used.
  if ((res = get_NetHandler(t)->startIO(this)) < 0) {
    goto fail;
  }

  if (!sock.is_ok()) {
    res = con.connect(nullptr, options);
    if (res != 0) {
      // fast stopIO
      goto fail;
    }
  }

  // Did not fail, increment connection count
  Metrics::Gauge::increment(net_rsb.connections_currently_open);
  ink_release_assert(con.sock.is_ok());

  // Setup a timeout callback handler.
  SET_HANDLER(&UnixNetVConnection::mainEvent);
  // Send this netvc to InactivityCop.
  nh->startCop(this);

  set_inactivity_timeout(0);
  ink_assert(!active_timeout_in);
  this->set_local_addr();
  action_.continuation->handleEvent(NET_EVENT_OPEN, this);
  return CONNECT_SUCCESS;

fail:
  lerrno = -res;
  action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)static_cast<intptr_t>(res));
  if (con.sock.is_ok()) {
    con.sock = UnixSocket{NO_FD};
  }
  if (nullptr != nh) {
    nh->free_netevent(this);
  } else {
    this->free_thread(t);
  }
  return CONNECT_FAILURE;
}

void
UnixNetVConnection::clear()
{
  // clear timeout variables
  next_inactivity_timeout_at = 0;
  next_activity_timeout_at   = 0;
  inactivity_timeout_in      = 0;
  active_timeout_in          = 0;

  // clear variables for reuse
  this->mutex.clear();
  action_.mutex.clear();
  got_remote_addr = false;
  got_local_addr  = false;
  attributes      = 0;
  read.vio.mutex.clear();
  write.vio.mutex.clear();
  flags               = 0;
  nh                  = nullptr;
  read.triggered      = 0;
  write.triggered     = 0;
  read.enabled        = 0;
  write.enabled       = 0;
  read.vio.cont       = nullptr;
  write.vio.cont      = nullptr;
  read.vio.vc_server  = nullptr;
  write.vio.vc_server = nullptr;
  options.reset();
  if (netvc_context == NET_VCONNECTION_OUT) {
    read.vio.buffer.clear();
    write.vio.buffer.clear();
  }
  closed        = 0;
  netvc_context = NET_VCONNECTION_UNSET;
  ink_assert(!read.ready_link.prev && !read.ready_link.next);
  ink_assert(!read.enable_link.next);
  ink_assert(!write.ready_link.prev && !write.ready_link.next);
  ink_assert(!write.enable_link.next);
  ink_assert(!link.next && !link.prev);
}

void
UnixNetVConnection::free_thread(EThread *t)
{
  Dbg(dbg_ctl_iocore_net, "Entering UnixNetVConnection::free()");

  ink_release_assert(t == this_ethread());

  // close socket fd
  if (con.sock.is_ok()) {
    release_inbound_connection_tracking();
    Metrics::Gauge::decrement(net_rsb.connections_currently_open);
  }
  con.close();

  if (is_tunnel_endpoint()) {
    Dbg(dbg_ctl_iocore_net, "Freeing UnixNetVConnection that is tunnel endpoint");

    Metrics::Gauge::decrement(([&]() -> Metrics::Gauge::AtomicType * {
      switch (get_context()) {
      case NET_VCONNECTION_IN:
        return net_rsb.tunnel_current_client_connections_blind_tcp;
      case NET_VCONNECTION_OUT:
        return net_rsb.tunnel_current_server_connections_blind_tcp;
      default:
        ink_release_assert(false);
      }
    })());
  }

  clear();
  SET_CONTINUATION_HANDLER(this, &UnixNetVConnection::startEvent);
  ink_assert(!con.sock.is_ok());
  ink_assert(t == this_ethread());

  if (from_accept_thread) {
    netVCAllocator.free(this);
  } else {
    THREAD_FREE(this, netVCAllocator, t);
  }
}

void
UnixNetVConnection::apply_options()
{
  con.apply_options(options);
}

TS_INLINE void
UnixNetVConnection::set_inactivity_timeout(ink_hrtime timeout_in)
{
  Dbg(dbg_ctl_socket, "Set inactive timeout=%" PRId64 ", for NetVC=%p", timeout_in, this);
  inactivity_timeout_in      = timeout_in;
  next_inactivity_timeout_at = (timeout_in > 0) ? ink_get_hrtime() + inactivity_timeout_in : 0;
}

TS_INLINE void
UnixNetVConnection::set_default_inactivity_timeout(ink_hrtime timeout_in)
{
  Dbg(dbg_ctl_socket, "Set default inactive timeout=%" PRId64 ", for NetVC=%p", timeout_in, this);
  default_inactivity_timeout_in = timeout_in;
}

TS_INLINE bool
UnixNetVConnection::is_default_inactivity_timeout()
{
  return (use_default_inactivity_timeout && inactivity_timeout_in == 0);
}

/*
 * Close down the current netVC.  Save aside the socket and SSL information
 * and create new netVC in the current thread/netVC
 */
UnixNetVConnection *
UnixNetVConnection::migrateToCurrentThread(Continuation *cont, EThread *t)
{
  NetHandler *client_nh = get_NetHandler(t);
  ink_assert(client_nh);
  if (this->nh == client_nh) {
    // We're already there!
    return this;
  }

  Connection hold_con;
  hold_con.move(this->con);

  void *arg = this->_prepareForMigration();

  // Do_io_close will signal the VC to be freed on the original thread
  // Since we moved the con context, the fd will not be closed
  // Go ahead and remove the fd from the original thread's epoll structure, so it is not
  // processed on two threads simultaneously
  this->ep.stop();

  // Create new VC:
  UnixNetVConnection *newvc = static_cast<UnixNetVConnection *>(this->_getNetProcessor()->allocate_vc(t));
  ink_assert(newvc != nullptr);
  if (newvc->populate(hold_con, cont, arg) != EVENT_DONE) {
    newvc->do_io_close();
    newvc = nullptr;
  }
  if (newvc) {
    newvc->set_context(get_context());
    newvc->options = this->options;
  }

  // Do not mark this closed until the end so it does not get freed by the other thread too soon
  this->do_io_close();
  return newvc;
}

void *
UnixNetVConnection::_prepareForMigration()
{
  return nullptr;
}

NetProcessor *
UnixNetVConnection::_getNetProcessor()
{
  return &netProcessor;
}

void
UnixNetVConnection::add_to_keep_alive_queue()
{
  MUTEX_TRY_LOCK(lock, nh->mutex, this_ethread());
  if (lock.is_locked()) {
    nh->add_to_keep_alive_queue(this);
  } else {
    ink_release_assert(!"BUG: It must have acquired the NetHandler's lock before doing anything on keep_alive_queue.");
  }
}

void
UnixNetVConnection::remove_from_keep_alive_queue()
{
  MUTEX_TRY_LOCK(lock, nh->mutex, this_ethread());
  if (lock.is_locked()) {
    nh->remove_from_keep_alive_queue(this);
  } else {
    ink_release_assert(!"BUG: It must have acquired the NetHandler's lock before doing anything on keep_alive_queue.");
  }
}

bool
UnixNetVConnection::add_to_active_queue()
{
  bool result = false;

  MUTEX_TRY_LOCK(lock, nh->mutex, this_ethread());
  if (lock.is_locked()) {
    result = nh->add_to_active_queue(this);
  } else {
    ink_release_assert(!"BUG: It must have acquired the NetHandler's lock before doing anything on active_queue.");
  }
  return result;
}

void
UnixNetVConnection::remove_from_active_queue()
{
  MUTEX_TRY_LOCK(lock, nh->mutex, this_ethread());
  if (lock.is_locked()) {
    nh->remove_from_active_queue(this);
  } else {
    ink_release_assert(!"BUG: It must have acquired the NetHandler's lock before doing anything on active_queue.");
  }
}

void
UnixNetVConnection::enable_inbound_connection_tracking(std::shared_ptr<ConnectionTracker::Group> group)
{
  ink_assert(nullptr == conn_track_group);
  conn_track_group = std::move(group);
}

void
UnixNetVConnection::release_inbound_connection_tracking()
{
  // Update upstream connection tracking data if present.
  if (conn_track_group) {
    conn_track_group->release();
    conn_track_group.reset();
  }
}

int
UnixNetVConnection::populate_protocol(std::string_view *results, int n) const
{
  int retval = 0;
  if (n > retval) {
    if (!(results[retval] = options.get_proto_string()).empty()) {
      ++retval;
    }
    if (n > retval) {
      if (!(results[retval] = options.get_family_string()).empty()) {
        ++retval;
      }
    }
  }
  return retval;
}

const char *
UnixNetVConnection::protocol_contains(std::string_view tag) const
{
  std::string_view retval = options.get_proto_string();
  if (!IsNoCasePrefixOf(tag, retval)) { // didn't match IP level, check TCP level
    retval = options.get_family_string();
    if (!IsNoCasePrefixOf(tag, retval)) { // no match here either, return empty.
      ink_zero(retval);
    }
  }
  return retval.data();
}

int
UnixNetVConnection::set_tcp_congestion_control([[maybe_unused]] int side)
{
#ifdef TCP_CONGESTION
  std::string_view ccp;

  if (side == CLIENT_SIDE) {
    ccp = net_ccp_in;
  } else {
    ccp = net_ccp_out;
  }

  if (!ccp.empty()) {
    int rv = setsockopt(con.sock.get_fd(), IPPROTO_TCP, TCP_CONGESTION, static_cast<const void *>(ccp.data()), ccp.size());

    if (rv < 0) {
      Error("Unable to set TCP congestion control on socket %d to \"%s\", errno=%d (%s)", con.sock.get_fd(), ccp.data(), errno,
            strerror(errno));
    } else {
      Dbg(dbg_ctl_socket, "Setting TCP congestion control on socket [%d] to \"%s\" -> %d", con.sock.get_fd(), ccp.data(), rv);
    }
    return 0;
  }
  return -1;
#else
  Dbg(dbg_ctl_socket, "Setting TCP congestion control is not supported on this platform.");
  return -1;
#endif
}

void
UnixNetVConnection::mark_as_tunnel_endpoint()
{
  Dbg(dbg_ctl_iocore_net, "Entering UnixNetVConnection::mark_as_tunnel_endpoint()");

  ink_assert(!_is_tunnel_endpoint);

  _is_tunnel_endpoint = true;

  switch (get_context()) {
  case NET_VCONNECTION_IN:
    _in_context_tunnel();
    break;
  case NET_VCONNECTION_OUT:
    _out_context_tunnel();
    break;
  default:
    ink_release_assert(false);
  }
}

void
UnixNetVConnection::_in_context_tunnel()
{
  Metrics::Counter::increment(net_rsb.tunnel_total_client_connections_blind_tcp);
  Metrics::Gauge::increment(net_rsb.tunnel_current_client_connections_blind_tcp);
}

void
UnixNetVConnection::_out_context_tunnel()
{
  Metrics::Counter::increment(net_rsb.tunnel_total_server_connections_blind_tcp);
  Metrics::Gauge::increment(net_rsb.tunnel_current_server_connections_blind_tcp);
}
