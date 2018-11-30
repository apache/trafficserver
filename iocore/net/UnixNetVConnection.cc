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
#include "tscore/ink_platform.h"
#include "tscore/InkErrno.h"
#include "Log.h"

#include <termios.h>

#define STATE_VIO_OFFSET ((uintptr_t) & ((NetState *)0)->vio)
#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))

// Global
ClassAllocator<UnixNetVConnection> netVCAllocator("netVCAllocator");

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

void
net_activity(UnixNetVConnection *vc, EThread *thread)
{
  Debug("socket", "net_activity updating inactivity %" PRId64 ", NetVC=%p", vc->inactivity_timeout_in, vc);
  (void)thread;
  if (vc->inactivity_timeout_in) {
    vc->next_inactivity_timeout_at = Thread::get_hrtime() + vc->inactivity_timeout_in;
  } else {
    vc->next_inactivity_timeout_at = 0;
  }
}

//
// Signal an event
//
static inline int
read_signal_and_update(int event, UnixNetVConnection *vc)
{
  vc->recursion++;
  if (vc->read.vio.cont) {
    vc->read.vio.cont->handleEvent(event, &vc->read.vio);
  } else {
    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Debug("inactivity_cop", "event %d: null read.vio cont, closing vc %p", event, vc);
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
    vc->nh->free_netvc(vc);
    return EVENT_DONE;
  } else {
    return EVENT_CONT;
  }
}

static inline int
write_signal_and_update(int event, UnixNetVConnection *vc)
{
  vc->recursion++;
  if (vc->write.vio.cont) {
    vc->write.vio.cont->handleEvent(event, &vc->write.vio);
  } else {
    switch (event) {
    case VC_EVENT_EOS:
    case VC_EVENT_ERROR:
    case VC_EVENT_ACTIVE_TIMEOUT:
    case VC_EVENT_INACTIVITY_TIMEOUT:
      Debug("inactivity_cop", "event %d: null write.vio cont, closing vc %p", event, vc);
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
    vc->nh->free_netvc(vc);
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

// Read the data for a UnixNetVConnection.
// Rescheduling the UnixNetVConnection by moving the VC
// onto or off of the ready_list.
// Had to wrap this function with net_read_io for SSL.
static void
read_from_net(NetHandler *nh, UnixNetVConnection *vc, EThread *thread)
{
  NetState *s       = &vc->read;
  ProxyMutex *mutex = thread->mutex.get();
  int64_t r         = 0;

  MUTEX_TRY_LOCK(lock, s->vio.mutex, thread);

  if (!lock.is_locked()) {
    read_reschedule(nh, vc);
    return;
  }

  // It is possible that the closed flag got set from HttpSessionManager in the
  // global session pool case.  If so, the closed flag should be stable once we get the
  // s->vio.mutex (the global session pool mutex).
  if (vc->closed) {
    vc->nh->free_netvc(vc);
    return;
  }
  // if it is not enabled.
  if (!s->enabled || s->vio.op != VIO::READ) {
    read_disable(nh, vc);
    return;
  }

  MIOBufferAccessor &buf = s->vio.buffer;
  ink_assert(buf.writer());

  // if there is nothing to do, disable connection
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    read_disable(nh, vc);
    return;
  }
  int64_t toread = buf.writer()->write_avail();
  if (toread > ntodo) {
    toread = ntodo;
  }

  // read data
  int64_t rattempted = 0, total_read = 0;
  unsigned niov = 0;
  IOVec tiovec[NET_MAX_IOV];
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
          tiovec[niov].iov_len = a;
          rattempted += a;
          niov++;
          if (a >= togo) {
            break;
          }
        }
        b = b->next.get();
      }

      ink_assert(niov > 0);
      ink_assert(niov <= countof(tiovec));
      r = socketManager.readv(vc->con.fd, &tiovec[0], niov);

      NET_INCREMENT_DYN_STAT(net_calls_to_read_stat);

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
        NET_INCREMENT_DYN_STAT(net_calls_to_read_nodata_stat);
        vc->read.triggered = 0;
        nh->read_ready_list.remove(vc);
        return;
      }

      if (!r || r == -ECONNRESET) {
        vc->read.triggered = 0;
        nh->read_ready_list.remove(vc);
        read_signal_done(VC_EVENT_EOS, nh, vc);
        return;
      }
      vc->read.triggered = 0;
      read_signal_error(nh, vc, (int)-r);
      return;
    }
    NET_SUM_DYN_STAT(net_read_bytes_stat, r);

    // Add data to buffer and signal continuation.
    buf.writer()->fill(r);
#ifdef DEBUG
    if (buf.writer()->write_avail() <= 0)
      Debug("iocore_net", "read_from_net, read buffer full");
#endif
    s->vio.ndone += r;
    net_activity(vc, thread);
  } else {
    r = 0;
  }

  // Signal read ready, check if user is not done
  if (r) {
    // If there are no more bytes to read, signal read complete
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      read_signal_done(VC_EVENT_READ_COMPLETE, nh, vc);
      Debug("iocore_net", "read_from_net, read finished - signal done");
      return;
    } else {
      if (read_signal_and_update(VC_EVENT_READ_READY, vc) != EVENT_CONT) {
        return;
      }

      // change of lock... don't look at shared variables!
      if (lock.get_mutex() != s->vio.mutex.get()) {
        read_reschedule(nh, vc);
        return;
      }
    }
  }
  // If here are is no more room, or nothing to do, disable the connection
  if (s->vio.ntodo() <= 0 || !s->enabled || !buf.writer()->write_avail()) {
    read_disable(nh, vc);
    return;
  }

  read_reschedule(nh, vc);
}

//
// Write the data for a UnixNetVConnection.
// Rescheduling the UnixNetVConnection when necessary.
//
void
write_to_net(NetHandler *nh, UnixNetVConnection *vc, EThread *thread)
{
  ProxyMutex *mutex = thread->mutex.get();

  NET_INCREMENT_DYN_STAT(net_calls_to_writetonet_stat);
  NET_INCREMENT_DYN_STAT(net_calls_to_writetonet_afterpoll_stat);

  write_to_net_io(nh, vc, thread);
}

void
write_to_net_io(NetHandler *nh, UnixNetVConnection *vc, EThread *thread)
{
  NetState *s       = &vc->write;
  ProxyMutex *mutex = thread->mutex.get();

  MUTEX_TRY_LOCK(lock, s->vio.mutex, thread);

  if (!lock.is_locked() || lock.get_mutex() != s->vio.mutex.get()) {
    write_reschedule(nh, vc);
    return;
  }

  // This function will always return true unless
  // vc is an SSLNetVConnection.
  if (!vc->getSSLHandShakeComplete()) {
    if (vc->trackFirstHandshake()) {
      // Send the write ready on up to the state machine
      write_signal_and_update(VC_EVENT_WRITE_READY, vc);
      vc->write.triggered = 0;
      nh->write_ready_list.remove(vc);
    }

    int err, ret;

    if (vc->get_context() == NET_VCONNECTION_OUT) {
      ret = vc->sslStartHandShake(SSL_EVENT_CLIENT, err);
    } else {
      ret = vc->sslStartHandShake(SSL_EVENT_SERVER, err);
    }

    if (ret == EVENT_ERROR) {
      vc->write.triggered = 0;
      write_signal_error(nh, vc, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT) {
      vc->read.triggered = 0;
      nh->read_ready_list.remove(vc);
      read_reschedule(nh, vc);
    } else if (ret == SSL_HANDSHAKE_WANT_CONNECT || ret == SSL_HANDSHAKE_WANT_WRITE) {
      vc->write.triggered = 0;
      nh->write_ready_list.remove(vc);
      write_reschedule(nh, vc);
    } else if (ret == EVENT_DONE) {
      vc->write.triggered = 1;
      if (vc->write.enabled) {
        nh->write_ready_list.in_or_enqueue(vc);
      }
    } else {
      write_reschedule(nh, vc);
    }

    return;
  }

  // If it is not enabled,add to WaitList.
  if (!s->enabled || s->vio.op != VIO::WRITE) {
    write_disable(nh, vc);
    return;
  }

  // If there is nothing to do, disable
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    write_disable(nh, vc);
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
  if (towrite != ntodo && buf.writer()->write_avail()) {
    if (write_signal_and_update(VC_EVENT_WRITE_READY, vc) != EVENT_CONT) {
      return;
    }

    ntodo = s->vio.ntodo();
    if (ntodo <= 0) {
      write_disable(nh, vc);
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
    write_disable(nh, vc);
    return;
  }

  int needs             = 0;
  int64_t total_written = 0;
  int64_t r             = vc->load_buffer_and_write(towrite, buf, total_written, needs);

  if (total_written > 0) {
    NET_SUM_DYN_STAT(net_write_bytes_stat, total_written);
    s->vio.ndone += total_written;
    net_activity(vc, thread);
  }

  // A write of 0 makes no sense since we tried to write more than 0.
  ink_assert(r != 0);
  // Either we wrote something or got an error.
  // check for errors
  if (r < 0) { // if the socket was not ready, add to WaitList
    if (r == -EAGAIN || r == -ENOTCONN || -r == EINPROGRESS) {
      NET_INCREMENT_DYN_STAT(net_calls_to_write_nodata_stat);
      if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
        vc->write.triggered = 0;
        nh->write_ready_list.remove(vc);
        write_reschedule(nh, vc);
      }

      if ((needs & EVENTIO_READ) == EVENTIO_READ) {
        vc->read.triggered = 0;
        nh->read_ready_list.remove(vc);
        read_reschedule(nh, vc);
      }

      return;
    }

    vc->write.triggered = 0;
    write_signal_error(nh, vc, (int)-total_written);
    return;
  } else {                                        // Wrote data.  Finished without error
    int wbe_event = vc->write_buffer_empty_event; // save so we can clear if needed.

    // If the empty write buffer trap is set, clear it.
    if (!(buf.reader()->is_read_avail_more_than(0))) {
      vc->write_buffer_empty_event = 0;
    }

    // If there are no more bytes to write, signal write complete,
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      write_signal_done(VC_EVENT_WRITE_COMPLETE, nh, vc);
      return;
    }

    int e = 0;
    if (!signalled) {
      e = VC_EVENT_WRITE_READY;
    } else if (wbe_event != vc->write_buffer_empty_event) {
      // @a signalled means we won't send an event, and the event values differing means we
      // had a write buffer trap and cleared it, so we need to send it now.
      e = wbe_event;
    }

    if (e) {
      if (write_signal_and_update(e, vc) != EVENT_CONT) {
        return;
      }

      // change of lock... don't look at shared variables!
      if (lock.get_mutex() != s->vio.mutex.get()) {
        write_reschedule(nh, vc);
        return;
      }
    }

    if ((needs & EVENTIO_READ) == EVENTIO_READ) {
      read_reschedule(nh, vc);
    }

    if (!(buf.reader()->is_read_avail_more_than(0))) {
      write_disable(nh, vc);
      return;
    }

    if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
      write_reschedule(nh, vc);
    }

    return;
  }
}

bool
UnixNetVConnection::get_data(int id, void *data)
{
  union {
    TSVIO *vio;
    void *data;
    int *n;
  } ptr;

  ptr.data = data;

  switch (id) {
  case TS_API_DATA_READ_VIO:
    *ptr.vio = (TSVIO) & this->read.vio;
    return true;
  case TS_API_DATA_WRITE_VIO:
    *ptr.vio = (TSVIO) & this->write.vio;
    return true;
  case TS_API_DATA_CLOSED:
    *ptr.n = this->closed;
    return true;
  default:
    return false;
  }
}

int64_t
UnixNetVConnection::outstanding()
{
  int n;
  int ret = ioctl(this->get_socket(), TIOCOUTQ, &n);
  // if there was an error (such as ioctl doesn't support this call on this platform) then
  // we return -1
  if (ret == -1) {
    return ret;
  }
  return n;
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
    read.vio.buffer.writer_for(buf);
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
    write.vio.buffer.reader_for(reader);
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
  // FIXME: the nh must not nullptr.
  ink_assert(nh);

  read.enabled  = 0;
  write.enabled = 0;
  read.vio.buffer.clear();
  read.vio.nbytes = 0;
  read.vio.op     = VIO::NONE;
  read.vio.cont   = nullptr;
  write.vio.buffer.clear();
  write.vio.nbytes = 0;
  write.vio.op     = VIO::NONE;
  write.vio.cont   = nullptr;

  EThread *t        = this_ethread();
  bool close_inline = !recursion && (!nh || nh->mutex->thread_holding == t);

  INK_WRITE_MEMORY_BARRIER;
  if (alerrno && alerrno != -1) {
    this->lerrno = alerrno;
  }
  if (alerrno == -1) {
    closed = 1;
  } else {
    closed = -1;
  }

  if (close_inline) {
    if (nh) {
      nh->free_netvc(this);
    } else {
      free(t);
    }
  }
}

void
UnixNetVConnection::do_io_shutdown(ShutdownHowTo_t howto)
{
  switch (howto) {
  case IO_SHUTDOWN_READ:
    socketManager.shutdown(((UnixNetVConnection *)this)->con.fd, 0);
    read.enabled = 0;
    read.vio.buffer.clear();
    read.vio.nbytes = 0;
    read.vio.cont   = nullptr;
    f.shutdown |= NET_VC_SHUTDOWN_READ;
    break;
  case IO_SHUTDOWN_WRITE:
    socketManager.shutdown(((UnixNetVConnection *)this)->con.fd, 1);
    write.enabled = 0;
    write.vio.buffer.clear();
    write.vio.nbytes = 0;
    write.vio.cont   = nullptr;
    f.shutdown |= NET_VC_SHUTDOWN_WRITE;
    break;
  case IO_SHUTDOWN_READWRITE:
    socketManager.shutdown(((UnixNetVConnection *)this)->con.fd, 2);
    read.enabled  = 0;
    write.enabled = 0;
    read.vio.buffer.clear();
    read.vio.nbytes = 0;
    write.vio.buffer.clear();
    write.vio.nbytes = 0;
    read.vio.cont    = nullptr;
    write.vio.cont   = nullptr;
    f.shutdown       = NET_VC_SHUTDOWN_READ | NET_VC_SHUTDOWN_WRITE;
    break;
  default:
    ink_assert(!"not reached");
  }
}

int
OOB_callback::retry_OOB_send(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
{
  ink_assert(mutex->thread_holding == this_ethread());
  // the NetVC and the OOB_callback share a mutex
  server_vc->oob_ptr = nullptr;
  server_vc->send_OOB(server_cont, data, length);
  delete this;
  return EVENT_DONE;
}

void
UnixNetVConnection::cancel_OOB()
{
  UnixNetVConnection *u = (UnixNetVConnection *)this;
  if (u->oob_ptr) {
    if (u->oob_ptr->trigger) {
      u->oob_ptr->trigger->cancel_action();
      u->oob_ptr->trigger = nullptr;
    }
    delete u->oob_ptr;
    u->oob_ptr = nullptr;
  }
}

Action *
UnixNetVConnection::send_OOB(Continuation *cont, char *buf, int len)
{
  UnixNetVConnection *u = (UnixNetVConnection *)this;
  ink_assert(len > 0);
  ink_assert(buf);
  ink_assert(!u->oob_ptr);
  int written;
  ink_assert(cont->mutex->thread_holding == this_ethread());
  written = socketManager.send(u->con.fd, buf, len, MSG_OOB);
  if (written == len) {
    cont->handleEvent(VC_EVENT_OOB_COMPLETE, nullptr);
    return ACTION_RESULT_DONE;
  } else if (!written) {
    cont->handleEvent(VC_EVENT_EOS, nullptr);
    return ACTION_RESULT_DONE;
  }
  if (written > 0 && written < len) {
    u->oob_ptr          = new OOB_callback(mutex, this, cont, buf + written, len - written);
    u->oob_ptr->trigger = mutex->thread_holding->schedule_in_local(u->oob_ptr, HRTIME_MSECONDS(10));
    return u->oob_ptr->trigger;
  } else {
    // should be a rare case : taking a new continuation should not be
    // expensive for this
    written = -errno;
    ink_assert(written == -EAGAIN || written == -ENOTCONN);
    u->oob_ptr          = new OOB_callback(mutex, this, cont, buf, len);
    u->oob_ptr->trigger = mutex->thread_holding->schedule_in_local(u->oob_ptr, HRTIME_MSECONDS(10));
    return u->oob_ptr->trigger;
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
        net_read_io(nh, t);
      } else {
        nh->read_ready_list.remove(this);
      }
    } else {
      ep.modify(EVENTIO_WRITE);
      ep.refresh(EVENTIO_WRITE);
      if (write.triggered) {
        write_to_net(nh, this, t);
      } else {
        nh->write_ready_list.remove(this);
      }
    }
  } else {
    reenable(vio);
  }
}

UnixNetVConnection::UnixNetVConnection()
  : closed(0),
    inactivity_timeout_in(0),
    active_timeout_in(0),
    next_inactivity_timeout_at(0),
    next_activity_timeout_at(0),
    nh(nullptr),
    id(0),
    flags(0),
    recursion(0),
    submit_time(0),
    oob_ptr(nullptr),
    from_accept_thread(false),
    accept_object(nullptr)
{
  SET_HANDLER((NetVConnHandler)&UnixNetVConnection::startEvent);
}

// Private methods

void
UnixNetVConnection::set_enabled(VIO *vio)
{
  ink_assert(vio->mutex->thread_holding == this_ethread() && thread);
  ink_release_assert(!closed);
  STATE_FROM_VIO(vio)->enabled = 1;
  if (!next_inactivity_timeout_at && inactivity_timeout_in) {
    next_inactivity_timeout_at = Thread::get_hrtime() + inactivity_timeout_in;
  }
}

void
UnixNetVConnection::net_read_io(NetHandler *nh, EThread *lthread)
{
  read_from_net(nh, this, lthread);
}

// This code was pulled out of write_to_net so
// I could overwrite it for the SSL implementation
// (SSL read does not support overlapped i/o)
// without duplicating all the code in write_to_net.
int64_t
UnixNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  int64_t r                  = 0;
  int64_t try_to_write       = 0;
  IOBufferReader *tmp_reader = buf.reader()->clone();

  do {
    IOVec tiovec[NET_MAX_IOV];
    unsigned niov = 0;
    try_to_write  = 0;

    while (niov < NET_MAX_IOV) {
      int64_t wavail = towrite - total_written;
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

    if (!this->con.is_connected && this->options.f_tcp_fastopen) {
      struct msghdr msg;

      ink_zero(msg);
      msg.msg_name    = const_cast<sockaddr *>(this->get_remote_addr());
      msg.msg_namelen = ats_ip_size(this->get_remote_addr());
      msg.msg_iov     = &tiovec[0];
      msg.msg_iovlen  = niov;

      NET_INCREMENT_DYN_STAT(net_fastopen_attempts_stat);

      r = socketManager.sendmsg(con.fd, &msg, MSG_FASTOPEN);
      if (r < 0) {
        if (r == -EINPROGRESS || r == -EWOULDBLOCK) {
          this->con.is_connected = true;
        }
      } else {
        NET_INCREMENT_DYN_STAT(net_fastopen_successes_stat);
        this->con.is_connected = true;
      }

    } else {
      r = socketManager.writev(con.fd, &tiovec[0], niov);
    }

    if (r > 0) {
      buf.reader()->consume(r);
      total_written += r;
    }

    ProxyMutex *mutex = thread->mutex.get();
    NET_INCREMENT_DYN_STAT(net_calls_to_write_stat);
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
UnixNetVConnection::netActivity(EThread *lthread)
{
  net_activity(this, lthread);
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
    free(e->ethread);
  }
  return EVENT_DONE;
}

int
UnixNetVConnection::acceptEvent(int event, Event *e)
{
  EThread *t    = (e == nullptr) ? this_ethread() : e->ethread;
  NetHandler *h = get_NetHandler(t);

  thread = t;

  // Send this NetVC to NetHandler and start to polling read & write event.
  if (h->startIO(this) < 0) {
    free(t);
    return EVENT_DONE;
  }

  // Switch vc->mutex from NetHandler->mutex to new mutex
  mutex = new_ProxyMutex();
  SCOPED_MUTEX_LOCK(lock2, mutex, t);

  // Setup a timeout callback handler.
  SET_HANDLER((NetVConnHandler)&UnixNetVConnection::mainEvent);

  // Send this netvc to InactivityCop.
  nh->startCop(this);

  if (inactivity_timeout_in) {
    UnixNetVConnection::set_inactivity_timeout(inactivity_timeout_in);
  } else {
    set_inactivity_timeout(0);
  }

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
  ink_assert(event == EVENT_IMMEDIATE || event == EVENT_INTERVAL);
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

  int signal_event;
  Continuation *reader_cont     = nullptr;
  Continuation *writer_cont     = nullptr;
  ink_hrtime *signal_timeout_at = nullptr;
  Event *t                      = nullptr;
  Event **signal_timeout        = &t;

  if (event == EVENT_IMMEDIATE) {
    /* BZ 49408 */
    // ink_assert(inactivity_timeout_in);
    // ink_assert(next_inactivity_timeout_at < Thread::get_hrtime());
    if (!inactivity_timeout_in || next_inactivity_timeout_at > Thread::get_hrtime()) {
      return EVENT_CONT;
    }
    signal_event      = VC_EVENT_INACTIVITY_TIMEOUT;
    signal_timeout_at = &next_inactivity_timeout_at;
  } else {
    signal_event      = VC_EVENT_ACTIVE_TIMEOUT;
    signal_timeout_at = &next_activity_timeout_at;
  }

  *signal_timeout    = nullptr;
  *signal_timeout_at = 0;
  writer_cont        = write.vio.cont;

  if (closed) {
    nh->free_netvc(this);
    return EVENT_DONE;
  }

  if (read.vio.op == VIO::READ && !(f.shutdown & NET_VC_SHUTDOWN_READ)) {
    reader_cont = read.vio.cont;
    if (read_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }

  if (!*signal_timeout && !*signal_timeout_at && !closed && write.vio.op == VIO::WRITE && !(f.shutdown & NET_VC_SHUTDOWN_WRITE) &&
      reader_cont != write.vio.cont && writer_cont == write.vio.cont) {
    if (write_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }
  return EVENT_DONE;
}

int
UnixNetVConnection::populate(Connection &con_in, Continuation *c, void *arg)
{
  this->con.move(con_in);
  this->mutex  = c->mutex;
  this->thread = this_ethread();

  EThread *t    = this_ethread();
  NetHandler *h = get_NetHandler(t);

  MUTEX_TRY_LOCK(lock, h->mutex, t);
  if (!lock.is_locked()) {
    // Clean up and go home
    return EVENT_ERROR;
  }

  if (h->startIO(this) < 0) {
    con_in.move(this->con);
    Debug("iocore_net", "populate : Failed to add to epoll list");
    return EVENT_ERROR;
  }

  ink_assert(this->nh != nullptr);
  SET_HANDLER(&UnixNetVConnection::mainEvent);
  this->nh->startCop(this);
  ink_assert(this->con.fd != NO_FD);
  return EVENT_DONE;
}

int
UnixNetVConnection::connectUp(EThread *t, int fd)
{
  ink_assert(get_NetHandler(t)->mutex->thread_holding == this_ethread());
  int res;

  thread = t;
  if (check_net_throttle(CONNECT)) {
    check_throttle_warning(CONNECT);
    res = -ENET_THROTTLING;
    NET_INCREMENT_DYN_STAT(net_connections_throttled_out_stat);
    goto fail;
  }

  // Force family to agree with remote (server) address.
  options.ip_family = con.addr.sa.sa_family;

  //
  // Initialize this UnixNetVConnection
  //
  if (is_debug_tag_set("iocore_net")) {
    char addrbuf[INET6_ADDRSTRLEN];
    Debug("iocore_net", "connectUp:: local_addr=%s:%d [%s]",
          options.local_ip.isValid() ? options.local_ip.toString(addrbuf, sizeof(addrbuf)) : "*", options.local_port,
          NetVCOptions::toString(options.addr_binding));
  }

  // If this is getting called from the TS API, then we are wiring up a file descriptor
  // provided by the caller. In that case, we know that the socket is already connected.
  if (fd == NO_FD) {
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
    safe_getsockopt(fd, SOL_SOCKET, SO_TYPE, (char *)&con.sock_type, &len);
    safe_nonblocking(fd);
    con.fd           = fd;
    con.is_connected = true;
    con.is_bound     = true;
  }

  // Must connect after EventIO::Start() to avoid a race condition
  // when edge triggering is used.
  if ((res = get_NetHandler(t)->startIO(this)) < 0) {
    goto fail;
  }

  if (fd == NO_FD) {
    res = con.connect(nullptr, options);
    if (res != 0) {
      // fast stopIO
      nh = nullptr;
      goto fail;
    }
  }

  // Did not fail, increment connection count
  NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, 1);
  ink_release_assert(con.fd != NO_FD);

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
  action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *)(intptr_t)res);
  if (fd != NO_FD) {
    con.fd = NO_FD;
  }
  free(t);
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
  closed        = 0;
  netvc_context = NET_VCONNECTION_UNSET;
  ink_assert(!read.ready_link.prev && !read.ready_link.next);
  ink_assert(!read.enable_link.next);
  ink_assert(!write.ready_link.prev && !write.ready_link.next);
  ink_assert(!write.enable_link.next);
  ink_assert(!link.next && !link.prev);
}

void
UnixNetVConnection::free(EThread *t)
{
  ink_release_assert(t == this_ethread());

  // cancel OOB
  cancel_OOB();
  // close socket fd
  if (con.fd != NO_FD) {
    NET_SUM_GLOBAL_DYN_STAT(net_connections_currently_open_stat, -1);
  }
  con.close();

  clear();
  SET_CONTINUATION_HANDLER(this, (NetVConnHandler)&UnixNetVConnection::startEvent);
  ink_assert(con.fd == NO_FD);
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
  Debug("socket", "Set inactive timeout=%" PRId64 ", for NetVC=%p", timeout_in, this);
  if (timeout_in == 0) {
    // set default inactivity timeout
    timeout_in = HRTIME_SECONDS(nh->config.default_inactivity_timeout);
  }
  inactivity_timeout_in      = timeout_in;
  next_inactivity_timeout_at = Thread::get_hrtime() + inactivity_timeout_in;
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

  // Lock the NetHandler first in order to put the new NetVC into NetHandler and InactivityCop.
  // It is safe and no performance issue to get the mutex lock for a NetHandler of current ethread.
  SCOPED_MUTEX_LOCK(lock, client_nh->mutex, t);

  // Try to get the mutex lock for NetHandler of this NetVC
  MUTEX_TRY_LOCK(lock_src, this->nh->mutex, t);
  if (lock_src.is_locked()) {
    // Deattach this NetVC from original NetHandler & InactivityCop.
    this->nh->stopCop(this);
    this->nh->stopIO(this);
    // Put this NetVC into current NetHandler & InactivityCop.
    this->thread = t;
    client_nh->startIO(this);
    client_nh->startCop(this);
    // Move this NetVC to current EThread Successfully.
    return this;
  }

  // Failed to get the mutex lock for original NetHandler.
  // Try to migrate it by create a new NetVC and then move con.fd and ssl ctx.
  SSLNetVConnection *sslvc = dynamic_cast<SSLNetVConnection *>(this);
  SSL *save_ssl            = (sslvc) ? sslvc->ssl : nullptr;

  UnixNetVConnection *ret_vc = nullptr;

  // Create new VC:
  if (save_ssl) {
    SSLNetVConnection *sslvc = static_cast<SSLNetVConnection *>(sslNetProcessor.allocate_vc(t));
    if (sslvc->populate(this->con, cont, save_ssl) != EVENT_DONE) {
      sslvc->free(t);
      sslvc  = nullptr;
      ret_vc = this;
    } else {
      sslvc->set_context(get_context());
      ret_vc = dynamic_cast<UnixNetVConnection *>(sslvc);
    }
  } else {
    UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(netProcessor.allocate_vc(t));
    if (netvc->populate(this->con, cont, save_ssl) != EVENT_DONE) {
      netvc->free(t);
      netvc  = nullptr;
      ret_vc = this;
    } else {
      netvc->set_context(get_context());
      ret_vc = netvc;
    }
  }

  // clear con.fd and ssl ctx from this NetVC since a new NetVC is created.
  if (ret_vc != this) {
    if (save_ssl) {
      SSLNetVCDetach(sslvc->ssl);
      sslvc->ssl = nullptr;
    }
    ink_assert(this->con.fd == NO_FD);

    // Do_io_close will signal the VC to be freed on the original thread
    // Since we moved the con context, the fd will not be closed
    // Go ahead and remove the fd from the original thread's epoll structure, so it is not
    // processed on two threads simultaneously
    this->ep.stop();
    this->do_io_close();
  }

  return ret_vc;
}

void
UnixNetVConnection::add_to_keep_alive_queue()
{
  nh->add_to_keep_alive_queue(this);
}

void
UnixNetVConnection::remove_from_keep_alive_queue()
{
  nh->remove_from_keep_alive_queue(this);
}

bool
UnixNetVConnection::add_to_active_queue()
{
  return nh->add_to_active_queue(this);
}

void
UnixNetVConnection::remove_from_active_queue()
{
  nh->remove_from_active_queue(this);
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
