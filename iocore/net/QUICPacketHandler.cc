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

#include "tscore/ink_config.h"
#include "P_Net.h"

#include "P_QUICClosedConCollector.h"

#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICPacket.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"

static constexpr int LONG_HDR_OFFSET_CONNECTION_ID = 6;
static constexpr char debug_tag[]                  = "quic_sec";

#define QUICDebug(fmt, ...) Debug(debug_tag, fmt, ##__VA_ARGS__)
#define QUICDebugQC(qc, fmt, ...) Debug(debug_tag, "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

// ["local dcid" - "local scid"]
#define QUICDebugDS(dcid, scid, fmt, ...) \
  Debug(debug_tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)

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
QUICPacketHandler::close_connection(QUICNetVConnection *conn)
{
  int isin = ink_atomic_swap(&conn->in_closed_queue, 1);
  if (!isin) {
    this->_closed_con_collector->closedQueue.push(conn);
  }
}

void
QUICPacketHandler::_send_packet(const QUICPacket &packet, UDPConnection *udp_con, IpEndpoint &addr, uint32_t pmtu,
                                const QUICPacketHeaderProtector *ph_protector, int dcil)
{
  size_t udp_len;
  Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
  udp_payload->alloc(iobuffer_size_to_index(pmtu));
  packet.store(reinterpret_cast<uint8_t *>(udp_payload->end()), &udp_len);
  udp_payload->fill(udp_len);

  if (ph_protector) {
    ph_protector->protect(reinterpret_cast<uint8_t *>(udp_payload->start()), udp_len, dcil);
  }

  this->_send_packet(udp_con, addr, udp_payload);
}

void
QUICPacketHandler::_send_packet(UDPConnection *udp_con, IpEndpoint &addr, Ptr<IOBufferBlock> udp_payload)
{
  UDPPacket *udp_packet = new_UDPPacket(addr, 0, udp_payload);

  if (is_debug_tag_set(debug_tag)) {
    ip_port_text_buffer ipb;
    QUICConnectionId dcid = QUICConnectionId::ZERO();
    QUICConnectionId scid = QUICConnectionId::ZERO();

    const uint8_t *buf = reinterpret_cast<uint8_t *>(udp_payload->buf());
    uint64_t buf_len   = udp_payload->size();

    if (!QUICInvariants::dcid(dcid, buf, buf_len)) {
      ink_assert(false);
    }

    if (QUICInvariants::is_long_header(buf)) {
      if (!QUICInvariants::scid(scid, buf, buf_len)) {
        ink_assert(false);
      }
    }

    QUICDebugDS(dcid, scid, "send %s packet to %s from port %u size=%" PRId64, (QUICInvariants::is_long_header(buf) ? "LH" : "SH"),
                ats_ip_nptop(&addr, ipb, sizeof(ipb)), udp_con->getPortNum(), buf_len);
  }

  udp_con->send(this->_get_continuation(), udp_packet);
  get_UDPNetHandler(static_cast<UnixUDPConnection *>(udp_con)->ethread)->signalActivity();
}

//
// QUICPacketHandlerIn
//
QUICPacketHandler::QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, QUICConnectionTable &ctable)
  : NetAccept(opt), QUICPacketHandler(), _ctable(ctable)
{
  this->mutex = new_ProxyMutex();
  // create Connection Table
  QUICConfig::scoped_config params;
}

QUICPacketHandler::~QUICPacketHandlerIn() {}

NetProcessor *
QUICPacketHandler::getNetProcessor() const
{
  return &quic_NetProcessor;
}

NetAccept *
QUICPacketHandler::clone() const
{
  NetAccept *na;
  na  = new QUICPacketHandlerIn(opt, this->_ctable);
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
QUICPacketHandler::init_accept(EThread *t = nullptr)
{
  SET_HANDLER(&QUICPacketHandlerIn::acceptEvent);
}

Continuation *
QUICPacketHandler::_get_continuation()
{
  return static_cast<NetAccept *>(this);
}

void
QUICPacketHandler::_recv_packet(int event, UDPPacket *udp_packet)
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

    if (is_debug_tag_set(debug_tag)) {
      ip_port_text_buffer ipb_from;
      ip_port_text_buffer ipb_to;
      QUICDebugDS(scid, dcid, "recv LH packet from %s to %s size=%" PRId64,
                  ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                  ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
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
      this->_send_packet(*vn, udp_packet->getConnection(), udp_packet->from, 1200, nullptr, 0);
      udp_packet->free();
      return;
    }

    if (dcid == QUICConnectionId::ZERO()) {
      // TODO: lookup DCID by 5-tuple when ATS omits SCID
      return;
    }

    QUICPacketType type = QUICPacketType::UNINITIALIZED;
    QUICPacketLongHeader::type(type, buf, buf_len);
    if (type == QUICPacketType::INITIAL) {
      // [draft-18] 7.2.
      // When an Initial packet is sent by a client which has not previously received a Retry packet from the server, it populates
      // the Destination Connection ID field with an unpredictable value. This MUST be at least 8 bytes in length.
      if (dcid != QUICConnectionId::ZERO() && dcid.length() < QUICConnectionId::MIN_LENGTH_FOR_INITIAL) {
        QUICDebug("Ignore packet - DCIL is too small for Initial packet");
        udp_packet->free();
        return;
      }
    }
  } else {
    // TODO: lookup DCID by 5-tuple when ATS omits SCID
    if (is_debug_tag_set(debug_tag)) {
      ip_port_text_buffer ipb_from;
      ip_port_text_buffer ipb_to;
      QUICDebugDS(scid, dcid, "recv SH packet from %s to %s size=%" PRId64,
                  ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                  ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
    }
  }

  QUICConnection *qc     = this->_ctable.lookup(dcid);
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(qc);

  // Server Stateless Retry
  QUICConfig::scoped_config params;
  QUICConnectionId cid_in_retry_token = QUICConnectionId::ZERO();
  if (!vc && params->stateless_retry() && QUICInvariants::is_long_header(buf)) {
    int ret = this->_stateless_retry(buf, buf_len, udp_packet->getConnection(), udp_packet->from, dcid, scid, &cid_in_retry_token);
    if (ret < 0) {
      udp_packet->free();
      return;
    }
  }

  // [draft-12] 6.1.2.  Server Packet Handling
  // Servers MUST drop incoming packets under all other circumstances. They SHOULD send a Stateless Reset (Section 6.10.4) if a
  // connection ID is present in the header.
  if ((!vc && !QUICInvariants::is_long_header(buf)) || (vc && vc->in_closed_queue)) {
    if (is_debug_tag_set(debug_tag)) {
      char dcid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
      dcid.hex(dcid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

      if (!vc && !QUICInvariants::is_long_header(buf)) {
        QUICDebugDS(scid, dcid, "sent Stateless Reset : connection not found, dcid=%s", dcid_str);
      } else if (vc && vc->in_closed_queue) {
        QUICDebugDS(scid, dcid, "sent Stateless Reset : connection is already closed, dcid=%s", dcid_str);
      }
    }

    QUICStatelessResetToken token(dcid, params->instance_id());
    auto packet = QUICPacketFactory::create_stateless_reset_packet(dcid, token);
    this->_send_packet(*packet, udp_packet->getConnection(), udp_packet->from, 1200, nullptr, 0);
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
    vc->init(peer_cid, original_cid, cid_in_retry_token, udp_packet->getConnection(), this, &this->_ctable);
    vc->id = net_next_connection_number();
    vc->con.move(con);
    vc->submit_time = Thread::get_hrtime();
    vc->thread      = eth;
    vc->mutex       = new_ProxyMutex();
    vc->action_     = *this->action_;
    vc->set_is_transparent(this->opt.f_inbound_transparent);
    vc->set_context(NET_VCONNECTION_IN);
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
  get_NetHandler(eth)->signalActivity();

  return;
}

// TODO: Should be called via eventProcessor?
void
QUICPacketHandler::send_packet(const QUICPacket &packet, QUICNetVConnection *vc, const QUICPacketHeaderProtector &ph_protector)
{
  this->_send_packet(packet, vc->get_udp_con(), vc->con.addr, vc->pmtu(), &ph_protector, vc->peer_connection_id().length());
}

void
QUICPacketHandler::send_packet(QUICNetVConnection *vc, Ptr<IOBufferBlock> udp_payload)
{
  this->_send_packet(vc->get_udp_con(), vc->con.addr, udp_payload);
}

int
QUICPacketHandler::_stateless_retry(const uint8_t *buf, uint64_t buf_len, UDPConnection *connection, IpEndpoint from,
                                    QUICConnectionId dcid, QUICConnectionId scid, QUICConnectionId *original_cid)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketLongHeader::type(type, buf, buf_len);

  if (type != QUICPacketType::INITIAL) {
    return 1;
  }

  // TODO: refine packet parsers in here, QUICPacketLongHeader, and QUICPacketReceiveQueue
  size_t token_length            = 0;
  uint8_t token_length_field_len = 0;
  if (!QUICPacketLongHeader::token_length(token_length, &token_length_field_len, buf, buf_len)) {
    return -1;
  }

  if (token_length == 0) {
    QUICRetryToken token(from, dcid);
    QUICConnectionId local_cid;
    local_cid.randomize();
    QUICPacketUPtr retry_packet = QUICPacketFactory::create_retry_packet(scid, local_cid, dcid, token);

    this->_send_packet(*retry_packet, connection, from, 1200, nullptr, 0);

    return -2;
  } else {
    uint8_t dcil, scil;
    QUICPacketLongHeader::dcil(dcil, buf, buf_len);
    QUICPacketLongHeader::scil(scil, buf, buf_len);
    const uint8_t *token = buf + LONG_HDR_OFFSET_CONNECTION_ID + dcil + scil + token_length_field_len;

    if (QUICAddressValidationToken::type(token) == QUICAddressValidationToken::Type::RETRY) {
      QUICRetryToken token1(token, token_length);
      if (token1.is_valid(from)) {
        *original_cid = token1.original_dcid();
        return 0;
      } else {
        return -3;
      }
    } else {
      // TODO Handle ResumptionToken
      return -4;
    }
  }

  return 0;
}

//
// QUICPacketHandlerOut
//
QUICPacketHandler::QUICPacketHandlerOut() : Continuation(new_ProxyMutex()), QUICPacketHandler()
{
  SET_HANDLER(&QUICPacketHandlerOut::event_handler);
}

void
QUICPacketHandler::init(QUICNetVConnection *vc)
{
  this->_vc = vc;
}

int
QUICPacketHandler::event_handler(int event, Event *data)
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

Continuation *
QUICPacketHandlerOut::_get_continuation()
{
  return this;
}

void
QUICPacketHandlerOut::_recv_packet(int event, UDPPacket *udp_packet)
{
  if (is_debug_tag_set(debug_tag)) {
    IOBufferBlock *block = udp_packet->getIOBlockChain();
    const uint8_t *buf   = reinterpret_cast<uint8_t *>(block->buf());

    ip_port_text_buffer ipb_from;
    ip_port_text_buffer ipb_to;
    QUICDebugQC(this->_vc, "recv %s packet from %s to %s size=%" PRId64, (QUICInvariants::is_long_header(buf) ? "LH" : "SH"),
                ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
  }

  this->_vc->handle_received_packet(udp_packet);
  eventProcessor.schedule_imm(this->_vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
}
