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

ClassAllocator<QUICPollEvent> quicPollEventAllocator("quicPollEvent");

void
QUICPollEvent::init(QUICConnection *con, UDPPacketInternal *p)
{
  if (con != nullptr) {
    con->refcount_inc();
  }

  this->con    = con;
  this->packet = p;
}

void
QUICPollEvent::free()
{
  if (this->con != nullptr) {
    this->con->refcount_dec();
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

QUICPollCont::~QUICPollCont()
{
}

void
QUICPollCont::_process_long_header_packet(QUICPollEvent *e, NetHandler *nh)
{
  uint8_t *buf;
  QUICPacketType ptype;
  UDPPacketInternal *p = e->packet;
  // FIXME: VC is nullptr ?
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(e->con);
  buf                    = (uint8_t *)p->getIOBlockChain()->buf();

  if (!QUICTypeUtil::has_connection_id(reinterpret_cast<const uint8_t *>(buf))) {
    // TODO: Some packets may not have connection id
    p->free();
    e->free();
    return;
  }

  ptype = static_cast<QUICPacketType>(buf[0] & 0x7f);
  switch (ptype) {
  case QUICPacketType::INITIAL:
    vc->read.triggered = 1;
    vc->handle_received_packet(p);
    this->mutex->thread_holding->schedule_imm(vc);
    e->free();
    return;
  case QUICPacketType::ZERO_RTT_PROTECTED:
  // TODO:: do something ?
  // break;
  case QUICPacketType::HANDSHAKE:
  default:
    // Just Pass Through
    if (vc) {
      vc->read.triggered = 1;
      vc->handle_received_packet(p);
    } else {
      longInQueue.push(p);
    }

    // Push QUICNetVC into nethandler's enabled list
    if (vc != nullptr) {
      int isin = ink_atomic_swap(&vc->read.in_enabled_list, 1);
      if (!isin) {
        nh->read_enable_list.push(vc);
      }
    }
    break;
  }
  // Note: We should free QUICPollEvent here to decrease the refcount
  // since vc could be freed from other thread when refcount == 0;
  e->free();
}

void
QUICPollCont::_process_short_header_packet(QUICPollEvent *e, NetHandler *nh)
{
  ink_release_assert(e->con != nullptr);
  uint8_t *buf;
  UDPPacketInternal *p   = e->packet;
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(e->con);
  buf                    = (uint8_t *)p->getIOBlockChain()->buf();

  if (!QUICTypeUtil::has_connection_id(reinterpret_cast<const uint8_t *>(buf))) {
    // TODO: Some packets may not have connection id
    p->free();
    e->free();
    return;
  }

  vc->read.triggered = 1;
  vc->handle_received_packet(p);

  // Push QUICNetVC into nethandler's enabled list
  int isin = ink_atomic_swap(&vc->read.in_enabled_list, 1);
  if (!isin) {
    nh->read_enable_list.push(vc);
  }

  // Note: We should free QUICPollEvent here to decrease the refcount
  // since vc could be freed from other thread.
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
    QUICConnection *qc   = e->con;
    UDPPacketInternal *p = e->packet;
    if (qc != nullptr && qc->in_closed_queue) {
      // A server that discards a packet that cannot be associated with a connection MAY also generate a stateless reset (Section
      // 7.9.4).
      // Note: Bad Bad, we can't send stateless reset since QUICPollCont don't have packet handler.
      // TODO: should add a statistic here;
      p->free();
      e->free();
      continue;
    }
    result.push(e);
  }

  while ((e = result.pop())) {
    buf = (uint8_t *)e->packet->getIOBlockChain()->buf();
    if (QUICTypeUtil::has_long_header(buf)) { // Long Header Packet with Connection ID, has a valid type value.
      this->_process_long_header_packet(e, nh);
    } else { // Short Header Packet with Connection ID, has a valid type value.
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

  new ((ink_dummy_for_new *)quicpc) QUICPollCont(thread->mutex, nh);

  thread->schedule_every(quicpc, -HRTIME_MSECONDS(UDP_PERIOD));
}
