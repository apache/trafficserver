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
#include "P_QUICNet.h"
#include "quic/QUICEvents.h"

ClassAllocator<QUICPollEvent> quicPollEventAllocator("quicPollEvent");

void
QUICPollEvent::init(QUICConnection *con, UDPPacketInternal *packet)
{
  this->con    = con;
  this->packet = packet;
  if (con != nullptr) {
    static_cast<QUICNetVConnection *>(con)->refcount_inc();
  }
}

void
QUICPollEvent::free()
{
  if (this->con != nullptr) {
    ink_assert(static_cast<QUICNetVConnection *>(this->con)->refcount_dec() >= 0);
    this->con = nullptr;
  }

  quicPollEventAllocator.free(this);
}

QUICPollCont::QUICPollCont(Ptr<ProxyMutex> &m) : Continuation(m.get()), net_handler(nullptr)
{
  SET_HANDLER(&QUICPollCont::pollEvent);
}

QUICPollCont::QUICPollCont(Ptr<ProxyMutex> &m, NetHandler *nh) : Continuation(m.get()), net_handler(nh)
{
  SET_HANDLER(&QUICPollCont::pollEvent);
}

QUICPollCont::~QUICPollCont() {}

void
QUICPollCont::_process_long_header_packet(QUICPollEvent *e, NetHandler *nh)
{
  UDPPacketInternal *p = e->packet;
  // FIXME: VC is nullptr ?
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(e->con);
  uint8_t *buf           = reinterpret_cast<uint8_t *>(p->getIOBlockChain()->buf());

  QUICPacketType ptype;
  QUICLongHeaderPacketR::type(ptype, buf, 1);
  if (ptype == QUICPacketType::INITIAL && !vc->read.triggered) {
    SCOPED_MUTEX_LOCK(lock, vc->mutex, this_ethread());
    vc->read.triggered = 1;
    vc->handle_received_packet(p);
    vc->handleEvent(QUIC_EVENT_PACKET_READ_READY, nullptr);
    e->free();

    return;
  }

  if (vc) {
    SCOPED_MUTEX_LOCK(lock, vc->mutex, this_ethread());
    vc->read.triggered = 1;
    vc->handle_received_packet(p);
  } else {
    this->_longInQueue.push(p);
  }

  // Push QUICNetVC into nethandler's enabled list
  if (vc != nullptr) {
    int isin = ink_atomic_swap(&vc->read.in_enabled_list, 1);
    if (!isin) {
      nh->read_enable_list.push(vc);
    }
  }

  // Note: We should free QUICPollEvent here since vc could be freed from other thread.
  e->free();
}

void
QUICPollCont::_process_short_header_packet(QUICPollEvent *e, NetHandler *nh)
{
  UDPPacketInternal *p   = e->packet;
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(e->con);

  vc->read.triggered = 1;
  vc->handle_received_packet(p);

  // Push QUICNetVC into nethandler's enabled list
  int isin = ink_atomic_swap(&vc->read.in_enabled_list, 1);
  if (!isin) {
    nh->read_enable_list.push(vc);
  }

  // Note: We should free QUICPollEvent here since vc could be freed from other thread.
  e->free();
}

//
// QUICPollCont continuation which traverse the inQueue(ASLL)
// and create new QUICNetVC for Initial Packet,
// and push the triggered QUICNetVC into enable list.
//
int
QUICPollCont::pollEvent(int, Event *)
{
  ink_assert(this->mutex->thread_holding == this_thread());
  uint8_t *buf;
  QUICPollEvent *e;
  NetHandler *nh = get_NetHandler(this->mutex->thread_holding);

  // Process the ASLL
  SList(QUICPollEvent, alink) aq(inQueue.popall());
  Queue<QUICPollEvent> result;
  while ((e = aq.pop())) {
    QUICNetVConnection *qvc = static_cast<QUICNetVConnection *>(e->con);
    UDPPacketInternal *p    = e->packet;
    if (qvc != nullptr && qvc->in_closed_queue) {
      p->free();
      e->free();
      continue;
    }
    result.push(e);
  }

  while ((e = result.pop())) {
    buf = reinterpret_cast<uint8_t *>(e->packet->getIOBlockChain()->buf());
    if (QUICInvariants::is_long_header(buf)) {
      // Long Header Packet with Connection ID, has a valid type value.
      this->_process_long_header_packet(e, nh);
    } else {
      // Short Header Packet with Connection ID, has a valid type value.
      this->_process_short_header_packet(e, nh);
    }
  }

  return EVENT_CONT;
}

void
initialize_thread_for_quic_net(EThread *thread)
{
  NetHandler *nh       = get_NetHandler(thread);
  QUICPollCont *quicpc = get_QUICPollCont(thread);

  new (reinterpret_cast<ink_dummy_for_new *>(quicpc)) QUICPollCont(thread->mutex, nh);

  thread->schedule_every(quicpc, -HRTIME_MSECONDS(UDP_PERIOD));
}
