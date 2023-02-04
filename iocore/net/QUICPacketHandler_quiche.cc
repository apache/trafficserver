
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

#include "tscore/I_Layout.h"
#include "tscore/ink_config.h"
#include "P_Net.h"

#include "P_QUICNet.h"
#include "P_QUICPacketHandler_quiche.h"
#include "P_QUICNetProcessor_quiche.h"
#include "P_QUICClosedConCollector.h"
#include "quic/QUICConnectionTable.h"
#include <quiche.h>

static constexpr char debug_tag[]   = "quic_sec";
static constexpr char v_debug_tag[] = "v_quic_sec";

#define QUICDebug(fmt, ...) Debug(debug_tag, fmt, ##__VA_ARGS__)
#define QUICPHDebug(dcid, scid, fmt, ...) \
  Debug(debug_tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)
#define QUICVPHDebug(dcid, scid, fmt, ...) \
  Debug(v_debug_tag, "[%08" PRIx32 "-%08" PRIx32 "] " fmt, dcid.h32(), scid.h32(), ##__VA_ARGS__)

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
QUICPacketHandler::send_packet(UDPConnection *udp_con, IpEndpoint &addr, Ptr<IOBufferBlock> udp_payload, uint16_t segment_size)
{
  UDPPacket *udp_packet = new_UDPPacket(addr, 0, udp_payload, segment_size);

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

QUICPacketHandlerIn::QUICPacketHandlerIn(const NetProcessor::AcceptOptions &opt, QUICConnectionTable &ctable, quiche_config &config)
  : NetAccept(opt), QUICPacketHandler(), _ctable(ctable), _quiche_config(config)
{
  this->mutex = new_ProxyMutex();
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
  na  = new QUICPacketHandlerIn(opt, this->_ctable, this->_quiche_config);
  *na = *this;
  return na;
}

int
QUICPacketHandlerIn::acceptEvent(int event, void *data)
{
  // NetVConnection *netvc;
  ink_release_assert(event == EVENT_IMMEDIATE || event == NET_EVENT_DATAGRAM_OPEN || event == NET_EVENT_DATAGRAM_READ_READY ||
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
  } else if (event == EVENT_IMMEDIATE) {
    this->setThreadAffinity(this_ethread());
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    udpNet.UDPBind((Continuation *)this, &this->server.accept_addr.sa, -1, 1048576, 1048576);
    return EVENT_CONT;
  }

  /////////////////
  // EVENT_ERROR //
  /////////////////
  if (((long)data) == -ECONNABORTED) {
  }

  ink_abort("QUIC accept received fatal error: errno = %d", -(static_cast<int>((intptr_t)data)));
  return EVENT_CONT;
}

void
QUICPacketHandlerIn::init_accept(EThread *t = nullptr)
{
  int i, n;

  SET_HANDLER(&QUICPacketHandlerIn::acceptEvent);

  n = eventProcessor.thread_group[ET_UDP]._count;
  for (i = 0; i < n; i++) {
    NetAccept *a = (i < n - 1) ? clone() : this;
    EThread *t   = eventProcessor.thread_group[ET_UDP]._thread[i];
    a->mutex     = get_NetHandler(t)->mutex;
    t->schedule_imm(a);
  }
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

  constexpr int MAX_TOKEN_LEN             = 1200;
  constexpr int DEFAULT_MAX_DATAGRAM_SIZE = 1350;
  uint8_t type;
  uint32_t version;
  uint8_t scid[QUICHE_MAX_CONN_ID_LEN];
  size_t scid_len = sizeof(scid);
  uint8_t dcid[QUICHE_MAX_CONN_ID_LEN];
  size_t dcid_len = sizeof(dcid);
  uint8_t token[MAX_TOKEN_LEN];
  size_t token_len = sizeof(token);

  int rc = quiche_header_info(buf, buf_len, QUICConnectionId::SCID_LEN, &version, &type, scid, &scid_len, dcid, &dcid_len, token,
                              &token_len);
  if (rc < 0) {
    QUICDebug("Ignore packet - failed to parse header");
    udp_packet->free();
    return;
  }

  if (dcid_len > 255 || scid_len > 255) {
    QUICDebug("Ignore packet - too long connection id");
    udp_packet->free();
    return;
  }
  QUICConnection *qc     = this->_ctable.lookup({dcid, static_cast<uint8_t>(dcid_len)});
  QUICNetVConnection *vc = static_cast<QUICNetVConnection *>(qc);

  EThread *eth = nullptr;
  if (vc == nullptr) {
    if (!quiche_version_is_supported(version)) {
      Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
      udp_payload->alloc(iobuffer_size_to_index(DEFAULT_MAX_DATAGRAM_SIZE, BUFFER_SIZE_INDEX_2K));
      QUICPHDebug(QUICConnectionId(scid, scid_len), QUICConnectionId(dcid, dcid_len), "Unsupported version: 0x%x", version);
      ssize_t written = quiche_negotiate_version(scid, scid_len, dcid, dcid_len, reinterpret_cast<uint8_t *>(udp_payload->end()),
                                                 udp_payload->write_avail());
      udp_payload->fill(written);
      this->send_packet(udp_packet->getConnection(), udp_packet->from, udp_payload);
      udp_packet->free();
      return;
    }

    QUICConfig::scoped_config params;
    if (params->stateless_retry() && token_len == 0) {
      QUICConnectionId new_cid;
      new_cid.randomize();
      QUICRetryToken retry_token = {
        udp_packet->from,
        {dcid, static_cast<uint8_t>(dcid_len)},
        new_cid
      };
      Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
      udp_payload->alloc(iobuffer_size_to_index(DEFAULT_MAX_DATAGRAM_SIZE, BUFFER_SIZE_INDEX_2K));
      ssize_t written =
        quiche_retry(scid, scid_len, dcid, dcid_len, new_cid, new_cid.length(), retry_token.buf(), retry_token.length(), version,
                     reinterpret_cast<uint8_t *>(udp_payload->end()), udp_payload->write_avail());
      udp_payload->fill(written);
      this->send_packet(udp_packet->getConnection(), udp_packet->from, udp_payload);

      udp_packet->free();
      return;
    }

    // Create a new connection
    Connection con;
    con.setRemote(&udp_packet->from.sa);

    eth                           = eventProcessor.assign_thread(ET_NET);
    QUICConnectionId original_cid = {dcid, static_cast<uint8_t>(dcid_len)};
    QUICConnectionId peer_cid     = {scid, static_cast<uint8_t>(scid_len)};

    if (is_debug_tag_set("quic_sec")) {
      QUICPHDebug(peer_cid, original_cid, "client initial dcid=%s", original_cid.hex().c_str());
    }

    QUICRetryToken retry_token = {token, token_len};
    if (params->stateless_retry() && !retry_token.is_valid(udp_packet->from)) {
      fprintf(stderr, "invalid address validation token\n");
      udp_packet->free();
      return;
    }

    QUICConnectionId new_cid;
    quiche_conn *quiche_con =
      quiche_accept(new_cid, new_cid.length(), retry_token.original_dcid(), retry_token.original_dcid().length(),
#ifdef HAVE_QUICHE_CONFIG_SET_ACTIVE_CONNECTION_ID_LIMIT
                    &udp_packet->to.sa, udp_packet->to.isIp4() ? sizeof(udp_packet->to.sin) : sizeof(udp_packet->to.sin6),
#endif
                    &udp_packet->from.sa, udp_packet->from.isIp4() ? sizeof(udp_packet->from.sin) : sizeof(udp_packet->from.sin6),
                    &this->_quiche_config);

    if (params->qlog_dir() != nullptr) {
      char qlog_filepath[PATH_MAX];
      const uint8_t *quic_trace_id;
      size_t quic_trace_id_len = 0;
      quiche_conn_trace_id(quiche_con, &quic_trace_id, &quic_trace_id_len);
      snprintf(qlog_filepath, PATH_MAX, "%s/%.*s.sqlog", Layout::get()->relative(params->qlog_dir()).c_str(),
               static_cast<int>(quic_trace_id_len), quic_trace_id);
      quiche_conn_set_qlog_path(quiche_con, qlog_filepath, "ats", "");
    }

    vc = static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(nullptr));
    // TODO Extract OCID and RCID from the token
    // vc->init(version, peer_cid, original_cid, ocid_in_retry_token, rcid_in_retry_token, udp_packet->getConnection(), this,
    // &this->_ctable);
    vc->init(version, peer_cid, new_cid, QUICConnectionId::ZERO(), QUICConnectionId::ZERO(), udp_packet->getConnection(),
             quiche_con, this, &this->_ctable);
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
    eth->schedule_imm(vc, EVENT_NONE, nullptr);
    qc = vc;
  } else if (vc && vc->in_closed_queue) {
    // TODO Send stateless reset
    udp_packet->free();
    return;
  }
  eth = vc->thread;

  QUICPollEvent *qe = quicPollEventAllocator.alloc();
  qe->init(qc, static_cast<UDPPacketInternal *>(udp_packet));
  // Push the packet into QUICPollCont
  get_QUICPollCont(eth)->inQueue.push(qe);
  get_NetHandler(eth)->signalActivity();

  return;
}

void
QUICPacketHandlerOut::init(QUICNetVConnection *vc)
{
}

Continuation *
QUICPacketHandlerOut::_get_continuation()
{
  return this;
}

void
QUICPacketHandlerOut::_recv_packet(int event, UDPPacket *udp_packet)
{
}
