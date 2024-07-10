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

#if defined(darwin)
/* This is for IPV6_PKTINFO and IPV6_RECVPKTINFO */
#define __APPLE_USE_RFC_3542
#endif

#include "iocore/net/AsyncSignalEventIO.h"
#include "iocore/aio/AIO.h"
#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"
#endif
#include "P_Net.h"
#include "P_UDPNet.h"
#include "tscore/ink_inet.h"
#include "tscore/ink_sock.h"
#include <netinet/udp.h>
#include "P_UnixNet.h"
#ifdef HAVE_SO_TXTIME
#include <linux/net_tstamp.h>
#endif

#ifndef UDP_SEGMENT
// This is needed because old glibc may not have the constant even if Kernel supports it.
#define UDP_SEGMENT 103
#endif

#ifndef UDP_GRO
#define UDP_GRO 104
#endif

using UDPNetContHandler = int (UDPNetHandler::*)(int, void *);

ClassAllocator<UDPPacket> udpPacketAllocator("udpPacketAllocator");
EventType                 ET_UDP;

namespace
{
#ifdef HAVE_RECVMMSG
const uint32_t MAX_RECEIVE_MSG_PER_CALL{16}; //< VLEN parameter for the recvmmsg call.
#endif

DbgCtl dbg_ctl_udpnet{"udpnet"};
DbgCtl dbg_ctl_udp_read{"udp-read"};
DbgCtl dbg_ctl_udp_send{"udp-send"};
DbgCtl dbg_ctl_iocore_udp_main{"iocore_udp_main-send"};

} // end anonymous namespace

UDPPacket *
UDPPacket::new_UDPPacket()
{
  return udpPacketAllocator.alloc();
}

UDPPacket *
UDPPacket::new_UDPPacket(struct sockaddr const *to, ink_hrtime when, Ptr<IOBufferBlock> &buf, uint16_t segment_size,
                         [[maybe_unused]] struct timespec *send_at_hint)
{
  UDPPacket *p = udpPacketAllocator.alloc();

  p->p.in_the_priority_queue = 0;
  p->p.in_heap               = 0;
  p->p.delivery_time         = when;
  if (to)
    ats_ip_copy(&p->to, to);
  p->p.chain        = buf;
  p->p.segment_size = segment_size;
#ifdef HAVE_SO_TXTIME
  if (send_at_hint) {
    memcpy(&p->p.send_at, send_at_hint, sizeof(struct timespec));
  }
#endif
  return p;
}

UDPPacket *
UDPPacket::new_incoming_UDPPacket(struct sockaddr *from, struct sockaddr *to, Ptr<IOBufferBlock> block)
{
  UDPPacket *p = udpPacketAllocator.alloc();

  p->p.in_the_priority_queue = 0;
  p->p.in_heap               = 0;
  p->p.delivery_time         = 0;
  ats_ip_copy(&p->from, from);
  ats_ip_copy(&p->to, to);
  p->p.chain = block;

  return p;
}

UDPPacket::UDPPacket()

{
  memset(&from, '\0', sizeof(from));
  memset(&to, '\0', sizeof(to));
}

UDPPacket::~UDPPacket()
{
  p.chain = nullptr;
}

void
UDPPacket::append_block(IOBufferBlock *block)
{
  if (block) {
    if (p.chain) { // append to end
      IOBufferBlock *last = p.chain.get();
      while (last->next) {
        last = last->next.get();
      }
      last->next = block;
    } else {
      p.chain = block;
    }
  }
}

int64_t
UDPPacket::getPktLength()
{
  UDPPacket     *pkt = this;
  IOBufferBlock *b;

  pkt->p.pktLength = 0;
  b                = pkt->p.chain.get();
  while (b) {
    pkt->p.pktLength += b->read_avail();
    b                 = b->next.get();
  }
  return pkt->p.pktLength;
}

void
UDPPacket::free()
{
  p.chain = nullptr;
  if (p.conn)
    p.conn->Release();
  p.conn = nullptr;

  if (this->_payload) {
    _payload.reset();
  }
  udpPacketAllocator.free(this);
}

uint8_t *
UDPPacket::get_entire_chain_buffer(size_t *buf_len)
{
  if (this->_payload == nullptr) {
    IOBufferBlock *block = this->getIOBlockChain();

    if (block && block->next.get() == nullptr) {
      *buf_len = this->getPktLength();
      return reinterpret_cast<uint8_t *>(block->buf());
    }

    // Store it. Should we try to avoid allocating here?
    this->_payload = ats_unique_malloc(this->getPktLength());
    uint64_t len{0};
    while (block) {
      memcpy(this->_payload.get() + len, block->start(), block->read_avail());
      len   += block->read_avail();
      block  = block->next.get();
    }
    ink_assert(len == static_cast<size_t>(this->getPktLength()));
  }

  *buf_len = this->getPktLength();
  return this->_payload.get();
}
//
// Global Data
//

UDPNetProcessorInternal udpNetInternal;
UDPNetProcessor        &udpNet = udpNetInternal;

int     g_udp_pollTimeout;
int32_t g_udp_periodicCleanupSlots;
int32_t g_udp_periodicFreeCancelledPkts;
int32_t g_udp_numSendRetries;

namespace
{
// We'd need to set some flags in case some of the config is enabled. So we have
// this object as global.
UDPNetHandler::Cfg G_udp_config;
} // namespace
//
// Public functions
// See header for documentation
//
int          G_bwGrapherFd;
sockaddr_in6 G_bwGrapherLoc;

void
initialize_thread_for_udp_net(EThread *thread)
{
  int enable_gso, enable_gro;
  REC_ReadConfigInteger(enable_gso, "proxy.config.udp.enable_gso");
  REC_ReadConfigInteger(enable_gro, "proxy.config.udp.enable_gro");

  UDPNetHandler *nh = get_UDPNetHandler(thread);

  UDPNetHandler::Cfg cfg{static_cast<bool>(enable_gso), static_cast<bool>(enable_gro)};

  G_udp_config = cfg; // keep a global copy.
  new (reinterpret_cast<ink_dummy_for_new *>(nh)) UDPNetHandler(std::move(cfg));

#ifndef SOL_UDP
  if (G_udp_config.enable_gro) {
    Warning("Attempted to use UDP GRO per configuration, but it is unavailable");
  }
#endif

  new (reinterpret_cast<ink_dummy_for_new *>(get_UDPPollCont(thread))) PollCont(thread->mutex);
  // The UDPNetHandler cannot be accessed across EThreads.
  // Because the UDPNetHandler should be called back immediately after UDPPollCont.
  nh->mutex  = thread->mutex.get();
  nh->thread = thread;

  PollCont       *upc = get_UDPPollCont(thread);
  PollDescriptor *upd = upc->pollDescriptor;

  REC_ReadConfigInteger(g_udp_pollTimeout, "proxy.config.udp.poll_timeout");
  upc->poll_timeout = g_udp_pollTimeout;

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

  thread->set_tail_handler(nh);
#if HAVE_EVENTFD
#if TS_USE_LINUX_IO_URING
  auto ep = new IOUringEventIO();
  ep->start(upd, IOUringContext::local_context());
#else
  auto ep = new AsyncSignalEventIO();
  ep->start(upd, thread->evfd, EVENTIO_READ);
#endif
#else
  auto ep = new AsyncSignalEventIO();
  ep->start(upd, thread->evpipe[0], EVENTIO_READ);
#endif
  thread->ep = ep;
}

EventType
UDPNetProcessorInternal::register_event_type()
{
  ET_UDP = eventProcessor.register_event_type("ET_UDP");
  return ET_UDP;
}

int
UDPNetProcessorInternal::start(int n_upd_threads, size_t stacksize)
{
  if (n_upd_threads < 1) {
    return -1;
  }

  pollCont_offset      = eventProcessor.allocate(sizeof(PollCont));
  udpNetHandler_offset = eventProcessor.allocate(sizeof(UDPNetHandler));

  eventProcessor.schedule_spawn(&initialize_thread_for_udp_net, ET_UDP);
  eventProcessor.spawn_event_threads(ET_UDP, n_upd_threads, stacksize);

  return 0;
}

namespace
{
bool
get_ip_address_from_cmsg(struct cmsghdr *cmsg, sockaddr_in6 *toaddr)
{
#ifdef IP_PKTINFO
  if (IP_PKTINFO == cmsg->cmsg_type) {
    if (cmsg->cmsg_level == IPPROTO_IP) {
      struct in_pktinfo *pktinfo                               = reinterpret_cast<struct in_pktinfo *>(CMSG_DATA(cmsg));
      reinterpret_cast<sockaddr_in *>(toaddr)->sin_addr.s_addr = pktinfo->ipi_addr.s_addr;
    }
    return true;
  }
#endif
#ifdef IP_RECVDSTADDR
  if (IP_RECVDSTADDR == cmsg->cmsg_type) {
    if (cmsg->cmsg_level == IPPROTO_IP) {
      struct in_addr *addr                                     = reinterpret_cast<struct in_addr *>(CMSG_DATA(cmsg));
      reinterpret_cast<sockaddr_in *>(toaddr)->sin_addr.s_addr = addr->s_addr;
    }
    return true;
  }
#endif
#if defined(IPV6_PKTINFO) || defined(IPV6_RECVPKTINFO)
  if (IPV6_PKTINFO == cmsg->cmsg_type) { // IPV6_RECVPKTINFO uses IPV6_PKTINFO too
    if (cmsg->cmsg_level == IPPROTO_IPV6) {
      struct in6_pktinfo *pktinfo = reinterpret_cast<struct in6_pktinfo *>(CMSG_DATA(cmsg));
      memcpy(toaddr->sin6_addr.s6_addr, &pktinfo->ipi6_addr, 16);
    }
    return true;
  }
#endif
  return false;
}

unsigned int
build_iovec_block_chain(unsigned max_niov, int64_t size_index, Ptr<IOBufferBlock> &chain, struct iovec *out_tiovec)
{
  unsigned int   niov;
  IOBufferBlock *b, *last;

  // build struct iov
  // reuse the block in chain if available
  b    = chain.get();
  last = nullptr;
  for (niov = 0; niov < max_niov; niov++) {
    if (b == nullptr) {
      b = new_IOBufferBlock();
      b->alloc(size_index);
      if (last == nullptr) {
        chain = b;
      } else {
        last->next = b;
      }
    }

    out_tiovec[niov].iov_base = b->buf();
    out_tiovec[niov].iov_len  = b->block_size();

    last = b;
    b    = b->next.get();
  }
  return niov;
}
} // namespace

void
UDPNetProcessorInternal::read_single_message_from_net(UDPNetHandler *nh, UDPConnection *xuc)
{
  UnixUDPConnection *uc = (UnixUDPConnection *)xuc;

  // receive packet and queue onto UDPConnection.
  // don't call back connection at this time.
  int64_t            r;
  int                iters    = 0;
  unsigned           max_niov = 32;
  int64_t            gso_size{0}; // in case is available.
  struct msghdr      msg;
  Ptr<IOBufferBlock> chain, next_chain;
  struct iovec       tiovec[max_niov];
  int64_t            size_index  = BUFFER_SIZE_INDEX_2K;
  int64_t            buffer_size = BUFFER_SIZE_FOR_INDEX(size_index);
  // The max length of receive buffer is 32 * buffer_size (2048) = 65536 bytes.
  // Because the 'UDP Length' is type of uint16_t defined in RFC 768.
  // And there is 8 octets in 'User Datagram Header' which means the max length of payload is no more than 65527 bytes.
  do {
    // create IOBufferBlock chain to receive data
    unsigned int niov = build_iovec_block_chain(max_niov, size_index, chain, tiovec);

    // build struct msghdr
    sockaddr_in6 fromaddr;
    sockaddr_in6 toaddr;
    int          toaddr_len = sizeof(toaddr);
    msg.msg_name            = &fromaddr;
    msg.msg_namelen         = sizeof(fromaddr);
    msg.msg_iov             = tiovec;
    msg.msg_iovlen          = niov;
    msg.msg_flags           = 0;

    static const size_t cmsg_size{CMSG_SPACE(sizeof(int))
#ifdef IP_PKTINFO
                                  + CMSG_SPACE(sizeof(struct in_pktinfo))
#endif
#if defined(IPV6_PKTINFO) || defined(IPV6_RECVPKTINFO)
                                  + CMSG_SPACE(sizeof(struct in6_pktinfo))
#endif
#ifdef IP_RECVDSTADDR
                                  + CMSG_SPACE(sizeof(struct in_addr))
#endif
#ifdef UDP_GRO
                                  + CMSG_SPACE(sizeof(uint16_t))
#endif
    };

    char control[cmsg_size] = {0};
    msg.msg_control         = control;
    msg.msg_controllen      = cmsg_size;

    // receive data by recvmsg
    r = SocketManager::recvmsg(uc->getFd(), &msg, 0);
    if (r <= 0) {
      // error
      break;
    }

    // truncated check
    if (msg.msg_flags & MSG_TRUNC) {
      Dbg(dbg_ctl_udp_read, "The UDP packet is truncated");
    }

    safe_getsockname(xuc->getFd(), reinterpret_cast<struct sockaddr *>(&toaddr), &toaddr_len);
    for (auto cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
      if (get_ip_address_from_cmsg(cmsg, &toaddr)) {
        break;
      }
#ifdef SOL_UDP
      if (UDP_GRO == cmsg->cmsg_type) {
        if (nh->is_gro_enabled()) {
          gso_size = *reinterpret_cast<uint16_t *>(CMSG_DATA(cmsg));
        }
        break;
      }
#endif
    }

    // If gro was used, then the kernel will tell us the size of each part that was spliced together.
    Dbg(dbg_ctl_udp_read, "Received %lld bytes. gso_size %lld (%s)", static_cast<long long>(r), static_cast<long long>(gso_size),
        (gso_size > 0 ? "GRO" : "No GRO"));

    IOBufferBlock *block;
    int64_t        remaining{r};
    while (remaining > 0) {
      block                 = chain.get();
      int64_t this_packet_r = gso_size ? std::min(gso_size, r) : r;
      while (block && this_packet_r > 0) {
        if (this_packet_r > buffer_size) {
          block->fill(buffer_size);
          this_packet_r -= buffer_size;
          block          = block->next.get();
          remaining     -= buffer_size;
        } else {
          block->fill(this_packet_r);
          remaining     -= this_packet_r;
          this_packet_r  = 0;
          next_chain     = block->next.get();
          block->next    = nullptr;
        }
      }
      Dbg(dbg_ctl_udp_read, "Creating packet");
      // create packet
      UDPPacket *p = UDPPacket::new_incoming_UDPPacket(ats_ip_sa_cast(&fromaddr), ats_ip_sa_cast(&toaddr), chain);
      p->setConnection(uc);
      // queue onto the UDPConnection
      uc->inQueue.push(p);

      // reload the unused block
      chain      = next_chain;
      next_chain = nullptr;
    }

    iters++;
  } while (r > 0);
  if (iters >= 1) {
    Dbg(dbg_ctl_udp_read, "read %d at a time", iters);
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

#ifdef HAVE_RECVMMSG
void
UDPNetProcessorInternal::read_multiple_messages_from_net(UDPNetHandler *nh, UDPConnection *xuc)
{
  UnixUDPConnection *uc = (UnixUDPConnection *)xuc;

  std::array<Ptr<IOBufferBlock>, MAX_RECEIVE_MSG_PER_CALL> buffer_chain;
  unsigned                                                 max_niov = 32;
  int64_t                                                  gso_size{0}; // In case is available

  struct mmsghdr mmsg[MAX_RECEIVE_MSG_PER_CALL];
  struct iovec   tiovec[MAX_RECEIVE_MSG_PER_CALL][max_niov];

  // Addresses
  sockaddr_in6 fromaddr[MAX_RECEIVE_MSG_PER_CALL];
  sockaddr_in6 toaddr[MAX_RECEIVE_MSG_PER_CALL];
  int          toaddr_len = sizeof(toaddr);

  size_t total_bytes_read{0};

  static const size_t cmsg_size{CMSG_SPACE(sizeof(int))
#ifdef IP_PKTINFO
                                + CMSG_SPACE(sizeof(struct in_pktinfo))
#endif
#if defined(IPV6_PKTINFO) || defined(IPV6_RECVPKTINFO)
                                + CMSG_SPACE(sizeof(struct in6_pktinfo))
#endif
#ifdef IP_RECVDSTADDR
                                + CMSG_SPACE(sizeof(struct in_addr))
#endif
#ifdef UDP_GRO
                                + CMSG_SPACE(sizeof(uint16_t))
#endif
  };

  // Ancillary data.
  char control[MAX_RECEIVE_MSG_PER_CALL][cmsg_size];

  int64_t size_index  = BUFFER_SIZE_INDEX_2K;
  int64_t buffer_size = BUFFER_SIZE_FOR_INDEX(size_index);
  // The max length of receive buffer is 32 * buffer_size (2048) = 65536 bytes.
  // Because the 'UDP Length' is type of uint16_t defined in RFC 768.
  // And there is 8 octets in 'User Datagram Header' which means the max length of payload is no more than 65527 bytes.

  for (uint32_t msg_num = 0; msg_num < MAX_RECEIVE_MSG_PER_CALL; msg_num++) {
    // build each block chain.
    unsigned int niov = build_iovec_block_chain(max_niov, size_index, buffer_chain[msg_num], tiovec[msg_num]);

    mmsg[msg_num].msg_hdr.msg_iov     = tiovec[msg_num];
    mmsg[msg_num].msg_hdr.msg_iovlen  = niov;
    mmsg[msg_num].msg_hdr.msg_name    = &fromaddr[msg_num];
    mmsg[msg_num].msg_hdr.msg_namelen = sizeof(fromaddr[msg_num]);
    memset(control[msg_num], 0, cmsg_size);
    mmsg[msg_num].msg_hdr.msg_control    = control[msg_num];
    mmsg[msg_num].msg_hdr.msg_controllen = cmsg_size;
  }

  const int return_val = SocketManager::recvmmsg(uc->getFd(), mmsg, MAX_RECEIVE_MSG_PER_CALL, MSG_WAITFORONE, nullptr);

  if (return_val <= 0) {
    Dbg(dbg_ctl_udp_read, "Done. recvmmsg() ret is %d, errno %s", return_val, strerror(errno));
    return;
  }
  Dbg(dbg_ctl_udp_read, "recvmmsg() read %d packets", return_val);

  Ptr<IOBufferBlock> chain, next_chain;
  for (auto packet_num = 0; packet_num < return_val; packet_num++) {
    gso_size = 0;

    Dbg(dbg_ctl_udp_read, "Processing message %d from a total of %d", packet_num, return_val);
    struct msghdr &mhdr = mmsg[packet_num].msg_hdr;

    if (mhdr.msg_flags & MSG_TRUNC) {
      Dbg(dbg_ctl_udp_read, "The UDP packet is truncated");
      break;
    }

    if (mhdr.msg_namelen <= 0) {
      Dbg(dbg_ctl_udp_read, "Unable to get remote address from recvmmsg() for fd: %d", uc->getFd());
      return;
    }

    safe_getsockname(xuc->getFd(), reinterpret_cast<struct sockaddr *>(&toaddr[packet_num]), &toaddr_len);
    if (mhdr.msg_controllen > 0) {
      for (auto cmsg = CMSG_FIRSTHDR(&mhdr); cmsg != nullptr; cmsg = CMSG_NXTHDR(&mhdr, cmsg)) {
        if (get_ip_address_from_cmsg(cmsg, &toaddr[packet_num])) {
          break;
        }
#ifdef SOL_UDP
        if (UDP_GRO == cmsg->cmsg_type) {
          if (nh->is_gro_enabled()) {
            gso_size = *reinterpret_cast<uint16_t *>(CMSG_DATA(cmsg));
          }
          break;
        }
#endif
      }
    }

    const int64_t received  = mmsg[packet_num].msg_len;
    total_bytes_read       += received;

    // If gro was used, then the kernel will tell us the size of each part that was spliced together.
    Dbg(dbg_ctl_udp_read, "Received %lld bytes. gso_size %lld (%s)", static_cast<long long>(received),
        static_cast<long long>(gso_size), (gso_size > 0 ? "GRO" : "No GRO"));

    auto           chain = buffer_chain[packet_num];
    IOBufferBlock *block;
    int64_t        remaining{received};
    while (remaining > 0) {
      block                 = chain.get();
      int64_t this_packet_r = gso_size ? std::min(gso_size, received) : received;
      while (block && this_packet_r > 0) {
        if (this_packet_r > buffer_size) {
          block->fill(buffer_size);
          this_packet_r -= buffer_size;
          block          = block->next.get();
          remaining     -= buffer_size;
        } else {
          block->fill(this_packet_r);
          remaining     -= this_packet_r;
          this_packet_r  = 0;
          next_chain     = block->next.get();
          block->next    = nullptr;
        }
      }
      Dbg(dbg_ctl_udp_read, "Creating packet");
      // create packet
      UDPPacket *p =
        UDPPacket::new_incoming_UDPPacket(ats_ip_sa_cast(&fromaddr[packet_num]), ats_ip_sa_cast(&toaddr[packet_num]), chain);
      p->setConnection(uc);
      // queue onto the UDPConnection
      uc->inQueue.push(p);

      // reload the unused block
      chain      = next_chain;
      next_chain = nullptr;
    }
  }
  Dbg(dbg_ctl_udp_read, "Total bytes read %ld from %d packets.", total_bytes_read, return_val);

  // if not already on to-be-called-back queue, then add it.
  if (!uc->onCallbackQueue) {
    ink_assert(uc->callback_link.next == nullptr);
    ink_assert(uc->callback_link.prev == nullptr);
    uc->AddRef();
    nh->udp_callbacks.enqueue(uc);
    uc->onCallbackQueue = 1;
  }
}
#endif

void
UDPNetProcessorInternal::udp_read_from_net(UDPNetHandler *nh, UDPConnection *xuc)
{
#if HAVE_RECVMMSG
  read_multiple_messages_from_net(nh, xuc);
#else
  read_single_message_from_net(nh, xuc);
#endif
}

int
UDPNetProcessorInternal::udp_callback(UDPNetHandler *nh, UDPConnection *xuc, EThread *thread)
{
  (void)nh;
  UnixUDPConnection *uc = (UnixUDPConnection *)xuc;

  if (uc->continuation && uc->mutex) {
    MUTEX_TRY_LOCK(lock, uc->mutex, thread);
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
  int  readPollEvent(int event, Event *e);

  Action *
  getAction()
  {
    return event;
  }

  void setupPollDescriptor();

private:
  Event *event = UNINITIALIZED_EVENT_PTR; // the completion event token created
  // on behalf of the client
  Ptr<IOBufferBlock>   readbuf{nullptr};
  int                  readlen          = 0;
  struct sockaddr_in6 *fromaddr         = nullptr;
  socklen_t           *fromaddrlen      = nullptr;
  int                  fd               = NO_FD; // fd we are reading from
  int                  ifd              = NO_FD; // poll fd index
  ink_hrtime           period           = 0;     // polling period
  ink_hrtime           elapsed_time     = 0;
  ink_hrtime           timeout_interval = 0;
};

ClassAllocator<UDPReadContinuation> udpReadContAllocator("udpReadContAllocator");

UDPReadContinuation::UDPReadContinuation(Event *completionToken)
  : Continuation(nullptr),
    event(completionToken),
    readbuf(nullptr),

    fd(-1),
    ifd(-1)

{
  if (completionToken->continuation) {
    this->mutex = completionToken->continuation->mutex;
  } else {
    this->mutex = new_ProxyMutex();
  }
}

UDPReadContinuation::UDPReadContinuation() : Continuation(nullptr) {}

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
  Pollfd   *pfd;
  EThread  *et = (EThread *)this_thread();
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
      return EVENT_DONE;
    }
  }

  c = completionUtil::getContinuation(event);
  // do read
  socklen_t tmp_fromlen = *fromaddrlen;
  int       rlen        = SocketManager::recvfrom(fd, readbuf->end(), readlen, 0, ats_ip_sa_cast(fromaddr), &tmp_fromlen);

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

    return EVENT_DONE;
  } else {
    completionUtil::setThread(event, nullptr);
  }

  if (event->cancelled) {
    e->cancel();
    free();

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
 *      return ACTION_RESULT_DONE
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
  int    actual;
  Event *event = completionUtil::create();

  completionUtil::setContinuation(event, cont);
  completionUtil::setHandle(event, token);
  actual = SocketManager::recvfrom(fd, buf->end(), len, 0, fromaddr, fromaddrlen);

  if (actual > 0) {
    completionUtil::setThread(event, this_ethread());
    completionUtil::setInfo(event, fd, make_ptr(buf), actual, errno);
    buf->fill(actual);
    cont->handleEvent(NET_EVENT_DATAGRAM_READ_COMPLETE, event);
    completionUtil::destroy(event);
    return ACTION_RESULT_DONE;
  } else if (actual == 0 || actual == -EAGAIN) {
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
  int    actual;
  Event *event = completionUtil::create();

  completionUtil::setContinuation(event, cont);
  completionUtil::setHandle(event, token);

  actual = SocketManager::sendmsg(fd, msg, 0);
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
  int nbytes_sent = SocketManager::sendto(fd, buf->start(), len, 0, toaddr, toaddrlen);

  if (nbytes_sent >= 0) {
    ink_assert(nbytes_sent == len);
    buf->consume(nbytes_sent);
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_COMPLETE, (void *)-1);
    return ACTION_RESULT_DONE;
  } else {
    cont->handleEvent(NET_EVENT_DATAGRAM_WRITE_ERROR, (void *)static_cast<intptr_t>(nbytes_sent));
    return ACTION_IO_ERROR;
  }
}

bool
UDPNetProcessor::CreateUDPSocket(int *resfd, sockaddr const *remote_addr, Action **status, NetVCOptions const &opt)
{
  int        res = 0, fd = -1;
  IpEndpoint local_addr;
  int        local_addr_len = sizeof(local_addr.sa);

  // Need to do address calculations first, so we can determine the
  // address family for socket creation.
  ink_zero(local_addr);

  bool is_any_address = false;
  if (NetVCOptions::FOREIGN_ADDR == opt.addr_binding || NetVCOptions::INTF_ADDR == opt.addr_binding) {
    // Same for now, transparency for foreign addresses must be handled
    // *after* the socket is created, and we need to do this calculation
    // before the socket to get the IP family correct.
    ink_release_assert(opt.local_ip.isValid());
    local_addr.assign(opt.local_ip, htons(opt.local_port));
    ink_assert(ats_ip_are_compatible(remote_addr, &local_addr.sa));
  } else {
    // No local address specified, so use family option if possible.
    int family = ats_is_ip(opt.ip_family) ? opt.ip_family : AF_INET;
    local_addr.setToAnyAddr(family);
    is_any_address                  = true;
    local_addr.network_order_port() = htons(opt.local_port);
  }

  *resfd = -1;
  if ((res = SocketManager::socket(remote_addr->sa_family, SOCK_DGRAM, 0)) < 0) {
    goto HardError;
  }

  fd = res;
  if (safe_fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    goto HardError;
  }

  if (opt.socket_recv_bufsize > 0) {
    if (unlikely(SocketManager::set_rcvbuf_size(fd, opt.socket_recv_bufsize))) {
      Dbg(dbg_ctl_udpnet, "set_dnsbuf_size(%d) failed", opt.socket_recv_bufsize);
    }
  }
  if (opt.socket_send_bufsize > 0) {
    if (unlikely(SocketManager::set_sndbuf_size(fd, opt.socket_send_bufsize))) {
      Dbg(dbg_ctl_udpnet, "set_dnsbuf_size(%d) failed", opt.socket_send_bufsize);
    }
  }

  if (opt.ip_family == AF_INET) {
    bool succeeded = false;
#ifdef IP_PKTINFO
    if (setsockopt_on(fd, IPPROTO_IP, IP_PKTINFO) == 0) {
      succeeded = true;
    }
#endif
#ifdef IP_RECVDSTADDR
    if (setsockopt_on(fd, IPPROTO_IP, IP_RECVDSTADDR) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for pktinfo failed");
      goto HardError;
    }
  } else if (opt.ip_family == AF_INET6) {
    bool succeeded = false;
#ifdef IPV6_PKTINFO
    if (setsockopt_on(fd, IPPROTO_IPV6, IPV6_PKTINFO) == 0) {
      succeeded = true;
    }
#endif
#ifdef IPV6_RECVPKTINFO
    if (setsockopt_on(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for pktinfo failed");
      goto HardError;
    }
  }

  if (local_addr.network_order_port() || !is_any_address) {
    if (-1 == SocketManager::ink_bind(fd, &local_addr.sa, ats_ip_size(&local_addr.sa))) {
      char buff[INET6_ADDRPORTSTRLEN];
      Dbg(dbg_ctl_udpnet, "ink bind failed on %s", ats_ip_nptop(local_addr, buff, sizeof(buff)));
      goto SoftError;
    }

    if (safe_getsockname(fd, &local_addr.sa, &local_addr_len) < 0) {
      Dbg(dbg_ctl_udpnet, "CreateUdpsocket: getsockname didn't work");
      goto HardError;
    }
  }

  *resfd  = fd;
  *status = nullptr;
  Dbg(dbg_ctl_udpnet, "creating a udp socket port = %d, %d---success", ats_ip_port_host_order(remote_addr),
      ats_ip_port_host_order(local_addr));
  return true;
SoftError:
  Dbg(dbg_ctl_udpnet, "creating a udp socket port = %d---soft failure", ats_ip_port_host_order(local_addr));
  if (fd != -1) {
    SocketManager::close(fd);
  }
  *resfd  = -1;
  *status = nullptr;
  return false;
HardError:
  Dbg(dbg_ctl_udpnet, "creating a udp socket port = %d---hard failure", ats_ip_port_host_order(local_addr));
  if (fd != -1) {
    SocketManager::close(fd);
  }
  *resfd  = -1;
  *status = ACTION_IO_ERROR;
  return false;
}

Action *
UDPNetProcessor::UDPBind(Continuation *cont, sockaddr const *addr, int fd, int send_bufsize, int recv_bufsize)
{
  int                res = 0;
  UnixUDPConnection *n   = nullptr;
  IpEndpoint         myaddr;
  int                myaddr_len = sizeof(myaddr);
  PollCont          *pc         = nullptr;
  PollDescriptor    *pd         = nullptr;
  bool               need_bind  = true;

  if (fd == -1) {
    if ((res = SocketManager::socket(addr->sa_family, SOCK_DGRAM, 0)) < 0) {
      goto Lerror;
    }
    fd = res;
  } else {
    need_bind = false;
  }
  if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) {
    goto Lerror;
  }

  if (addr->sa_family == AF_INET) {
    bool succeeded = false;
#ifdef IP_PKTINFO
    if (setsockopt_on(fd, IPPROTO_IP, IP_PKTINFO) == 0) {
      succeeded = true;
    }
#endif
#ifdef IP_RECVDSTADDR
    if (setsockopt_on(fd, IPPROTO_IP, IP_RECVDSTADDR) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for pktinfo failed");
      goto Lerror;
    }
#ifdef IP_MTU_DISCOVER
    int probe = IP_PMTUDISC_PROBE; // Set DF but ignore Path MTU
    if (safe_setsockopt(fd, IPPROTO_IP, IP_MTU_DISCOVER, &probe, sizeof(probe)) == -1) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for IP_MTU_DISCOVER failed");
    }
#endif
  } else if (addr->sa_family == AF_INET6) {
    bool succeeded = false;
#ifdef IPV6_PKTINFO
    if (setsockopt_on(fd, IPPROTO_IPV6, IPV6_PKTINFO) == 0) {
      succeeded = true;
    }
#endif
#ifdef IPV6_RECVPKTINFO
    if (setsockopt_on(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO) == 0) {
      succeeded = true;
    }
#endif
    if (!succeeded) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for pktinfo failed");
      goto Lerror;
    }
#ifdef IPV6_MTU_DISCOVER
    int probe = IPV6_PMTUDISC_PROBE; // Set DF but ignore Path MTU
    if (safe_setsockopt(fd, IPPROTO_IPV6, IPV6_MTU_DISCOVER, &probe, sizeof(probe)) == -1) {
      Dbg(dbg_ctl_udpnet, "setsockeopt for IPV6_MTU_DISCOVER failed");
    }
#endif
  }

#ifdef SOL_UDP
  if (G_udp_config.enable_gro) {
    int gro = 1;
    if (safe_setsockopt(fd, IPPROTO_UDP, UDP_GRO, (char *)&gro, sizeof(gro)) == -1) {
      Dbg(dbg_ctl_udpnet, "setsockopt UDP_GRO. errno=%d", errno);
    }
  }
#endif

#ifdef HAVE_SO_TXTIME
  struct sock_txtime sk_txtime;

  sk_txtime.clockid = CLOCK_MONOTONIC;
  sk_txtime.flags   = 0;
  if (setsockopt(fd, SOL_SOCKET, SO_TXTIME, &sk_txtime, sizeof(sk_txtime)) == -1) {
    Dbg(dbg_ctl_udpnet, "Failed to setsockopt SO_TXTIME. errno=%d", errno);
  }
#endif
  // If this is a class D address (i.e. multicast address), use REUSEADDR.
  if (ats_is_ip_multicast(addr)) {
    if (setsockopt_on(fd, SOL_SOCKET, SO_REUSEADDR) < 0) {
      goto Lerror;
    }
  }

  if (need_bind && ats_is_ip6(addr) && setsockopt_on(fd, IPPROTO_IPV6, IPV6_V6ONLY) < 0) {
    goto Lerror;
  }

  if (setsockopt_on(fd, SOL_SOCKET, SO_REUSEPORT) < 0) {
    Dbg(dbg_ctl_udpnet, "setsockopt for SO_REUSEPORT failed");
    goto Lerror;
  }

#ifdef SO_REUSEPORT_LB
  if (setsockopt_on(fd, SOL_SOCKET, SO_REUSEPORT_LB) < 0) {
    Dbg(dbg_ctl_udpnet, "setsockopt for SO_REUSEPORT_LB failed");
    goto Lerror;
  }
#endif

  if (need_bind && (SocketManager::ink_bind(fd, addr, ats_ip_size(addr)) < 0)) {
    Dbg(dbg_ctl_udpnet, "ink_bind failed");
    goto Lerror;
  }

  // check this for GRO
  if (recv_bufsize) {
    if (unlikely(SocketManager::set_rcvbuf_size(fd, recv_bufsize))) {
      Dbg(dbg_ctl_udpnet, "set_dnsbuf_size(%d) failed", recv_bufsize);
    }
  }
  if (send_bufsize) {
    if (unlikely(SocketManager::set_sndbuf_size(fd, send_bufsize))) {
      Dbg(dbg_ctl_udpnet, "set_dnsbuf_size(%d) failed", send_bufsize);
    }
  }
  if (safe_getsockname(fd, &myaddr.sa, &myaddr_len) < 0) {
    goto Lerror;
  }
  n = new UnixUDPConnection(fd);

  Dbg(dbg_ctl_udpnet, "UDPNetProcessor::UDPBind: %p fd=%d", n, fd);
  n->setBinding(&myaddr.sa);
  n->bindToThread(cont, cont->getThreadAffinity());

  pc = get_UDPPollCont(n->ethread);
  pd = pc->pollDescriptor;

  n->ep.start(pd, n, get_UDPNetHandler(cont->getThreadAffinity()), EVENTIO_READ);

  cont->handleEvent(NET_EVENT_DATAGRAM_OPEN, n);
  return ACTION_RESULT_DONE;
Lerror:
  if (fd != NO_FD) {
    SocketManager::close(fd);
  }
  Dbg(dbg_ctl_udpnet, "Error: %s (%d)", strerror(errno), errno);

  cont->handleEvent(NET_EVENT_DATAGRAM_ERROR, nullptr);
  return ACTION_IO_ERROR;
}

// send out all packets that need to be sent out as of time=now
#ifdef SOL_UDP
UDPQueue::UDPQueue(bool enable_gso) : use_udp_gso(enable_gso) {}
#else
UDPQueue::UDPQueue(bool enable_gso)
{
  if (enable_gso) {
    Warning("Attempted to use UDP GSO per configuration, but it is unavailable");
  }
}
#endif

UDPQueue::~UDPQueue() {}

/*
 * Driver function that aggregates packets across cont's and sends them
 */
void
UDPQueue::service(UDPNetHandler *nh)
{
  (void)nh;
  ink_hrtime now       = ink_get_hrtime();
  uint64_t   timeSpent = 0;
  uint64_t   pktSendStartTime;
  ink_hrtime pktSendTime;
  UDPPacket *p = nullptr;

  SList(UDPPacket, alink) aq(outQueue.popall());
  Queue<UDPPacket> stk;
  while ((p = aq.pop())) {
    stk.push(p);
  }

  // walk backwards down list since this is actually an atomic stack.
  while ((p = stk.pop())) {
    ink_assert(p->link.prev == nullptr);
    ink_assert(p->link.next == nullptr);
    // insert into our queue.
    Dbg(dbg_ctl_udp_send, "Adding %p", p);
    if (p->p.conn->lastPktStartTime == 0) {
      pktSendStartTime = std::max(now, p->p.delivery_time);
    } else {
      pktSendTime      = p->p.delivery_time;
      pktSendStartTime = std::max(std::max(now, pktSendTime), p->p.delivery_time);
    }
    p->p.conn->lastPktStartTime = pktSendStartTime;
    p->p.delivery_time          = pktSendStartTime;

    pipeInfo.addPacket(p, now);
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
  UDPPacket        *p;
  static ink_hrtime lastCleanupTime     = ink_get_hrtime();
  ink_hrtime        now                 = ink_get_hrtime();
  ink_hrtime        send_threshold_time = now + SLOT_TIME;
  int32_t           bytesThisSlot = INT_MAX, bytesUsed = 0;
  int32_t           bytesThisPipe;
  int64_t           pktLen;

  bytesThisSlot = INT_MAX;

#ifdef UIO_MAXIOV
  constexpr int N_MAX_PACKETS = UIO_MAXIOV; // The limit comes from sendmmsg
#else
  constexpr int N_MAX_PACKETS = 1024;
#endif
  UDPPacket *packets[N_MAX_PACKETS];
  int        nsent;
  int        npackets;

sendPackets:
  nsent         = 0;
  npackets      = 0;
  bytesThisPipe = bytesThisSlot;

  while ((bytesThisPipe > 0) && (pipeInfo.firstPacket(send_threshold_time))) {
    p      = pipeInfo.getFirstPacket();
    pktLen = p->getPktLength();

    if (p->p.conn->shouldDestroy()) {
      goto next_pkt;
    }
    if (p->p.conn->GetSendGenerationNumber() != p->p.reqGenerationNum) {
      goto next_pkt;
    }

    bytesUsed           += pktLen;
    bytesThisPipe       -= pktLen;
    packets[npackets++]  = p;
  next_pkt:
    if (bytesThisPipe < 0 || npackets == N_MAX_PACKETS) {
      break;
    }
  }

  if (npackets > 0) {
    nsent = SendMultipleUDPPackets(packets, npackets);
  }
  for (int i = 0; i < nsent; ++i) {
    packets[i]->free();
  }

  bytesThisSlot -= bytesUsed;

  if ((bytesThisSlot > 0) && nsent) {
    // redistribute the slack...
    now = ink_get_hrtime();
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
UDPQueue::SendUDPPacket(UDPPacket *p)
{
  struct msghdr msg;
  struct iovec  iov[32];
  int           n, count = 0;

  p->p.conn->lastSentPktStartTime = p->p.delivery_time;
  Dbg(dbg_ctl_udp_send, "Sending %p", p);

  msg.msg_control    = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags      = 0;
  msg.msg_name       = reinterpret_cast<caddr_t>(&p->to.sa);
  msg.msg_namelen    = ats_ip_size(p->to);

#if defined(SOL_UDP) || defined(HAVE_SO_TXTIME) // to avoid unused variables compiler warning.
  struct cmsghdr *cm = nullptr;
  uint8_t         msg_ctrl[0
#ifdef SOL_UDP
                   + CMSG_SPACE(sizeof(uint16_t))
#endif
#ifdef HAVE_SO_TXTIME
                   + CMSG_SPACE(sizeof(uint64_t))
#endif
  ];
  memset(msg_ctrl, 0, sizeof(msg_ctrl));
#endif // defined(SOL_UDP) || defined(HAVE_SO_TXTIME)

#ifdef HAVE_SO_TXTIME
  if (p->p.send_at.tv_sec > 0) {
    msg.msg_control    = msg_ctrl;
    msg.msg_controllen = CMSG_SPACE(sizeof(uint64_t));
    cm                 = CMSG_FIRSTHDR(&msg);

    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type  = SCM_TXTIME;
    cm->cmsg_len   = CMSG_LEN(sizeof(uint64_t));

    // Convert struct timespec to nanoseconds.
    *((uint64_t *)CMSG_DATA(cm)) = p->p.send_at.tv_sec * (1000ULL * 1000 * 1000) + p->p.send_at.tv_nsec;
  }
#endif

  if (p->p.segment_size > 0) {
    ink_assert(p->p.chain->next == nullptr);
    msg.msg_iov    = iov;
    msg.msg_iovlen = 1;
#ifdef SOL_UDP
    if (use_udp_gso) {
      iov[0].iov_base = p->p.chain.get()->start();
      iov[0].iov_len  = p->p.chain.get()->size();

      union udp_segment_hdr {
        char           buf[CMSG_SPACE(sizeof(uint16_t))];
        struct cmsghdr align;
      } u;

      if (cm == nullptr) {
        msg.msg_control    = msg_ctrl;
        msg.msg_controllen = sizeof(u.buf);
        cm                 = CMSG_FIRSTHDR(&msg);

      } else {
        msg.msg_controllen += sizeof(u.buf);
        cm                  = CMSG_NXTHDR(&msg, cm);
      }
      cm->cmsg_level               = SOL_UDP;
      cm->cmsg_type                = UDP_SEGMENT;
      cm->cmsg_len                 = CMSG_LEN(sizeof(uint16_t));
      *((uint16_t *)CMSG_DATA(cm)) = p->p.segment_size;

      count = 0;
      while (true) {
        // stupid Linux problem: sendmsg can return EAGAIN
        n = ::sendmsg(p->p.conn->getFd(), &msg, 0);
        if (n >= 0) {
          break;
        }
        if (errno == EIO && use_udp_gso) {
          Warning("Disabling UDP GSO due to an error");
          use_udp_gso = false;
          SendUDPPacket(p);
          return;
        }
        if (errno == EAGAIN) {
          ++count;
          if ((g_udp_numSendRetries > 0) && (count >= g_udp_numSendRetries)) {
            // tried too many times; give up
            Dbg(dbg_ctl_udpnet, "Send failed: too many retries");
            return;
          }
        } else {
          Dbg(dbg_ctl_udp_send, "Error: %s (%d)", strerror(errno), errno);
          return;
        }
      }
    } else {
#endif
      // Send segments seprately if UDP_SEGMENT is not supported
      int offset = 0;
      while (offset < p->p.chain.get()->size()) {
        iov[0].iov_base = p->p.chain.get()->start() + offset;
        iov[0].iov_len =
          std::min(static_cast<long>(p->p.segment_size), p->p.chain.get()->end() - static_cast<char *>(iov[0].iov_base));

        count = 0;
        while (true) {
          // stupid Linux problem: sendmsg can return EAGAIN
          n = ::sendmsg(p->p.conn->getFd(), &msg, 0);
          if (n >= 0) {
            break;
          }
          if (errno == EAGAIN) {
            ++count;
            if ((g_udp_numSendRetries > 0) && (count >= g_udp_numSendRetries)) {
              // tried too many times; give up
              Dbg(dbg_ctl_udpnet, "Send failed: too many retries");
              return;
            }
          } else {
            Dbg(dbg_ctl_udp_send, "Error: %s (%d)", strerror(errno), errno);
            return;
          }
        }

        offset += iov[0].iov_len;
      }
      ink_assert(offset == p->p.chain.get()->size());
#ifdef SOL_UDP
    } // use_udp_segment
#endif
  } else {
    // Nothing is special
    int iov_len = 0;
    for (IOBufferBlock *b = p->p.chain.get(); b != nullptr; b = b->next.get()) {
      iov[iov_len].iov_base = static_cast<caddr_t>(b->start());
      iov[iov_len].iov_len  = b->size();
      iov_len++;
    }
    msg.msg_iov    = iov;
    msg.msg_iovlen = iov_len;

    count = 0;
    while (true) {
      // stupid Linux problem: sendmsg can return EAGAIN
      n = ::sendmsg(p->p.conn->getFd(), &msg, 0);
      if ((n >= 0) || (errno != EAGAIN)) {
        // send succeeded or some random error happened.
        if (n < 0) {
          Dbg(dbg_ctl_udp_send, "Error: %s (%d)", strerror(errno), errno);
        }

        break;
      }
      if (errno == EAGAIN) {
        ++count;
        if ((g_udp_numSendRetries > 0) && (count >= g_udp_numSendRetries)) {
          // tried too many times; give up
          Dbg(dbg_ctl_udpnet, "Send failed: too many retries");
          break;
        }
      }
    }
  }
}

void
UDPQueue::send(UDPPacket *p)
{
  // XXX: maybe fastpath for immediate send?
  outQueue.push(p);
}

int
UDPQueue::SendMultipleUDPPackets(UDPPacket **p, uint16_t n)
{
#ifdef HAVE_SENDMMSG
  struct mmsghdr *msgvec;
  int             msgvec_size;

#ifdef SOL_UDP
  union udp_segment_hdr {
    char           buf[CMSG_SPACE(sizeof(uint16_t))];
    struct cmsghdr align;
  };
  if (use_udp_gso) {
    msgvec_size = sizeof(struct mmsghdr) * n;
  } else {
    msgvec_size = sizeof(struct mmsghdr) * n * 64;
  }

#else
  msgvec_size = sizeof(struct mmsghdr) * n * 64;
#endif

#if defined(SOL_UDP) || defined(HAVE_SO_TXTIME) // to avoid unused variables compiler warning.
  uint8_t msg_ctrl[0
#ifdef SOL_UDP
                   + CMSG_SPACE(sizeof(uint16_t))
#endif
#ifdef HAVE_SO_TXTIME
                   + CMSG_SPACE(sizeof(uint64_t))
#endif
  ];
  memset(msg_ctrl, 0, sizeof(msg_ctrl));
#endif // defined(SOL_UDP) || defined(HAVE_SO_TXTIME)

  // The sizeof(struct msghdr) is 56 bytes or so. It can be too big to stack (alloca).
  IOBufferBlock *tmp = new_IOBufferBlock();
  tmp->alloc(iobuffer_size_to_index(msgvec_size, BUFFER_SIZE_INDEX_1M));
  msgvec = reinterpret_cast<struct mmsghdr *>(tmp->buf());
  memset(msgvec, 0, msgvec_size);

  // The sizeof(struct iove) is 16 bytes or so. It can be too big to stack (alloca).
  int            iovec_size = sizeof(struct iovec) * n * 64;
  IOBufferBlock *tmp2       = new_IOBufferBlock();
  tmp2->alloc(iobuffer_size_to_index(iovec_size, BUFFER_SIZE_INDEX_1M));
  struct iovec *iovec = reinterpret_cast<struct iovec *>(tmp2->buf());
  memset(iovec, 0, iovec_size);
  int iovec_used = 0;

  int vlen = 0;
  int fd   = p[0]->p.conn->getFd();
  for (int i = 0; i < n; ++i) {
    UDPPacket     *packet;
    struct msghdr *msg;
    struct iovec  *iov;
    int            iov_len;

    packet                               = p[i];
    packet->p.conn->lastSentPktStartTime = packet->p.delivery_time;
    ink_assert(packet->p.conn->getFd() == fd);
#if defined(SOL_UDP) || defined(HAVE_SO_TXTIME)
    struct cmsghdr *cm = nullptr;
#endif
#ifdef HAVE_SO_TXTIME
    if (packet->p.send_at.tv_sec > 0) { // if set?
      msg                 = &msgvec[vlen].msg_hdr;
      msg->msg_controllen = CMSG_SPACE(sizeof(uint64_t));
      msg->msg_control    = msg_ctrl;
      cm                  = CMSG_FIRSTHDR(msg);

      cm->cmsg_level = SOL_SOCKET;
      cm->cmsg_type  = SCM_TXTIME;
      cm->cmsg_len   = CMSG_LEN(sizeof(uint64_t));

      // Convert struct timespec to nanoseconds.
      *((uint64_t *)CMSG_DATA(cm)) = packet->p.send_at.tv_sec * (1000ULL * 1000 * 1000) + packet->p.send_at.tv_nsec;
      ;
    }
#endif

    if (packet->p.segment_size > 0) {
      // Presumes one big super buffer is given
      ink_assert(packet->p.chain->next == nullptr);
#ifdef SOL_UDP
      if (use_udp_gso) {
        msg              = &msgvec[vlen].msg_hdr;
        msg->msg_name    = reinterpret_cast<caddr_t>(&packet->to.sa);
        msg->msg_namelen = ats_ip_size(packet->to);

        union udp_segment_hdr *u;
        u = static_cast<union udp_segment_hdr *>(alloca(sizeof(union udp_segment_hdr)));

        iov             = &iovec[iovec_used++];
        iov_len         = 1;
        iov->iov_base   = packet->p.chain.get()->start();
        iov->iov_len    = packet->p.chain.get()->size();
        msg->msg_iov    = iov;
        msg->msg_iovlen = iov_len;

        if (cm == nullptr) {
          msg->msg_control    = u->buf;
          msg->msg_controllen = sizeof(u->buf);
          cm                  = CMSG_FIRSTHDR(msg);
        } else {
          msg->msg_controllen += sizeof(u->buf);
          cm                   = CMSG_NXTHDR(msg, cm);
        }

        cm->cmsg_level               = SOL_UDP;
        cm->cmsg_type                = UDP_SEGMENT;
        cm->cmsg_len                 = CMSG_LEN(sizeof(uint16_t));
        *((uint16_t *)CMSG_DATA(cm)) = packet->p.segment_size;
        vlen++;
      } else {
#endif
        // UDP_SEGMENT is unavailable
        // Send the given data as multiple messages
        int offset = 0;
        while (offset < packet->p.chain.get()->size()) {
          msg               = &msgvec[vlen].msg_hdr;
          msg->msg_name     = reinterpret_cast<caddr_t>(&packet->to.sa);
          msg->msg_namelen  = ats_ip_size(packet->to);
          iov               = &iovec[iovec_used++];
          iov_len           = 1;
          iov->iov_base     = packet->p.chain.get()->start() + offset;
          iov->iov_len      = std::min(packet->p.segment_size,
                                       static_cast<uint16_t>(packet->p.chain.get()->end() - static_cast<char *>(iov->iov_base)));
          msg->msg_iov      = iov;
          msg->msg_iovlen   = iov_len;
          offset           += iov->iov_len;
          vlen++;
        }
        ink_assert(offset == packet->p.chain.get()->size());
#ifdef SOL_UDP
      } // use_udp_gso
#endif
    } else {
      // Nothing is special
      msg              = &msgvec[vlen].msg_hdr;
      msg->msg_name    = reinterpret_cast<caddr_t>(&packet->to.sa);
      msg->msg_namelen = ats_ip_size(packet->to);
      iov              = &iovec[iovec_used++];
      iov_len          = 0;
      for (IOBufferBlock *b = packet->p.chain.get(); b != nullptr; b = b->next.get()) {
        iov[iov_len].iov_base = static_cast<caddr_t>(b->start());
        iov[iov_len].iov_len  = b->size();
        iov_len++;
      }
      msg->msg_iov    = iov;
      msg->msg_iovlen = iov_len;
      vlen++;
    }
  }

  if (vlen == 0) {
    return 0;
  }

  int res = ::sendmmsg(fd, msgvec, vlen, 0);
  if (res < 0) {
#ifdef SOL_UDP
    if (use_udp_gso && errno == EIO) {
      Warning("Disabling UDP GSO due to an error");
      Dbg(dbg_ctl_udp_send, "Disabling UDP GSO due to an error");
      use_udp_gso = false;
      return SendMultipleUDPPackets(p, n);
    } else {
      Dbg(dbg_ctl_udp_send, "udp_gso=%d res=%d errno=%d", use_udp_gso, res, errno);
      return res;
    }
#else
    Dbg(dbg_ctl_udp_send, "res=%d errno=%d", res, errno);
    return res;
#endif
  }

  if (res > 0) {
#ifdef SOL_UDP
    if (use_udp_gso) {
      ink_assert(res <= n);
      Dbg(dbg_ctl_udp_send, "Sent %d messages by processing %d UDPPackets (GSO)", res, n);
    } else {
#endif
      int i    = 0;
      int nmsg = res;
      for (i = 0; i < n && res > 0; ++i) {
        if (p[i]->p.segment_size == 0) {
          res -= 1;
        } else {
          res -= (p[i]->p.chain.get()->size() / p[i]->p.segment_size) + ((p[i]->p.chain.get()->size() % p[i]->p.segment_size) != 0);
        }
      }
      Dbg(dbg_ctl_udp_send, "Sent %d messages by processing %d UDPPackets", nmsg, i);
      res = i;
#ifdef SOL_UDP
    }
#endif
  }
  tmp->free();
  tmp2->free();

  return res;
#else
  // sendmmsg is unavailable
  for (int i = 0; i < n; ++i) {
    SendUDPPacket(p[i]);
  }
  return n;
#endif
}

#undef LINK

UDPNetHandler::UDPNetHandler(Cfg &&cfg) : udpOutQueue(cfg.enable_gso), _cfg{std::move(cfg)}
{
  nextCheck = ink_get_hrtime() + HRTIME_MSECONDS(1000);
  lastCheck = 0;
  SET_HANDLER(&UDPNetHandler::startNetEvent);
}

bool
UDPNetHandler::is_gro_enabled() const
{
#ifndef SOL_UDP
  return false;
#else
  return this->_cfg.enable_gro;
#endif
}

int
UDPNetHandler::startNetEvent(int event, Event *e)
{
  (void)event;
  SET_HANDLER(&UDPNetHandler::mainNetEvent);
  trigger_event = e;
  e->schedule_every(-HRTIME_MSECONDS(UDP_NH_PERIOD));
  return EVENT_CONT;
}

int
UDPNetHandler::mainNetEvent(int event, Event *e)
{
  ink_assert(trigger_event == e && event == EVENT_POLL);
  return this->waitForActivity(EThread::default_wait_interval_ms);
}

int
UDPNetHandler::waitForActivity(ink_hrtime timeout)
{
  UnixUDPConnection *uc;
  PollCont          *pc = get_UDPPollCont(this->thread);
  pc->do_poll(timeout);

  /* Notice: the race between traversal of newconn_list and UDPBind()
   *
   * If the UDPBind() is called after the traversal of newconn_list,
   * the UDPConnection, the one from the pollDescriptor->result, did not push into the open_list.
   *
   * TODO:
   *
   * Take UnixNetVConnection::acceptEvent() as reference to create UnixUDPConnection::newconnEvent().
   */

  // handle new UDP connection
  SList(UnixUDPConnection, newconn_alink) ncq(newconn_list.popall());
  while ((uc = ncq.pop())) {
    if (uc->shouldDestroy()) {
      open_list.remove(uc); // due to the above race
      uc->Release();
    } else {
      ink_assert(uc->mutex && uc->continuation);
      open_list.in_or_enqueue(uc); // due to the above race
    }
  }

  // handle UDP outgoing engine
  udpOutQueue.service(this);

  // handle UDP read operations
  int      i   = 0;
  EventIO *epd = nullptr;
  for (i = 0; i < pc->pollDescriptor->result; i++) {
    epd                                = static_cast<EventIO *> get_ev_data(pc->pollDescriptor, i);
    int                          flags = get_ev_events(pc->pollDescriptor, i);
    epd->process_event(flags);
  } // end for

  // remove dead UDP connections
  ink_hrtime now = ink_get_hrtime();
  if (now >= nextCheck) {
    forl_LL(UnixUDPConnection, xuc, open_list)
    {
      ink_assert(xuc->mutex && xuc->continuation);
      ink_assert(xuc->refcount >= 1);
      if (xuc->shouldDestroy()) {
        open_list.remove(xuc);
        xuc->Release();
      }
    }
    nextCheck = ink_get_hrtime() + HRTIME_MSECONDS(1000);
  }
  // service UDPConnections with data ready for callback.
  Que(UnixUDPConnection, callback_link) q = udp_callbacks;
  udp_callbacks.clear();
  while ((uc = q.dequeue())) {
    ink_assert(uc->mutex && uc->continuation);
    if (udpNetInternal.udp_callback(this, uc, this->thread)) { // not successful
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

void
UDPNetHandler::signalActivity()
{
#if HAVE_EVENTFD
  uint64_t counter = 1;
  ATS_UNUSED_RETURN(write(thread->evfd, &counter, sizeof(uint64_t)));
#else
  char dummy = 1;
  ATS_UNUSED_RETURN(write(thread->evpipe[1], &dummy, 1));
#endif
}
