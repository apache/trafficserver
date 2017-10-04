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

// TODO: Integrate with QUICPacketHeader::connection_id()
bool
QUICPacketHandler::_read_connection_id(QUICConnectionId &cid, IOBufferBlock *block)
{
  const uint8_t *buf       = reinterpret_cast<const uint8_t *>(block->buf());
  const uint8_t cid_offset = 1;
  const uint8_t cid_len    = 8;

  if (QUICTypeUtil::hasLongHeader(buf)) {
    cid = QUICTypeUtil::read_QUICConnectionId(buf + cid_offset, cid_len);
  } else {
    if (buf[0] & 0x40) {
      cid = QUICTypeUtil::read_QUICConnectionId(buf + cid_offset, cid_len);
    } else {
      return false;
    }
  }

  return true;
}

void
QUICPacketHandler::_recv_packet(int event, UDPPacket *udpPacket)
{
  IOBufferBlock *block = udpPacket->getIOBlockChain();

  QUICConnectionId cid;
  bool res = this->_read_connection_id(cid, block);

  QUICNetVConnection *vc = nullptr;
  if (res) {
    vc = this->_connections.get(cid);
  } else {
    // TODO: find vc from five tuples
    ink_assert(false);
  }

  if (!vc) {
    Connection con;
    con.setRemote(&udpPacket->from.sa);

    // Send stateless reset if the packet is not a initial packet
    if (!QUICTypeUtil::hasLongHeader(reinterpret_cast<const uint8_t *>(block->buf()))) {
      QUICStatelessToken token;
      {
        QUICConfig::scoped_config params;
        token.gen_token(cid ^ params->server_id());
      }
      auto packet = QUICPacketFactory::create_stateless_reset_packet(cid, token);
      this->send_packet(*packet, udpPacket->getConnection(), con.addr, 1200);
      return;
    }

    // Create a new NetVConnection
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
    vc->options.ip_proto  = NetVCOptions::USE_UDP;
    vc->options.ip_family = udpPacket->from.sa.sa_family;

    this->_connections.put(cid, vc);
    this->action_->continuation->handleEvent(NET_EVENT_ACCEPT, vc);
  }

  QUICPacketUPtr qPkt = QUICPacketFactory::create(block, vc->largest_received_packet_number());
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

  this->send_packet(packet, vc->get_udp_con(), vc->con.addr, vc->pmtu());
}

void
QUICPacketHandler::send_packet(const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu)
{
  size_t udp_len;
  Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
  udp_payload->alloc(iobuffer_size_to_index(pmtu));
  packet.store(reinterpret_cast<uint8_t *>(udp_payload->end()), &udp_len);
  udp_payload->fill(udp_len);

  UDPPacket *udpPkt = new_UDPPacket(addr, 0, udp_payload);

  // NOTE: p will be enqueued to udpOutQueue of UDPNetHandler
  ip_port_text_buffer ipb;
  Debug("quic_sec", "send %s packet to %s, size=%" PRId64, QUICDebugNames::packet_type(packet.type()),
        ats_ip_nptop(&udpPkt->to.sa, ipb, sizeof(ipb)), udpPkt->getPktLength());

  udp_con->send(this, udpPkt);
}

void
QUICPacketHandler::forget(QUICNetVConnection *vc)
{
  this->_connections.put(vc->connection_id(), nullptr);
}
