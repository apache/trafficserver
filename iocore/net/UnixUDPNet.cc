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

typedef int (UDPNetHandler::*UDPNetContHandler) (int, void *);

inkcoreapi ClassAllocator<UDPPacketInternal> udpPacketAllocator("udpPacketAllocator");

inkcoreapi ClassAllocator<UDPWorkContinuation> udpWorkContinuationAllocator("udpWorkContinuationAllocator");

EventType ET_UDP;

#if (HOST_OS == linux) && !defined(DEBUG)
#define NODIAGS
#endif

//
// Global Data
//

UDPNetProcessorInternal udpNetInternal;
UDPNetProcessor & udpNet = udpNetInternal;

inku64 g_udp_bytesPending;
ink32 g_udp_periodicCleanupSlots;
ink32 g_udp_periodicFreeCancelledPkts;
ink32 g_udp_numSendRetries;

#include "P_LibBulkIO.h"
void *G_bulkIOState = NULL;

//
// Public functions
// See header for documentation
//

InkPipeInfo G_inkPipeInfo;

int G_bwGrapherFd;
struct sockaddr_in G_bwGrapherLoc;

void
initialize_thread_for_udp_net(EThread * thread)
{
  new((ink_dummy_for_new *) get_UDPPollCont(thread)) PollCont(thread->mutex);
  new((ink_dummy_for_new *) get_UDPNetHandler(thread)) UDPNetHandler;

  // These are hidden variables that control the amount of memory used by UDP
  // packets.  As usual, defaults are in RecordsConfig.cc

  // This variable controls how often we cleanup the cancelled packets.
  // If it is set to 0, then cleanup never occurs.
  REC_ReadConfigInt32(g_udp_periodicFreeCancelledPkts, "proxy.config.udp.free_cancelled_pkts_sec");
  // This variable controls how many "slots" of the udp calendar queue we cleanup.
  // If it is set to 0, then cleanup never occurs.  This value makes sense
  // only if the above variable is set.
  REC_ReadConfigInt32(g_udp_periodicFreeCancelledPkts, "proxy.config.udp.periodic_cleanup");

  // UDP sends can fail with errno=EAGAIN.  This variable determines the # of
  // times the UDP thread retries before giving up.  Set to 0 to keep trying forever.
  REC_ReadConfigInt32(g_udp_numSendRetries, "proxy.config.udp.send_retries");
  g_udp_numSendRetries = g_udp_numSendRetries < 0 ? 0 : g_udp_numSendRetries;

  thread->schedule_every(get_UDPPollCont(thread), -9);
  thread->schedule_imm(get_UDPNetHandler(thread));
  Debug("bulk-io", "%s bulk-io for sends", G_bulkIOState ? "Using" : "Not using");
}

int
UDPNetProcessorInternal::start(int n_upd_threads)
{
  if (n_upd_threads < 1)
    return -1;

  ET_UDP = eventProcessor.spawn_event_threads(n_upd_threads);
  if (ET_UDP < 0)               // Probably can't happen, maybe at some point EventType should be unsigned ?
    return -1;

  pollCont_offset = eventProcessor.allocate(sizeof(PollCont));
  udpNetHandler_offset = eventProcessor.allocate(sizeof(NetHandler));

  for (int i = 0; i < eventProcessor.n_threads_for_type[ET_UDP]; i++)
    initialize_thread_for_udp_net(eventProcessor.eventthread[ET_UDP][i]);

#if 0
  unsigned long hoodIpaddr;
  unsigned char *ip;
  struct sockaddr_in addr;
  Action *status;

  ip = (unsigned char *) &hoodIpaddr;
  ip[0] = 216;
  ip[1] = 155;
  ip[2] = 202;
  ip[3] = 240;
  G_bwGrapherLoc.sin_family = AF_INET;
  G_bwGrapherLoc.sin_addr.s_addr = hoodIpaddr;
  G_bwGrapherLoc.sin_port = 7777;
  CreateUDPSocket(&G_bwGrapherFd, &addr, &status, 0, 0, 65536, 65536);
#endif

  return 0;
}

void
UDPNetProcessorInternal::udp_read_from_net(UDPNetHandler * nh,
                                           UDPConnection * xuc, PollDescriptor * pd, EThread * thread)
{
  (void) thread;
  UnixUDPConnection *uc = (UnixUDPConnection *) xuc;

//epoll changes 
/*
  int i = uc->getPollvecIndex();
  if (pd && i >= 0) {
    Pollfd * pfd = &pd->pfd[i];
    uc->clearPollvecIndex();
    if (!(pfd->revents & POLLIN)) { // not ready for read
      return;
    }
  } else {
    uc->clearPollvecIndex();
    return;
  }
 */
//epoll changes ends here
  // receive packet and queue onto UDPConnection.
  // don't call back connection at this time.
  int r;
  int iters = 0;
  do {
    struct sockaddr_in fromaddr;
    socklen_t fromlen = sizeof(fromaddr);
    // XXX: want to be 0 copy.
    // XXX: really should read into next contiguous region of an IOBufferData
    // which gets referenced by IOBufferBlock.
    char buf[65536];
    int buflen = sizeof(buf);
    r = socketManager.recvfrom(uc->getFd(), buf, buflen, 0, (struct sockaddr *) &fromaddr, &fromlen);
    if (r <= 0) {
      // error
      break;
    }
    // create packet
    UDPPacket *p = new_incoming_UDPPacket(&fromaddr, buf, r);
    p->setConnection(uc);
    // XXX: is this expensive?  I] really want to know this information
    p->setArrivalTime(ink_get_hrtime_internal());
    // queue onto the UDPConnection
    ink_atomiclist_push(&uc->inQueue, p);
    iters++;
  } while (r > 0);
  if (iters >= 1) {
    Debug("udp-read", "read %d at a time", iters);
  }
  // if not already on to-be-called-back queue, then add it.
  if (!uc->onCallbackQueue) {
    ink_assert(uc->callback_link.next == NULL);
    ink_assert(uc->callback_link.prev == NULL);
    uc->AddRef();
    nh->udp_callbacks->enqueue(uc, uc->callback_link);
    uc->onCallbackQueue = 1;
  }
}


int
UDPNetProcessorInternal::udp_callback(UDPNetHandler * nh, UDPConnection * xuc, EThread * thread)
{
  (void) nh;
  UnixUDPConnection *uc = (UnixUDPConnection *) xuc;

  if (uc->continuation && uc->mutex) {
    MUTEX_TRY_LOCK_FOR(lock, uc->mutex, thread, uc->continuation);
    if (!lock) {
      return 1;
    }
    uc->AddRef();
    uc->callbackHandler(0, 0);
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

// cheesy implementation of a asynchronous read and callback for Unix
class UDPReadContinuation:public Continuation
{
public:
  UDPReadContinuation(Event * completionToken);
  UDPReadContinuation();
  ~UDPReadContinuation();
  inline void free(void);
  inline void init_token(Event * completionToken);

  inline void init_read(int fd, IOBufferBlock * buf, int len, struct sockaddr *fromaddr, socklen_t *fromaddrlen);     // start up polling
  void set_timer(int seconds)
  {
    timeout_interval = HRTIME_SECONDS(seconds);
  }
  void cancel();
  int readPollEvent(int event, Event * e);
  Action *getAction()
  {
    return event;
  }
  void setupPollDescriptor();
private:

  Event * event;                // the completion event token created
  // on behalf of the client
  Ptr<IOBufferBlock> readbuf;
  int readlen;
  struct sockaddr *fromaddr;
  socklen_t *fromaddrlen;
  int fd;                       // fd we are reading from
  int ifd;                      // poll fd index

  int ifd_seq_num;              // some assertable information

  ink_hrtime period;            // polling period
  ink_hrtime elapsed_time;
  ink_hrtime timeout_interval;

#ifdef DEBUG_UDP
  DynArray<ink_hrtime> *eventStamp;
  DynArray<int>*eventType;
  int nevents;
#endif
  // some i/o information
};

#ifdef DEBUG_UDP
static ink_hrtime default_hrtime = NULL;
static int default_event = 0;
#endif

ClassAllocator<UDPReadContinuation> udpReadContAllocator("udpReadContAllocator");

UDPReadContinuation::UDPReadContinuation(Event * completionToken)
:Continuation(NULL),
event(completionToken),
readbuf(NULL),
readlen(0), fromaddrlen(0), fd(-1), ifd(-1), ifd_seq_num(-1), period(0), elapsed_time(0), timeout_interval(0)
#ifdef DEBUG_UDP
  , nevents(0)
#endif
{
#ifdef DEBUG_UDP
  eventStamp = NEW(new DynArray<ink_hrtime> (&default_hrtime));
  eventType = NEW(new DynArray<int>(&default_event));
#endif

  if (completionToken->continuation) {
    this->mutex = completionToken->continuation->mutex;
  } else {
    this->mutex = new_ProxyMutex();
  }
}

UDPReadContinuation::UDPReadContinuation()
:Continuation(NULL),
event(0),
readbuf(NULL),
readlen(0), fromaddrlen(0), fd(-1), ifd(-1), ifd_seq_num(-1), period(0), elapsed_time(0), timeout_interval(0)
#ifdef DEBUG_UDP
  , nevents(0)
#endif
{
#ifdef DEBUG_UDP
  eventStamp = NEW(new DynArray<ink_hrtime> (&default_hrtime));
  eventType = NEW(new DynArray<int>(&default_event));
#endif

}

inline void
UDPReadContinuation::free(void)
{
  ink_assert(event != NULL);
  completionUtil::destroy(event);
  event = NULL;
  readbuf = NULL;
  readlen = 0;
  fromaddrlen = 0;
  fd = -1;
  ifd = -1;
  ifd_seq_num = 0;
  period = 0;
  elapsed_time = 0;
  timeout_interval = 0;
  mutex = NULL;
#ifdef DEBUG_UDP
  nevents = 0;
#endif
  udpReadContAllocator.free(this);
}

inline void
UDPReadContinuation::init_token(Event * completionToken)
{
  if (completionToken->continuation) {
    this->mutex = completionToken->continuation->mutex;
  } else {
    this->mutex = new_ProxyMutex();
  }
  event = completionToken;
}

inline void
UDPReadContinuation::init_read(int rfd, IOBufferBlock * buf, int len, struct sockaddr *fromaddr_, socklen_t *fromaddrlen_)
{
#ifdef DEBUG_UDP
  (*eventStamp) (nevents) = ink_get_hrtime();
  (*eventType) (nevents) = 0;
  nevents++;
#endif
  ink_assert(rfd >= 0 && buf != NULL && fromaddr_ != NULL && fromaddrlen_ != NULL);
  fd = rfd;
  readbuf = buf;
  readlen = len;
  fromaddr = fromaddr_;
  fromaddrlen = fromaddrlen_;
  SET_HANDLER(&UDPReadContinuation::readPollEvent);
  period = NET_PERIOD;
  setupPollDescriptor();
  this_ethread()->schedule_every(this, period);
}

UDPReadContinuation::~UDPReadContinuation()
{
  ink_assert(event != NULL);
  completionUtil::destroy(event);
  event = NULL;
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
  Pollfd *pfd;
  EThread *et = (EThread *) this_thread();
  PollCont *pc = get_PollCont(et);
#ifdef BSD_TCP
  pfd = pc->nextPollDescriptor->alloc(fd);
  ifd = pfd - pc->nextPollDescriptor->pfd;
  if (ifd >= POLL_DESCRIPTOR_SIZE) {
    ink_assert(pc->nextPollDescriptor->nfds_bsd > (ifd - POLL_DESCRIPTOR_SIZE));
  } else {
    ink_assert(pc->nextPollDescriptor->nfds_sol > ifd);
  }
#else
  pfd = pc->nextPollDescriptor->alloc();
  pfd->fd = fd;
  ifd = pfd - pc->nextPollDescriptor->pfd;
  ink_assert(pc->nextPollDescriptor->nfds > ifd);
#endif
  ifd_seq_num = pc->nextPollDescriptor->seq_num;
  pfd->events = POLLIN;
  pfd->revents = 0;
}

int
UDPReadContinuation::readPollEvent(int event_, Event * e)
{
  (void) event_;
  (void) e;
  int res;

  PollCont *pc = get_PollCont(e->ethread);
  Continuation *c;

#ifdef DEBUG_UDP
  (*eventStamp) (nevents) = ink_get_hrtime();
  (*eventType) (nevents) = event_;
  nevents++;
#endif

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
      res = c->handleEvent(NET_EVENT_DATAGRAM_READ_ERROR, event);
      e->cancel();
      free();
      //      delete this;
      return EVENT_DONE;
    }
  }
#ifdef BSD_TCP
  ink_assert(ifd < 0 || event_ == EVENT_INTERVAL ||
             (event_ == EVENT_POLL &&
              ifd_seq_num == pc->pollDescriptor->seq_num &&
              (ifd >= POLL_DESCRIPTOR_SIZE ?
               (pc->pollDescriptor->nfds_bsd >
                (ifd - POLL_DESCRIPTOR_SIZE)) :
               (pc->pollDescriptor->nfds_sol > ifd)) && pc->pollDescriptor->pfd[ifd].fd == fd));
#else
  ink_assert(ifd < 0 || event_ == EVENT_INTERVAL ||
             (event_ == EVENT_POLL &&
              ifd_seq_num == pc->pollDescriptor->seq_num &&
              pc->pollDescriptor->nfds > ifd && pc->pollDescriptor->pfd[ifd].fd == fd));
#endif
  if (ifd < 0 || event_ == EVENT_INTERVAL || (pc->pollDescriptor->pfd[ifd].revents & POLLIN)) {
    c = completionUtil::getContinuation(event);
    // do read
    socklen_t tmp_fromlen = *fromaddrlen;
    int rlen = socketManager.recvfrom(fd, readbuf->end(), readlen,
                                      0,        // default flags
                                      fromaddr, &tmp_fromlen);
    completionUtil::setThread(event, e->ethread);
    // call back user with their event
    if (rlen > 0) {
      // do callback if read is successful
      *fromaddrlen = tmp_fromlen;
      completionUtil::setInfo(event, fd, readbuf, rlen, errno);
      readbuf->fill(rlen);
#ifdef DEBUG_UDP
      (*eventStamp) (nevents) = ink_get_hrtime();
      (*eventType) (nevents) = NET_EVENT_DATAGRAM_READ_COMPLETE;
      nevents++;
#endif
      res = c->handleEvent(NET_EVENT_DATAGRAM_READ_COMPLETE, event);
      e->cancel();
      free();
      // delete this;
      return EVENT_DONE;
    } else if (rlen < 0 && rlen != -EAGAIN) {
      // signal error.
      *fromaddrlen = tmp_fromlen;
      completionUtil::setInfo(event, fd, (IOBufferBlock *) readbuf, rlen, errno);
      c = completionUtil::getContinuation(event);
#ifdef DEBUG_UDP
      (*eventStamp) (nevents) = ink_get_hrtime();
      (*eventType) (nevents) = NET_EVENT_DATAGRAM_READ_ERROR;
      nevents++;
#endif
      res = c->handleEvent(NET_EVENT_DATAGRAM_READ_ERROR, event);
      e->cancel();
      free();
      //delete this;
      return EVENT_DONE;
    } else {
      completionUtil::setThread(event, NULL);
    }
  }
  if (event->cancelled) {
    e->cancel();
    free();
    //delete this;
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
UDPNetProcessor::recvfrom_re(Continuation * cont,
                             void *token,
                             int fd,
                             struct sockaddr * fromaddr, socklen_t *fromaddrlen,
                             IOBufferBlock * buf, int len, bool useReadCont, int timeout)
{
  (void) useReadCont;
  ink_assert(buf->write_avail() >= len);
  int actual;
  Event *event = completionUtil::create();
  completionUtil::setContinuation(event, cont);
  completionUtil::setHandle(event, token);
  actual = socketManager.recvfrom(fd, buf->end(), len, 0,       // default flags
                                  fromaddr, fromaddrlen);
  if (actual > 0) {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, buf, actual, errno);
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
    completionUtil::setInfo(event, fd, buf, actual, errno);
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
UDPNetProcessor::sendmsg_re(Continuation * cont, void *token, int fd, struct msghdr * msg)
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
UDPNetProcessor::sendto_re(Continuation * cont,
                           void *token, int fd, struct sockaddr * toaddr, int toaddrlen, IOBufferBlock * buf, int len)
{
  (void) token;
  ink_assert(buf->read_avail() >= len);
  int nbytes_sent = socketManager.sendto(fd, buf->start(), len, 0,
                                         toaddr, toaddrlen);
  if (nbytes_sent >= 0) {
    ink_assert(nbytes_sent == len);
    buf->consume(nbytes_sent);
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_COMPLETE, (void *) -1);
    return ACTION_RESULT_DONE;
  } else {
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_ERROR, (void *) nbytes_sent);
    return ACTION_IO_ERROR;
  }
}

Action *
UDPNetProcessor::UDPCreatePortPairs(Continuation * cont,
                                    int nPairs,
                                    unsigned int myIP, unsigned int destIP, int send_bufsize, int recv_bufsize)
{

  UDPWorkContinuation *worker = udpWorkContinuationAllocator.alloc();
  // UDPWorkContinuation *worker = NEW(new UDPWorkContinuation);

  worker->init(cont, nPairs, myIP, destIP, send_bufsize, recv_bufsize);
  eventProcessor.schedule_imm(worker, ET_UDP);
  return &(worker->m_action);
}


bool
UDPNetProcessor::CreateUDPSocket(int *resfd,
                                 struct sockaddr_in * addr,
                                 Action ** status, int my_port, unsigned int my_ip, int send_bufsize, int recv_bufsize)
{
  int res = 0, fd = -1;
  struct sockaddr_in bind_sa;
  struct sockaddr_in myaddr;
  int myaddr_len = sizeof(myaddr);

  *resfd = -1;
  if ((res = socketManager.socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    goto HardError;
  fd = res;
  if ((res = safe_fcntl(fd, F_SETFL, O_NONBLOCK)) < 0)
    goto HardError;
  memset(&bind_sa, 0, sizeof(bind_sa));
  bind_sa.sin_family = AF_INET;
  bind_sa.sin_port = htons(my_port);
  bind_sa.sin_addr.s_addr = my_ip;
  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &bind_sa, sizeof(bind_sa), IPPROTO_UDP)) < 0) {
    unsigned char *pt = (unsigned char *) &my_ip;
    Debug("udpnet", "ink bind failed --- my_ip = %d.%d.%d.%d", pt[0], pt[1], pt[2], pt[3]);
    goto SoftError;
  }

  if (recv_bufsize) {
    if (unlikely(socketManager.set_rcvbuf_size(fd, recv_bufsize)))
      Debug("udpnet", "set_dnsbuf_size(%d) failed", recv_bufsize);
  }
  if (send_bufsize) {
    if (unlikely(socketManager.set_sndbuf_size(fd, send_bufsize)))
      Debug("udpnet", "set_dnsbuf_size(%d) failed", send_bufsize);
  }
  if ((res = safe_getsockname(fd, (struct sockaddr *) &myaddr, &myaddr_len)) < 0) {
    Debug("udpnet", "CreateUdpsocket: getsockname didnt' work");
    goto HardError;
  }
  if (!myaddr.sin_addr.s_addr) {
    // set to default IP address for machine
    /** netfixme ... this_machine() is in proxy.
    if (this_machine()) {
      myaddr.sin_addr.s_addr = this_machine()->ip;
    } else {
      Debug("udpnet","CreateUdpSocket -- machine not initialized");
    }
    */
  }
  *resfd = fd;
  memcpy(addr, &myaddr, myaddr_len);
  *status = NULL;
  Debug("udpnet", "creating a udp socket port = %d, %d---success", my_port, addr->sin_port);
  return true;
SoftError:
  Debug("udpnet", "creating a udp socket port = %d---soft failure", my_port);
  if (fd != -1)
    socketManager.close(fd, keSocket);
  *resfd = -1;
  *status = NULL;
  return false;
HardError:
  Debug("udpnet", "creating a udp socket port = %d---hard failure", my_port);
  if (fd != -1)
    socketManager.close(fd, keSocket);
  *resfd = -1;
  *status = ACTION_IO_ERROR;
  return false;
}

void
UDPNetProcessor::UDPClassifyConnection(Continuation * udpConn, int destIP)
{
  int i;
  UDPConnectionInternal *p = (UDPConnectionInternal *) udpConn;

  if (G_inkPipeInfo.m_numPipes == 0) {
    p->m_pipe_class = 0;
    return;
  }
  p->m_pipe_class = -1;
  // find a match: 0 is best-effort
  for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++)
    if (G_inkPipeInfo.m_perPipeInfo[i].m_destIP == destIP)
      p->m_pipe_class = i;
  // no match; set it to the destIP=0 class
  if (p->m_pipe_class == -1) {
    for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++)
      if (G_inkPipeInfo.m_perPipeInfo[i].m_destIP == 0) {
        p->m_pipe_class = i;
        break;
      }
  }
  Debug("udpnet-pipe", "Pipe class = %d", p->m_pipe_class);
  ink_debug_assert(p->m_pipe_class != -1);
  if (p->m_pipe_class == -1)
    p->m_pipe_class = 0;
  G_inkPipeInfo.m_perPipeInfo[p->m_pipe_class].m_count++;
}

Action *
UDPNetProcessor::UDPBind(Continuation * cont, int my_port, int my_ip, int send_bufsize, int recv_bufsize)
{
  int res = 0;
  int fd = -1;
  UnixUDPConnection *n = NULL;
  struct sockaddr_in bind_sa;
  struct sockaddr_in myaddr;
  int myaddr_len = sizeof(myaddr);

  if ((res = socketManager.socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    goto Lerror;
  fd = res;
  if ((res = fcntl(fd, F_SETFL, O_NONBLOCK) < 0))
    goto Lerror;

  // If this is a class D address (i.e. multicast address), use REUSEADDR.
  if ((((unsigned int) (ntohl(my_ip))) & 0xf0000000) == 0xe0000000) {
    int enable_reuseaddr = 1;

    if ((res = safe_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &enable_reuseaddr, sizeof(enable_reuseaddr)) < 0)) {
      goto Lerror;
    }
  }

  memset(&bind_sa, 0, sizeof(bind_sa));
  bind_sa.sin_family = AF_INET;
  bind_sa.sin_port = htons(my_port);
  bind_sa.sin_addr.s_addr = my_ip;
  if ((res = socketManager.ink_bind(fd, (struct sockaddr *) &bind_sa, sizeof(bind_sa))) < 0) {
    goto Lerror;
  }

  if (recv_bufsize) {
    if (unlikely(socketManager.set_rcvbuf_size(fd, recv_bufsize)))
      Debug("udpnet", "set_dnsbuf_size(%d) failed", recv_bufsize);
  }
  if (send_bufsize) {
    if (unlikely(socketManager.set_sndbuf_size(fd, send_bufsize)))
      Debug("udpnet", "set_dnsbuf_size(%d) failed", send_bufsize);
  }
  if ((res = safe_getsockname(fd, (struct sockaddr *) &myaddr, &myaddr_len)) < 0) {
    goto Lerror;
  }
  if (!myaddr.sin_addr.s_addr) {
    // set to default IP address for machine
    /** netfixme this_machine is in proxy/
    if (this_machine()) {
      myaddr.sin_addr.s_addr = this_machine()->ip;
    } else {
      Debug("udpnet","UDPNetProcessor::UDPBind -- machine not initialized");
    }
    */
  }
  n = NEW(new UnixUDPConnection(fd));

  Debug("udpnet", "UDPNetProcessor::UDPBind: %x fd=%d", n, fd);
  n->setBinding(&myaddr);
  n->bindToThread(cont);

  cont->handleEvent(NET_EVENT_DATAGRAM_OPEN, n);
  return ACTION_RESULT_DONE;
Lerror:
  if (fd != NO_FD)
    socketManager.close(fd, keSocket);
  cont->handleEvent(NET_EVENT_DATAGRAM_ERROR, NULL);
  return ACTION_IO_ERROR;
}

bool
UDPNetProcessor::AllocBandwidth(Continuation * udpConn, double desiredMbps)
{
  UDPConnectionInternal *udpIntConn = (UDPConnectionInternal *) udpConn;
  ink64 desiredbps = (ink64) (desiredMbps * 1024.0 * 1024.0);

  if (G_inkPipeInfo.m_numPipes == 0) {
    udpIntConn->m_flowRateBps = (desiredMbps * 1024.0 * 1024.0) / 8.0;
    return true;
  }

  if ((udpIntConn->m_pipe_class == 0) ||
      (G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc + desiredbps >
       G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwLimit)) {
    Debug("udpnet-admit", "Denying flow with %lf Mbps", desiredMbps);
    return false;
  }
  udpIntConn->m_flowRateBps = (desiredMbps * 1024.0 * 1024.0) / 8.0;
  udpIntConn->m_allocedbps = desiredbps;
  ink_atomic_increment64(&G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc, desiredbps);
  Debug("udpnet-admit", "Admitting flow with %lf Mbps (a=%lld, lim=%lld)",
        desiredMbps,
        G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc,
        G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwLimit);
  return true;
}

bool
UDPNetProcessor::ChangeBandwidth(Continuation * udpConn, double desiredMbps)
{
  UDPConnectionInternal *udpIntConn = (UDPConnectionInternal *) udpConn;
  ink64 desiredbps = (ink64) (desiredMbps * 1024.0 * 1024.0);
  ink64 oldbps = (ink64) (udpIntConn->m_flowRateBps * 8.0);

  if (G_inkPipeInfo.m_numPipes == 0) {
    udpIntConn->m_flowRateBps = (desiredMbps * 1024.0 * 1024.0) / 8.0;
    return true;
  }
  // arithmetic here is in bits-per-sec.
  if ((udpIntConn->m_pipe_class == 0) ||
      (G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc +
       desiredbps - oldbps) > G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwLimit) {
    Debug("udpnet-admit", "Unable to change b/w for flow to %lf Mbps", desiredMbps);
    return false;
  }
  udpIntConn->m_flowRateBps = (desiredMbps * 1024.0 * 1024.0) / 8.0;
  udpIntConn->m_allocedbps = desiredbps;
  ink_atomic_increment64(&G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc, desiredbps - oldbps);
  Debug("udpnet-admit", "Changing flow's b/w from %lf Mbps to %lf Mbps (a=%lld, lim=%lld)",
        (double) oldbps / (1024.0 * 1024.0),
        desiredMbps,
        G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc,
        G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwLimit);
  return true;
}

void
UDPNetProcessor::FreeBandwidth(Continuation * udpConn)
{
  UDPConnectionInternal *udpIntConn = (UDPConnectionInternal *) udpConn;
  ink64 bps;

  if (G_inkPipeInfo.m_numPipes == 0)
    return;

  Debug("udpnet-free", "Trying to releasing %lf (%lld) Kbps", udpIntConn->m_flowRateBps, udpIntConn->m_allocedbps);

  bps = udpIntConn->m_allocedbps;
  if (bps <= 0)
    return;

  ink_atomic_increment64(&G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc, -bps);

  Debug("udpnet-free", "Releasing %lf Kbps", bps / 1024.0);

  if (G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc < 0)
    G_inkPipeInfo.m_perPipeInfo[udpIntConn->m_pipe_class].m_bwAlloc = 0;

  udpIntConn->m_flowRateBps = 0.0;
  udpIntConn->m_allocedbps = 0;
}

double
UDPNetProcessor::GetAvailableBandwidth()
{
  int i;
  double usedBw = 0.0;

  if (G_inkPipeInfo.m_numPipes == 0)
    // return 100Mbps if there are no pipes
    return 100.0;

  for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
    usedBw += G_inkPipeInfo.m_perPipeInfo[i].m_bwUsed;
  }
  return G_inkPipeInfo.m_interfaceMbps - usedBw;
}

// send out all packets that need to be sent out as of time=now
UDPQueue::UDPQueue(InkAtomicList * pQueue)
:m_atomicQueue(pQueue)
  , m_last_report(0)
  , m_last_service(0)
  , m_last_byteperiod(0)
  , m_bytesSent(0)
  , m_packets(0)
  , m_added(0)
{

}

UDPQueue::~UDPQueue()
{
  UDPPacketInternal *p;

  while ((p = m_reliabilityPktQueue.dequeue()) != NULL) {
    p->free();
  }
}

/*
 * Driver function that aggregates packets across cont's and sends them
 */
void
UDPQueue::service(UDPNetHandler * nh)
{
  ink_hrtime now = ink_get_hrtime_internal();
  inku64 timeSpent = 0;
  UDPPacketInternal *p;
  ink_hrtime pktSendTime;
  double minPktSpacing;
  inku32 pktSize;
  ink32 pktLen;
  int i;
  bool addToGuaranteedQ;
  (void) nh;
  static ink_hrtime lastPrintTime = ink_get_hrtime_internal();
  static ink_hrtime lastSchedTime = ink_get_hrtime_internal();
  static inku32 schedJitter = 0;
  static inku32 numTimesSched = 0;

  schedJitter += ink_hrtime_to_msec(now - lastSchedTime);
  numTimesSched++;

  p = (UDPPacketInternal *) ink_atomiclist_popall(m_atomicQueue);
  if (p) {

    UDPPacketInternal *pnext = NULL;
    Queue<UDPPacketInternal> stk;

    while (p) {
      pnext = p->alink.next;
      p->alink.next = NULL;
      stk.push(p);
      p = pnext;
    }
    // walk backwards down list since this is actually an atomic stack.
    while (stk.head) {
      p = stk.pop();
      ink_assert(p->link.prev == NULL);
      ink_assert(p->link.next == NULL);
      if (p->m_isReliabilityPkt) {
        m_reliabilityPktQueue.enqueue(p);
        continue;
      }
      // insert into our queue.
      Debug("udp-send", "Adding 0x%x", p);
      addToGuaranteedQ = ((p->m_conn->m_pipe_class > 0) && (p->m_conn->m_flowRateBps > 10.0));
      pktLen = p->getPktLength();
      if (p->m_conn->m_lastPktStartTime == 0) {
        p->m_pktSendStartTime = MAX(now, p->m_delivery_time);
      } else {
        pktSize = MAX(INK_ETHERNET_MTU_SIZE, pktLen);
        if (addToGuaranteedQ) {
          // NOTE: this is flow rate in Bytes per sec.; convert to milli-sec.
          minPktSpacing = 1000.0 / (p->m_conn->m_flowRateBps / p->m_conn->m_avgPktSize);

          pktSendTime = p->m_conn->m_lastPktStartTime + ink_hrtime_from_msec((inku32) minPktSpacing);
        } else {
          minPktSpacing = 0.0;
          pktSendTime = p->m_delivery_time;
        }
        p->m_pktSendStartTime = MAX(MAX(now, pktSendTime), p->m_delivery_time);
        if (p->m_conn->m_flowRateBps > 25600.0)
          Debug("udpnet-pkt", "Pkt size = %.1lf now = %lld, send = %lld, del = %lld, Delay delta = %lld; delta = %lld",
                p->m_conn->m_avgPktSize,
                now, pktSendTime, p->m_delivery_time,
                ink_hrtime_to_msec(p->m_pktSendStartTime - now),
                ink_hrtime_to_msec(p->m_pktSendStartTime - p->m_conn->m_lastPktStartTime));

        p->m_conn->m_avgPktSize = ((4.0 * p->m_conn->m_avgPktSize) / 5.0) + (pktSize / 5.0);
      }
      p->m_conn->m_lastPktStartTime = p->m_pktSendStartTime;
      p->m_delivery_time = p->m_pktSendStartTime;
      p->m_conn->m_nBytesTodo += pktLen;

      g_udp_bytesPending += pktLen;

      if (addToGuaranteedQ)
        G_inkPipeInfo.m_perPipeInfo[p->m_conn->m_pipe_class].m_queue->addPacket(p, now);
      else {
        // stick in the best-effort queue: either it was a best-effort flow or
        // the thingy wasn't alloc'ed bandwidth
        G_inkPipeInfo.m_perPipeInfo[0].m_queue->addPacket(p, now);
      }
    }
  }

  if ((now - lastPrintTime) > ink_hrtime_from_sec(30)) {
    Debug("udp-pending-packets", "udp bytes pending: %lld", g_udp_bytesPending);
    Debug("udp-sched-jitter", "avg. udp sched jitter: %f", (double) schedJitter / numTimesSched);
    schedJitter = 0;
    numTimesSched = 0;
    lastPrintTime = now;
  }

  for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++)
    G_inkPipeInfo.m_perPipeInfo[i].m_queue->advanceNow(now);

  if (G_bulkIOState) {
    BulkIOSend();
  } else {
    SendPackets();
  }

  timeSpent = ink_hrtime_to_msec(now - m_last_report);
  if (timeSpent > 10000) {
    // if (m_bytesSent > 0)
    // timespent is in milli-seconds
    char temp[2048], *p1;
    char bwMessage[2048];
    double bw, totalBw;
    unsigned char *ip;

    temp[0] = '\0';
    bwMessage[0] = '\0';
    p1 = temp;

    if (m_bytesSent > 0)
      totalBw = (m_bytesSent * 8.0 * 1000.0) / (timeSpent * 1024.0 * 1024.0);
    else
      totalBw = 1.0;

    for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
      // bw is in Mbps 
      bw = (G_inkPipeInfo.m_perPipeInfo[i].m_bytesSent * 8.0 * 1000.0) / (timeSpent * 1024.0 * 1024.0);
      snprintf(p1, sizeof(temp), "\t class[%d] = %f Mbps, alloc = %f Mbps, (conf'ed = %f, got = %f) \n",
               i, bw, (G_inkPipeInfo.m_perPipeInfo[i].m_bwAlloc / (1024.0 * 1024.0)),
               G_inkPipeInfo.m_perPipeInfo[i].m_wt, bw / totalBw);
      p1 += strlen(p1);

      ip = (unsigned char *) &(G_inkPipeInfo.m_perPipeInfo[i].m_destIP);
#if 0
      if (i == 0)
        sprintf(bwMessage, "%d mixt Best-Effort %f %f\n", time(0), bw, bw / G_inkPipeInfo.m_interfaceMbps);
      else
        sprintf(bwMessage, "%d mixt %d.%d.%d.%d %f %f\n",
                time(0), ip[0], ip[1], ip[2], ip[3], bw, bw / G_inkPipeInfo.m_interfaceMbps);

      ::sendto(G_bwGrapherFd, bwMessage, strlen(bwMessage), 0,
               (struct sockaddr *) &G_bwGrapherLoc, sizeof(struct sockaddr_in));
#endif
      // use a weighted estimator of current usage
      G_inkPipeInfo.m_perPipeInfo[i].m_bwUsed = (4.0 * G_inkPipeInfo.m_perPipeInfo[i].m_bwUsed / 5.0) + (bw / 5.0);
      G_inkPipeInfo.m_perPipeInfo[i].m_bytesSent = 0;
      G_inkPipeInfo.m_perPipeInfo[i].m_pktsSent = 0;
    }
    if (temp[0])
      Debug("udpnet-bw", "B/w: %f Mbps; breakdown: \n%s", totalBw, temp);


    m_bytesSent = 0;
    m_last_report = now;
    m_added = 0;
    m_packets = 0;
  }
  m_last_service = now;
}

void
UDPQueue::SendPackets()
{
  UDPPacketInternal *p;
  static ink_hrtime lastCleanupTime = ink_get_hrtime_internal();
  ink_hrtime now = ink_get_hrtime_internal();
  // ink_hrtime send_threshold_time = now + HRTIME_MSECONDS(5);
  // send packets for SLOT_TIME per attempt
  ink_hrtime send_threshold_time = now + SLOT_TIME;
  ink32 bytesThisSlot = INT_MAX, bytesUsed = 0, reliabilityBytes = 0;
  ink32 bytesThisPipe, sentOne, i;
  ink32 pktLen;
  ink_hrtime timeDelta = 0;

  if (now > m_last_service)
    timeDelta = ink_hrtime_to_msec(now - m_last_service);

  if (G_inkPipeInfo.m_numPipes > 0) {
    bytesThisSlot = (ink32) (((G_inkPipeInfo.m_reliabilityMbps * 1024.0 * 1024.0) / (8.0 * 1000.0)) * timeDelta);
    if (bytesThisSlot == 0) {
      // use at most 10% for reliability
      bytesThisSlot = (ink32) (((G_inkPipeInfo.m_interfaceMbps * 1024.0 * 1024.0) / (8.0 * 1000.0)) * timeDelta * 0.1);
      reliabilityBytes = bytesThisSlot;
    }
  }

  while ((p = m_reliabilityPktQueue.dequeue()) != NULL) {
    pktLen = p->getPktLength();
    g_udp_bytesPending -= pktLen;

    p->m_conn->m_nBytesTodo -= pktLen;
    p->m_conn->m_nBytesDone += pktLen;

    if (p->m_conn->shouldDestroy())
      goto next_pkt_3;
    if (p->m_conn->GetSendGenerationNumber() != p->m_reqGenerationNum)
      goto next_pkt_3;

    SendUDPPacket(p, pktLen);
    bytesThisSlot -= pktLen;
    if (bytesThisSlot < 0)
      break;
  next_pkt_3:
    p->free();
  }


  if (G_inkPipeInfo.m_numPipes > 0)
    bytesThisSlot = (ink32) (((G_inkPipeInfo.m_interfaceMbps * 1024.0 * 1024.0) /
                              (8.0 * 1000.0)) * timeDelta - reliabilityBytes);
  else
    bytesThisSlot = INT_MAX;

sendPackets:
  sentOne = false;
  send_threshold_time = now + SLOT_TIME;
  for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
    bytesThisPipe = (ink32) (bytesThisSlot * G_inkPipeInfo.m_perPipeInfo[i].m_wt);
    while ((bytesThisPipe > 0) && (G_inkPipeInfo.m_perPipeInfo[i].m_queue->firstPacket(send_threshold_time))) {
      p = G_inkPipeInfo.m_perPipeInfo[i].m_queue->getFirstPacket();
      pktLen = p->getPktLength();
      g_udp_bytesPending -= pktLen;

      p->m_conn->m_nBytesTodo -= pktLen;
      p->m_conn->m_nBytesDone += pktLen;
      if (p->m_conn->shouldDestroy())
        goto next_pkt;
      if (p->m_conn->GetSendGenerationNumber() != p->m_reqGenerationNum)
        goto next_pkt;

      G_inkPipeInfo.m_perPipeInfo[i].m_bytesSent += pktLen;
      SendUDPPacket(p, pktLen);
      bytesUsed += pktLen;
      bytesThisPipe -= pktLen;
    next_pkt:
      sentOne = true;
      p->free();

      if (bytesThisPipe < 0)
        break;
    }
  }

  bytesThisSlot -= bytesUsed;

  if ((bytesThisSlot > 0) && (sentOne)) {
    // redistribute the slack...
    now = ink_get_hrtime_internal();
    for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
      if (G_inkPipeInfo.m_perPipeInfo[i].m_queue->firstPacket(now) == NULL) {
        G_inkPipeInfo.m_perPipeInfo[i].m_queue->advanceNow(now);
      }
    }
    goto sendPackets;
  }

  if ((g_udp_periodicFreeCancelledPkts) &&
      (now - lastCleanupTime > ink_hrtime_from_sec(g_udp_periodicFreeCancelledPkts))) {
    inku64 nbytes = g_udp_bytesPending;
    ink_hrtime startTime = ink_get_hrtime_internal(), endTime;
    for (i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
      G_inkPipeInfo.m_perPipeInfo[i].m_queue->FreeCancelledPackets(g_udp_periodicCleanupSlots);
    }
    endTime = ink_get_hrtime_internal();
    Debug("udp-pending-packets", "Did cleanup of %d buckets: %lld bytes in %d m.sec",
          g_udp_periodicCleanupSlots, nbytes - g_udp_bytesPending, ink_hrtime_to_msec(endTime - startTime));
    lastCleanupTime = now;
  }
}

void
UDPQueue::SendUDPPacket(UDPPacketInternal * p, ink32 pktLen)
{
  IOBufferBlock *b;
  struct msghdr msg;
  struct iovec iov[32];
  int real_len = 0;
  int n, count, iov_len = 0;

  if (!p->m_isReliabilityPkt) {
    p->m_conn->SetLastSentPktTSSeqNum(p->m_pktTSSeqNum);
    p->m_conn->m_lastSentPktStartTime = p->m_delivery_time;
  }

  Debug("udp-send", "Sending 0x%x", p);
  msg.msg_control = 0;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;
  msg.msg_name = (caddr_t) & p->m_to;
  msg.msg_namelen = sizeof(p->m_to);
  iov_len = 0;
  m_bytesSent += pktLen;
  for (b = p->m_chain; b != NULL; b = b->next) {
    iov[iov_len].iov_base = (caddr_t) b->start();
    iov[iov_len].iov_len = b->size();
    real_len += iov[iov_len].iov_len;
    iov_len++;
  }
  msg.msg_iov = iov;
  msg.msg_iovlen = iov_len;

  count = 0;
  while (1) {
    // stupid Linux problem: sendmsg can return EAGAIN
    n =::sendmsg(p->m_conn->getFd(), &msg, 0);
    if ((n >= 0) || ((n < 0) && (errno != EAGAIN)))
      // send succeeded or some random error happened.
      break;
    if (errno == EAGAIN) {
      count++;
      if ((g_udp_numSendRetries > 0) && (count >= g_udp_numSendRetries)) {
        // tried too many times; give up
        Debug("udpnet", "Send failed: too many retries");
        break;
      }
    }
  }
}

#ifndef BULK_IO_SEND_IS_BROKEN
void
UDPQueue::BulkIOSend()
{
  ink_assert(!"Don't call here...");
}
#else
void
UDPQueue::BulkIOSend()
{
  bool sentOne = false;
  UDPPacketInternal *p;
  ink_hrtime now = ink_get_hrtime_internal();
  ink_hrtime send_threshold_time = now + SLOT_TIME;

  for (int i = 0; i < G_inkPipeInfo.m_numPipes + 1; i++) {
    while (p = G_inkPipeInfo.m_perPipeInfo[i].m_queue->firstPacket(send_threshold_time)) {
      p = G_inkPipeInfo.m_perPipeInfo[i].m_queue->getFirstPacket();
      sentOne = true;
      Debug("bulk-io-pkt", "Adding a packet...");
      BulkIOAddPkt(G_bulkIOState, &G_bulkIOAggregator, p, p->m_conn->getPortNum());
      m_bytesSent += p->getPktLength();
      // Now the packet is "sent"; get rid of it
      p->free();
    }
  }
  if (sentOne) {
    BulkIOFlush(G_bulkIOState, &G_bulkIOAggregator);
  }
}
#endif

void
UDPQueue::send(UDPPacket * p)
{
  // XXX: maybe fastpath for immediate send?
//  Debug("udpnet","add packet %x, delivery=%lld",p,p->m_delivery_time);
  ink_atomiclist_push(m_atomicQueue, p);
  //m_queue->addPacket(p,ink_get_hrtime());
}

#undef LINK

UDPNetHandler::UDPNetHandler()
{

  mutex = new_ProxyMutex();

  //pollDescriptor = (PollDescriptor*)xmalloc(sizeof(PollDescriptor));
  //pollDescriptor->init();
  udpConnections = (UnixUDPConnection **) xmalloc(sizeof(UnixUDPConnection *) * MAX_UDP_CONNECTION);
  int i;
  for (i = 0; i < MAX_UDP_CONNECTION; i++) {
    udpConnections[i] = NULL;
  }

  ink_atomiclist_init(&udpAtomicQueue, "Outgoing UDP Packet queue", (uintptr_t) &((UDPPacketInternal *) 0)->alink.next);

  ink_atomiclist_init(&udpNewConnections, "UDP Connection queue", (uintptr_t)
                      &((UnixUDPConnection *) 0)->newconn_alink.next);

  udpOutQueue = NEW(new UDPQueue(&udpAtomicQueue));
  udp_polling = NEW(new Queue<UnixUDPConnection> ());
  udp_callbacks = NEW(new Queue<UnixUDPConnection> ());
  nextCheck = ink_get_hrtime_internal() + HRTIME_MSECONDS(1000);
  lastCheck = 0;
  SET_HANDLER((UDPNetContHandler) & UDPNetHandler::startNetEvent);
}

int
UDPNetHandler::startNetEvent(int event, Event * e)
{
  (void) event;
  SET_HANDLER((UDPNetContHandler) & UDPNetHandler::mainNetEvent);
  e->schedule_every(-HRTIME_MSECONDS(9));
  trigger_event = e;
  return EVENT_CONT;
}
inline PollDescriptor *
UDPNetHandler::build_one_udpread_poll(int fd, UnixUDPConnection * uc, PollDescriptor * pd)
{
  int i;

  // XXX: just hack until figure things out
  ink_assert(uc->getFd() > 0);
  Pollfd *pfd;
#ifdef BSD_TCP
  pfd = pd->alloc(fd);
#else
  pfd = pd->alloc();
  pfd->fd = fd;
#endif
  i = pfd - pd->pfd;
  uc->setPollvecIndex(i);
  udpConnections[i] = uc;
  pfd->events = POLLIN;
  pfd->revents = 0;
  return pd;
}

PollDescriptor *
UDPNetHandler::build_poll(PollDescriptor * pd)
{
  // build read poll for UDP connections.
  UnixUDPConnection *uc;
  ink_assert(pd->empty());
  int i = 0;
  for (uc = udp_polling->head; uc; uc = uc->polling_link.next) {
    if (uc->recvActive) {
      pd = build_one_udpread_poll(uc->getFd(), uc, pd);
      i++;
    }
  }
  if (i > 500) {
    Debug("udpnet-poll", "%d fds", i);
  }
  return pd;
}

int
UDPNetHandler::mainNetEvent(int event, Event * e)
{
  ink_assert(trigger_event == e && event == EVENT_POLL);
  (void) event;
  (void) e;

  PollCont *pc = get_UDPPollCont(e->ethread);

  //changed by YTS Team, yamsat 
  // pick up new UDP connections for servicing
  /* if (!INK_ATOMICLIST_EMPTY(udpNewConnections)) {
     UnixUDPConnection *c = 
     (UnixUDPConnection *)ink_atomiclist_popall(&udpNewConnections);
     if (c) {
     UnixUDPConnection *cnext = NULL;
     while (c) {
     if (c->shouldDestroy()) {
     cnext = c->newconn_alink.next;
     c->newconn_alink.next = NULL;
     if (G_inkPipeInfo.m_numPipes > 0)
     G_inkPipeInfo.m_perPipeInfo[c->m_pipe_class].m_count--;
     c->Release();
     c = cnext;
     continue;
     }
     ink_assert(!c->mutex == !c->continuation);
     cnext = c->newconn_alink.next;
     c->newconn_alink.next = NULL;
     ink_assert(c->polling_link.next ==NULL);
     ink_assert(c->polling_link.prev ==NULL);
     ink_assert(c->m_ethread == trigger_event->ethread);
     c->setEthread(trigger_event->ethread);
     //udp_polling->enqueue(c,c->polling_link);
     c = cnext;
     }
     }
     } */

  // handle UDP outgoing engine
  udpOutQueue->service(this);

  // handle UDP read operations
  UnixUDPConnection *uc, *next;
  int i;
  int nread = 0;
#ifdef BSD_TCP
  for (i = POLL_DESCRIPTOR_SIZE; i < (POLL_DESCRIPTOR_SIZE + pc->pollDescriptor->nfds_bsd); i++) {
    if (pc->pollDescriptor->pfd[i].revents & POLLIN) {
      uc = udpConnections[i];
      ink_assert(uc && uc->mutex && uc->continuation);
      ink_assert(uc->m_refcount >= 1);
      if (uc->shouldDestroy()) {
        udp_polling->remove(uc, uc->polling_link);
        uc->Release();
      } else {
        udpNet.udp_read_from_net(this, uc, pc->pollDescriptor, trigger_event->ethread);
        nread++;
      }
    }
  }

  for (i = 0; i < pc->pollDescriptor->nfds_sol; i++) {
    if (pc->pollDescriptor->pfd[i].revents & POLLIN) {
      uc = udpConnections[i];
      ink_assert(uc && uc->mutex && uc->continuation);
      ink_assert(uc->m_refcount >= 1);
      if (uc->shouldDestroy()) {
        //changed by YTS Team, yamsat
        //udp_polling->remove(uc,uc->polling_link);
        uc->Release();
      } else {
        udpNet.udp_read_from_net(this, uc, pc->pollDescriptor, trigger_event->ethread);
        nread++;
      }
    }
  }

#else
//epoll changes

  //changed by YTS Team, yamsat 
  struct epoll_data_ptr *temp_eptr = NULL;
  for (i = 0; i < pc->pollDescriptor->result; i++) {
    temp_eptr = (struct epoll_data_ptr *) get_ev_data(pc->pollDescriptor,i);
    if ((get_ev_events(pc->pollDescriptor,i) & INK_EVP_IN)
        && temp_eptr->type == EPOLL_UDP_CONNECTION) {
      uc = temp_eptr->data.uc;
      ink_assert(uc && uc->mutex && uc->continuation);
      ink_assert(uc->m_refcount >= 1);
      if (uc->shouldDestroy()) {
        // udp_polling->remove(uc,uc->polling_link);
        uc->Release();
      } else {
        udpNetInternal.udp_read_from_net(this, uc, pc->pollDescriptor, trigger_event->ethread);
        nread++;
      }
    }                           //if EPOLLIN        
  }                             //end for

//epoll changes ends here

/*
  for (i = 0; i <pc->pollDescriptor->nfds; i++) {
    if (pc->pollDescriptor->pfd[i].revents & POLLIN) {
      uc = udpConnections[i];
      ink_assert(uc && uc->mutex && uc->continuation);
      ink_assert(uc->m_refcount >= 1);
      if (uc->shouldDestroy()) {
	udp_polling->remove(uc,uc->polling_link);
	uc->Release();
      } else {
	udpNetInternal.udp_read_from_net(this,uc,pc->pollDescriptor,trigger_event->ethread);
	nread++;
      }
    }
  } */
#endif

  // remove dead UDP connections
  ink_hrtime now = ink_get_hrtime_internal();
  if (now >= nextCheck) {
    for (uc = udp_polling->head; uc; uc = next) {
      ink_assert(uc->mutex && uc->continuation);
      ink_assert(uc->m_refcount >= 1);
      next = uc->polling_link.next;
      if (uc->shouldDestroy()) {
        if (G_inkPipeInfo.m_numPipes > 0)
          G_inkPipeInfo.m_perPipeInfo[uc->m_pipe_class].m_count--;
        //changed by YTS Team, yamsat
        //udp_polling->remove(uc,uc->polling_link);
        uc->Release();
      }
    }
    nextCheck = ink_get_hrtime_internal() + HRTIME_MSECONDS(1000);
  }
  // service UDPConnections with data ready for callback.
  Queue<UnixUDPConnection> q = *udp_callbacks;
  udp_callbacks->clear();
  while ((uc = q.dequeue(q.head, q.head->callback_link))) {
    ink_assert(uc->mutex && uc->continuation);
    if (udpNetInternal.udp_callback(this, uc, trigger_event->ethread)) {        // not successful
      // schedule on a thread of its own.
      ink_assert(uc->callback_link.next == NULL);
      ink_assert(uc->callback_link.prev == NULL);
      udp_callbacks->enqueue(uc, uc->callback_link);
    } else {
      ink_assert(uc->callback_link.next == NULL);
      ink_assert(uc->callback_link.prev == NULL);
      uc->onCallbackQueue = 0;
      uc->Release();
    }
  }

  //changed by YTS Team, yamsat 
  return EVENT_CONT;
}

/////////////////////////////////////////////////////////////////////
//
// A helper continuation that creates a pair of UDP ports in a non-blocking
// way.  This continuation runs on the UDP thread; a run lasts for at most 500ms.
//
/////////////////////////////////////////////////////////////////////

void
UDPWorkContinuation::init(Continuation * c, int numPairs,
                          unsigned int my_ip, unsigned int dest_ip, int s_bufsize, int r_bufsize)
{
  mutex = c->mutex;
  m_cont = c;
  m_action = c;
  m_numPairs = numPairs;
  m_myIP = my_ip;
  m_destIP = dest_ip;
  m_sendbufsize = s_bufsize;
  m_recvbufsize = r_bufsize;
  m_udpConns = NULL;
  SET_HANDLER((UDPWorkContinuation_Handler) & UDPWorkContinuation::StateCreatePortPairs);
}

int
UDPWorkContinuation::StateCreatePortPairs(int event, void *data)
{
//  int res = 0;
  int numUdpPorts = 2 * m_numPairs;
  int fd1 = -1, fd2 = -1;
//  struct sockaddr_in bind_sa;
  struct sockaddr_in myaddr1, myaddr2;
  int portNum, i;
//  int myaddr_len = sizeof(myaddr1);
  static int lastAllocPort = 10000;
  ink_hrtime startTime, endTime;
  Action *status;
  //epoll changes

  //added by YTS Team, yamsat 
  struct epoll_data_ptr *eptr = NULL;
  PollCont *pc = NULL;
  //epoll changes ends here
  ink_debug_assert(mutex->thread_holding == this_ethread());

  if (m_action.cancelled) {
    m_action = NULL;
    mutex = NULL;
    udpWorkContinuationAllocator.free(this);
    return EVENT_CONT;
  }

  startTime = ink_get_hrtime_internal();

  m_udpConns = NEW(new UnixUDPConnection *[numUdpPorts]);
  for (i = 0; i < numUdpPorts; i++)
    m_udpConns[i] = NULL;
  ink_atomic_swap(&portNum, lastAllocPort);
  portNum %= 50000;
  if (portNum == 0)
    portNum = 10000;

  i = 0;
  while (i < numUdpPorts) {

    if (udpNet.CreateUDPSocket(&fd1, &myaddr1, &status, portNum, m_myIP, m_sendbufsize, m_recvbufsize)) {
      if (udpNet.CreateUDPSocket(&fd2, &myaddr2, &status, portNum + 1, m_myIP, m_sendbufsize, m_recvbufsize)) {
        m_udpConns[i] = NEW(new UnixUDPConnection(fd1));        // new_UnixUDPConnection(fd1);
        m_udpConns[i]->setBinding(&myaddr1);
        i++;
        m_udpConns[i] = NEW(new UnixUDPConnection(fd2));        // new_UnixUDPConnection(fd2);
        m_udpConns[i]->setBinding(&myaddr2);
        i++;
        // remember the last alloc'ed port
        ink_atomic_swap(&lastAllocPort, portNum + 2);
      } else {
        if (fd1 != NO_FD)
          socketManager.close(fd1, keSocket);
        if (status == ACTION_IO_ERROR)
          goto Lerror;
      }
      Debug("udpnet", "Created port pair with ports = %d, %d", portNum, portNum + 1);
    } else if (status == ACTION_IO_ERROR)
      goto Lerror;
    // pick the next port pair value
    portNum += 2;
    // wrap around at 50K
    portNum %= 50000;
    if (portNum == 0)
      portNum = 10000;
    endTime = ink_get_hrtime_internal();
    // if we spend more than 500 ms. bail!
    if (ink_hrtime_to_msec(endTime - startTime) > 500) {
      status = ACTION_IO_ERROR;
      goto Lerror;
    }

  }

  for (i = 0; i < numUdpPorts; i++) {
    udpNet.UDPClassifyConnection(m_udpConns[i], m_destIP);
    Debug("udpnet-pipe", "Adding (port = %d) to Pipe class: %d",
          m_udpConns[i]->getPortNum(), m_udpConns[i]->m_pipe_class);
  }

  // assert should *never* fire; we check for this at the begin of the func.
  ink_assert(!m_action.cancelled);

  // Bind to threads only on a success.  Currently, after you have
  // bound to have a thread, the only way to remove a UDPConnection is
  // to call destroy(); the thread to which the UDPConnection will
  // remove the connection from a linked list and call delete.
  //struct epoll_event ev;
  //struct epoll_data_ptr *eptr=NULL;
  //PollCont * pc  = NULL;

#if defined(USE_EPOLL)
  struct epoll_event ev;
#elif defined(USE_KQUEUE)
  struct kevent ev;
#else
#error port me
#endif
  //changed by YTS Team, yamsat
  for (i = 0; i < numUdpPorts; i++) {
    m_udpConns[i]->bindToThread(m_cont);
    //epoll changes
    pc = get_UDPPollCont(m_udpConns[i]->m_ethread);
    eptr = (struct epoll_data_ptr *) malloc(sizeof(struct epoll_data_ptr));

    eptr->type = 5;             //UDP
    eptr->data.uc = m_udpConns[i];
    m_udpConns[i]->eptr = eptr;

#if defined(USE_EPOLL)
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = eptr;
    epoll_ctl(pc->pollDescriptor->epoll_fd, EPOLL_CTL_ADD, m_udpConns[i]->getFd(), &ev);
#elif defined(USE_KQUEUE)
    EV_SET(&ev, m_udpConns[i]->getFd(), EVFILT_READ, EV_ADD, 0, 0, eptr);
    kevent(pc->pollDescriptor->kqueue_fd, &ev, 1, NULL, 0, NULL);
#else
#error port me
#endif
    //epoll changes ends here
  }                             //for

  m_resultCode = NET_EVENT_DATAGRAM_OPEN;
  goto out;

Lerror:
  m_resultCode = NET_EVENT_DATAGRAM_ERROR;
  for (i = 0; i < numUdpPorts; i++) {
    delete m_udpConns[i];
  }
  delete[]m_udpConns;
  m_udpConns = NULL;

out:
  SET_HANDLER((UDPWorkContinuation_Handler) & UDPWorkContinuation::StateDoCallback);
  return StateDoCallback(0, NULL);
}

int
UDPWorkContinuation::StateDoCallback(int event, void *data)
{
  MUTEX_TRY_LOCK(lock, m_action.mutex, this_ethread());
  if (!lock) {
    this_ethread()->schedule_in(this, MUTEX_RETRY_DELAY);
    return EVENT_CONT;
  }
  if (!m_action.cancelled) {
    m_action.continuation->handleEvent(m_resultCode, m_udpConns);
  } else {
    // else m_action.cancelled
    if (m_resultCode == NET_EVENT_DATAGRAM_OPEN) {
      for (int i = 0; i < m_numPairs * 2; i++)
        // don't call delete on individual connections; the udp thread will do
        // that when it cleans up an fd.
        m_udpConns[i]->destroy();
      delete[]m_udpConns;       // I think this is OK to delete the array, what we shouldn't do is loop over
      m_udpConns = NULL;        // the conns and and do delete m_udpConns[i].
    }
  }

  m_action = NULL;
  mutex = NULL;
  udpWorkContinuationAllocator.free(this);

  return EVENT_CONT;
}
