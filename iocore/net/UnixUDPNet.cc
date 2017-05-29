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

/****************************************************************************

  UnixUDPNet.cc
  UDPNet implementation


 ****************************************************************************/

#include "P_Net.h"
#include "P_UDPNet.h"

typedef int (UDPNetHandler::*UDPNetContHandler)(int, void *);

inkcoreapi ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");
EventType ET_UDP;

#if defined(linux) && !defined(DEBUG)
#define NODIAGS
#endif

//
// Global Data
//

UDPNetProcessorInternal udpNetInternal;
UDPNetProcessor &udpNet = udpNetInternal;

int32_t g_udp_periodicCleanupSlots;
int32_t g_udp_periodicFreeCancelledPkts;
int32_t g_udp_numSendRetries;

#include "P_LibBulkIO.h"

//
// Public functions
// See header for documentation
//
int G_bwGrapherFd;
sockaddr_in6 G_bwGrapherLoc;

void
initialize_thread_for_udp_net(EThread *thread)
{
  new ((ink_dummy_for_new *)get_UDPPollCont(thread)) PollCont(thread->mutex);
  new ((ink_dummy_for_new *)get_UDPNetHandler(thread)) UDPNetHandler;

  // This variable controls how often we cleanup the cancelled packets.
  // If it is set to 0, then cleanup never occurs.
  REC_ReadConfigInt32(g_udp_periodicFreeCancelledPkts, "proxy.config.udp.free_cancelled_pkts_sec");

  // This variable controls how many "slots" of the udp calendar queue we cleanup.
  // If it is set to 0, then cleanup never occurs.  This value makes sense
  // only if the above variable is set.
  REC_ReadConfigInt32(g_udp_periodicCleanupSlots, "proxy.config.udp.periodic_cleanup");

  // UDP sends can fail with errno=EAGAIN.  This variable determines the # of
  // times the UDP thread retries before giving up.  Set to 0 to keep trying forever.
  REC_ReadConfigInt32(g_udp_numSendRetries, "proxy.config.udp.send_retries");
  g_udp_numSendRetries = g_udp_numSendRetries < 0 ? 0 : g_udp_numSendRetries;

  thread->schedule_every(get_UDPPollCont(thread), -9);
  thread->schedule_imm(get_UDPNetHandler(thread));
}

int
UDPNetProcessorInternal::start(int n_upd_threads, size_t stacksize)
{
  if (n_upd_threads < 1) {
    return -1;
  }

  ET_UDP = eventProcessor.spawn_event_threads(n_upd_threads, "ET_UDP", stacksize);
  if (ET_UDP < 0) { // Probably can't happen, maybe at some point EventType should be unsigned ?
    return -1;
  }

  pollCont_offset      = eventProcessor.allocate(sizeof(PollCont));
  udpNetHandler_offset = eventProcessor.allocate(sizeof(UDPNetHandler));

  for (int i = 0; i < eventProcessor.n_threads_for_type[ET_UDP]; i++) {
    initialize_thread_for_udp_net(eventProcessor.eventthread[ET_UDP][i]);
  }

  return 0;
}

void
UDPNetProcessorInternal::udp_read_from_net(UDPNetHandler *nh, UDPConnection *xuc)
{
  UnixUDPConnection *uc = (UnixUDPConnection *)xuc;

  // receive packet and queue onto UDPConnection.
  // don't call back connection at this time.
  int r;
  int iters = 0;
  do {
    sockaddr_in6 fromaddr;
    socklen_t fromlen = sizeof(fromaddr);
    // XXX: want to be 0 copy.
    // XXX: really should read into next contiguous region of an IOBufferData
    // which gets referenced by IOBufferBlock.
    char buf[65536];
    int buflen = sizeof(buf);
    r          = socketManager.recvfrom(uc->getFd(), buf, buflen, 0, (struct sockaddr *)&fromaddr, &fromlen);
    if (r <= 0) {
      // error
      break;
    }
    // create packet
    UDPPacket *p = new_incoming_UDPPacket(ats_ip_sa_cast(&fromaddr), buf, r);
    p->setConnection(uc);
    // queue onto the UDPConnection
    ink_atomiclist_push(&uc->inQueue, p);
    iters++;
  } while (r > 0);
  if (iters >= 1) {
    Debug("udp-read", "read %d at a time", iters);
  }
  // if not already on to-be-called-back queue, then add it.
  if (!uc->onCallbackQueue) {
    ink_assert(uc->callback_link.next == nullptr);
    ink_assert(uc->callback_link.prev == nullptr);
    uc->AddRef();
    nh->udp_callbacks.enqueue(uc);
    uc->onCallbackQueue = 1;
  }
}

int
UDPNetProcessorInternal::udp_callback(UDPNetHandler *nh, UDPConnection *xuc, EThread *thread)
{
  (void)nh;
  UnixUDPConnection *uc = (UnixUDPConnection *)xuc;

  if (uc->continuation && uc->mutex) {
    MUTEX_TRY_LOCK_FOR(lock, uc->mutex, thread, uc->continuation);
    if (!lock.is_locked()) {
      return 1;
    }
    uc->AddRef();
    uc->callbackHandler(0, nullptr);
    return 0;
  } else {
    ink_assert(!"doesn't reach here");
    if (!uc->callbackAction) {
      uc->AddRef();
      uc->callbackAction = eventProcessor.schedule_imm(uc);
    }
    return 0;
  }
}

#define UNINITIALIZED_EVENT_PTR (Event *)0xdeadbeef

// cheesy implementation of a asynchronous read and callback for Unix
class UDPReadContinuation : public Continuation
{
public:
  UDPReadContinuation(Event *completionToken);
  UDPReadContinuation();
  ~UDPReadContinuation() override;
  inline void free();
  inline void init_token(Event *completionToken);
  inline void init_read(int fd, IOBufferBlock *buf, int len, struct sockaddr *fromaddr, socklen_t *fromaddrlen);

  void
  set_timer(int seconds)
  {
    timeout_interval = HRTIME_SECONDS(seconds);
  }

  void cancel();
  int readPollEvent(int event, Event *e);

  Action *
  getAction()
  {
    return event;
  }

  void setupPollDescriptor();

private:
  Event *event = UNINITIALIZED_EVENT_PTR; // the completion event token created
  // on behalf of the client
  Ptr<IOBufferBlock> readbuf{nullptr};
  int readlen                   = 0;
  struct sockaddr_in6 *fromaddr = nullptr;
  socklen_t *fromaddrlen        = nullptr;
  int fd                        = NO_FD; // fd we are reading from
  int ifd                       = NO_FD; // poll fd index
  ink_hrtime period             = 0;     // polling period
  ink_hrtime elapsed_time       = 0;
  ink_hrtime timeout_interval   = 0;
};

ClassAllocator<UDPReadContinuation> udpReadContAllocator("udpReadContAllocator");

UDPReadContinuation::UDPReadContinuation(Event *completionToken)
  : Continuation(nullptr),
    event(completionToken),
    readbuf(nullptr),
    readlen(0),
    fromaddrlen(nullptr),
    fd(-1),
    ifd(-1),
    period(0),
    elapsed_time(0),
    timeout_interval(0)
{
  if (completionToken->continuation) {
    this->mutex = completionToken->continuation->mutex;
  } else {
    this->mutex = new_ProxyMutex();
  }
}

UDPReadContinuation::UDPReadContinuation() : Continuation(nullptr)
{
}

inline void
UDPReadContinuation::free()
{
  ink_assert(event != nullptr);
  completionUtil::destroy(event);
  event            = nullptr;
  readbuf          = nullptr;
  readlen          = 0;
  fromaddrlen      = nullptr;
  fd               = -1;
  ifd              = -1;
  period           = 0;
  elapsed_time     = 0;
  timeout_interval = 0;
  mutex            = nullptr;
  udpReadContAllocator.free(this);
}

inline void
UDPReadContinuation::init_token(Event *completionToken)
{
  if (completionToken->continuation) {
    this->mutex = completionToken->continuation->mutex;
  } else {
    this->mutex = new_ProxyMutex();
  }
  event = completionToken;
}

inline void
UDPReadContinuation::init_read(int rfd, IOBufferBlock *buf, int len, struct sockaddr *fromaddr_, socklen_t *fromaddrlen_)
{
  ink_assert(rfd >= 0 && buf != nullptr && fromaddr_ != nullptr && fromaddrlen_ != nullptr);
  fd          = rfd;
  readbuf     = buf;
  readlen     = len;
  fromaddr    = ats_ip6_cast(fromaddr_);
  fromaddrlen = fromaddrlen_;
  SET_HANDLER(&UDPReadContinuation::readPollEvent);
  period = -HRTIME_MSECONDS(net_event_period);
  setupPollDescriptor();
  this_ethread()->schedule_every(this, period);
}

UDPReadContinuation::~UDPReadContinuation()
{
  if (event != UNINITIALIZED_EVENT_PTR) {
    ink_assert(event != nullptr);
    completionUtil::destroy(event);
    event = nullptr;
  }
}

void
UDPReadContinuation::cancel()
{
  // I don't think this actually cancels it correctly right now.
  event->cancel();
}

void
UDPReadContinuation::setupPollDescriptor()
{
#if TS_USE_EPOLL
  Pollfd *pfd;
  EThread *et  = (EThread *)this_thread();
  PollCont *pc = get_PollCont(et);
  if (pc->nextPollDescriptor == nullptr) {
    pc->nextPollDescriptor = new PollDescriptor();
  }
  pfd     = pc->nextPollDescriptor->alloc();
  pfd->fd = fd;
  ifd     = pfd - pc->nextPollDescriptor->pfd;
  ink_assert(pc->nextPollDescriptor->nfds > ifd);
  pfd->events  = POLLIN;
  pfd->revents = 0;
#endif
}

int
UDPReadContinuation::readPollEvent(int event_, Event *e)
{
  (void)event_;
  (void)e;

  // PollCont *pc = get_PollCont(e->ethread);
  Continuation *c;

  if (event->cancelled) {
    e->cancel();
    free();
    //    delete this;
    return EVENT_DONE;
  }
  // See if the request has timed out
  if (timeout_interval) {
    elapsed_time += -period;
    if (elapsed_time >= timeout_interval) {
      c = completionUtil::getContinuation(event);
      // TODO: Should we deal with the return code?
      c->handleEvent(NET_EVENT_DATAGRAM_READ_ERROR, event);
      e->cancel();
      free();
      //      delete this;
      return EVENT_DONE;
    }
  }
  // ink_assert(ifd < 0 || event_ == EVENT_INTERVAL || (event_ == EVENT_POLL && pc->pollDescriptor->nfds > ifd &&
  // pc->pollDescriptor->pfd[ifd].fd == fd));
  // if (ifd < 0 || event_ == EVENT_INTERVAL || (pc->pollDescriptor->pfd[ifd].revents & POLLIN)) {
  // ink_assert(!"incomplete");
  c = completionUtil::getContinuation(event);
  // do read
  socklen_t tmp_fromlen = *fromaddrlen;
  int rlen              = socketManager.recvfrom(fd, readbuf->end(), readlen, 0, ats_ip_sa_cast(fromaddr), &tmp_fromlen);

  completionUtil::setThread(event, e->ethread);
  // call back user with their event
  if (rlen > 0) {
    // do callback if read is successful
    *fromaddrlen = tmp_fromlen;
    completionUtil::setInfo(event, fd, readbuf, rlen, errno);
    readbuf->fill(rlen);
    // TODO: Should we deal with the return code?
    c->handleEvent(NET_EVENT_DATAGRAM_READ_COMPLETE, event);
    e->cancel();
    free();
    // delete this;
    return EVENT_DONE;
  } else if (rlen < 0 && rlen != -EAGAIN) {
    // signal error.
    *fromaddrlen = tmp_fromlen;
    completionUtil::setInfo(event, fd, readbuf, rlen, errno);
    c = completionUtil::getContinuation(event);
    // TODO: Should we deal with the return code?
    c->handleEvent(NET_EVENT_DATAGRAM_READ_ERROR, event);
    e->cancel();
    free();
    // delete this;
    return EVENT_DONE;
  } else {
    completionUtil::setThread(event, nullptr);
  }

  if (event->cancelled) {
    e->cancel();
    free();
    // delete this;
    return EVENT_DONE;
  }
  // reestablish poll
  setupPollDescriptor();

  return EVENT_CONT;
}

/* recvfrom:
 * Unix:
 *   assert(buf->write_avail() >= len);
 *   *actual_len = recvfrom(fd,addr,buf->end(),len)
 *   if successful then
 *      buf->fill(*actual_len);
 *	    return ACTION_RESULT_DONE
 *   else if nothing read
 *      *actual_len is 0
 *      create "UDP read continuation" C with 'cont's lock
 *         set user callback to 'cont'
 *      return C's action.
 *   else
 *      return error;
 */
Action *
UDPNetProcessor::recvfrom_re(Continuation *cont, void *token, int fd, struct sockaddr *fromaddr, socklen_t *fromaddrlen,
                             IOBufferBlock *buf, int len, bool useReadCont, int timeout)
{
  (void)useReadCont;
  ink_assert(buf->write_avail() >= len);
  int actual;
  Event *event = completionUtil::create();

  completionUtil::setContinuation(event, cont);
  completionUtil::setHandle(event, token);
  actual = socketManager.recvfrom(fd, buf->end(), len, 0, fromaddr, fromaddrlen);

  if (actual > 0) {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, make_ptr(buf), actual, errno);
    buf->fill(actual);
    cont->handleEvent(NET_EVENT_DATAGRAM_READ_COMPLETE, event);
    completionUtil::destroy(event);
    return ACTION_RESULT_DONE;
  } else if (actual == 0 || (actual < 0 && actual == -EAGAIN)) {
    UDPReadContinuation *c = udpReadContAllocator.alloc();
    c->init_token(event);
    c->init_read(fd, buf, len, fromaddr, fromaddrlen);
    if (timeout) {
      c->set_timer(timeout);
    }
    return event;
  } else {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, make_ptr(buf), actual, errno);
    cont->handleEvent(NET_EVENT_DATAGRAM_READ_ERROR, event);
    completionUtil::destroy(event);
    return ACTION_IO_ERROR;
  }
}

/* sendmsg:
 * Unix:
 *   *actual_len = sendmsg(fd,msg,default-flags);
 *   if successful,
 *      return ACTION_RESULT_DONE
 *   else
 *      return error
 */
Action *
UDPNetProcessor::sendmsg_re(Continuation *cont, void *token, int fd, struct msghdr *msg)
{
  int actual;
  Event *event = completionUtil::create();

  completionUtil::setContinuation(event, cont);
  completionUtil::setHandle(event, token);

  actual = socketManager.sendmsg(fd, msg, 0);
  if (actual >= 0) {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, msg, actual, errno);
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_COMPLETE, event);
    completionUtil::destroy(event);
    return ACTION_RESULT_DONE;
  } else {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, msg, actual, errno);
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_ERROR, event);
    completionUtil::destroy(event);
    return ACTION_IO_ERROR;
  }
}

/* sendto:
 * If this were implemented, it might be implemented like this:
 * Unix:
 *   call sendto(fd,addr,buf->reader()->start(),len);
 *   if successful,
 *      buf->consume(len);
 *      return ACTION_RESULT_DONE
 *   else
 *      return error
 *
 */
Action *
UDPNetProcessor::sendto_re(Continuation *cont, void *token, int fd, struct sockaddr const *toaddr, int toaddrlen,
                           IOBufferBlock *buf, int len)
{
  (void)token;
  ink_assert(buf->read_avail() >= len);
  int nbytes_sent = socketManager.sendto(fd, buf->start(), len, 0, toaddr, toaddrlen);

  if (nbytes_sent >= 0) {
    ink_assert(nbytes_sent == len);
    buf->consume(nbytes_sent);
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_COMPLETE, (void *)-1);
    return ACTION_RESULT_DONE;
  } else {
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_ERROR, (void *)(intptr_t)nbytes_sent);
    return ACTION_IO_ERROR;
  }
}

bool
UDPNetProcessor::CreateUDPSocket(int *resfd, sockaddr const *remote_addr, sockaddr *local_addr, int *local_addr_len,
                                 Action **status, int send_bufsize, int recv_bufsize)
{
  int res = 0, fd = -1;

  ink_assert(ats_ip_are_compatible(remote_addr, local_addr));

  *resfd = -1;
  if ((res = socketManager.socket(remote_addr->sa_family, SOCK_DGRAM, 0)) < 0) {
    goto HardError;
  }
  fd = res;
  if ((res = safe_fcntl(fd, F_SETFL, O_NONBLOCK)) < 0) {
    goto HardError;
  }
  if ((res = socketManager.ink_bind(fd, remote_addr, ats_ip_size(remote_addr), IPPROTO_UDP)) < 0) {
    char buff[INET6_ADDRPORTSTRLEN];
    Debug("udpnet", "ink bind failed on %s", ats_ip_nptop(remote_addr, buff, sizeof(buff)));
    goto SoftError;
  }

  if (recv_bufsize) {
    if (unlikely(socketManager.set_rcvbuf_size(fd, recv_bufsize))) {
      Debug("udpnet", "set_dnsbuf_size(%d) failed", recv_bufsize);
    }
  }
  if (send_bufsize) {
    if (unlikely(socketManager.set_sndbuf_size(fd, send_bufsize))) {
      Debug("udpnet", "set_dnsbuf_size(%d) failed", send_bufsize);
    }
  }
  if ((res = safe_getsockname(fd, local_addr, local_addr_len)) < 0) {
    Debug("udpnet", "CreateUdpsocket: getsockname didnt' work");
    goto HardError;
  }
  *resfd  = fd;
  *status = nullptr;
  Debug("udpnet", "creating a udp socket port = %d, %d---success", ats_ip_port_host_order(remote_addr),
        ats_ip_port_host_order(local_addr));
  return true;
SoftError:
  Debug("udpnet", "creating a udp socket port = %d---soft failure", ats_ip_port_host_order(local_addr));
  if (fd != -1) {
    socketManager.close(fd);
  }
  *resfd  = -1;
  *status = nullptr;
  return false;
HardError:
  Debug("udpnet", "creating a udp socket port = %d---hard failure", ats_ip_port_host_order(local_addr));
  if (fd != -1) {
    socketManager.close(fd);
  }
  *resfd  = -1;
  *status = ACTION_IO_ERROR;
  return false;
}

Action *
UDPNetProcessor::UDPBind(Continuation *cont, sockaddr const *addr, int send_bufsize, int recv_bufsize)
{
  int res              = 0;
  int fd               = -1;
  UnixUDPConnection *n = nullptr;
  IpEndpoint myaddr;
  int myaddr_len = sizeof(myaddr);

  if ((res = socketManager.socket(addr->sa_family, SOCK_DGRAM, 0)) < 0) {
    goto Lerror;
  }
  fd = res;
  if ((res = fcntl(fd, F_SETFL, O_NONBLOCK) < 0)) {
    goto Lerror;
  }

  // If this is a class D address (i.e. multicast address), use REUSEADDR.
  if (ats_is_ip_multicast(addr)) {
    int enable_reuseaddr = 1;

    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&enable_reuseaddr, sizeof(enable_reuseaddr)) < 0)) {
      goto Lerror;
    }
  }

  if ((res = socketManager.ink_bind(fd, addr, ats_ip_size(addr))) < 0) {
    goto Lerror;
  }

  if (recv_bufsize) {
    if (unlikely(socketManager.set_rcvbuf_size(fd, recv_bufsize))) {
      Debug("udpnet", "set_dnsbuf_size(%d) failed", recv_bufsize);
    }
  }
  if (send_bufsize) {
    if (unlikely(socketManager.set_sndbuf_size(fd, send_bufsize))) {
      Debug("udpnet", "set_dnsbuf_size(%d) failed", send_bufsize);
    }
  }
  if ((res = safe_getsockname(fd, &myaddr.sa, &myaddr_len)) < 0) {
    goto Lerror;
  }
  n = new UnixUDPConnection(fd);

  Debug("udpnet", "UDPNetProcessor::UDPBind: %p fd=%d", n, fd);
  n->setBinding(&myaddr.sa);
  n->bindToThread(cont);

  cont->handleEvent(NET_EVENT_DATAGRAM_OPEN, n);
  return ACTION_RESULT_DONE;
Lerror:
  if (fd != NO_FD) {
    socketManager.close(fd);
  }
  cont->handleEvent(NET_EVENT_DATAGRAM_ERROR, nullptr);
  return ACTION_IO_ERROR;
}

// send out all packets that need to be sent out as of time=now
UDPQueue::UDPQueue()
{
}

UDPQueue::~UDPQueue()
{
}

/*
 * Driver function that aggregates packets across cont's and sends them
 */
void
UDPQueue::service(UDPNetHandler *nh)
{
  (void)nh;
  ink_hrtime now     = Thread::get_hrtime_updated();
  uint64_t timeSpent = 0;
  uint64_t pktSendStartTime;
  UDPPacketInternal *p;
  ink_hrtime pktSendTime;

  p = (UDPPacketInternal *)ink_atomiclist_popall(&atomicQueue);
  if (p) {
    UDPPacketInternal *pnext = nullptr;
    Queue<UDPPacketInternal> stk;

    while (p) {
      pnext         = p->alink.next;
      p->alink.next = nullptr;
      stk.push(p);
      p = pnext;
    }

    // walk backwards down list since this is actually an atomic stack.
    while (stk.head) {
      p = stk.pop();
      ink_assert(p->link.prev == nullptr);
      ink_assert(p->link.next == nullptr);
      // insert into our queue.
      Debug("udp-send", "Adding %p", p);
      if (p->conn->lastPktStartTime == 0) {
        pktSendStartTime = MAX(now, p->delivery_time);
      } else {
        pktSendTime      = p->delivery_time;
        pktSendStartTime = MAX(MAX(now, pktSendTime), p->delivery_time);
      }
      p->conn->lastPktStartTime = pktSendStartTime;
      p->delivery_time          = pktSendStartTime;

      pipeInfo.addPacket(p, now);
    }
  }

  pipeInfo.advanceNow(now);
  SendPackets();

  timeSpent = ink_hrtime_to_msec(now - last_report);
  if (timeSpent > 10000) {
    last_report = now;
    added       = 0;
    packets     = 0;
  }
  last_service = now;
}

void
UDPQueue::SendPackets()
{
  UDPPacketInternal *p;
  static ink_hrtime lastCleanupTime = Thread::get_hrtime_updated();
  ink_hrtime now                    = Thread::get_hrtime_updated();
  ink_hrtime send_threshold_time    = now + SLOT_TIME;
  int32_t bytesThisSlot = INT_MAX, bytesUsed = 0;
  int32_t bytesThisPipe, sentOne;
  int64_t pktLen;

  bytesThisSlot = INT_MAX;

sendPackets:
  sentOne       = false;
  bytesThisPipe = (int32_t)bytesThisSlot;

  while ((bytesThisPipe > 0) && (pipeInfo.firstPacket(send_threshold_time))) {
    p      = pipeInfo.getFirstPacket();
    pktLen = p->getPktLength();

    if (p->conn->shouldDestroy()) {
      goto next_pkt;
    }
    if (p->conn->GetSendGenerationNumber() != p->reqGenerationNum) {
      goto next_pkt;
    }

    SendUDPPacket(p, pktLen);
    bytesUsed += pktLen;
    bytesThisPipe -= pktLen;
  next_pkt:
    sentOne = true;
    p->free();

    if (bytesThisPipe < 0) {
      break;
    }
  }

  bytesThisSlot -= bytesUsed;

  if ((bytesThisSlot > 0) && sentOne) {
    // redistribute the slack...
    now = Thread::get_hrtime_updated();
    if (pipeInfo.firstPacket(now) == nullptr) {
      pipeInfo.advanceNow(now);
    }
    goto sendPackets;
  }

  if ((g_udp_periodicFreeCancelledPkts) && (now - lastCleanupTime > ink_hrtime_from_sec(g_udp_periodicFreeCancelledPkts))) {
    pipeInfo.FreeCancelledPackets(g_udp_periodicCleanupSlots);
    lastCleanupTime = now;
  }
}

void
UDPQueue::SendUDPPacket(UDPPacketInternal *p, int32_t /* pktLen ATS_UNUSED */)
{
  struct msghdr msg;
  struct iovec iov[32];
  int real_len = 0;
  int n, count, iov_len = 0;

  p->conn->lastSentPktStartTime = p->delivery_time;
  Debug("udp-send", "Sending %p", p);

#if !defined(solaris)
  msg.msg_control    = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags      = 0;
#endif
  msg.msg_name    = (caddr_t)&p->to;
  msg.msg_namelen = sizeof(p->to);
  iov_len         = 0;

  for (IOBufferBlock *b = p->chain.get(); b != nullptr; b = b->next.get()) {
    iov[iov_len].iov_base = (caddr_t)b->start();
    iov[iov_len].iov_len  = b->size();
    real_len += iov[iov_len].iov_len;
    iov_len++;
  }
  msg.msg_iov    = iov;
  msg.msg_iovlen = iov_len;

  count = 0;
  while (true) {
    // stupid Linux problem: sendmsg can return EAGAIN
    n = ::sendmsg(p->conn->getFd(), &msg, 0);
    if ((n >= 0) || ((n < 0) && (errno != EAGAIN))) {
      // send succeeded or some random error happened.
      break;
    }
    if (errno == EAGAIN) {
      ++count;
      if ((g_udp_numSendRetries > 0) && (count >= g_udp_numSendRetries)) {
        // tried too many times; give up
        Debug("udpnet", "Send failed: too many retries");
        break;
      }
    }
  }
}

void
UDPQueue::send(UDPPacket *p)
{
  // XXX: maybe fastpath for immediate send?
  ink_atomiclist_push(&atomicQueue, p);
}

#undef LINK

UDPNetHandler::UDPNetHandler()
{
  mutex = new_ProxyMutex();
  ink_atomiclist_init(&udpOutQueue.atomicQueue, "Outgoing UDP Packet queue", offsetof(UDPPacketInternal, alink.next));
  ink_atomiclist_init(&udpNewConnections, "UDP Connection queue", offsetof(UnixUDPConnection, newconn_alink.next));
  nextCheck = Thread::get_hrtime_updated() + HRTIME_MSECONDS(1000);
  lastCheck = 0;
  SET_HANDLER((UDPNetContHandler)&UDPNetHandler::startNetEvent);
}

int
UDPNetHandler::startNetEvent(int event, Event *e)
{
  (void)event;
  SET_HANDLER((UDPNetContHandler)&UDPNetHandler::mainNetEvent);
  trigger_event = e;
  e->schedule_every(-HRTIME_MSECONDS(9));
  return EVENT_CONT;
}

int
UDPNetHandler::mainNetEvent(int event, Event *e)
{
  ink_assert(trigger_event == e && event == EVENT_POLL);
  (void)event;
  (void)e;

  PollCont *pc = get_UDPPollCont(e->ethread);

  // handle UDP outgoing engine
  udpOutQueue.service(this);

  // handle UDP read operations
  UnixUDPConnection *uc, *next;
  int i;
  int nread = 0;

  EventIO *temp_eptr = nullptr;
  for (i = 0; i < pc->pollDescriptor->result; i++) {
    temp_eptr = (EventIO *)get_ev_data(pc->pollDescriptor, i);
    if ((get_ev_events(pc->pollDescriptor, i) & EVENTIO_READ) && temp_eptr->type == EVENTIO_UDP_CONNECTION) {
      uc = temp_eptr->data.uc;
      ink_assert(uc && uc->mutex && uc->continuation);
      ink_assert(uc->refcount >= 1);
      if (uc->shouldDestroy()) {
        // udp_polling->remove(uc,uc->polling_link);
        uc->Release();
      } else {
        udpNetInternal.udp_read_from_net(this, uc);
        nread++;
      }
    } // if EPOLLIN
  }   // end for

  // remove dead UDP connections
  ink_hrtime now = Thread::get_hrtime_updated();
  if (now >= nextCheck) {
    for (uc = udp_polling.head; uc; uc = next) {
      ink_assert(uc->mutex && uc->continuation);
      ink_assert(uc->refcount >= 1);
      next = uc->polling_link.next;
      if (uc->shouldDestroy()) {
        // changed by YTS Team, yamsat
        // udp_polling->remove(uc,uc->polling_link);
        uc->Release();
      }
    }
    nextCheck = Thread::get_hrtime_updated() + HRTIME_MSECONDS(1000);
  }
  // service UDPConnections with data ready for callback.
  Que(UnixUDPConnection, callback_link) q = udp_callbacks;
  udp_callbacks.clear();
  while ((uc = q.dequeue())) {
    ink_assert(uc->mutex && uc->continuation);
    if (udpNetInternal.udp_callback(this, uc, trigger_event->ethread)) { // not successful
      // schedule on a thread of its own.
      ink_assert(uc->callback_link.next == nullptr);
      ink_assert(uc->callback_link.prev == nullptr);
      udp_callbacks.enqueue(uc);
    } else {
      ink_assert(uc->callback_link.next == nullptr);
      ink_assert(uc->callback_link.prev == nullptr);
      uc->onCallbackQueue = 0;
      uc->Release();
    }
  }

  return EVENT_CONT;
}
