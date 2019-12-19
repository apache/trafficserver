/** @file

  ALPNSupport.cc provides implmentations for ALPNSupport methods

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

#include "UDPConnection.h"

static const char *
udp_event_name(UDP2ConnectionImpl::UDPEvents e)
{
  switch (e) {
  case UDP2ConnectionImpl::UDPEvents::UDP_CONNECT_EVENT:
    return "UDP_CONNECT_EVENT";
  case UDP2ConnectionImpl::UDPEvents::UDP_SEND_EVENT:
    return "UDP_SEND_EVENT";
  case UDP2ConnectionImpl::UDPEvents::UDP_USER_READ_READY:
    return "UDP_USER_READ_READY";
  case UDP2ConnectionImpl::UDPEvents::UDP_CLOSE_EVENT:
    return "UDP_CLOSE_EVENT";
  default:
    return "UNKNOWN EVENT";
  };

  return nullptr;
}

//
// Reschedule a NetEvent by moving it
// onto or off of the ready_list
//
static inline void
read_reschedule(NetHandler *nh, NetEvent *vc)
{
  vc->ep.refresh(EVENTIO_READ);
  if (vc->read.triggered && vc->read.enabled) {
    nh->read_ready_list.in_or_enqueue(vc);
  } else {
    nh->read_ready_list.remove(vc);
  }
}

static inline void
write_reschedule(NetHandler *nh, NetEvent *vc)
{
  vc->ep.refresh(EVENTIO_WRITE);
  if (vc->write.triggered && vc->write.enabled) {
    nh->write_ready_list.in_or_enqueue(vc);
  } else {
    nh->write_ready_list.remove(vc);
  }
}

//
// UDP2ConnectionImpl
//
UDP2ConnectionImpl::UDP2ConnectionImpl(Continuation *con, EThread *thread) : _con(con), _thread(thread)
{
  this->mutex        = con->mutex;
  this->read.enabled = 1; // read enabled is always true because we expected all data;
  if (thread == nullptr) {
    this->_thread = this_ethread();
  }
  if (this->mutex == nullptr) {
    this->mutex = new_ProxyMutex();
  }
  this->refcount_inc();
  SET_HANDLER(&UDP2ConnectionImpl::startEvent);
}

UDP2ConnectionImpl::UDP2ConnectionImpl(AcceptUDP2ConnectionImpl *accept, Continuation *con, EThread *ethread)
  : _con(con), _thread(ethread), _accept_con(accept)
{
  this->mutex        = con->mutex;
  this->read.enabled = 1; // read enabled is always true because we expected all data;
  if (ethread == nullptr) {
    this->_thread = this_ethread();
  }
  if (this->mutex == nullptr) {
    this->mutex = new_ProxyMutex();
  }
  this->refcount_inc();
  SET_HANDLER(&UDP2ConnectionImpl::startEvent);
}

UDP2ConnectionImpl::~UDP2ConnectionImpl()
{
  ink_assert(this->mutex->thread_holding == this_thread());
  Debug("udp_con", "connection close");
  this->mutex = nullptr;

  int fd = this->_fd;

  if (this->nh != nullptr) {
    this->nh->stopIO(this);
  }

  this->_fd = -1;
  if (fd != -1) {
    ::close(fd);
  }

  SList(UDP2Packet, out_link) aq(this->_send_queue.popall());
  UDP2Packet *p;
  while ((p = aq.pop())) {
    delete p;
  }

  SList(UDP2Packet, in_link) aq2(this->_recv_queue.popall());
  while ((p = aq2.pop())) {
    delete p;
  }
}

void
UDP2ConnectionImpl::free(EThread *t)
{
  // should never be called;
  ink_release_assert(0);
}

int
UDP2ConnectionImpl::callback(int event, void *data)
{
  if (this->_con == nullptr) {
    return 0;
  }

  MUTEX_TRY_LOCK(lock, this->_con->mutex == nullptr ? this->mutex : this->_con->mutex, this_ethread());
  if (!lock.is_locked()) {
    // TODO reuse cached event
    Debug("udpcon", "callback get con lock failed");
    this->_reschedule(UDPEvents::UDP_USER_READ_READY, nullptr);
    return 0;
  }
  return this->_con->handleEvent(event, data);
}

void
UDP2ConnectionImpl::set_inactivity_timeout(ink_hrtime timeout_in)
{
}

EThread *
UDP2ConnectionImpl::get_thread()
{
  return this->_thread;
}

int
UDP2ConnectionImpl::close()
{
  // detach contiuation. we should not callback to con after `close` has been called
  this->_con  = nullptr;
  this->mutex = this->_thread->mutex;

  SList(UDP2Packet, in_link) aq(this->_recv_queue.popall());
  UDP2Packet *p;
  while ((p = aq.pop())) {
    delete p;
  }

  ink_assert(this->refcount() > 0);
  // if we have something to send or have events out, waiting ...
  Debug("udp_conn", "connection close, refcount %d", this->refcount());
  if ((this->_send_queue.empty() && this->_send_list.empty()) && this->refcount() == 1) {
    if (this->refcount_dec() == 0) {
      if (this->_accept_con != nullptr) {
        this->_accept_con->close_connection(this);
      } else {
        delete this;
      }
    } else {
      // nothing to send, enter end state.
      SET_HANDLER(&UDP2ConnectionImpl::endEvent);
    }
  } else {
    // have something to send, waiting for sending completely.
    // SET_HANDLER(&UDP2ConnectionImpl::endEvent);
    this->_reschedule(UDPEvents::UDP_SEND_EVENT, nullptr);
  }

  return 0;
}

int
UDP2ConnectionImpl::get_fd()
{
  return this->_fd;
}

Ptr<ProxyMutex> &
UDP2ConnectionImpl::get_mutex()
{
  return this->mutex;
}

ContFlags &
UDP2ConnectionImpl::get_control_flags()
{
  return _cont_flags;
}

sockaddr const *
UDP2ConnectionImpl::get_remote_addr()
{
  return &_to.sa;
}

const NetVCOptions &
UDP2ConnectionImpl::get_options()
{
  return _options;
}

bool
UDP2ConnectionImpl::_is_closed() const
{
  return this->_con == nullptr;
}

int
UDP2ConnectionImpl::mainEvent(int event, void *data)
{
  ink_assert(this->mutex->thread_holding == this->_thread);
  Debug("udp_conn", "mainEvent refcount: %d %s", this->refcount(), udp_event_name(static_cast<UDPEvents>(event)));
  ink_assert(this->refcount_dec() > 0);
  switch (static_cast<UDPEvents>(event)) {
  case UDPEvents::UDP_CONNECT_EVENT:
    ink_assert(data != nullptr);
    this->connect(static_cast<sockaddr *>(data));
    break;
  case UDPEvents::UDP_SEND_EVENT:
    this->write.triggered = 1;
    this->_reenable(&this->write.vio);
    break;
  case UDPEvents::UDP_USER_READ_READY:
    this->callback(NET_EVENT_DATAGRAM_READ_READY, this);
    break;
  case UDPEvents::UDP_CLOSE_EVENT:
    this->_process_close_connection(static_cast<UDP2ConnectionImpl *>(data));
    break;
  default:
    Debug("udp_con", "unknown events: %d", event);
    ink_release_assert(0);
    break;
  }

  if (this->_is_closed() && (this->_send_queue.empty() && this->_send_list.empty())) {
    SET_HANDLER(&UDP2ConnectionImpl::endEvent);
    this->handleEvent(0, nullptr);
  }

  return 0;
}

int
UDP2ConnectionImpl::startEvent(int event, void *data)
{
  // ink_assert(this->mutex->thread_holding == this->_thread);
  ink_assert(this->refcount_dec() > 0);
  NetHandler *nh = get_NetHandler(this->_thread);
  MUTEX_TRY_LOCK(lock, nh->mutex, this_ethread());
  if (!lock.is_locked()) {
    this->refcount_inc();
    SET_HANDLER(&UDP2ConnectionImpl::startEvent);
    this->_thread->schedule_in(this, net_retry_delay);
    return 1;
  }

  Debug("udp_conn", "startEvent complete refcount: %d", this->refcount());
  SET_HANDLER(&UDP2ConnectionImpl::mainEvent);
  ink_assert(nh->startIO(this) >= 0);
  return 0;
}

int
UDP2ConnectionImpl::endEvent(int event, void *data)
{
  ink_assert(this->mutex->thread_holding == this->_thread);
  ink_assert(this->refcount() > 0);
  Debug("udp_conn", "endEvent refcount: %d", this->refcount());
  if (this->refcount_dec() != 0) {
    return 0;
  }

  MUTEX_TRY_LOCK(lock, this->nh->mutex, this_ethread());
  if (!lock.is_locked()) {
    this->_reschedule(UDPEvents::UDP_SEND_EVENT, nullptr);
    return 0;
  }

  // kick out netevent from NetHandler
  this->nh->stopIO(this);
  if (this->_accept_con != nullptr) {
    this->_accept_con->close_connection(this);
  } else {
    delete this;
  }

  return 0;
}

int
UDP2ConnectionImpl::start_io()
{
  this->refcount_inc();
  return this->startEvent(0, nullptr);
}

int
UDP2ConnectionImpl::create_socket(sockaddr const *addr, int recv_buf, int send_buf)
{
  int res = 0;
  int fd  = -1;
  IpEndpoint local_addr{};
  int local_addr_len = sizeof(local_addr);
  if ((res = socketManager.socket(addr->sa_family, SOCK_DGRAM, 0)) < 0) {
    goto Lerror;
  }

  fd = res;
  if ((res = safe_fcntl(fd, F_SETFL, O_NONBLOCK)) < 0) {
    goto Lerror;
  }

  if (recv_buf > 0) {
    if (unlikely(socketManager.set_rcvbuf_size(fd, recv_buf))) {
      Debug("udp_con", "set_dnsbuf_size(%d) failed", recv_buf);
    }
  }
  if (send_buf > 0) {
    if (unlikely(socketManager.set_sndbuf_size(fd, send_buf))) {
      Debug("udp_con", "set_dnsbuf_size(%d) failed", send_buf);
    }
  }

  if (addr->sa_family == AF_INET) {
    bool succeeded = false;
    int enable     = 1;
#ifdef IP_PKTINFO
    if ((res = safe_setsockopt(fd, IPPROTO_IP, IP_PKTINFO, reinterpret_cast<char *>(&enable), sizeof(enable))) == 0) {
      succeeded = true;
    }
#endif
#ifdef IP_RECVDSTADDR
    if ((res = safe_setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, reinterpret_cast<char *>(&enable), sizeof(enable))) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Debug("udp_con", "setsockeopt for pktinfo failed");
      goto Lerror;
    }
  } else if (addr->sa_family == AF_INET6) {
    bool succeeded = false;
    int enable     = 1;
#ifdef IPV6_PKTINFO
    if ((res = safe_setsockopt(fd, IPPROTO_IPV6, IPV6_PKTINFO, reinterpret_cast<char *>(&enable), sizeof(enable))) == 0) {
      succeeded = true;
    }
#endif
#ifdef IPV6_RECVPKTINFO
    if ((res = safe_setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, reinterpret_cast<char *>(&enable), sizeof(enable))) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Debug("udp_con", "setsockeopt for pktinfo failed");
      goto Lerror;
    }
  }

  // If this is a class D address (i.e. multicast address), use REUSEADDR.
  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(SOCKOPT_ON), sizeof(int)) < 0)) {
    goto Lerror;
  }

  if (ats_is_ip6(addr) && (res = safe_setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, SOCKOPT_ON, sizeof(int))) < 0) {
    goto Lerror;
  }

  if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, SOCKOPT_ON, sizeof(int))) < 0) {
    goto Lerror;
  }

  if (-1 == socketManager.ink_bind(fd, addr, ats_ip_size(addr))) {
    char buff[INET6_ADDRPORTSTRLEN];
    Debug("udp_con", "ink bind failed on %s %s", ats_ip_nptop(addr, buff, sizeof(buff)), strerror(errno));
    goto Lerror;
  }

  if ((res = safe_getsockname(fd, &local_addr.sa, &local_addr_len)) < 0) {
    Debug("udp_con", "CreateUdpsocket: getsockname didn't work");
    goto Lerror;
  }

  ats_ip_copy(&this->_from, &local_addr.sa);
  this->_fd = fd;
  Debug("udp_con", "creating a udp socket port = %d---success", ats_ip_port_host_order(local_addr));
  return 0;
Lerror:
  Debug("udp_con", "creating a udp socket port = %d---soft failure", ats_ip_port_host_order(local_addr));
  if (fd != -1) {
    socketManager.close(fd);
  }

  return -errno;
}

IpEndpoint
UDP2ConnectionImpl::from()
{
  return this->_from;
}

IpEndpoint
UDP2ConnectionImpl::to()
{
  return this->_to;
}

int
UDP2ConnectionImpl::_connect(sockaddr const *addr)
{
  ink_assert(this->_fd != NO_FD);
  int res = ::connect(this->_fd, addr, ats_ip_size(addr));
  if (res >= 0) {
    ats_ip_copy(&_to, addr);
    return 0;
  }

  return -errno;
}

int
UDP2ConnectionImpl::connect(sockaddr const *addr)
{
  int res = this->_connect(addr);
  if (res < 0) {
    if ((res == -EINPROGRESS) || (res == -EWOULDBLOCK)) {
      this->_reschedule(UDPEvents::UDP_CONNECT_EVENT, const_cast<sockaddr *>(addr));
      return 0;
    }
    return this->callback(NET_EVENT_DATAGRAM_CONNECT_ERROR, this);
  }
  return this->callback(NET_EVENT_DATAGRAM_CONNECT_SUCCESS, this);
}

bool
UDP2ConnectionImpl::is_connected()
{
  return this->_to.port() != 0;
}

void
UDP2ConnectionImpl::set_continuation(Continuation *con)
{
  // ink_assert(this->mutex == nullptr);
  // rebind mutex;
  this->_con  = con;
  this->mutex = con->mutex;
  if (this->mutex == nullptr) {
    this->mutex = new_ProxyMutex();
  }
}

void
UDP2ConnectionImpl::bind_thread(EThread *thread)
{
  this->_thread = thread;
}

void
UDP2ConnectionImpl::_reschedule(UDPEvents e, void *data)
{
  this->refcount_inc();
  Debug("udp_con", "schedule event %s refcount: %d", udp_event_name(e), this->refcount());
  this->_thread->schedule_imm(this, static_cast<int>(e), data);
}

void
UDP2ConnectionImpl::_process_recv_event()
{
  this->callback(NET_EVENT_DATAGRAM_READ_READY, this);
}

void
UDP2ConnectionImpl::net_read_io(NetHandler *nh, EThread *thread)
{
  ink_assert(this->nh = nh);
  ink_assert(this->nh->mutex->thread_holding == thread);
  MUTEX_TRY_LOCK(lock, this->mutex, thread);
  if (!lock.is_locked()) {
    read_reschedule(nh, this);
    return;
  }

  NetState *s = &this->read;
  if (!s->enabled) {
    read_disable(nh, this);
    return;
  }

  // receive packet and queue onto UDPConnection.
  // don't call back connection at this time.
  int64_t r;
  int count = 0;

  struct msghdr msg;
  Ptr<IOBufferBlock> chain, next_chain;
  struct iovec tiovec[MAX_NIOV];
  int64_t size_index  = BUFFER_SIZE_INDEX_2K;
  int64_t buffer_size = BUFFER_SIZE_FOR_INDEX(size_index);
  // The max length of receive buffer is 32 * buffer_size (2048) = 65536 bytes.
  // Because the 'UDP Length' is type of uint16_t defined in RFC 768.
  // And there is 8 octets in 'User Datagram Header' which means the max length of payload is no more than 65527 bytes.
  do {
    // create IOBufferBlock chain to receive data
    unsigned int niov;
    IOBufferBlock *b, *last;

    // build struct iov
    // reuse the block in chain if available
    b    = chain.get();
    last = nullptr;
    for (niov = 0; niov < MAX_NIOV; niov++) {
      if (b == nullptr) {
        b = new_IOBufferBlock();
        b->alloc(size_index);
        if (last == nullptr) {
          chain = b;
        } else {
          last->next = b;
        }
      }

      tiovec[niov].iov_base = b->buf();
      tiovec[niov].iov_len  = b->block_size();

      last = b;
      b    = b->next.get();
    }

    // build struct msghdr
    sockaddr_in6 fromaddr;
    sockaddr_in6 toaddr;
    int toaddr_len = sizeof(toaddr);
    char *cbuf[1024];
    msg.msg_name       = &fromaddr;
    msg.msg_namelen    = sizeof(fromaddr);
    msg.msg_iov        = tiovec;
    msg.msg_iovlen     = niov;
    msg.msg_control    = cbuf;
    msg.msg_controllen = sizeof(cbuf);

    // receive data by recvmsg
    r = socketManager.recvmsg(this->get_fd(), &msg, 0);
    if (r <= 0) {
      if (r == -EAGAIN || r == -ENOTCONN) {
        this->read.triggered = 0;
        read_reschedule(nh, this);
        break;
      }
      this->callback(NET_EVENT_DATAGRAM_READ_ERROR, this);
      return;
    }

    // truncated check
    if (msg.msg_flags & MSG_TRUNC) {
      Debug("udp-read", "The UDP packet is truncated");
      ink_assert(!"truncate should not happen, if so please increase MAX_NIOV");
      this->callback(NET_EVENT_DATAGRAM_READ_ERROR, this);
      return;
    }

    // fill the IOBufferBlock chain
    int64_t saved = r;
    b             = chain.get();
    while (b && saved > 0) {
      if (saved > buffer_size) {
        b->fill(buffer_size);
        saved -= buffer_size;
        b = b->next.get();
      } else {
        b->fill(saved);
        saved      = 0;
        next_chain = b->next.get();
        b->next    = nullptr;
      }
    }

    safe_getsockname(this->get_fd(), reinterpret_cast<struct sockaddr *>(&toaddr), &toaddr_len);
    for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      switch (cmsg->cmsg_type) {
#ifdef IP_PKTINFO
      case IP_PKTINFO:
        if (cmsg->cmsg_level == IPPROTO_IP) {
          struct in_pktinfo *pktinfo                                = reinterpret_cast<struct in_pktinfo *>(CMSG_DATA(cmsg));
          reinterpret_cast<sockaddr_in *>(&toaddr)->sin_addr.s_addr = pktinfo->ipi_addr.s_addr;
        }
        break;
#endif
#ifdef IP_RECVDSTADDR
      case IP_RECVDSTADDR:
        if (cmsg->cmsg_level == IPPROTO_IP) {
          struct in_addr *addr                                      = reinterpret_cast<struct in_addr *>(CMSG_DATA(cmsg));
          reinterpret_cast<sockaddr_in *>(&toaddr)->sin_addr.s_addr = addr->s_addr;
        }
        break;
#endif
#if defined(IPV6_PKTINFO) || defined(IPV6_RECVPKTINFO)
      case IPV6_PKTINFO: // IPV6_RECVPKTINFO uses IPV6_PKTINFO too
        if (cmsg->cmsg_level == IPPROTO_IPV6) {
          struct in6_pktinfo *pktinfo = reinterpret_cast<struct in6_pktinfo *>(CMSG_DATA(cmsg));
          memcpy(toaddr.sin6_addr.s6_addr, &pktinfo->ipi6_addr, 16);
        }
        break;
#endif
      }
    }

    // queue onto the UDPConnection
    this->_recv_queue.push(new UDP2Packet(ats_ip_sa_cast(&fromaddr), ats_ip_sa_cast(&toaddr), chain));

    // reload the unused block
    chain      = next_chain;
    next_chain = nullptr;
    count++;
  } while (r > 0);

  if (count) {
    this->_process_recv_event();
  }
  read_reschedule(nh, this);
  return;
}

void
UDP2ConnectionImpl::net_write_io(NetHandler *nh, EThread *thread)
{
  ink_assert(this->nh = nh);
  ink_assert(this->nh->mutex->thread_holding == thread);
  MUTEX_TRY_LOCK(lock, this->mutex, thread);
  if (!lock.is_locked()) {
    write_reschedule(nh, this);
    return;
  }

  MUTEX_TRY_LOCK(lock2, this->nh->mutex, thread);
  if (!lock2.is_locked()) {
    read_reschedule(nh, this);
    return;
  }

  NetState *s = &this->write;
  if (!s->enabled) {
    write_disable(nh, this);
    return;
  }

  SList(UDP2Packet, out_link) aq(this->_send_queue.popall());
  UDP2Packet *p;
  while ((p = aq.pop())) {
    this->_send_list.push_back(UDP2PacketUPtr(p));
  }

  int count = 0;
  while (!this->_send_list.empty()) {
    auto p = std::move(this->_send_list.front());
    this->_send_list.pop_front();

    int rc = 0;
    if (this->is_connected()) {
      rc = this->_send(p.get());
    } else {
      ink_assert(p->to.port() != 0);
      rc = this->_send_to(p.get());
    }

    if (rc >= 0) {
      count++;
      continue;
    }

    if (errno == EAGAIN) {
      this->write.triggered = 0;
      write_reschedule(nh, this);
      break;
    } else {
      this->write.triggered = 0;
      this->callback(NET_EVENT_DATAGRAM_WRITE_ERROR, this);
      return;
    }
  }

  if (count > 0) {
    this->callback(NET_EVENT_DATAGRAM_WRITE_READY, this);
  }

  return;
}

int
UDP2ConnectionImpl::_send(UDP2Packet *p)
{
  ink_assert(this->is_connected());
  struct iovec iov[MAX_NIOV];
  int n, iov_len = 0;

  for (IOBufferBlock *b = p->chain.get(); b != nullptr; b = b->next.get()) {
    iov[iov_len].iov_base = static_cast<caddr_t>(b->start());
    iov[iov_len].iov_len  = b->size();
    iov_len++;
  }

  n = socketManager.writev(this->_fd, iov, iov_len);
  if (n >= 0) {
    return n;
  }

  Debug("udp_con", "writev failed: %s", strerror(errno));
  return -errno;
}

int
UDP2ConnectionImpl::_send_to(UDP2Packet *p)
{
  ink_assert(this->is_connected() == false);
  struct msghdr msg;
  struct iovec iov[MAX_NIOV];
  int real_len = 0;
  int n, iov_len = 0;

#if !defined(solaris)
  msg.msg_control    = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags      = 0;
#endif
  msg.msg_name    = reinterpret_cast<caddr_t>(&p->to.sa);
  msg.msg_namelen = ats_ip_size(p->to);
  iov_len         = 0;

  for (IOBufferBlock *b = p->chain.get(); b != nullptr; b = b->next.get()) {
    iov[iov_len].iov_base = static_cast<caddr_t>(b->start());
    iov[iov_len].iov_len  = b->size();
    real_len += iov[iov_len].iov_len;
    iov_len++;
  }

  msg.msg_iov    = iov;
  msg.msg_iovlen = iov_len;

  n = socketManager.sendmsg(this->_fd, &msg, 0);
  if (n >= 0) {
    return n;
  }

  Debug("udp_conn", "send from external thread failed: %d-%s", errno, strerror(errno));
  return -errno;
}

UDP2Packet *
UDP2ConnectionImpl::recv()
{
  // user should call recv immediatly when UDP2Connection callback to
  // contiuation. Since the mutex is already grabed from eventsystem or
  // NetHandler, we don't need explicit take lock here.
  ink_assert(!this->_is_closed());
  ink_assert(this->mutex->thread_holding == this->_thread);
  SList(UDP2Packet, in_link) aq(this->_recv_queue.popall());
  UDP2Packet *t;
  while ((t = aq.pop())) {
    this->_recv_list.push_back(UDP2PacketUPtr(t));
  }

  if (this->_recv_list.empty()) {
    return nullptr;
  }

  auto p = std::move(this->_recv_list.front());
  this->_recv_list.pop_front();
  auto ret = p.get();
  p.release();
  return ret;
}

void
UDP2ConnectionImpl::_reenable(VIO *vio)
{
  NetState *state = &this->write;
  if (vio != &this->write.vio) {
    state = &this->read;
  }

  if (state->enabled) {
    return;
  }

  state->enabled = 1;
  EThread *t     = this->mutex->thread_holding;
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

int
UDP2ConnectionImpl::receive(UDP2Packet *packet)
{
  if (this->_is_closed()) {
    return 0;
  }

  this->_recv_queue.push(packet);
  if (this->_thread == this_ethread()) {
    this->callback(NET_EVENT_DATAGRAM_READ_READY, nullptr);
    return 0;
  }

  this->_reschedule(UDPEvents::UDP_USER_READ_READY, nullptr);
  return 0;
}

int
UDP2ConnectionImpl::send(UDP2Packet *p)
{
  ink_assert(!this->_is_closed());
  ink_assert(this->is_connected() || p->to.isValid());
  this->_send_queue.push(p);
  if (this->_thread == this_thread()) {
    // in local thread;
    this->_reenable(&this->write.vio);
  } else {
    // cross thread
    this->_reschedule(UDPEvents::UDP_SEND_EVENT, nullptr);
  }
  this->nh->signalActivity();
  return 0;
}

void
UDP2ConnectionImpl::_process_close_connection(UDP2ConnectionImpl *con)
{
  ink_release_assert(!"never be called");
}

//
// AcceptUDP2ConnectionImpl
//
uint64_t
hash_code(const IpEndpoint &peer)
{
  return ats_ip_hash(&peer.sa) ^ ats_ip_port_hash(&peer.sa);
}

UDP2ConnectionImpl *
AcceptUDP2ConnectionImpl::find_connection(const IpEndpoint &peer)
{
  ink_assert(this->mutex->thread_holding == this->_thread);
  uint64_t hash = hash_code(peer);
  auto &map     = this->_connection_map;
  auto it       = map.find(hash);
  if (it == map.end()) {
    return nullptr;
  }

  for (auto tt : it->second) {
    if (ats_ip_addr_port_eq(peer, tt->to())) {
      return tt;
    }
  }

  return nullptr;
}

UDP2ConnectionImpl *
AcceptUDP2ConnectionImpl::create_connection(const IpEndpoint &local, const IpEndpoint &peer, Continuation *c, EThread *thread)
{
  ink_assert(this->mutex->thread_holding == this->_thread);
  ink_assert(peer.isValid());

  uint64_t hash = hash_code(peer);
  auto con      = this->find_connection(peer);
  if (con != nullptr) {
    return con;
  }

  con = new UDP2ConnectionImpl(this, c, thread);
  ink_release_assert(con->create_socket(local, 1048576, 1048576) >= 0);
  if (con->connect(&peer.sa) < 0) {
    char tmp[128] = {};
    Debug("udp_con", "Accept conn connect to peer failed: %s", ats_ip_ntop(&peer.sa, tmp, 128));
    delete con;
    return nullptr;
  }

  this->refcount_inc();
  auto it = this->_connection_map.find(hash);
  if (it == this->_connection_map.end()) {
    std::list<UDP2ConnectionImpl *> l;
    l.push_back(con);
    this->_connection_map.insert(std::make_pair(hash, l));
  } else {
    it->second.push_back(con);
  }

  // in this time every packets read from accept will dispatch to new conn
  // So we need to take old packet which already in list.
  SList(UDP2Packet, in_link) aq(this->_recv_queue.popall());
  Queue<UDP2Packet> st;
  UDP2Packet *p;
  while ((p = aq.pop())) {
    st.push(p);
  }

  while ((p = st.pop())) {
    this->_recv_list.push_back(UDP2PacketUPtr(p));
  }

  for (auto it = this->_recv_list.begin(); it != this->_recv_list.end(); it++) {
    if (ats_ip_addr_port_eq((*it)->to, con->to())) {
      auto p = (*it).get();
      (*it).release();
      this->_recv_list.erase(it);
      con->receive(p);
    }
  }

  ink_assert(con->start_io() >= 0);

  return con;
}

void
AcceptUDP2ConnectionImpl::_process_close_connection(UDP2ConnectionImpl *con)
{
  ink_assert(con != nullptr);
  uint64_t hash = hash_code(con->to());
  auto it       = this->_connection_map.find(hash);
  if (it != this->_connection_map.end()) {
    for (auto itt = it->second.begin(); itt != it->second.end(); itt++) {
      if (*itt == con) {
        it->second.erase(itt);
        if (it->second.empty()) {
          this->_connection_map.erase(it);
        }
        this->refcount_dec();
        break;
      }
    }
  }

  delete con;
}

int
AcceptUDP2ConnectionImpl::close_connection(UDP2ConnectionImpl *con)
{
  ink_assert(con != nullptr);
  ink_assert(con->refcount() == 0);
  MUTEX_TRY_LOCK(lock, this->mutex, this_ethread());
  if (lock.is_locked()) {
    this->_process_close_connection(con);
    return 0;
  }

  this->_reschedule(UDPEvents::UDP_CLOSE_EVENT, con);
  return 0;
}

void
AcceptUDP2ConnectionImpl::_process_recv_event()
{
  SList(UDP2Packet, in_link) aq(this->_recv_queue.popall());
  Queue<UDP2Packet> st;
  UDP2Packet *p;
  while ((p = aq.pop())) {
    st.push(p);
  }

  while ((p = st.pop())) {
    auto c = this->find_connection(p->from);
    if (c == nullptr) {
      this->_recv_list.push_back(UDP2PacketUPtr(p));
    } else {
      c->receive(p);
    }
  }

  // Have something to read, signal continuation
  if (!this->_recv_list.empty()) {
    this->callback(NET_EVENT_DATAGRAM_READ_READY, this);
  }
}
