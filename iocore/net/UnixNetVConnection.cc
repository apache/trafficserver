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

#define STATE_VIO_OFFSET ((uintptr_t)&((NetState*)0)->vio)
#define STATE_FROM_VIO(_x) ((NetState*)(((char*)(_x)) - STATE_VIO_OFFSET))

#define disable_read(_vc) (_vc)->read.enabled = 0
#define disable_write(_vc) (_vc)->write.enabled = 0
#define enable_read(_vc) (_vc)->read.enabled = 1
#define enable_write(_vc) (_vc)->write.enabled = 1

typedef struct iovec IOVec;
#ifndef UIO_MAXIOV
#define NET_MAX_IOV 16          // UIO_MAXIOV shall be at least 16 1003.1g (5.4.1.1)
#else
#define NET_MAX_IOV UIO_MAXIOV
#endif

#define NET_THREAD_STEALING

#ifdef DEBUG
// Initialize class UnixNetVConnection static data
int
  UnixNetVConnection::enable_debug_trace = 0;
#endif

// Global
ClassAllocator<UnixNetVConnection> netVCAllocator("netVCAllocator");

//
// Prototypes
//
void
net_update_priority(NetHandler * nh, UnixNetVConnection * vc, NetState * ns, int ndone);


//
// Reschedule a UnixNetVConnection by placing the VC 
// into ReadyQueue or WaitList
//
static inline void
read_reschedule(NetHandler * nh, UnixNetVConnection * vc)
{
  if (vc->read.triggered && vc->read.enabled) {
    if (!vc->read.netready_queue) {
      nh->ready_queue.epoll_addto_read_ready_queue(vc);
    }
  } else {
    if (vc->read.netready_queue) {
      ReadyQueue::epoll_remove_from_read_ready_queue(vc);
    }
  }
}

static inline void
write_reschedule(NetHandler * nh, UnixNetVConnection * vc)
{
  if (vc->write.triggered && vc->write.enabled) {
    if (!vc->write.netready_queue) {
      nh->ready_queue.epoll_addto_write_ready_queue(vc);
    }
  } else {
    if (vc->write.netready_queue) {
      ReadyQueue::epoll_remove_from_write_ready_queue(vc);
    }
  }
}

void
net_activity(UnixNetVConnection * vc, EThread * thread)
{
  (void) thread;
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout && vc->inactivity_timeout_in && vc->inactivity_timeout->ethread == thread)
    vc->inactivity_timeout->schedule_in(vc->inactivity_timeout_in);
  else {
    if (vc->inactivity_timeout)
      vc->inactivity_timeout->cancel_action();
    if (vc->inactivity_timeout_in) {
      vc->inactivity_timeout = vc->thread->schedule_in_local(vc, vc->inactivity_timeout_in);
    } else
      vc->inactivity_timeout = 0;
  }
#else
  vc->next_inactivity_timeout_at = 0;
  if (vc->inactivity_timeout_in) {
    vc->next_inactivity_timeout_at = ink_get_hrtime() + vc->inactivity_timeout_in;
  }
#endif

}

//
// Function used to close a UnixNetVConnection and free the vc
// Modified by YTS Team, yamsat
//
void
close_UnixNetVConnection(UnixNetVConnection * vc, EThread * t)
{
  if (vc->loggingEnabled()) {
    vc->addLogMessage("close_UnixNetVConnection");
    // display the slow log for the http client session
    if (vc->getLogsTotalTime() / 1000000 > 30000) {
      vc->printLogs();
    }
    vc->clearLogs();
  }

  XTIME(printf("%d %d close\n", vc->id, (int) ((ink_get_hrtime_internal() - vc->submit_time) / HRTIME_MSECOND)));

  vc->cancel_OOB();

  //added by YTS Team, yamsat
  PollDescriptor *pd = get_PollDescriptor(t);
#if defined(USE_EPOLL)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(struct epoll_event));
  ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  epoll_ctl(pd->epoll_fd, EPOLL_CTL_DEL, vc->con.fd, &ev);
#elif defined(USE_KQUEUE)
  struct kevent ev[2];
  EV_SET(&ev[0], vc->con.fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
  EV_SET(&ev[1], vc->con.fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
  kevent(pd->kqueue_fd, &ev[0], 2, NULL, 0, NULL);
#else
#error port me
#endif
  if (vc->ep != NULL) {
    xfree(vc->ep);
    vc->ep = NULL;
  }

  socketManager.fast_close(vc->con.fd);
  vc->con.fd = NO_FD;

#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    vc->inactivity_timeout->cancel_action(vc);
    vc->inactivity_timeout = NULL;
  }
#else
  vc->next_inactivity_timeout_at = 0;
#endif
  vc->inactivity_timeout_in = 0;
  if (vc->active_timeout) {
    vc->active_timeout->cancel_action(vc);
    vc->active_timeout = NULL;
  }
  vc->active_timeout_in = 0;

  //added by YTS Team, yamsat

  if (vc->read.queue) {
    WaitList::epoll_remove_from_read_wait_list(vc);
  }
  if (vc->write.queue) {
    WaitList::epoll_remove_from_write_wait_list(vc);
  }

  if (vc->read.netready_queue) {
    ReadyQueue::epoll_remove_from_read_ready_queue(vc);
  }
  if (vc->write.netready_queue) {
    ReadyQueue::epoll_remove_from_write_ready_queue(vc);
  }

  if (vc->read.enable_queue) {
    ((Queue<UnixNetVConnection> *)vc->read.enable_queue)->remove(vc, vc->read.enable_link);
    vc->read.enable_queue = NULL;
  }
  if (vc->write.enable_queue) {
    ((Queue<UnixNetVConnection> *)vc->write.enable_queue)->remove(vc, vc->write.enable_link);
    vc->write.enable_queue = NULL;
  }
  // clear variables for reuse
  vc->nh = NULL;                //added by YTS Team, yamsat
  vc->closed = 1;
  vc->read.ifd = -1;
  vc->read.triggered = 0;       //added by YTS Team, yamsat
  vc->write.ifd = -1;
  vc->write.triggered = 0;      //added by YTS Team, yamsat
  vc->options.reset();
  vc->free(t);
}

//
// Signal an event
//
static inline int
read_signal_and_update(int event, UnixNetVConnection * vc)
{
  vc->recursion++;
#ifdef AUTO_PILOT_MODE
  if ((event == VC_EVENT_READ_READY) && vc->read.vio.buffer.mbuf->autopilot) {
    vc->read.vio.buffer.mbuf->reenable_readers();
  } else {
    vc->read.vio._cont->handleEvent(event, &vc->read.vio);
  }
#else
  vc->read.vio._cont->handleEvent(event, &vc->read.vio);
#endif
  if (!--vc->recursion && vc->closed) {
    /* BZ  31932 */
    ink_debug_assert(vc->thread == this_ethread());
    close_UnixNetVConnection(vc, vc->thread);
    return EVENT_DONE;
  } else {
    return EVENT_CONT;
  }
}

static inline int
write_signal_and_update(int event, UnixNetVConnection * vc)
{
  vc->recursion++;
#ifdef AUTO_PILOT_MODE
  if ((event == VC_EVENT_WRITE_READY) && vc->write.vio.buffer.mbuf->autopilot) {
    vc->write.vio.buffer.mbuf->reenable_writer();
  } else {
    vc->write.vio._cont->handleEvent(event, &vc->write.vio);
  }
#else
  vc->write.vio._cont->handleEvent(event, &vc->write.vio);
#endif
  if (!--vc->recursion && vc->closed) {
    /* BZ  31932 */
    ink_debug_assert(vc->thread == this_ethread());
    close_UnixNetVConnection(vc, vc->thread);
    return EVENT_DONE;
  } else {
    return EVENT_CONT;
  }
}

static inline int
read_signal_done(int event, NetHandler * nh, UnixNetVConnection * vc)
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
write_signal_done(int event, NetHandler * nh, UnixNetVConnection * vc)
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
read_signal_error(NetHandler * nh, UnixNetVConnection * vc, int lerrno)
{
  vc->lerrno = lerrno;
  return read_signal_done(VC_EVENT_ERROR, nh, vc);
}

static inline int
write_signal_error(NetHandler * nh, UnixNetVConnection * vc, int lerrno)
{
  vc->lerrno = lerrno;
  return write_signal_done(VC_EVENT_ERROR, nh, vc);
}

// read the data for a UnixNetVConnection.
// Rescheduling the UnixNetVConnection by placing the VC into 
// ReadyQueue (or) WaitList
// Had to wrap this function with net_read_io to make SSL work..
static void
read_from_net(NetHandler * nh, UnixNetVConnection * vc, EThread * thread)
{
  vc->addLogMessage("read from net");
  NetState *
    s = &vc->read;
  ProxyMutex *
    mutex = thread->mutex;
  MIOBufferAccessor & buf = s->vio.buffer;
  int
    r = 0;

  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, thread, s->vio._cont);

  if (!lock || lock.m.m_ptr != s->vio.mutex.m_ptr) {
    vc->addLogMessage("can't get lock");
    return;
  }
  // If it is not enabled.  
  if (!s->enabled || s->vio.op != VIO::READ) {
    vc->addLogMessage("not enabled");
    read_disable(nh, vc);
    return;
  }

  ink_debug_assert(buf.writer());

  // If there is nothing to do, disable connection
  // and add into WaitList
  int
    ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    read_disable(nh, vc);
    return;
  }
  // Calculate amount to read
  int
    toread = buf.writer()->write_avail();
  if (toread > ntodo)
    toread = ntodo;

  // Read data
  int
    rattempted = 0, total_read = 0;
  int
    niov = 0;
  IOVec tiovec[NET_MAX_IOV];
  if (toread) {
    IOBufferBlock *
      b = buf.mbuf->_writer;
    do {
      niov = 0;
      rattempted = 0;
      while (b && niov < NET_MAX_IOV) {
        int
          a = b->write_avail();
        if (a > 0) {
          tiovec[niov].iov_base = b->_end;
          int
            togo = toread - total_read - rattempted;
          if (a > togo)
            a = togo;
          tiovec[niov].iov_len = a;
          rattempted += a;
          niov++;
          if (a >= togo)
            break;
        }
        b = b->next;
      }
      if (niov == 1) {
        r = socketManager.read(vc->con.fd, tiovec[0].iov_base, tiovec[0].iov_len);
      } else {
        r = socketManager.readv(vc->con.fd, &tiovec[0], niov);
      }
      //ProxyMutex *mutex = thread->mutex;
      NET_DEBUG_COUNT_DYN_STAT(net_calls_to_read_stat, 1);
      total_read += rattempted;
    } while (r == rattempted && total_read < toread);

    if (vc->loggingEnabled()) {
      char
        message[256];
      snprintf(message, sizeof(message), "rval: %d toread: %d ntodo: %d total_read: %d", r, toread, ntodo, total_read);
      vc->addLogMessage(message);
    }
    // if we have already moved some bytes successfully, summarize in r
    if (total_read != rattempted) {

      if (r <= 0)
        r = total_read - rattempted;
      else
        r = total_read - rattempted + r;
    }
    // check for errors
    if (r <= 0) {

      // If the socket was not ready,add into the WaitList
      if (r == -EAGAIN || r == -ENOTCONN) {
        NET_DEBUG_COUNT_DYN_STAT(net_calls_to_read_nodata_stat, 1);
        vc->addLogMessage("EAGAIN or ENOTCONN");

        vc->read.triggered = 0;
        ReadyQueue::epoll_remove_from_read_ready_queue(vc);

        return;
      }

      if (!r || r == -ECONNRESET) {
        // display the slow log for the http client session
        if (vc->loggingEnabled()) {
          if (vc->getLogsTotalTime() / 1000000 > 30000) {
            vc->printLogs();
          }
          vc->clearLogs();
        }
        // connection is closed
        vc->read.triggered = 0;
        ReadyQueue::epoll_remove_from_read_ready_queue(vc);
        read_signal_done(VC_EVENT_EOS, nh, vc);

        return;
      }
      vc->read.triggered = 0;
      read_signal_error(nh, vc, -r);
      return;
    }
    NET_TRUSS(Debug("net_truss_read", "VC[%d:%d], read %d bytes", vc->id, vc->con.fd, r));
    XTIME(printf("%d %d read: %d\n", vc->id, (int) ((ink_get_hrtime() - vc->submit_time) / HRTIME_MSECOND), r));
    NET_SUM_DYN_STAT(net_read_bytes_stat, r);

    // Add data to buffer and signal continuation.
    buf.writer()->fill(r);
#ifdef DEBUG
    if (buf.writer()->write_avail() <= 0)
      Debug("ssl", "read_from_net, read buffer full");
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
      Debug("ssl", "read_from_net, read finished - signal done");
      return;
    } else {
      if (read_signal_and_update(VC_EVENT_READ_READY, vc) != EVENT_CONT) {
        return;
      }
      // ink_assert(s->enabled);
      // change of lock... don't look at shared variables!
      if (lock.m.m_ptr != s->vio.mutex.m_ptr) {
        read_reschedule(nh, vc);
        return;
      }
    }
  }
  // If here are is no more room, or nothing to do, disable the connection
  if (!s->enabled || !buf.writer()->write_avail() || s->vio.ntodo() <= 0) {
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
write_to_net(NetHandler * nh, UnixNetVConnection * vc, PollDescriptor * pd, EThread * thread)
{
  ProxyMutex *
    mutex = thread->mutex;

  NET_DEBUG_COUNT_DYN_STAT(net_calls_to_writetonet_stat, 1);
  NET_DEBUG_COUNT_DYN_STAT(net_calls_to_writetonet_afterpoll_stat, 1);

  write_to_net_io(nh, vc, thread);
}


void
write_to_net_io(NetHandler * nh, UnixNetVConnection * vc, EThread * thread)
{
  vc->addLogMessage("write to net io");

  NetState *
    s = &vc->write;
  ProxyMutex *
    mutex = thread->mutex;

  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, thread, s->vio._cont);

  if (!lock || lock.m.m_ptr != s->vio.mutex.m_ptr) {
    return;
  }
  // This function will always return true unless
  // vc is an SSLNetVConnection.
  if (!vc->getSSLHandShakeComplete()) {
    int
      err,
      ret;

    if (vc->getSSLClientConnection())
      ret = vc->sslStartHandShake(SSL_EVENT_CLIENT, err);
    else
      ret = vc->sslStartHandShake(SSL_EVENT_SERVER, err);

    if (ret == EVENT_ERROR) {
      if (vc->write.triggered) {
        vc->write.triggered = 0;
      }
      write_signal_error(nh, vc, err);
    } else if (ret == SSL_HANDSHAKE_WANT_READ || ret == SSL_HANDSHAKE_WANT_ACCEPT || ret == SSL_HANDSHAKE_WANT_CONNECT
               || ret == SSL_HANDSHAKE_WANT_WRITE) {
      vc->read.triggered = 0;
      if (vc->read.netready_queue) {
        ReadyQueue::epoll_remove_from_read_ready_queue(vc);
      }
      vc->write.triggered = 0;
      if (vc->write.netready_queue) {
        ReadyQueue::epoll_remove_from_write_ready_queue(vc);
      }
    } else if (ret == EVENT_DONE) {
      vc->read.triggered = 1;
      if (vc->read.enabled) {
        if (!vc->read.netready_queue) {
          nh->ready_queue.epoll_addto_read_ready_queue(vc);
        }
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
  int
    ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    write_disable(nh, vc);
    return;
  }

  MIOBufferAccessor & buf = s->vio.buffer;
  ink_debug_assert(buf.writer());

  // Calculate amount to write
  int
    towrite = buf.reader()->read_avail();
  if (towrite > ntodo)
    towrite = ntodo;
  int
    signalled = 0;

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
    if (towrite > ntodo)
      towrite = ntodo;
  }
  // if there is nothing to do, disable
  ink_assert(towrite >= 0);
  if (towrite <= 0) {
    write_disable(nh, vc);
    return;
  }

  int
    r = 0, total_wrote = 0, wattempted = 0;
  r = vc->loadBufferAndCallWrite(towrite, wattempted, total_wrote, buf);
  if (vc->loggingEnabled()) {
    char
      message[256];
    snprintf(message, sizeof(message), "rval: %d towrite: %d ntodo: %d total_wrote: %d", r, towrite, ntodo,
             total_wrote);
    vc->addLogMessage(message);
  }
  // if we have already moved some bytes successfully, summarize in r
  if (total_wrote != wattempted) {
    if (r <= 0)
      r = total_wrote - wattempted;
    else
      r = total_wrote - wattempted + r;
  }
  // check for errors
  if (r <= 0) {                 // If the socket was not ready,add to WaitList
    if (r == -EAGAIN || r == -ENOTCONN) {
      NET_DEBUG_COUNT_DYN_STAT(net_calls_to_write_nodata_stat, 1);
      vc->write.triggered = 0;
      if (vc->write.netready_queue) {
        ReadyQueue::epoll_remove_from_write_ready_queue(vc);
      }
      return;
    }
    if (!r || r == -ECONNRESET) {
      vc->write.triggered = 0;
      write_signal_done(VC_EVENT_EOS, nh, vc);
      return;
    }
    vc->write.triggered = 0;
    write_signal_error(nh, vc, -r);
    return;
  } else {
    NET_TRUSS(Debug("net_truss_write", "VC[%d:%d], write %d bytes", vc->id, vc->con.fd, r));
    XTIME(printf("%d %d write: %d\n", vc->id,
                 (int) ((ink_get_hrtime_internal() - vc->submit_time) / HRTIME_MSECOND), r));
    NET_SUM_DYN_STAT(net_write_bytes_stat, r);

    // Remove data from the buffer and signal continuation.
    ink_debug_assert(buf.reader()->read_avail() >= r);
    buf.reader()->consume(r);
    ink_debug_assert(buf.reader()->read_avail() >= 0);
    s->vio.ndone += r;

    net_activity(vc, thread);
    // If there are no more bytes to write, signal write complete,
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      write_signal_done(VC_EVENT_WRITE_COMPLETE, nh, vc);
      return;
    } else if (!signalled) {
      if (write_signal_and_update(VC_EVENT_WRITE_READY, vc) != EVENT_CONT) {
        return;
      }
      // change of lock... don't look at shared variables!
      if (lock.m.m_ptr != s->vio.mutex.m_ptr) {
        write_reschedule(nh, vc);
        return;
      }
      if (s->vio.ntodo() <= 0 || !buf.reader()->read_avail()) {
        write_disable(nh, vc);
        return;
      }
    }

    write_reschedule(nh, vc);
    return;
  }
}


VIO *
UnixNetVConnection::do_io_read(Continuation * c, int nbytes, MIOBuffer * buf)
{
  //  addLogMessage("do_io_read");
  NET_TRUSS(Debug("net_truss_read", "VC[%d:%d] do_io_read(%d)", id, con.fd, nbytes));
  ink_assert(!closed);
  if (buf)
    read.vio.buffer.writer_for(buf);
  else
    read.vio.buffer.clear();
  // boost the NetVC
  read.priority = INK_MIN_PRIORITY;
  write.priority = INK_MIN_PRIORITY;
  read.vio.op = VIO::READ;
  read.vio.mutex = c->mutex;
  read.vio._cont = c;
  read.vio.nbytes = nbytes;
  read.vio.data = 0;
  read.vio.ndone = 0;
  read.vio.vc_server = (VConnection *) this;
  XTIME(printf("%d %d do_io_read\n", id, (int) ((ink_get_hrtime_internal() - submit_time) / HRTIME_MSECOND)));
  if (buf) {
    if (!read.enabled)
      read.vio.reenable();
  } else {
    disable_read(this);
  }
  return &read.vio;
}

VIO *
UnixNetVConnection::do_io_write(Continuation * acont, int anbytes, IOBufferReader * abuffer, bool owner)
{
  addLogMessage("do_io_write");

  NET_TRUSS(Debug("net_truss_write", "VC[%d:%d] do_io_write(%d)", id, con.fd, anbytes));
  ink_assert(!closed);
  if (abuffer) {
    ink_assert(!owner);
    write.vio.buffer.reader_for(abuffer);
  } else
    write.vio.buffer.clear();
  // boost the NetVC
  read.priority = INK_MIN_PRIORITY;
  write.priority = INK_MIN_PRIORITY;
  write.vio.op = VIO::WRITE;
  write.vio.mutex = acont->mutex;
  write.vio._cont = acont;
  write.vio.nbytes = anbytes;
  write.vio.data = 0;
  write.vio.ndone = 0;
  write.vio.vc_server = (VConnection *) this;
  XTIME(printf("%d %d do_io_write\n", id, (int) ((ink_get_hrtime_internal() - submit_time) / HRTIME_MSECOND)));
  if (abuffer) {
    if (!write.enabled)
      write.vio.reenable();
  } else {
    disable_write(this);
  }
  return &write.vio;
}


void
UnixNetVConnection::do_io_close(int alerrno /* = -1 */ )
{
  if (loggingEnabled()) {
    addLogMessage("UnixNetVConnection::do_io_close");
    // display the slow log for the http client session
    if (getLogsTotalTime() / 1000000 > 30000) {
      printLogs();
    }
    clearLogs();
  }

  disable_read(this);
  disable_write(this);
  read.vio.buffer.clear();
  read.vio.nbytes = 0;
  read.vio.op = VIO::NONE;
  write.vio.buffer.clear();
  write.vio.nbytes = 0;
  write.vio.op = VIO::NONE;

  INK_WRITE_MEMORY_BARRIER;
  if (alerrno && alerrno != -1)
    this->lerrno = alerrno;
  if (alerrno == -1) {
    closed = 1;
  } else {
    closed = -1;
  }
}

void
UnixNetVConnection::do_io_shutdown(ShutdownHowTo_t howto)
{
  addLogMessage("UnixNetVConnection::do_io_shutdown");
  switch (howto) {
  case IO_SHUTDOWN_READ:
    socketManager.shutdown(((UnixNetVConnection *) this)->con.fd, 0);
    disable_read(this);
    read.vio.buffer.clear();
    read.vio.nbytes = 0;
    f.shutdown = NET_VC_SHUTDOWN_READ;
    break;
  case IO_SHUTDOWN_WRITE:
    socketManager.shutdown(((UnixNetVConnection *) this)->con.fd, 1);
    disable_write(this);
    write.vio.buffer.clear();
    write.vio.nbytes = 0;
    f.shutdown = NET_VC_SHUTDOWN_WRITE;
    break;
  case IO_SHUTDOWN_READWRITE:
    socketManager.shutdown(((UnixNetVConnection *) this)->con.fd, 2);
    disable_read(this);
    disable_write(this);
    read.vio.buffer.clear();
    read.vio.nbytes = 0;
    write.vio.buffer.clear();
    write.vio.nbytes = 0;
    f.shutdown = NET_VC_SHUTDOWN_READ | NET_VC_SHUTDOWN_WRITE;
    break;
  default:
    ink_assert(!"not reached");
  }
}

int
OOB_callback::retry_OOB_send(int event, Event * e)
{
  (void) event;
  (void) e;
  ink_debug_assert(mutex->thread_holding == this_ethread());
  // the NetVC and the OOB_callback share a mutex
  server_vc->oob_ptr = NULL;
  server_vc->send_OOB(server_cont, data, length);
  delete this;
  return EVENT_DONE;
}

void
UnixNetVConnection::cancel_OOB()
{
  UnixNetVConnection *u = (UnixNetVConnection *) this;
  if (u->oob_ptr) {
    if (u->oob_ptr->trigger) {
      u->oob_ptr->trigger->cancel_action();
      u->oob_ptr->trigger = NULL;
    }
    delete u->oob_ptr;
    u->oob_ptr = NULL;
  }
}

Action *
UnixNetVConnection::send_OOB(Continuation * cont, char *buf, int len)
{
  UnixNetVConnection *u = (UnixNetVConnection *) this;
  ink_debug_assert(len > 0);
  ink_debug_assert(buf);
  ink_debug_assert(!u->oob_ptr);
  int written;
  ink_debug_assert(cont->mutex->thread_holding == this_ethread());
  written = socketManager.send(u->con.fd, buf, len, MSG_OOB);
  if (written == len) {
    cont->handleEvent(VC_EVENT_OOB_COMPLETE, NULL);
    return ACTION_RESULT_DONE;
  } else if (!written) {
    cont->handleEvent(VC_EVENT_EOS, NULL);
    return ACTION_RESULT_DONE;
  }
  if (written > 0 && written < len) {
    u->oob_ptr = NEW(new OOB_callback(mutex, this, cont, buf + written, len - written));
    u->oob_ptr->trigger = mutex->thread_holding->schedule_in_local(u->oob_ptr, HRTIME_MSECONDS(10));
    return u->oob_ptr->trigger;
  } else {
    // should be a rare case : taking a new continuation should not be
    // expensive for this
    written = -errno;
    ink_assert(written == -EAGAIN || written == -ENOTCONN);
    u->oob_ptr = NEW(new OOB_callback(mutex, this, cont, buf, len));
    u->oob_ptr->trigger = mutex->thread_holding->schedule_in_local(u->oob_ptr, HRTIME_MSECONDS(10));
    return u->oob_ptr->trigger;
  }
}

//
// Function used to reenable the VC for reading or
// writing. Modified by YTS Team, yamsat
//
void
UnixNetVConnection::reenable(VIO * vio)
{

  if (STATE_FROM_VIO(vio)->enabled) {
    return;
  }
  NET_TRUSS(Debug("net_truss", "VC[%d:%d] UnixNetVConnection::reenable", id, con.fd));
  set_enabled(vio);
#ifdef NET_THREAD_STEALING
  if (!thread)
    return;
  EThread *t = vio->mutex->thread_holding;
  ink_debug_assert(t == this_ethread());
  //Modified by YTS Team, yamsat
  if (nh->mutex->thread_holding == t) {
    if (vio == &read.vio) {
      if (read.triggered) {
        if (!read.netready_queue) {
          nh->ready_queue.epoll_addto_read_ready_queue(this);
        }
      } else {
        if (read.netready_queue) {
          ReadyQueue::epoll_remove_from_read_ready_queue(this);
        }
      }

    } else {
      if (write.triggered) {
        if (!write.netready_queue) {
          nh->ready_queue.epoll_addto_write_ready_queue(this);
        }
      } else {
        if (write.netready_queue) {
          ReadyQueue::epoll_remove_from_write_ready_queue(this);
        }
      }
    }
  } else if (!nh->mutex->is_thread()) {
    MUTEX_TRY_LOCK(lock, nh->mutex, t);
    if (!lock) {
      if (vio == &read.vio) {
        ink_mutex_acquire(&nh->read_enable_mutex.m_ptr->the_mutex);
        if (!read.enable_queue) {
          read.enable_queue = &nh->read_enable_list;
          nh->read_enable_list.enqueue(this, read.enable_link);
        }
        ink_mutex_release(&nh->read_enable_mutex.m_ptr->the_mutex);
      } else {
        ink_mutex_acquire(&nh->write_enable_mutex.m_ptr->the_mutex);
        if (!write.enable_queue) {
          write.enable_queue = &nh->write_enable_list;
          nh->write_enable_list.enqueue(this, write.enable_link);
        }
        ink_mutex_release(&nh->write_enable_mutex.m_ptr->the_mutex);
      }
      return;
    }
    if (vio == &read.vio) {
      if (read.triggered) {
        if (!read.netready_queue) {
          nh->ready_queue.epoll_addto_read_ready_queue(this);
        }
      } else {
        if (read.netready_queue) {
          ReadyQueue::epoll_remove_from_read_ready_queue(this);
        }
      }
    } else {
      if (write.triggered) {
        if (!write.netready_queue) {
          nh->ready_queue.epoll_addto_write_ready_queue(this);
        }
      } else {
        if (write.netready_queue) {
          ReadyQueue::epoll_remove_from_write_ready_queue(this);
        }
      }
    }
  }
#endif
}

void
UnixNetVConnection::reenable_re(VIO * vio)
{

  set_enabled(vio);

#ifdef NET_THREAD_STEALING
  if (!thread) {
    return;
  }
  EThread *t = vio->mutex->thread_holding;
  ink_debug_assert(t == this_ethread());
  if (nh->mutex->thread_holding == t) {
    if (vio == &read.vio) {
      if (read.triggered) {
        net_read_io(nh, t);
      } else {
        if (read.netready_queue != NULL) {
          ReadyQueue::epoll_remove_from_read_ready_queue(this);
        }
      }
    } else {
      if (write.triggered) {
        write_to_net(nh, this, NULL, t);
      } else {
        if (write.netready_queue != NULL) {
          ReadyQueue::epoll_remove_from_write_ready_queue(this);
        }
      }
    }
  }
#endif
}


void
UnixNetVConnection::boost()
{
  ink_assert(thread);
}


UnixNetVConnection::UnixNetVConnection():
closed(1), inactivity_timeout_in(0), active_timeout_in(0),
#ifdef INACTIVITY_TIMEOUT
  inactivity_timeout(NULL),
#else
  next_inactivity_timeout_at(0),
#endif
  active_timeout(NULL), ep(NULL),       //added by YTS Team, yamsat
  nh(NULL),                     //added by YTS Team, yamsat
  id(0), ip(0), _interface(0), accept_port(0), port(0), flags(0), recursion(0), submit_time(0), oob_ptr(0)
{
  memset(&local_sa, 0, sizeof local_sa);
  SET_HANDLER((NetVConnHandler) & UnixNetVConnection::startEvent);
}


// Private methods

void
UnixNetVConnection::set_enabled(VIO * vio)
{
  ink_debug_assert(vio->mutex->thread_holding == this_ethread());
  ink_assert(!closed);
  STATE_FROM_VIO(vio)->enabled = 1;
#ifdef DEBUG
  if (vio == &read.vio) {
    XTIME(printf("%d %d reenable read\n", id, (int) ((ink_get_hrtime_internal() - submit_time) / HRTIME_MSECOND)));
    if (enable_debug_trace && (vio->buffer.mbuf && !vio->buffer.writer()->write_avail()));
  } else {
    ink_assert(vio == &write.vio);
    XTIME(printf("%d %d reenable write\n", id, (int) ((ink_get_hrtime_internal() - submit_time) / HRTIME_MSECOND)));
    if (enable_debug_trace && (vio->buffer.mbuf && !vio->buffer.reader()->read_avail()));
  }
#endif
#ifdef INACTIVITY_TIMEOUT
  if (!inactivity_timeout && inactivity_timeout_in)
    inactivity_timeout = vio->mutex->thread_holding->schedule_in_local(this, inactivity_timeout_in);
#else
  if (!next_inactivity_timeout_at && inactivity_timeout_in)
    next_inactivity_timeout_at = ink_get_hrtime() + inactivity_timeout_in;
#endif
}



void
UnixNetVConnection::net_read_io(NetHandler * nh, EThread * lthread)
{
  read_from_net(nh, this, lthread);
}

// This code was pulled out of write_to_net so
// I could overwrite it for the SSL implementation
// (SSL read does not support overlapped i/o)
// without duplicating all the code in write_to_net.
int
UnixNetVConnection::loadBufferAndCallWrite(int towrite, int &wattempted, int &total_wrote, MIOBufferAccessor & buf)
{
  int r = 0;
  int offset = buf.entry->start_offset;
  IOBufferBlock *b = buf.entry->block;
  do {
    IOVec tiovec[NET_MAX_IOV];
    int niov = 0, total_wrote_last = total_wrote;
    while (b && niov < NET_MAX_IOV) {
      // check if we have done this block
      int l = b->read_avail();
      l -= offset;
      if (l <= 0) {
        offset = -l;
        b = b->next;
        continue;
      }
      // check if to amount to write exceeds that in this buffer
      int wavail = towrite - total_wrote;
      if (l > wavail)
        l = wavail;
      if (!l)
        break;
      total_wrote += l;
      // build an iov entry
      tiovec[niov].iov_len = l;
      tiovec[niov].iov_base = b->start() + offset;
      niov++;
      // on to the next block
      offset = 0;
      b = b->next;
    }
    wattempted = total_wrote - total_wrote_last;
    if (niov == 1)
      r = socketManager.write(con.fd, tiovec[0].iov_base, tiovec[0].iov_len);
    else
      r = socketManager.writev(con.fd, &tiovec[0], niov);
    ProxyMutex *mutex = thread->mutex;
    NET_DEBUG_COUNT_DYN_STAT(net_calls_to_write_stat, 1);
  } while (r == wattempted && total_wrote < towrite);

  return (r);
}

void
UnixNetVConnection::readDisable(NetHandler * nh)
{
  read_disable(nh, this);
}

void
UnixNetVConnection::readSignalError(NetHandler * nh, int err)
{
  read_signal_error(nh, this, err);
}

int
UnixNetVConnection::readSignalDone(int event, NetHandler * nh)
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
UnixNetVConnection::readReschedule(NetHandler * nh)
{
  read_reschedule(nh, this);
}

void
UnixNetVConnection::netActivity(EThread * lthread)
{
  net_activity(this, lthread);
}



int
UnixNetVConnection::startEvent(int event, Event * e)
{
  (void) event;
  MUTEX_TRY_LOCK(lock, action_.mutex, e->ethread);
#ifdef __INKIO
  MUTEX_TRY_LOCK(lock2, e->ethread->netQueueMonitor->mutex, e->ethread);
#else
  MUTEX_TRY_LOCK(lock2, get_NetHandler(e->ethread)->mutex, e->ethread);
#endif

  if (!lock || !lock2) {
    e->schedule_in(NET_RETRY_DELAY);
    return EVENT_CONT;
  }
  if (!action_.cancelled)
    connectUp(e->ethread);
  else
    free(e->ethread);
  return EVENT_DONE;
}

int
UnixNetVConnection::acceptEvent(int event, Event * e)
{
  (void) event;
  thread = e->ethread;
  MUTEX_TRY_LOCK(lock, action_.mutex, e->ethread);
  MUTEX_TRY_LOCK(lock2, get_NetHandler(thread)->mutex, e->ethread);
  if (!lock || !lock2) {
    if (event == EVENT_NONE) {
      thread->schedule_in(this, NET_RETRY_DELAY);
      return EVENT_DONE;
    } else {
      e->schedule_in(NET_RETRY_DELAY);
      return EVENT_CONT;
    }
  }

  if (action_.cancelled) {
    free(thread);
    return EVENT_DONE;
  }

  SET_HANDLER((NetVConnHandler) & UnixNetVConnection::mainEvent);

  //added by YTS Team, yamsat
  nh = get_NetHandler(thread);
  PollDescriptor *pd = get_PollDescriptor(thread);

  struct epoll_data_ptr *eptr;
  eptr = (struct epoll_data_ptr *) xmalloc(sizeof(struct epoll_data_ptr));
  eptr->type = EPOLL_READWRITE_VC;
  eptr->data.vc = this;

  this->ep = eptr;

#if defined(USE_EPOLL)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(struct epoll_event));

  ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  ev.data.ptr = eptr;
  //printf("Added to epoll ctl fd %d and number is %d\n",con.fd,id);
  if (epoll_ctl(pd->epoll_fd, EPOLL_CTL_ADD, con.fd, &ev) < 0) {
    Debug("iocore_net", "acceptEvent : Failed to add to epoll list\n");
    close_UnixNetVConnection(this, e->ethread);
    return EVENT_DONE;
  }
#elif defined(USE_KQUEUE)
  struct kevent ev[2];
  EV_SET(&ev[0], con.fd, EVFILT_READ, EV_ADD, 0, 0, eptr);
  EV_SET(&ev[1], con.fd, EVFILT_WRITE, EV_ADD, 0, 0, eptr);
  if (kevent(pd->kqueue_fd, &ev[0], 2, NULL, 0, NULL) < 0) {
    Debug("iocore_net", "acceptEvent : Failed to add to kqueue list\n");
    close_UnixNetVConnection(this, e->ethread);
    return EVENT_DONE;
  }
#else
#error port me
#endif

  Debug("iocore_net", "acceptEvent : Adding fd %d to read wait list\n", con.fd);
  nh->wait_list.epoll_addto_read_wait_list(this);
  Debug("iocore_net", "acceptEvent : Adding fd %d to write wait list\n", con.fd);
  nh->wait_list.epoll_addto_write_wait_list(this);

  //Debug("iocore_net", "acceptEvent : Setting triggered and adding to the read ready queue");
  //read.triggered = 1;
  //nh->ready_queue.epoll_addto_read_ready_queue(this);

  if (inactivity_timeout_in)
    UnixNetVConnection::set_inactivity_timeout(inactivity_timeout_in);
  if (active_timeout_in)
    UnixNetVConnection::set_active_timeout(active_timeout_in);
  action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  return EVENT_DONE;
}

//
// The main event for UnixNetVConnections.
// This is called by the Event subsystem to initialize the UnixNetVConnection
// and for active and inactivity timeouts.
//
int
UnixNetVConnection::mainEvent(int event, Event * e)
{
  addLogMessage("main event");
  NET_TRUSS(Debug("netvc_truss_timeout", "UnixNetVConnection[%d]::timeout", con.fd));
  ink_debug_assert(event == EVENT_IMMEDIATE || event == EVENT_INTERVAL);
  /* BZ 31932 */
  ink_debug_assert(thread == this_ethread());

  MUTEX_TRY_LOCK(hlock, get_NetHandler(thread)->mutex, e->ethread);
  MUTEX_TRY_LOCK(rlock, read.vio.mutex ? (ProxyMutex *) read.vio.mutex : (ProxyMutex *) e->ethread->mutex, e->ethread);
  MUTEX_TRY_LOCK(wlock, write.vio.mutex ? (ProxyMutex *) write.vio.mutex :
                 (ProxyMutex *) e->ethread->mutex, e->ethread);
  if (!hlock || !rlock || !wlock) {
#ifndef INACTIVITY_TIMEOUT
    if (e == active_timeout)
#endif
      e->schedule_in(NET_RETRY_DELAY);

    return EVENT_CONT;
  }

  if (e->cancelled) {

    return EVENT_DONE;
  }
  int signal_event;
  Event **signal_timeout;
  Continuation *reader_cont = NULL;
  Continuation *writer_cont = NULL;
  ink_hrtime next_activity_timeout_at = 0;
  ink_hrtime *signal_timeout_at = &next_activity_timeout_at;
  Event *t = NULL;
  signal_timeout = &t;

#ifdef INACTIVITY_TIMEOUT
  if (e == inactivity_timeout) {
    signal_event = VC_EVENT_INACTIVITY_TIMEOUT;
    signal_timeout = &inactivity_timeout;
  }
#else
  if (event == EVENT_IMMEDIATE) {
    /* BZ 49408 */
    //ink_debug_assert(inactivity_timeout_in);
    //ink_debug_assert(next_inactivity_timeout_at < ink_get_hrtime());
    if (!inactivity_timeout_in || next_inactivity_timeout_at > ink_get_hrtime()) {
      return EVENT_CONT;
    }
    signal_event = VC_EVENT_INACTIVITY_TIMEOUT;
    signal_timeout_at = &next_inactivity_timeout_at;
  }
#endif
  else {
    ink_debug_assert(e == active_timeout);
    signal_event = VC_EVENT_ACTIVE_TIMEOUT;
    signal_timeout = &active_timeout;
  }
  *signal_timeout = 0;
  *signal_timeout_at = 0;
  writer_cont = write.vio._cont;

  if (closed) {
    //added by YTS Team, yamsat
    close_UnixNetVConnection(this, thread);
    return EVENT_DONE;
  }
  if (read.vio.op == VIO::READ && !(f.shutdown & NET_VC_SHUTDOWN_READ)) {
    reader_cont = read.vio._cont;
    if (read_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }

  if (!*signal_timeout &&
      !*signal_timeout_at &&
      !closed && write.vio.op == VIO::WRITE &&
      !(f.shutdown & NET_VC_SHUTDOWN_WRITE) && reader_cont != write.vio._cont && writer_cont == write.vio._cont) {
    if (write_signal_and_update(signal_event, this) == EVENT_DONE) {
      return EVENT_DONE;
    }
  }
  return EVENT_DONE;
}


int
UnixNetVConnection::connectUp(EThread * t)
{
  addLogMessage("connectUp");

  thread = t;
  if (check_net_throttle(CONNECT, submit_time)) {
    check_throttle_warning();
    action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) -ENET_THROTTLING);
    free(t);
    return CONNECT_FAILURE;
  }
  //
  // Initialize this UnixNetVConnection
  //
  int res = 0;
  Debug("arm_spoofing", "connectUp:: interface=%x and options.spoofip=%x\n", _interface, options.spoof_ip);
  if (_interface || options.local_port || options.spoof_ip) {
    // TODO move socketManager.socket() and epoll_ctl here too

    res = con.bind_connect(ip, port, _interface, &options);
  } else {
    // we need to add the socket to the epoll fd before we call connect or we can miss an event
    int socketFd = -1;
    if ((socketFd = socketManager.socket(AF_INET, SOCK_STREAM, 0)) >= 0) {

      // this should be moved into a PollDescriptor method
      nh = get_NetHandler(t);
      PollDescriptor *pd = get_PollDescriptor(t);

      closed = 0;               // need to set this before adding it to epoll

      struct epoll_data_ptr *eptr;
      eptr = (struct epoll_data_ptr *) xmalloc(sizeof(struct epoll_data_ptr));
      eptr->type = EPOLL_READWRITE_VC;
      eptr->data.vc = this;
      ep = eptr;

#if defined(USE_EPOLL)
      struct epoll_event ev;
      memset(&ev, 0, sizeof(struct epoll_event));
      ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
      ev.data.ptr = eptr;
      int rval = epoll_ctl(pd->epoll_fd, EPOLL_CTL_ADD, socketFd, &ev);
      if (rval != 0) {
        lerrno = errno;
        Debug("iocore_net", "connectUp : Failed to add to epoll list\n");
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) rval);
        free(t);
        return CONNECT_FAILURE;
      }
#elif defined(USE_KQUEUE)
      struct kevent ev[2];
      EV_SET(&ev[0], socketFd, EVFILT_READ, EV_ADD, 0, 0, eptr);
      EV_SET(&ev[1], socketFd, EVFILT_WRITE, EV_ADD, 0, 0, eptr);
      int rval = kevent(pd->kqueue_fd, &ev[0], 2, NULL, 0, NULL);
      if (rval < 0) {
        lerrno = errno;
        Debug("iocore_net", "connectUp : Failed to add to kqueue list\n");
        action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) rval);
        free(t);
        return CONNECT_FAILURE;
      }
#else
#error port me
#endif
      res = con.fast_connect(ip, port, &options, socketFd);
    } else {
      res = socketFd;
    }
  }
  if (res) {
    lerrno = errno;
    action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) res);
    free(t);
    return CONNECT_FAILURE;
  }

  check_emergency_throttle(con);

  // start up next round immediately

  SET_HANDLER((NetVConnHandler) & UnixNetVConnection::mainEvent);
  // This function is empty for regular UnixNetVConnection, it has code
  // in it for the inherited SSLUnixNetVConnection.  Allows the connectUp 
  // function code not to be duplicated in the inherited SSL class.
  //  sslStartHandShake (SSL_EVENT_CLIENT, err);

  //added for epoll by YTS Team, yamsat

  if (_interface || options.local_port || options.spoof_ip) {
    nh = get_NetHandler(t);
    PollDescriptor *pd = get_PollDescriptor(t);

    struct epoll_data_ptr *eptr;
    eptr = (struct epoll_data_ptr *) xmalloc(sizeof(struct epoll_data_ptr));
    eptr->type = EPOLL_READWRITE_VC;
    eptr->data.vc = this;

    ep = eptr;

#if defined(USE_EPOLL)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = eptr;
    res = epoll_ctl(pd->epoll_fd, EPOLL_CTL_ADD, con.fd, &ev);
    if (res < 0) {
      Debug("iocore_net", "connectUp : Failed to add to epoll list\n");
      lerrno = errno;
      action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) res);
      free(t);
      return CONNECT_FAILURE;
    }
#elif defined(USE_KQUEUE)
    struct kevent ev[2];
    EV_SET(&ev[0], con.fd, EVFILT_READ, EV_ADD, 0, 0, eptr);
    EV_SET(&ev[1], con.fd, EVFILT_WRITE, EV_ADD, 0, 0, eptr);
    res = kevent(pd->kqueue_fd, &ev[0], 2, NULL, 0, NULL);
    if (res < 0) {
      lerrno = errno;
      Debug("iocore_net", "connectUp : Failed to add to kqueue list\n");
      action_.continuation->handleEvent(NET_EVENT_OPEN_FAILED, (void *) res);
      free(t);
      return CONNECT_FAILURE;
    }
#else
#error port me
#endif
  }

  closed = 0;

  Debug("iocore_net", "connectUp : Adding fd %d to read wait list\n", con.fd);
  nh->wait_list.epoll_addto_read_wait_list(this);

  Debug("iocore_net", "connectUp : Adding fd %d to write wait list\n", con.fd);
  nh->wait_list.epoll_addto_write_wait_list(this);

  ink_assert(!inactivity_timeout_in);
  ink_assert(!active_timeout_in);
  action_.continuation->handleEvent(NET_EVENT_OPEN, this);
  XTIME(printf("%d 2connect\n", id));
  return CONNECT_SUCCESS;
}


void
UnixNetVConnection::free(EThread * t)
{
  NET_DECREMENT_THREAD_DYN_STAT(net_connections_currently_open_stat, t);
  got_remote_addr = 0;
  got_local_addr = 0;
  read.vio.mutex.clear();
  write.vio.mutex.clear();
  action_.mutex.clear();
  this->mutex.clear();
  flags = 0;
  accept_port = 0;
  SET_CONTINUATION_HANDLER(this, (NetVConnHandler) & UnixNetVConnection::startEvent);
  //added for epoll by YTS Team, yamsat
  if (ep != NULL) {
    xfree(ep);
    ep = NULL;
  }
  if (nh != NULL) {
    nh = NULL;
  }
  ink_debug_assert(!read.queue && !write.queue);
  ink_debug_assert(!read.netready_queue && !write.netready_queue);
  ink_debug_assert(!read.enable_queue && !write.enable_queue);
  ink_debug_assert(!read.link.prev && !read.link.next);
  ink_debug_assert(!read.netready_link.prev && !read.netready_link.next);
  ink_debug_assert(!read.enable_link.prev && !read.enable_link.next);
  ink_debug_assert(!write.link.prev && !write.link.next);
  ink_debug_assert(!write.netready_link.prev && !write.netready_link.next);
  ink_debug_assert(!write.enable_link.prev && !write.enable_link.next);
  ink_debug_assert(!link.next && !link.prev);
  ink_debug_assert(!active_timeout);
  ink_debug_assert(con.fd == NO_FD);
  ink_debug_assert(t == this_ethread());
  THREAD_FREE(this, netVCAllocator, t);
}
