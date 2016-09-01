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
#include "Log.h"

// reference from read_from_net()
void
UnixNetProfileSM::handle_read(NetHandler *nh, EThread *lthread)
{
  int64_t r                 = 0;
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);
  NetState *s               = &netvc->read;
  ProxyMutex *tmp_mutex     = s->vio.mutex.get();

  MIOBufferAccessor &buf = s->vio.buffer;
  ink_assert(buf.writer());

  // if there is nothing to do, disable connection
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0 || !buf.writer()->write_avail()) {
    netvc->readDisable();
    return;
  }

  // It is possible that the closed flag got set from HttpSessionManager in the
  // global session pool case.  If so, the closed flag should be stable once we get the
  // s->vio.mutex (the global session pool mutex).
  if (netvc->closed) {
    close_UnixNetVConnection(netvc, lthread);
    return;
  }

  int64_t toread = buf.writer()->write_avail();
  if (toread > ntodo)
    toread = ntodo;

  // read data
  int64_t rattempted = 0, total_read = 0;
  r = this->read_from_net(toread, rattempted, total_read, buf);
  Debug("iocore_net", "[UnixNetProfileSM::handle_read read_from_net = %ld", r);
  // check for errors
  if (r <= 0) {
    if (r == -EAGAIN || r == -ENOTCONN) {
      NET_INCREMENT_DYN_STAT(net_calls_to_read_nodata_stat);
      netvc->read.triggered = 0;
      nh->read_ready_list.remove(netvc);
      return;
    }

    if (!r || r == -ECONNRESET) {
      netvc->read.triggered = 0;
      nh->read_ready_list.remove(netvc);
      netvc->readSignalDone(VC_EVENT_EOS);
      return;
    }
    netvc->read.triggered = 0;
    netvc->readSignalError((int)-r);
    return;
  }
  NET_SUM_DYN_STAT(net_read_bytes_stat, r);

  // Add data to buffer and signal continuation.
  buf.writer()->fill(r);
#ifdef DEBUG
  if (buf.writer()->write_avail() <= 0)
    Debug("iocore_net", "[UnixNetProfileSM::handle_read] read buffer full");
#endif
  s->vio.ndone += r;
  netvc->netActivity(lthread);

  // Signal read ready, check if user is not done
  // If there are no more bytes to read, signal read complete
  ink_assert(ntodo >= 0);
  if (s->vio.ntodo() <= 0) {
    netvc->readSignalDone(VC_EVENT_READ_COMPLETE);
    Debug("iocore_net", "[UnixNetProfileSM::handle_read] read finished - signal done");
    return;
  } else {
    if (netvc->readSignalAndUpdate(VC_EVENT_READ_READY) != EVENT_CONT) {
      return;
    }

    // change of lock... don't look at shared variables!
    if (tmp_mutex != s->vio.mutex.get()) {
      netvc->readReschedule();
      return;
    }
  }
  // If here are is no more room, or nothing to do, disable the connection
  if (s->vio.ntodo() <= 0 || !s->enabled || !buf.writer()->write_avail()) {
    netvc->readDisable();
    return;
  }

  netvc->readReschedule();
}

// reference from write_to_net_io()
void
UnixNetProfileSM::handle_write(NetHandler *nh, EThread *lthread)
{
  NET_INCREMENT_DYN_STAT(net_calls_to_writetonet_stat);
  NET_INCREMENT_DYN_STAT(net_calls_to_writetonet_afterpoll_stat);

  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);
  NetState *s               = &netvc->write;
  ProxyMutex *tmp_mutex     = s->vio.mutex.get();

  // If there is nothing to do, disable
  int64_t ntodo = s->vio.ntodo();
  if (ntodo <= 0) {
    netvc->writeDisable();
    return;
  }

  MIOBufferAccessor &buf = s->vio.buffer;
  ink_assert(buf.writer());

  // Calculate amount to write
  int64_t towrite = buf.reader()->read_avail();
  if (towrite > ntodo)
    towrite     = ntodo;
  int signalled = 0;

  // signal write ready to allow user to fill the buffer
  if (towrite != ntodo && buf.writer()->write_avail()) {
    if (netvc->writeSignalAndUpdate(VC_EVENT_WRITE_READY) != EVENT_CONT) {
      return;
    }
    // TODO: change of lock check.
    ntodo = s->vio.ntodo();
    if (ntodo <= 0) {
      netvc->writeDisable();
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
    netvc->writeDisable();
    return;
  }

  int needs             = 0;
  int64_t total_written = 0;
  int64_t r             = this->load_buffer_and_write(towrite, buf, total_written, needs);

  if (total_written > 0) {
    NET_SUM_DYN_STAT(net_write_bytes_stat, total_written);
    s->vio.ndone += total_written;
  }

  // A write of 0 makes no sense since we tried to write more than 0.
  ink_assert(r != 0);
  // Either we wrote something or got an error.
  // check for errors
  if (r < 0) { // if the socket was not ready,add to WaitList
    if (r == -EAGAIN || r == -ENOTCONN) {
      NET_INCREMENT_DYN_STAT(net_calls_to_write_nodata_stat);
      if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
        netvc->write.triggered = 0;
        nh->write_ready_list.remove(netvc);
        netvc->writeReschedule();
      }
      if ((needs & EVENTIO_READ) == EVENTIO_READ) {
        netvc->read.triggered = 0;
        nh->read_ready_list.remove(netvc);
        netvc->readReschedule();
      }
      return;
    }
    netvc->write.triggered = 0;
    netvc->writeSignalError((int)-total_written);
    return;
  } else {                                        // Wrote data.  Finished without error
    int wbe_event = netvc->getWriteBufferEmpty(); // save so we can clear if needed.

    // If the empty write buffer trap is set, clear it.
    if (!(buf.reader()->is_read_avail_more_than(0)))
      netvc->trapWriteBufferEmpty(0);

    netvc->netActivity(lthread);
    // If there are no more bytes to write, signal write complete,
    ink_assert(ntodo >= 0);
    if (s->vio.ntodo() <= 0) {
      netvc->writeSignalDone(VC_EVENT_WRITE_COMPLETE);
      return;
    }
    int e = 0;
    if (!signalled) {
      e = VC_EVENT_WRITE_READY;
    } else if (wbe_event != vc->getWriteBufferEmpty()) {
      // @a signalled means we won't send an event, and the event values differing means we
      // had a write buffer trap and cleared it, so we need to send it now.
      e = wbe_event;
    }
    if (e) {
      if (netvc->writeSignalAndUpdate(e) != EVENT_CONT) {
        return;
      }

      // change of lock... don't look at shared variables!
      if (tmp_mutex != s->vio.mutex.get()) {
        netvc->writeReschedule();
        return;
      }
    }

    if ((needs & EVENTIO_READ) == EVENTIO_READ) {
      netvc->readReschedule();
    }

    if (!(buf.reader()->is_read_avail_more_than(0))) {
      netvc->writeDisable();
      return;
    }

    if ((needs & EVENTIO_WRITE) == EVENTIO_WRITE) {
      netvc->writeReschedule();
    }
    return;
  }
}

// Global
ClassAllocator<TcpProfileSM> tcpProfileSMAllocator("tcpProfileSMAllocator");

TcpProfileSM::TcpProfileSM() : UnixNetProfileSM(NULL)
{
  type = PROFILE_SM_TCP;
  SET_HANDLER(&TcpProfileSM::mainEvent);
}

int
TcpProfileSM::mainEvent(int event, void *data)
{
  NetHandler *nh            = static_cast<NetHandler *>(data);
  UnixNetVConnection *netvc = static_cast<UnixNetVConnection *>(vc);

  NetState *s = NULL;
  switch (event) {
  case IOCORE_EVENTS_READ:
    s = &netvc->read;
    break;
  case IOCORE_EVENTS_WRITE:
    s = &netvc->write;
    break;
  default:
    ink_release_assert(!"should not reached");
    break;
  }

  MUTEX_TRY_LOCK_FOR(lock, s->vio.mutex, nh->trigger_event->ethread, s->vio._cont);

  if (!lock.is_locked()) {
    switch (event) {
    case IOCORE_EVENTS_READ:
      netvc->readReschedule();
      break;
    case IOCORE_EVENTS_WRITE:
      netvc->writeReschedule();
      break;
    default:
      ink_release_assert(!"should not reached");
      break;
    }
    return EVENT_DONE;
  }
  ink_release_assert(lock.get_mutex() == s->vio.mutex.get());

  switch (event) {
  case IOCORE_EVENTS_READ:
    if (s->enabled && s->vio.op == VIO::READ)
      handle_read(nh, nh->trigger_event->ethread);
    else
      netvc->readDisable();
    break;
  case IOCORE_EVENTS_WRITE:
    if (s->enabled && s->vio.op == VIO::WRITE)
      handle_write(nh, nh->trigger_event->ethread);
    else
      netvc->writeDisable();
    break;
  default:
    ink_release_assert(!"should not reached");
    break;
  }
  return EVENT_DONE;
}

TcpProfileSM *
TcpProfileSM::allocate(EThread *t)
{
  TcpProfileSM *tcp_profile;

  if (t) {
    tcp_profile = THREAD_ALLOC_INIT(tcpProfileSMAllocator, t);
  } else {
    if (likely(tcp_profile = tcpProfileSMAllocator.alloc()))
      tcp_profile->globally_allocated = true;
  }
  return tcp_profile;
}

void
TcpProfileSM::free(EThread *t)
{
  NetProfileSM::clear();

  if (globally_allocated) {
    tcpProfileSMAllocator.free(this);
  } else {
    THREAD_FREE(this, tcpProfileSMAllocator, t);
  }
}
int64_t
TcpProfileSM::read(void *buf, int64_t size, int &err)
{
  int64_t nread = socketManager.read(vc->get_socket(), buf, size);
  err           = errno;

  if (vc->getOriginTrace()) {
    char origin_trace_ip[INET6_ADDRSTRLEN];

    ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));

    if (nread > 0) {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
              vc->getOriginTraceAddr()->port(), (int)nread, (int)nread, (char *)buf);

    } else if (nread == 0) {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d closed connection", origin_trace_ip,
              vc->getOriginTraceAddr()->port());
    } else {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d error=%s", origin_trace_ip,
              vc->getOriginTraceAddr()->port(), strerror(errno));
    }
  }
  return nread;
}

int64_t
TcpProfileSM::readv(struct iovec *vector, int count)
{
  int64_t nread = socketManager.readv(vc->get_socket(), vector, count);

  if (vc->getOriginTrace()) {
    char origin_trace_ip[INET6_ADDRSTRLEN];

    ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));

    if (nread > 0) {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
              vc->getOriginTraceAddr()->port(), (int)nread, (int)nread, (char *)vector[0].iov_base);

    } else if (nread == 0) {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d closed connection", origin_trace_ip,
              vc->getOriginTraceAddr()->port());
    } else {
      TraceIn(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d error=%s", origin_trace_ip,
              vc->getOriginTraceAddr()->port(), strerror(errno));
    }
  }
  return nread;
}

int64_t
TcpProfileSM::write(void *buf, int64_t size, int &err)
{
  int64_t nwritten = socketManager.write(vc->get_socket(), buf, size);
  err              = errno;

  if (vc->getOriginTrace()) {
    char origin_trace_ip[INET6_ADDRSTRLEN];

    ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));

    if (nwritten > 0) {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
               vc->getOriginTraceAddr()->port(), (int)nwritten, (int)nwritten, (char *)buf);

    } else if (nwritten == 0) {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=0", origin_trace_ip,
               vc->getOriginTraceAddr()->port());
    } else {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d error=%s", origin_trace_ip,
               vc->getOriginTraceAddr()->port(), strerror(errno));
    }
  }
  return nwritten;
}

int64_t
TcpProfileSM::writev(struct iovec *vector, int count)
{
  int64_t nwritten = socketManager.writev(vc->get_socket(), vector, count);

  if (vc->getOriginTrace()) {
    char origin_trace_ip[INET6_ADDRSTRLEN];

    ats_ip_ntop(vc->getOriginTraceAddr(), origin_trace_ip, sizeof(origin_trace_ip));

    if (nwritten > 0) {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=%d\n%.*s", origin_trace_ip,
               vc->getOriginTraceAddr()->port(), (int)nwritten, (int)nwritten, (char *)vector[0].iov_base);

    } else if (nwritten == 0) {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d\tbytes=0", origin_trace_ip,
               vc->getOriginTraceAddr()->port());
    } else {
      TraceOut(vc->getOriginTrace(), vc->get_remote_addr(), vc->get_remote_port(), "CLIENT %s:%d error=%s", origin_trace_ip,
               vc->getOriginTraceAddr()->port(), strerror(errno));
    }
  }
  return nwritten;
}

int64_t
TcpProfileSM::read_from_net(int64_t toread, int64_t &rattempted, int64_t &total_read, MIOBufferAccessor &buf)
{
  int64_t r     = 0;
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
          if (a > togo)
            a                  = togo;
          tiovec[niov].iov_len = a;
          rattempted += a;
          niov++;
          if (a >= togo)
            break;
        }
        b = b->next.get();
      }

      ink_assert(niov > 0);
      ink_assert(niov <= countof(tiovec));
      r = this->readv(&tiovec[0], niov);

      NET_INCREMENT_DYN_STAT(net_calls_to_read_stat);

      total_read += rattempted;
    } while (rattempted && r == rattempted && total_read < toread);

    // if we have already moved some bytes successfully, summarize in r
    if (total_read != rattempted) {
      if (r <= 0)
        r = total_read - rattempted;
      else
        r = total_read - rattempted + r;
    }
  } else
    r = 0;

  return r;
}

int64_t
TcpProfileSM::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  int64_t r                  = 0;
  int64_t try_to_write       = 0;
  IOBufferReader *tmp_reader = buf.reader()->clone();

  do {
    IOVec tiovec[NET_MAX_IOV];
    unsigned niov = 0;
    try_to_write  = 0;
    while (niov < NET_MAX_IOV) {
      // check if we have done this block
      int64_t l = tmp_reader->block_read_avail();
      if (l <= 0)
        break;
      char *current_block = tmp_reader->start();

      // check if to amount to write exceeds that in this buffer
      int64_t wavail = towrite - total_written;
      if (l > wavail) {
        l = wavail;
      }

      if (!l) {
        break;
      }

      // build an iov entry
      tiovec[niov].iov_len = l;
      try_to_write += l;
      tiovec[niov].iov_base = current_block;
      niov++;
      tmp_reader->consume(l);
    }

    ink_assert(niov > 0);
    ink_assert(niov <= countof(tiovec));
    r = this->writev(&tiovec[0], niov);

    if (r > 0) {
      buf.reader()->consume(r);
    }
    total_written += r;

    ProxyMutex *mutex = vc->thread->mutex.get();
    NET_INCREMENT_DYN_STAT(net_calls_to_write_stat);
  } while (r == try_to_write && total_written < towrite);

  tmp_reader->dealloc();

  needs |= EVENTIO_WRITE;

  return r;
}
