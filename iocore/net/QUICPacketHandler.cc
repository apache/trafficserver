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

#include "P_QUICClosedConCollector.h"

#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICPacket.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"

static constexpr char tag[] = "quic_sec";

#define QUICDebug(fmt, ...) Debug(tag, fmt, ##__VA_ARGS__)
#define QUICDebugQC(qc, fmt, ...) Debug(tag, "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

// ["local dcid" - "local scid"]
#define QUICDebugDS(dcid, scid, fmt, ...) Debug(tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)

//
// QUICPacketHandler
//
QUICPacketHandler::QUICPacketHandler()
{
  this->_closed_con_collector        = new QUICClosedConCollector;
  this->_closed_con_collector->mutex = new_ProxyMutex();
}

QUICPacketHandler::~QUICPacketHandler()
{
  if (this->_collector_event != nullptr) {
    this->_collector_event->cancel();
    this->_collector_event = nullptr;
  }

  if (this->_closed_con_collector != nullptr) {
    delete this->_closed_con_collector;
    this->_closed_con_collector = nullptr;
  }
}

void
QUICPacketHandler::close_conenction(QUICNetVConnection *conn)
{
  int isin = ink_atomic_swap(&conn->in_closed_queue, 1);
  if (!isin) {
    this->_closed_con_collector->closedQueue.push(conn);
  }
}

void
QUICPacketHandler::_send_packet(Continuation *c, const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu,
                                QUICPacketNumberProtector *pn_protector, int dcil)
{
  size_t udp_len;
  Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
  udp_payload->alloc(iobuffer_size_to_index(pmtu));
  packet.store(reinterpret_cast<uint8_t *>(udp_payload->end()), &udp_len);
  udp_payload->fill(udp_len);

  if (pn_protector) {
    QUICPacket::protect_packet_number(reinterpret_cast<uint8_t *>(udp_payload->start()), udp_len, pn_protector, dcil);
  }

  UDPPacket *udp_packet = new_UDPPacket(addr, 0, udp_payload);

  if (is_debug_tag_set(tag)) {
    ip_port_text_buffer ipb;
    QUICConnectionId dcid = packet.destination_cid();
    QUICConnectionId scid = QUICConnectionId::ZERO();
    if (packet.type() != QUICPacketType::PROTECTED) {
      scid = packet.source_cid();
    }
    QUICDebugDS(dcid, scid, "send %s packet to %s size=%" PRId64, QUICDebugNames::packet_type(packet.type()),
                ats_ip_nptop(&udp_packet->to.sa, ipb, sizeof(ipb)), udp_packet->getPktLength());
  }

  // NOTE: packet will be enqueued to udpOutQueue of UDPNetHandler
  udp_con->send(c, udp_packet);
}

//
// QUICPacketHandlerIn
//
QUICPacketHandlerIn::QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt) : NetAccept(opt)
{
  this->mutex = new_ProxyMutex();
  // create Connection Table
  QUICConfig::scoped_config params;
  _ctable = new QUICConnectionTable(params->connection_table_size());
}

QUICPacketHandlerIn::~QUICPacketHandlerIn()
{
  // TODO: clear all values before destory the table.
  delete _ctable;
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
  na  = new QUICPacketHandlerIn(opt);
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
    if (this->_collector_event == nullptr) {
      this->_collector_event = this_ethread()->schedule_every(this->_closed_con_collector, HRTIME_MSECONDS(100));
    }

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
  // Assumption: udp_packet has only one IOBufferBlock
  IOBufferBlock *block = udp_packet->getIOBlockChain();
  const uint8_t *buf   = reinterpret_cast<uint8_t *>(block->buf());
  uint64_t buf_len     = block->size();

  if (buf_len == 0) {
    QUICDebug("Ignore packet - payload is too small");
    udp_packet->free();
    return;
  }

  QUICConnectionId dcid = QUICConnectionId::ZERO();
  QUICConnectionId scid = QUICConnectionId::ZERO();

  if (!QUICInvariants::dcid(dcid, buf, buf_len)) {
    QUICDebug("Ignore packet - payload is too small");
    udp_packet->free();
    return;
  }

  if (QUICInvariants::is_long_header(buf)) {
    if (!QUICInvariants::scid(scid, buf, buf_len)) {
      QUICDebug("Ignore packet - payload is too small");
      udp_packet->free();
      return;
    }

    if (is_debug_tag_set(tag)) {
      ip_port_text_buffer ipb;
      QUICDebugDS(scid, dcid, "recv LH packet from %s size=%" PRId64, ats_ip_nptop(&udp_packet->from.sa, ipb, sizeof(ipb)),
                  udp_packet->getPktLength());
    }

    QUICVersion v;
    if (unlikely(!QUICInvariants::version(v, buf, buf_len))) {
      QUICDebug("Ignore packet - payload is too small");
      udp_packet->free();
      return;
    }

    if (!QUICInvariants::is_version_negotiation(v) && !QUICTypeUtil::is_supported_version(v)) {
      QUICDebugDS(scid, dcid, "Unsupported version: 0x%x", v);

      QUICPacketUPtr vn = QUICPacketFactory::create_version_negotiation_packet(scid, dcid);
      this->_send_packet(this, *vn, udp_packet->getConnection(), udp_packet->from, 1200, nullptr, 0);
      udp_packet->free();
      return;
    }

    if (dcid == QUICConnectionId::ZERO()) {
      // TODO: lookup DCID by 5-tuple when ATS omits SCID
      return;
    }
  } else {
    // TODO: lookup DCID by 5-tuple when ATS omits SCID
    if (is_debug_tag_set(tag)) {
      ip_port_text_buffer ipb;
      QUICDebugDS(scid, dcid, "recv SH packet from %s size=%" PRId64, ats_ip_nptop(&udp_packet->from.sa, ipb, sizeof(ipb)),
                  udp_packet->getPktLength());
    }
  }

  QUICConnection *qc     = this->_ctable->lookup(dcid);
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(qc);

  // [draft-12] 6.1.2.  Server Packet Handling
  // Servers MUST drop incoming packets under all other circumstances. They SHOULD send a Stateless Reset (Section 6.10.4) if a
  // connection ID is present in the header.
  if ((!vc && !QUICInvariants::is_long_header(buf)) || (vc && vc->in_closed_queue)) {
    QUICStatelessResetToken token;
    {
      QUICConfig::scoped_config params;
      token.generate(dcid, params->server_id());
    }
    auto packet = QUICPacketFactory::create_stateless_reset_packet(dcid, token);
    this->_send_packet(this, *packet, udp_packet->getConnection(), udp_packet->from, 1200, nullptr, 0);
    udp_packet->free();
    return;
  }

  EThread *eth = nullptr;
  if (!vc) {
    // Create a new NetVConnection
    Connection con;
    con.setRemote(&udp_packet->from.sa);

    eth                           = eventProcessor.assign_thread(ET_NET);
    QUICConnectionId original_cid = dcid;
    QUICConnectionId peer_cid     = scid;

    if (is_debug_tag_set("quic_sec")) {
      char client_dcid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
      original_cid.hex(client_dcid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
      QUICDebugDS(peer_cid, original_cid, "client initial dcid=%s", client_dcid_hex_str);
    }

    vc = static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(nullptr));
    vc->init(peer_cid, original_cid, udp_packet->getConnection(), this, this->_ctable);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->submit_time = Thread::get_hrtime();
    vc->thread      = eth;
    vc->mutex       = new_ProxyMutex();
    vc->action_     = *this->action_;
    vc->set_is_transparent(this->opt.f_inbound_transparent);
    vc->set_context(NET_VCONNECTION_IN);
    vc->start();
    vc->options.ip_proto  = NetVCOptions::USE_UDP;
    vc->options.ip_family = udp_packet->from.sa.sa_family;

    qc = vc;
  } else {
    eth = vc->thread;
  }

  QUICPollEvent *qe = quicPollEventAllocator.alloc();
  qe->init(qc, static_cast<UDPPacketInternal *>(udp_packet));
  // Push the packet into QUICPollCont
  get_QUICPollCont(eth)->inQueue.push(qe);

  return;
}

// TODO: Should be called via eventProcessor?
void
QUICPacketHandlerIn::send_packet(const QUICPacket &packet, QUICNetVConnection *vc, QUICPacketNumberProtector &pn_protector)
{
  this->_send_packet(this, packet, vc->get_udp_con(), vc->con.addr, vc->pmtu(), &pn_protector, vc->peer_connection_id().length());
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
QUICPacketHandlerOut::send_packet(const QUICPacket &packet, QUICNetVConnection *vc, QUICPacketNumberProtector &pn_protector)
{
  // TODO Pass QUICPacketNumberProtector
  this->_send_packet(this, packet, vc->get_udp_con(), vc->con.addr, vc->pmtu(), &pn_protector, vc->peer_connection_id().length());
}

void
QUICPacketHandlerOut::_recv_packet(int event, UDPPacket *udp_packet)
{
  ip_port_text_buffer ipb;
  // TODO: print packet type
  QUICDebugQC(this->_vc, "recv packet from %s size=%" PRId64, ats_ip_nptop(&udp_packet->from.sa, ipb, sizeof(ipb)),
              udp_packet->getPktLength());

  this->_vc->handle_received_packet(udp_packet);
  eventProcessor.schedule_imm(this->_vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
}
