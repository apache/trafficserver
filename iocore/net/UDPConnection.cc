/** @file

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

#include <set>

#include "UDPConnection.h"

#include "tscore/ink_atomic.h"

static const char *
udp_event_name(UDP2ConnectionImpl::UDPEvents e)
{
  switch (e) {
  case UDP2ConnectionImpl::UDPEvents::UDP_START_EVENT:
    return "UDP_START_EVENT";
  case UDP2ConnectionImpl::UDPEvents::UDP_CONNECT_EVENT:
    return "UDP_CONNECT_EVENT";
  case UDP2ConnectionImpl::UDPEvents::UDP_USER_READ_READY:
    return "UDP_USER_READ_READY";
  default:
    return "UNKNOWN EVENT";
  };

  return nullptr;
}

static const char *
udp_event_name(int e)
{
  return udp_event_name(static_cast<UDP2ConnectionImpl::UDPEvents>(e));
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
  SET_HANDLER(&UDP2ConnectionImpl::startEvent);
}

UDP2ConnectionImpl::~UDP2ConnectionImpl()
{
  Debug("udp_con", "destroy");

  int fd = this->_fd;

  this->_fd = -1;
  if (fd != -1) {
    ::close(fd);
  }
}

void
UDP2ConnectionImpl::free(EThread *t)
{
  Debug("udp_con", "free connection");
  this->mutex = nullptr;

  this->_close_event(UDPEvents::UDP_USER_READ_READY);
  this->_close_event(UDPEvents::UDP_START_EVENT);
  this->_close_event(UDPEvents::UDP_CONNECT_EVENT);

  this->read.enabled   = 0;
  this->read.triggered = 0;

  this->write.enabled   = 0;
  this->write.triggered = 0;
  this->nh->stopIO(this);

  int fd = this->_fd;

  this->_fd = -1;
  if (fd != -1) {
    ::close(fd);
  }

  delete this;
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

  this->_recv_list.clear();
  if (this->_is_send_complete()) {
    this->free(nullptr);
    return 0;
  }

  this->_reenable(&this->write.vio);
  this->nh->signalActivity();
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

bool
UDP2ConnectionImpl::_is_closed() const
{
  return this->_con == nullptr;
}

int
UDP2ConnectionImpl::startEvent(int event, void *data)
{
  Debug("udp_con", "startEvent %s-%d", udp_event_name(event), event);
  this->_close_event(event);
  switch (static_cast<UDPEvents>(event)) {
  case UDPEvents::UDP_CONNECT_EVENT:
    this->connect(&this->_to.sa);
    break;
  case UDPEvents::UDP_START_EVENT: {
    NetHandler *nh = get_NetHandler(this->_thread);
    if (this->_thread == this_ethread()) {
      MUTEX_TRY_LOCK(lock, nh->mutex, this->_thread);
      if (lock.is_locked()) {
        SET_HANDLER(&UDP2ConnectionImpl::mainEvent);
        ink_assert(nh->startIO(this) >= 0);
        // reenable read since there might be some packets in socket's buffer.
        if (!this->_recv_list.empty()) {
          this->callback(NET_EVENT_DATAGRAM_READ_READY, this);
        }
        break;
      }
    }
    this->_reschedule(UDPEvents::UDP_START_EVENT, nullptr, net_retry_delay);
    break;
  }
  default:
    ink_release_assert(0);
    break;
  }

  if (this->_is_closed() && this->_is_send_complete()) {
    this->free(nullptr);
  } else if (this->_is_closed()) {
    this->_reenable(&this->write.vio);
    this->nh->signalActivity();
  }
  return 0;
}

int
UDP2ConnectionImpl::mainEvent(int event, void *data)
{
  ink_assert(this->mutex->thread_holding == this->_thread);
  this->_close_event(event);
  switch (static_cast<UDPEvents>(event)) {
  case UDPEvents::UDP_CONNECT_EVENT:
    this->connect(&this->_to.sa);
    break;
  default:
    Debug("udp_con", "unknown events: %d", event);
    ink_release_assert(0);
    break;
  }

  if (this->_is_closed() && this->_is_send_complete()) {
    this->free(nullptr);
  } else if (this->_is_closed()) {
    this->_reenable(&this->write.vio);
    this->nh->signalActivity();
  }

  return 0;
}

int
UDP2ConnectionImpl::start_io()
{
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
  // if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(SOCKOPT_ON), sizeof(int)) < 0)) {
  //   goto Lerror;
  // }

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
UDP2ConnectionImpl::_connect()
{
  ink_assert(this->_fd != NO_FD);
  ink_assert(this->_to.port() != 0);
  int res = ::connect(this->_fd, &this->_to.sa, ats_ip_size(&this->_to.sa));
  if (res >= 0) {
    this->_connected = true;
    return 0;
  }

  return -errno;
}

int
UDP2ConnectionImpl::connect(sockaddr const *addr)
{
  if (this->_to.port() == 0) {
    ats_ip_copy(&this->_to, addr);
  }
  int res = this->_connect();
  if (res < 0) {
    if ((res == -EINPROGRESS) || (res == -EWOULDBLOCK)) {
      this->_reschedule(UDPEvents::UDP_CONNECT_EVENT, nullptr);
      return 0;
    }
    return this->callback(NET_EVENT_DATAGRAM_CONNECT_ERROR, this);
  }
  return this->callback(NET_EVENT_DATAGRAM_CONNECT_SUCCESS, this);
}

bool
UDP2ConnectionImpl::is_connected() const
{
  return this->_connected;
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
UDP2ConnectionImpl::_reschedule(UDPEvents e, void *data, int64_t delay)
{
  Debug("udp_con", "schedule event %s", udp_event_name(e));
  Event **event = nullptr;
  switch (e) {
  case UDPEvents::UDP_START_EVENT:
    event = &this->_start_event;
    break;
  case UDPEvents::UDP_CONNECT_EVENT:
    event = &this->_connect_event;
    break;
  case UDPEvents::UDP_USER_READ_READY:
    event = &this->_user_read_ready_event;
    break;
  default:
    ink_release_assert(!"unknown events");
    break;
  }

  if (*event != nullptr) {
    (*event)->cancel();
    (*event) = nullptr;
  }

  if (delay) {
    *event = this->_thread->schedule_in(this, delay, static_cast<int>(e), data);
  } else {
    *event = this->_thread->schedule_imm(this, static_cast<int>(e), data);
  }
}

void
UDP2ConnectionImpl::_close_event(int e)
{
  this->_close_event(static_cast<UDPEvents>(e));
}

void
UDP2ConnectionImpl::_close_event(UDPEvents e)
{
  Event **ptr = nullptr;
  switch (e) {
  case UDPEvents::UDP_START_EVENT:
    ptr = &this->_start_event;
    break;
  case UDPEvents::UDP_CONNECT_EVENT:
    ptr = &this->_connect_event;
    break;
  case UDPEvents::UDP_USER_READ_READY:
    ptr = &this->_user_read_ready_event;
    break;
  default:
    ink_release_assert(!"unknown ptrs");
    break;
  }

  if (*ptr != nullptr) {
    (*ptr)->cancel();
    *ptr = nullptr;
  }
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

  this->_read_from_net(nh, thread, true);

  read_reschedule(nh, this);
}

void
UDP2ConnectionImpl::_read_from_net(NetHandler *nh, EThread *thread, bool callback)
{
  // receive packet and queue onto UDPConnection.
  // don't call back connection at this time.
  int64_t r = 0;
  int count = 0;

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

    UDP2PacketUPtr p = std::make_unique<UDP2Packet>();
    r = this->is_connected() ? this->_read(tiovec, niov, p->from, p->to) : this->_readmsg(tiovec, niov, p->from, p->to);
    if (r <= 0) {
      if (r == -EAGAIN || r == -ENOTCONN) {
        this->read.triggered = 0;
        break;
      }

      if (callback) {
        this->callback(NET_EVENT_DATAGRAM_READ_ERROR, this);
      }
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

    p->chain = chain;
    std::string str(p->chain->start(), p->chain->read_avail());
    if (str == "helloword1") {
      std::cout << "mother fuck" << std::endl;
    }
    std::cout << "fuck read: " << std::string(p->chain->start(), p->chain->read_avail()) << std::endl;

    // queue onto the UDPConnection
    this->_recv_list.push_back(std::move(p));

    // reload the unused block
    chain      = next_chain;
    next_chain = nullptr;
    count++;
  } while (r > 0);

  Debug("udp_con", "read %d packets from net", count);

  if (callback && !this->_recv_list.empty()) {
    this->callback(NET_EVENT_DATAGRAM_READ_READY, this);
  }
  return;
}

int
UDP2ConnectionImpl::_readmsg(struct iovec *iov, int len, IpEndpoint &fromaddr, IpEndpoint &toaddr)
{
  struct msghdr msg;
  int toaddr_len = sizeof(toaddr);
  char *cbuf[1024];
  msg.msg_name       = &fromaddr.sin6;
  msg.msg_namelen    = sizeof(fromaddr);
  msg.msg_iov        = iov;
  msg.msg_iovlen     = len;
  msg.msg_control    = cbuf;
  msg.msg_controllen = sizeof(cbuf);

  int rc = socketManager.recvmsg(this->get_fd(), &msg, 0);
  if (rc <= 0) {
    return rc;
  }

  // truncated check
  if (msg.msg_flags & MSG_TRUNC) {
    Debug("udp-read", "The UDP packet is truncated");
    ink_assert(!"truncate should not happen, if so please increase MAX_NIOV");
    rc = -0x12345;
    return rc;
  }

  safe_getsockname(this->get_fd(), &toaddr.sa, &toaddr_len);
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
        memcpy(toaddr.sin6.sin6_addr.s6_addr, &pktinfo->ipi6_addr, 16);
      }
      break;
#endif
    }
  }

  char buff[INET6_ADDRPORTSTRLEN * 2] = {0};
  Debug("udp_accept", "read packet %s ----> %s", ats_ip_nptop(&fromaddr.sa, buff, sizeof(buff) - INET6_ADDRPORTSTRLEN),
        ats_ip_nptop(&toaddr.sa, buff + INET6_ADDRPORTSTRLEN, sizeof(buff) - INET6_ADDRPORTSTRLEN));
  ink_release_assert(!ats_ip_addr_port_eq(&fromaddr.sa, &toaddr.sa));
  return rc;
}

int
UDP2ConnectionImpl::_read(struct iovec *iov, int len, IpEndpoint &from, IpEndpoint &to)
{
  ink_release_assert(this->_from.isValid() && this->_to.isValid());
  int rc = socketManager.readv(this->get_fd(), iov, len);
  if (rc <= 0) {
    return rc;
  }

  ats_ip_copy(&from, &this->_to.sa);
  ats_ip_copy(&to, &this->_from.sa);
  return rc;
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

  SList(UDP2Packet, link) aq(this->_external_send_list.popall());
  UDP2Packet *tp;
  Queue<UDP2Packet> tmp;
  while ((tp = aq.pop())) {
    tmp.push(tp);
  }

  while ((tp = tmp.pop())) {
    this->_send_list.push_back(UDP2PacketUPtr(tp));
  }

  int count = 0;
  while (!this->_send_list.empty()) {
    auto p = this->_send_list.front().get();

    int rc = this->is_connected() ? this->_send(p) : this->_sendmsg(p);
    if (rc >= 0) {
      count++;
      std::cout << "fuck send: " << std::string(p->chain->start(), p->chain->read_avail()) << std::endl;
      this->_send_list.pop_front();
      continue;
    }

    if (errno == EAGAIN) {
      this->write.triggered = 0;
      write_reschedule(nh, this);
      break;
    } else {
      this->write.triggered = 0;
      this->callback(NET_EVENT_DATAGRAM_WRITE_ERROR, this);
    }
  }

  if (count > 0) {
    this->callback(NET_EVENT_DATAGRAM_WRITE_READY, this);
  }

  if (this->_is_closed() && this->_is_send_complete()) {
    this->free(nullptr);
  }

  return;
}

bool
UDP2ConnectionImpl::_is_send_complete()
{
  return this->_send_list.empty() && this->_external_send_list.empty();
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
UDP2ConnectionImpl::_sendmsg(UDP2Packet *p)
{
  ink_assert(p->to.isValid());
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

  n = socketManager.sendmsg(this->get_fd(), &msg, 0);
  if (n >= 0) {
    char buff[INET6_ADDRPORTSTRLEN * 2] = {0};
    Debug("udp_accept", "send packet %s ----> %s", ats_ip_nptop(&p->from.sa, buff, sizeof(buff) - INET6_ADDRPORTSTRLEN),
          ats_ip_nptop(&p->to.sa, buff + INET6_ADDRPORTSTRLEN, sizeof(buff) - INET6_ADDRPORTSTRLEN));
    return n;
  }

  Debug("udp_conn", "send from external thread failed: %d-%s", errno, strerror(errno));
  return -errno;
}

UDP2PacketUPtr
UDP2ConnectionImpl::recv()
{
  ink_assert(!this->_is_closed());
  ink_assert(this->mutex->thread_holding == this->_thread);
  if (this->_recv_list.empty()) {
    return nullptr;
  }

  auto p = std::move(this->_recv_list.front());
  this->_recv_list.pop_front();
  return p;
}

void
UDP2ConnectionImpl::_reenable(VIO *vio)
{
  NetState *state = &this->write;
  if (vio != &this->write.vio) {
    state = &this->read;
  }

  Debug("udp_con", "udp connection reenable %s", vio == &this->read.vio ? "read" : "write");
  state->enabled = 1;
  ink_release_assert(!closed);
  auto t = this_ethread();
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
UDP2ConnectionImpl::send(UDP2PacketUPtr p, bool flush)
{
  ink_assert(!this->_is_closed());
  ink_assert(this->is_connected() || p->to.isValid());
  this->_external_send_list.push(p.get());
  p.release();
  if (flush) {
    this->flush();
  }
  return 0;
}

void
UDP2ConnectionImpl::flush()
{
  this->_reenable(&this->write.vio);
  this->nh->signalActivity();
}
