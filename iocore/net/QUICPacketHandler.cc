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

#include "P_QUICPacketHandler.h"
#include "P_QUICNetProcessor.h"
#include "P_QUICNet.h"
#include "P_QUICClosedConCollector.h"

#include "QUICGlobals.h"
#include "QUICConfig.h"
#include "QUICPacket.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICResetTokenTable.h"

#include "QUICMultiCertConfigLoader.h"
#include "QUICTLS.h"

static constexpr char debug_tag[]   = "quic_sec";
static constexpr char v_debug_tag[] = "v_quic_sec";

#define QUICDebug(fmt, ...) Debug(debug_tag, fmt, ##__VA_ARGS__)
#define QUICQCDebug(qc, fmt, ...) Debug(debug_tag, "[%s] " fmt, qc->cids().data(), ##__VA_ARGS__)

// ["local dcid" - "local scid"]
#define QUICPHDebug(dcid, scid, fmt, ...) \
  Debug(debug_tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)
#define QUICVPHDebug(dcid, scid, fmt, ...) \
  Debug(v_debug_tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)

//
// QUICPacketHandler
//
QUICPacketHandler::QUICPacketHandler(QUICResetTokenTable &rtable) : _rtable(rtable)
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
  udp_payload->alloc(iobuffer_size_to_index(pmtu, BUFFER_SIZE_INDEX_32K));
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

  if (is_debug_tag_set(v_debug_tag)) {
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

    QUICVPHDebug(dcid, scid, "send %s packet to %s from port %u size=%" PRId64, (QUICInvariants::is_long_header(buf) ? "LH" : "SH"),
                 ats_ip_nptop(&addr, ipb, sizeof(ipb)), udp_con->getPortNum(), buf_len);
  }

  udp_con->send(this->_get_continuation(), udp_packet);
  get_UDPNetHandler(static_cast<UnixUDPConnection *>(udp_con)->ethread)->signalActivity();
}

QUICConnection *
QUICPacketHandler::_check_stateless_reset(const uint8_t *buf, size_t buf_len)
{
  return this->_rtable.lookup({buf + (buf_len - 16)});
}

//
// QUICPacketHandlerIn
//
QUICPacketHandlerIn::QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, QUICConnectionTable &ctable,
                                         QUICResetTokenTable &rtable)
  : NetAccept(opt), QUICPacketHandler(rtable), _ctable(ctable)
{
  this->mutex = new_ProxyMutex();
  // create Connection Table
  QUICConfig::scoped_config params;
}

QUICPacketHandlerIn::~QUICPacketHandlerIn() {}

NetProcessor *
QUICPacketHandlerIn::getNetProcessor() const
{
  return &quic_NetProcessor;
}

NetAccept *
QUICPacketHandlerIn::clone() const
{
  NetAccept *na;
  na  = new QUICPacketHandlerIn(opt, this->_ctable, this->_rtable);
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

    Queue<UDPPacket> *queue = static_cast<Queue<UDPPacket> *>(data);
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

  ink_abort("QUIC accept received fatal error: errno = %d", -(static_cast<int>((intptr_t)data)));
  return EVENT_CONT;
  return 0;
}

void
QUICPacketHandlerIn::init_accept(EThread *t = nullptr)
{
  SET_HANDLER(&QUICPacketHandlerIn::acceptEvent);
}

Continuation *
QUICPacketHandlerIn::_get_continuation()
{
  return static_cast<NetAccept *>(this);
}

void
QUICPacketHandlerIn::_recv_packet(int event, UDPPacket *udp_packet)
{
  // Assumption: udp_packet has only one IOBufferBlock
  IOBufferBlock *block = udp_packet->getIOBlockChain();
  const uint8_t *buf   = reinterpret_cast<uint8_t *>(block->buf());
  uint64_t buf_len     = block->size();
  QUICVersion version;

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

    if (is_debug_tag_set(v_debug_tag)) {
      ip_port_text_buffer ipb_from;
      ip_port_text_buffer ipb_to;
      QUICVPHDebug(scid, dcid, "recv LH packet from %s to %s size=%" PRId64,
                   ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                   ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
    }

    if (unlikely(!QUICInvariants::version(version, buf, buf_len))) {
      QUICDebug("Ignore packet - payload is too small");
      udp_packet->free();
      return;
    }

    if (!QUICInvariants::is_version_negotiation(version) && !QUICTypeUtil::is_supported_version(version)) {
      QUICPHDebug(scid, dcid, "Unsupported version: 0x%x", version);

      QUICPacketUPtr vn = QUICPacketFactory::create_version_negotiation_packet(scid, dcid, version);
      this->_send_packet(*vn, udp_packet->getConnection(), udp_packet->from, 1200, nullptr, 0);
      udp_packet->free();
      return;
    }

    if (dcid == QUICConnectionId::ZERO()) {
      // TODO: lookup DCID by 5-tuple when ATS omits SCID
      return;
    }

    QUICPacketType type = QUICPacketType::UNINITIALIZED;
    QUICLongHeaderPacketR::type(type, buf, buf_len);
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
    if (is_debug_tag_set(v_debug_tag)) {
      ip_port_text_buffer ipb_from;
      ip_port_text_buffer ipb_to;
      QUICVPHDebug(scid, dcid, "recv SH packet from %s to %s size=%" PRId64,
                   ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                   ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
    }
  }

  QUICConnection *qc     = this->_ctable.lookup(dcid);
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(qc);

  // Server Stateless Retry
  QUICConfig::scoped_config params;
  QUICConnectionId ocid_in_retry_token = QUICConnectionId::ZERO();
  QUICConnectionId rcid_in_retry_token = QUICConnectionId::ZERO();
  if (!vc && params->stateless_retry() && QUICInvariants::is_long_header(buf)) {
    int ret = this->_stateless_retry(buf, buf_len, udp_packet->getConnection(), udp_packet->from, dcid, scid, &ocid_in_retry_token,
                                     &rcid_in_retry_token, version);
    if (ret < 0) {
      udp_packet->free();
      return;
    }
  }

  // [draft-12] 6.1.2.  Server Packet Handling
  // Servers MUST drop incoming packets under all other circumstances. They SHOULD send a Stateless Reset (Section 6.10.4) if a
  // connection ID is present in the header.
  if (!vc && !QUICInvariants::is_long_header(buf)) {
    auto connection = static_cast<QUICNetVConnection *>(this->_check_stateless_reset(buf, buf_len));
    if (connection) {
      QUICDebug("Stateless Reset has been received");
      connection->thread->schedule_imm(connection, QUIC_EVENT_STATELESS_RESET);
      return;
    }

    bool sent =
      this->_send_stateless_reset(dcid, params->instance_id(), udp_packet->getConnection(), udp_packet->from, buf_len - 1);
    udp_packet->free();

    if (is_debug_tag_set(debug_tag) && sent) {
      QUICPHDebug(scid, dcid, "sent Stateless Reset : connection not found, dcid=%s", dcid.hex().c_str());
    }

    return;

  } else if (vc && vc->in_closed_queue) {
    bool sent =
      this->_send_stateless_reset(dcid, params->instance_id(), udp_packet->getConnection(), udp_packet->from, buf_len - 1);
    udp_packet->free();

    if (is_debug_tag_set(debug_tag) && sent) {
      QUICPHDebug(scid, dcid, "sent Stateless Reset : connection is already closed, dcid=%s", dcid.hex().c_str());
    }

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
      QUICPHDebug(peer_cid, original_cid, "client initial dcid=%s", original_cid.hex().c_str());
    }

    vc = static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(nullptr));
    vc->init(version, peer_cid, original_cid, ocid_in_retry_token, rcid_in_retry_token, udp_packet->getConnection(), this,
             &this->_rtable, &this->_ctable);
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
QUICPacketHandler::send_packet(QUICNetVConnection *vc, const Ptr<IOBufferBlock> &udp_payload)
{
  this->_send_packet(vc->get_udp_con(), vc->con.addr, udp_payload);
}

int
QUICPacketHandlerIn::_stateless_retry(const uint8_t *buf, uint64_t buf_len, UDPConnection *connection, IpEndpoint from,
                                      QUICConnectionId dcid, QUICConnectionId scid, QUICConnectionId *original_cid,
                                      QUICConnectionId *retry_cid, QUICVersion version)
{
  QUICPacketType type = QUICPacketType::UNINITIALIZED;
  QUICPacketR::type(type, buf, buf_len);

  if (type != QUICPacketType::INITIAL) {
    return 1;
  }

  // TODO: refine packet parsers in here, QUICPacketLongHeader, and QUICPacketReceiveQueue
  size_t token_length              = 0;
  uint8_t token_length_field_len   = 0;
  size_t token_length_field_offset = 0;
  if (!QUICInitialPacketR::token_length(token_length, token_length_field_len, token_length_field_offset, buf, buf_len)) {
    return -1;
  }

  if (token_length == 0) {
    QUICConnectionId local_cid;
    local_cid.randomize();
    QUICRetryToken token(from, dcid, local_cid);
    QUICPacketUPtr retry_packet = QUICPacketFactory::create_retry_packet(version, scid, local_cid, token);

    QUICDebug("[TX] %s packet ODCID=%" PRIx64 " RCID=%" PRIx64 " token_length=%u token=%02x%02x%02x%02x...",
              QUICDebugNames::packet_type(retry_packet->type()), static_cast<uint64_t>(token.original_dcid()),
              static_cast<uint64_t>(token.scid()), token.length(), token.buf()[0], token.buf()[1], token.buf()[2], token.buf()[3]);
    this->_send_packet(*retry_packet, connection, from, 1200, nullptr, 0);

    return -2;
  } else {
    size_t token_offset = token_length_field_offset + token_length_field_len;

    if (QUICAddressValidationToken::type(buf + token_offset) == QUICAddressValidationToken::Type::RETRY) {
      QUICRetryToken token(buf + token_offset, token_length);
      if (token.is_valid(from)) {
        *original_cid = token.original_dcid();
        *retry_cid    = token.scid();
        QUICDebug("Retry Token is valid. ODCID=%" PRIx64 " RCID=%" PRIx64, static_cast<uint64_t>(*original_cid),
                  static_cast<uint64_t>(*retry_cid));
        return 0;
      } else {
        QUICDebug("Retry token is invalid: ODCID=%" PRIx64 " RCID=%" PRIx64 " token_length=%u token=%02x%02x%02x%02x...",
                  static_cast<uint64_t>(token.original_dcid()), static_cast<uint64_t>(*retry_cid), token.length(), token.buf()[0],
                  token.buf()[1], token.buf()[2], token.buf()[3]);
        this->_send_invalid_token_error(buf, buf_len, connection, from);
        return -3;
      }
    } else {
      // TODO Handle ResumptionToken
      return -4;
    }
  }

  return 0;
}

bool
QUICPacketHandlerIn::_send_stateless_reset(QUICConnectionId dcid, uint32_t instance_id, UDPConnection *udp_con, IpEndpoint &addr,
                                           size_t maximum_size)
{
  QUICStatelessResetToken token(dcid, instance_id);
  auto packet = QUICPacketFactory::create_stateless_reset_packet(token, maximum_size);
  if (packet) {
    this->_send_packet(*packet, udp_con, addr, 1200, nullptr, 0);
    return true;
  }
  return false;
}

void
QUICPacketHandlerIn::_send_invalid_token_error(const uint8_t *initial_packet, uint64_t initial_packet_len,
                                               UDPConnection *connection, IpEndpoint from)
{
  QUICConnectionId scid_in_initial;
  QUICConnectionId dcid_in_initial;
  QUICInvariants::scid(scid_in_initial, initial_packet, initial_packet_len);
  QUICInvariants::dcid(dcid_in_initial, initial_packet, initial_packet_len);
  QUICVersion version_in_initial;
  QUICLongHeaderPacketR::version(version_in_initial, initial_packet, initial_packet_len);

  // Create CONNECTION_CLOSE frame
  auto error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::INVALID_TOKEN);
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  QUICFrame *frame         = QUICFrameFactory::create_connection_close_frame(frame_buf, *error);
  Ptr<IOBufferBlock> block = frame->to_io_buffer_block(1200);
  size_t block_len         = 0;
  for (Ptr<IOBufferBlock> tmp = block; tmp; tmp = tmp->next) {
    block_len += tmp->size();
  }
  frame->~QUICFrame();

  // Prepare for packet protection
  QUICPacketProtectionKeyInfo ppki;
  ppki.set_context(QUICPacketProtectionKeyInfo::Context::SERVER);
  QUICPacketFactory pf(ppki);
  QUICPacketHeaderProtector php(ppki);
  QUICCertConfig::scoped_config server_cert;
  QUICTLS tls(ppki, server_cert->ssl_default.get(), NET_VCONNECTION_IN, {}, "", "");
  tls.initialize_key_materials(dcid_in_initial, version_in_initial);

  // Create INITIAL packet
  QUICConnectionId scid;
  scid.randomize();
  uint8_t packet_buf[QUICPacket::MAX_INSTANCE_SIZE];
  QUICPacketUPtr cc_packet = pf.create_initial_packet(packet_buf, scid_in_initial, scid, 0, block, block_len, false, false, true);

  this->_send_packet(*cc_packet, connection, from, 0, &php, scid_in_initial);
}

//
// QUICPacketHandlerOut
//
QUICPacketHandlerOut::QUICPacketHandlerOut(QUICResetTokenTable &rtable) : Continuation(new_ProxyMutex()), QUICPacketHandler(rtable)
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
    Queue<UDPPacket> *queue = reinterpret_cast<Queue<UDPPacket> *>(data);
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
  IOBufferBlock *block = udp_packet->getIOBlockChain();
  const uint8_t *buf   = reinterpret_cast<uint8_t *>(block->buf());
  uint64_t buf_len     = block->size();

  if (is_debug_tag_set(debug_tag)) {
    ip_port_text_buffer ipb_from;
    ip_port_text_buffer ipb_to;
    QUICQCDebug(this->_vc, "recv %s packet from %s to %s size=%" PRId64, (QUICInvariants::is_long_header(buf) ? "LH" : "SH"),
                ats_ip_nptop(&udp_packet->from.sa, ipb_from, sizeof(ipb_from)),
                ats_ip_nptop(&udp_packet->to.sa, ipb_to, sizeof(ipb_to)), udp_packet->getPktLength());
  }

  QUICConnectionId dcid;
  if (!QUICInvariants::dcid(dcid, buf, buf_len)) {
    QUICDebug("Ignore packet - payload is too small");
    udp_packet->free();
    return;
  }

  if (!QUICInvariants::is_long_header(buf) && dcid != this->_vc->connection_id()) {
    auto connection = static_cast<QUICNetVConnection *>(this->_check_stateless_reset(buf, buf_len));
    if (connection) {
      if (connection->connection_id() == this->_vc->connection_id()) {
        QUICDebug("Stateless Reset has been received");
        this->_vc->thread->schedule_imm(this->_vc, QUIC_EVENT_STATELESS_RESET);
      }
      return;
    }
  }

  this->_vc->handle_received_packet(udp_packet);
  eventProcessor.schedule_imm(this->_vc, ET_CALL, QUIC_EVENT_PACKET_READ_READY, nullptr);
}
