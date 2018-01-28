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

QUICPollCont::QUICPollCont(Ptr<ProxyMutex> &m)
  : Continuation(m.get()), net_handler(nullptr)
{
  SET_HANDLER(&PollCont::pollEvent);
}

QUICPollCont::QUICPollCont(Ptr<ProxyMutex> &m, NetHandler *nh)
  : Continuation(m.get()), net_handler(nh)
{
  SET_HANDLER(&QUICPollCont::pollEvent);
}

QUICPollCont::~QUICPollCont()
{
}

//
// QUICPollCont continuation which traverse the inQueue(ASLL)
// and create new QUICNetVC for Initial Packet,
// and push the triggered QUICNetVC into enable list.
//
int
QUICPollCont::pollEvent(int, Event *)
{
  UnixUDPConnection *uc;
  QUICPacketHandler *ph;
  QUICNetVConnection *vc;
  QUICConnectionId cid;
  uint8_t *buf;
  uint8_t ptype;
  UDPPacket *packet_r;
  UDPPacketInternal *p = nullptr;
  NetHandler *nh       = get_NetHandler(t);

  // Process the ASLL
  SList(UDPPacketInternal, alink) aq(inQueue.popall());
  Queue<UDPPacketInternal> result;
  while ((p = aq.pop())) {
    result.push(p);
  }

  while ((p = result.pop())) {
    uc  = static_cast<UnixUDPConnection *>(p->getConnection());
    ph  = static_cast<QUICPacketHandler *>(uc->continuation);
    vc  = static_cast<QUICNetVConnection *>(p->data.ptr);
    buf = (uint8_t *)p->getIOBlockChain()->buf();
    cid = QUICPacket::connection_id(buf)
    if (buf[0] & 0x80) { // Long Header Packet with Connection ID, has a valid type value.
      ptype = buf[0] & 0x7f;
      if (ptype == QUICPacketType::INITIAL) { // Initial Packet
        vc->read.triggered = 1;
        vc->push_packet(p);
        // reschedule the vc and callback vc->acceptEvent
        this_ethread()->schedule_imm(vc);
      } elseif (ptype == QUICPacketType::ZERO_RTT_PROTECTED) { // 0-RTT Packet
        // TODO:
      } elseif (ptype == QUICPacketType::HANDSHAKE) { // Handshake Packet
        if (vc) {
          vc->read.triggered = 1;
          vc->push_packet(p);
        } else {
          longInQueue.push(p);
        }
      } else {
        ink_assert(!"not reached!");
      }
    } elseif (buf[0] & 0x40) { // Short Header Packet with Connection ID, has a valid type value.
      if (vc) {
        vc->read.triggered = 1;
        vc->push_packet(p);
      } else {
        shortInQueue.push(p);
      }
    } else {
      ink_assert(!"not reached!");
    }

    // Push QUICNetVC into nethandler's enabled list
    if (vc != nullptr) {
      int isin = ink_atomic_swap(&vc->read.in_enabled_list, 1);
      if (!isin) {
        nh->read_enable_list.push(vc);
      }
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

  thread->schedule_every(quicpc, -9);
}

