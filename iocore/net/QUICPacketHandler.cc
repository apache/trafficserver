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

#include "QUICConfig.h"
#include "QUICPacket.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"

//
// QUICPacketHandler
//
void
QUICPacketHandler::_send_packet(Continuation *c, const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu)
{
  size_t udp_len;
  Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
  udp_payload->alloc(iobuffer_size_to_index(pmtu));
  packet.store(reinterpret_cast<uint8_t *>(udp_payload->end()), &udp_len);
  udp_payload->fill(udp_len);

  UDPPacket *udp_packet = new_UDPPacket(addr, 0, udp_payload);

  // NOTE: p will be enqueued to udpOutQueue of UDPNetHandler
  ip_port_text_buffer ipb;
  Debug("quic_sec", "[%" PRIx64 "] send %s packet to %s, size=%" PRId64, static_cast<uint64_t>(packet.connection_id()),
        QUICDebugNames::packet_type(packet.type()), ats_ip_nptop(&udp_packet->to.sa, ipb, sizeof(ipb)), udp_packet->getPktLength());

  udp_con->send(c, udp_packet);
}

QUICConnectionId
QUICPacketHandler::_read_connection_id(IOBufferBlock *block)
{
  const uint8_t *buf = reinterpret_cast<const uint8_t *>(block->buf());
  return QUICPacket::connection_id(buf);
}

//
// QUICPacketHandlerIn
//
QUICPacketHandlerIn::QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, SSL_CTX *ctx) : NetAccept(opt), _ssl_ctx(ctx)
{
  this->mutex = new_ProxyMutex();
}

QUICPacketHandlerIn::~QUICPacketHandlerIn()
{
}

NetProcessor *
QUICPacketHandlerIn::getNetProcessor() const
{
  return &quic_NetProcessor;
}

NetAccept *
QUICPacketHandlerIn::clone() const
{
  NetAccept *na;
  na  = new QUICPacketHandlerIn(opt, this->_ssl_ctx);
  *na = *this;
  return na;
}

int
QUICPacketHandlerIn::acceptEvent(int event, void *data)
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
    while ((packet_r = queue->dequeue())) {
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
QUICPacketHandlerIn::init_accept(EThread *t = nullptr)
{
  SET_HANDLER(&QUICPacketHandlerIn::acceptEvent);
}

void
QUICPacketHandlerIn::_recv_packet(int event, UDPPacket *udp_packet)
{
  IOBufferBlock *block = udp_packet->getIOBlockChain();

  QUICConnectionId cid;
  if (QUICTypeUtil::has_connection_id(reinterpret_cast<const uint8_t *>(block->buf()))) {
    cid = this->_read_connection_id(block);
  } else {
    // TODO: find cid from five tuples
    ink_assert(false);
  }

  ip_port_text_buffer ipb;
  Debug("quic_sec", "[%" PRIx64 "] received packet from %s, size=%" PRId64, static_cast<uint64_t>(cid),
        ats_ip_nptop(&udp_packet->from.sa, ipb, sizeof(ipb)), udp_packet->getPktLength());

  QUICNetVConnection *vc = nullptr;
  vc                     = this->_connections.get(cid);

  if (!vc) {
    Connection con;
    con.setRemote(&udp_packet->from.sa);

    // Send stateless reset if the packet is not a initial packet
    if (!QUICTypeUtil::has_long_header(reinterpret_cast<const uint8_t *>(block->buf()))) {
      QUICStatelessResetToken token;
      {
        QUICConfig::scoped_config params;
        token.generate(cid, params->server_id());
      }
      auto packet = QUICPacketFactory::create_stateless_reset_packet(cid, token);
      this->_send_packet(this, *packet, udp_packet->getConnection(), con.addr, 1200);
      return;
    }

    // Create a new NetVConnection
    vc = static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(nullptr));
    vc->init(cid, udp_packet->getConnection(), this);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->submit_time = Thread::get_hrtime();
    vc->mutex       = this->mutex;
    vc->action_     = *this->action_;
    vc->set_is_transparent(this->opt.f_inbound_transparent);
    vc->set_context(NET_VCONNECTION_IN);
    vc->read.triggered = 1;
    vc->start(this->_ssl_ctx);
    vc->options.ip_proto  = NetVCOptions::USE_UDP;
    vc->options.ip_family = udp_packet->from.sa.sa_family;

    this->_connections.put(cid, vc);
    this->action_->continuation->handleEvent(NET_EVENT_ACCEPT, vc);
  }

  if (vc->is_closed()) {
    this->_connections.put(vc->connection_id(), nullptr);
    // FIXME QUICNetVConnection is NOT freed to prevent crashes. #2674
    // QUICNetVConnections are going to be freed by QUICNetHandler
    // vc->free(vc->thread);
  } else {
    vc->push_packet(udp_packet);
    eventProcessor.schedule_imm(vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
  }
}

// TODO: Should be called via eventProcessor?
void
QUICPacketHandlerIn::send_packet(const QUICPacket &packet, QUICNetVConnection *vc)
{
  // TODO: remove a connection which is created by Client Initial
  //       or update key to new one
  if (!this->_connections.get(packet.connection_id())) {
    this->_connections.put(packet.connection_id(), vc);
  }

  this->_send_packet(this, packet, vc->get_udp_con(), vc->con.addr, vc->pmtu());
}

void
QUICPacketHandlerIn::registerAltConnectionId(QUICConnectionId id, QUICNetVConnection *vc)
{
  this->_connections.put(id, vc);
}

//
// QUICPacketHandlerOut
//
QUICPacketHandlerOut::QUICPacketHandlerOut() : Continuation(new_ProxyMutex())
{
  SET_HANDLER(&QUICPacketHandlerOut::event_handler);
}

void
QUICPacketHandlerOut::init(QUICNetVConnection *vc)
{
  this->_vc = vc;
}

int
QUICPacketHandlerOut::event_handler(int event, Event *data)
{
  switch (event) {
  case NET_EVENT_DATAGRAM_OPEN: {
    // Nothing to do.
    return EVENT_CONT;
  }
  case NET_EVENT_DATAGRAM_READ_READY: {
    Queue<UDPPacket> *queue = (Queue<UDPPacket> *)data;
    UDPPacket *packet_r;
    while ((packet_r = queue->dequeue())) {
      this->_recv_packet(event, packet_r);
    }
    return EVENT_CONT;
  }
  default:
    Debug("quic_ph", "Unknown Event (%d)", event);

    break;
  }

  return EVENT_DONE;
}

void
QUICPacketHandlerOut::send_packet(const QUICPacket &packet, QUICNetVConnection *vc)
{
  this->_send_packet(this, packet, vc->get_udp_con(), vc->con.addr, vc->pmtu());
}

void
QUICPacketHandlerOut::registerAltConnectionId(QUICConnectionId id, QUICNetVConnection *vc)
{
  // do nothing
  ink_assert(false);
}

void
QUICPacketHandlerOut::_recv_packet(int event, UDPPacket *udp_packet)
{
  IOBufferBlock *block = udp_packet->getIOBlockChain();

  QUICConnectionId cid = this->_read_connection_id(block);

  ip_port_text_buffer ipb;
  Debug("quic_sec", "[%" PRIx64 "] received packet from %s, size=%" PRId64, static_cast<uint64_t>(cid),
        ats_ip_nptop(&udp_packet->from.sa, ipb, sizeof(ipb)), udp_packet->getPktLength());

  this->_vc->push_packet(udp_packet);
  eventProcessor.schedule_imm(this->_vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
}
