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

#include "ts/ink_config.h"
#include "P_Net.h"

#include "QUICPacket.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"

QUICPacketHandler::QUICPacketHandler(const NetProcessor::AcceptOptions &opt, SSL_CTX *ctx) : NetAccept(opt), _ssl_ctx(ctx)
{
  this->mutex = new_ProxyMutex();
}

QUICPacketHandler::~QUICPacketHandler()
{
}

NetProcessor *
QUICPacketHandler::getNetProcessor() const
{
  return &quic_NetProcessor;
}

NetAccept *
QUICPacketHandler::clone() const
{
  NetAccept *na;
  na  = new QUICPacketHandler(opt, this->_ssl_ctx);
  *na = *this;
  return na;
}

int
QUICPacketHandler::acceptEvent(int event, void *data)
{
  // NetVConnection *netvc;
  ink_release_assert(event == NET_EVENT_DATAGRAM_OPEN || event == NET_EVENT_DATAGRAM_READ_READY ||
                     event == NET_EVENT_DATAGRAM_ERROR);
  ink_release_assert((event == NET_EVENT_DATAGRAM_OPEN) ? (data != nullptr) : (1));
  ink_release_assert((event == NET_EVENT_DATAGRAM_READ_READY) ? (data != nullptr) : (1));

  if (event == NET_EVENT_DATAGRAM_OPEN) {
    // Nothing to do.
    return EVENT_CONT;
  } else if (event == NET_EVENT_DATAGRAM_READ_READY) {
    Queue<UDPPacket> *queue = (Queue<UDPPacket> *)data;
    UDPPacket *packet_r;
    ip_port_text_buffer ipb;
    while ((packet_r = queue->dequeue())) {
      Debug("quic_sec", "received packet from %s, size=%" PRId64, ats_ip_nptop(&packet_r->from.sa, ipb, sizeof(ipb)),
            packet_r->getPktLength());
      this->_recv_packet(event, packet_r);
    }
    return EVENT_CONT;
  }

  /////////////////
  // EVENT_ERROR //
  /////////////////
  if (((long)data) == -ECONNABORTED) {
  }

  ink_abort("QUIC accept received fatal error: errno = %d", -((int)(intptr_t)data));
  return EVENT_CONT;
  return 0;
}

void
QUICPacketHandler::init_accept(EThread *t = nullptr)
{
  SET_HANDLER(&QUICPacketHandler::acceptEvent);
}

void
QUICPacketHandler::_recv_packet(int event, UDPPacket *udpPacket)
{
  IOBufferBlock *block = udpPacket->getIOBlockChain();

  std::unique_ptr<QUICPacket> qPkt = std::unique_ptr<QUICPacket>(QUICPacketFactory::create(block));
  QUICNetVConnection *vc           = this->_connections.get(qPkt->connection_id());

  if (!vc) {
    // Unknown Connection ID
    Connection con;
    con.setRemote(&udpPacket->from.sa);
    vc =
      static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(((UnixUDPConnection *)udpPacket->getConnection())->ethread));
    vc->init(udpPacket->getConnection(), this);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->submit_time = Thread::get_hrtime();
    vc->mutex       = this->mutex;
    vc->action_     = *this->action_;
    vc->set_is_transparent(this->opt.f_inbound_transparent);
    vc->set_context(NET_VCONNECTION_IN);
    vc->read.triggered = 1;
    vc->start(this->_ssl_ctx);
    // TODO: Handle Connection ID of Client Cleartext / Non-Final Server Cleartext Packet
    this->_connections.put(qPkt->connection_id(), vc);
  }

  vc->push_packet(std::move(qPkt));

  // send to EThread
  eventProcessor.schedule_imm(vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
}

// TODO: Should be called via eventProcessor?
void
QUICPacketHandler::send_packet(const QUICPacket &packet, QUICNetVConnection *vc)
{
  // TODO: remove a connection which is created by Client Initial
  //       or update key to new one
  if (!this->_connections.get(packet.connection_id())) {
    this->_connections.put(packet.connection_id(), vc);
  }

  uint8_t udp_payload[65536];
  size_t udp_len;
  packet.store(udp_payload, &udp_len);
  UDPPacket *udpPkt = new_UDPPacket(vc->con.addr, 0, reinterpret_cast<char *>(udp_payload), udp_len);

  // NOTE: p will be enqueued to udpOutQueue of UDPNetHandler
  ip_port_text_buffer ipb;
  Debug("quic_sec", "send %s packet to %s, size=%" PRId64, QUICDebugNames::packet_type(packet.type()),
        ats_ip_nptop(&udpPkt->to.sa, ipb, sizeof(ipb)), udpPkt->getPktLength());
  vc->get_udp_con()->send(this, udpPkt);
}
