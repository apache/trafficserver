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
#include <climits>
#include <string>

#include "tscore/ink_config.h"
#include "records/I_RecHttp.h"
#include "tscore/Diags.h"

#include "P_Net.h"
#include "InkAPIInternal.h" // Added to include the quic_hook definitions
#include "Log.h"

#include "P_SSLNextProtocolSet.h"
#include "QUICMultiCertConfigLoader.h"
#include "QUICTLS.h"

#include "QUICStats.h"
#include "QUICGlobals.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICConfig.h"
#include "QUICIntUtil.h"

using namespace std::literals;
static constexpr std::string_view QUIC_DEBUG_TAG = "quic_net"sv;

#define QUICConDebug(fmt, ...) Debug(QUIC_DEBUG_TAG.data(), "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICConVDebug(fmt, ...) Debug("v_quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)
#define QUICConVVVDebug(fmt, ...) Debug("vvv_quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICFCDebug(fmt, ...) Debug("quic_flow_ctrl", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICError(fmt, ...)                                           \
  Debug("quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__); \
  Error("quic_net [%s] " fmt, this->cids().data(), ##__VA_ARGS__)

static constexpr uint32_t IPV4_HEADER_SIZE            = 20;
static constexpr uint32_t IPV6_HEADER_SIZE            = 40;
static constexpr uint32_t UDP_HEADER_SIZE             = 8;
static constexpr uint32_t MAX_PACKET_OVERHEAD         = 62; ///< Max long header len without length of token field of Initial packet
static constexpr uint32_t MINIMUM_INITIAL_PACKET_SIZE = 1200;
static constexpr ink_hrtime WRITE_READY_INTERVAL      = HRTIME_MSECONDS(2);
static constexpr uint32_t PACKET_PER_EVENT            = 256;
static constexpr uint32_t MAX_CONSECUTIVE_STREAMS     = 8; ///< Interrupt sending STREAM frames to send ACK frame
static constexpr uint32_t MIN_PKT_PAYLOAD_LEN         = 3; ///< Minimum payload length for sampling for header protection

static constexpr uint32_t STATE_CLOSING_MAX_SEND_PKT_NUM  = 8; ///< Max number of sending packets which contain a closing frame.
static constexpr uint32_t STATE_CLOSING_MAX_RECV_PKT_WIND = 1 << STATE_CLOSING_MAX_SEND_PKT_NUM;

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

class QUICTPConfigQCP : public QUICTPConfig
{
public:
  QUICTPConfigQCP(const QUICConfigParams *params, NetVConnectionContext_t ctx) : _params(params), _ctx(ctx) {}

  uint32_t
  no_activity_timeout() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->no_activity_timeout_in();
    } else {
      return this->_params->no_activity_timeout_out();
    }
  }

  const IpEndpoint *
  preferred_address_ipv4() const override
  {
    return this->_params->preferred_address_ipv4();
  }

  const IpEndpoint *
  preferred_address_ipv6() const override
  {
    return this->_params->preferred_address_ipv6();
  }

  uint32_t
  initial_max_data() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_data_in();
    } else {
      return this->_params->initial_max_data_out();
    }
  }

  uint32_t
  initial_max_stream_data_bidi_local() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_stream_data_bidi_local_in();
    } else {
      return this->_params->initial_max_stream_data_bidi_local_out();
    }
  }

  uint32_t
  initial_max_stream_data_bidi_remote() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_stream_data_bidi_remote_in();
    } else {
      return this->_params->initial_max_stream_data_bidi_remote_out();
    }
  }

  uint32_t
  initial_max_stream_data_uni() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_stream_data_uni_in();
    } else {
      return this->_params->initial_max_stream_data_uni_out();
    }
  }

  uint64_t
  initial_max_streams_bidi() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_streams_bidi_in();
    } else {
      return this->_params->initial_max_streams_bidi_out();
    }
  }

  uint64_t
  initial_max_streams_uni() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->initial_max_streams_uni_in();
    } else {
      return this->_params->initial_max_streams_uni_out();
    }
  }

  uint8_t
  ack_delay_exponent() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->ack_delay_exponent_in();
    } else {
      return this->_params->ack_delay_exponent_out();
    }
  }

  uint8_t
  max_ack_delay() const override
  {
    if (this->_ctx == NET_VCONNECTION_IN) {
      return this->_params->max_ack_delay_in();
    } else {
      return this->_params->max_ack_delay_out();
    }
  }

private:
  const QUICConfigParams *_params;
  NetVConnectionContext_t _ctx;
};

class QUICCCConfigQCP : public QUICCCConfig
{
public:
  QUICCCConfigQCP(const QUICConfigParams *params) : _params(params) {}

  uint32_t
  max_datagram_size() const override
  {
    return this->_params->cc_max_datagram_size();
  }

  uint32_t
  initial_window() const override
  {
    return this->_params->cc_initial_window();
  }

  uint32_t
  minimum_window() const override
  {
    return this->_params->cc_minimum_window();
  }

  float
  loss_reduction_factor() const override
  {
    return this->_params->cc_loss_reduction_factor();
  }

  uint32_t
  persistent_congestion_threshold() const override
  {
    return this->_params->cc_persistent_congestion_threshold();
  }

private:
  const QUICConfigParams *_params;
};

class QUICLDConfigQCP : public QUICLDConfig
{
public:
  QUICLDConfigQCP(const QUICConfigParams *params) : _params(params) {}

  uint32_t
  packet_threshold() const override
  {
    return this->_params->ld_packet_threshold();
  }

  float
  time_threshold() const override
  {
    return this->_params->ld_time_threshold();
  }

  ink_hrtime
  granularity() const override
  {
    return this->_params->ld_granularity();
  }

  ink_hrtime
  initial_rtt() const override
  {
    return this->_params->ld_initial_rtt();
  }

private:
  const QUICConfigParams *_params;
};

QUICNetVConnection::QUICNetVConnection() : _packet_factory(this->_pp_key_info), _ph_protector(this->_pp_key_info) {}

QUICNetVConnection::~QUICNetVConnection()
{
  this->_unschedule_ack_manager_periodic();
  this->_unschedule_packet_write_ready();
  this->_unschedule_closing_timeout();
  this->_unschedule_closed_event();
  this->_unschedule_path_validation_timeout();
}

// XXX This might be called on ET_UDP thread
// Initialize QUICNetVC for out going connection (NET_VCONNECTION_OUT)
void
QUICNetVConnection::init(QUICConnectionId peer_cid, QUICConnectionId original_cid, UDPConnection *udp_con,
                         QUICPacketHandler *packet_handler)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::startEvent);
  this->_udp_con                     = udp_con;
  this->_packet_handler              = packet_handler;
  this->_peer_quic_connection_id     = peer_cid;
  this->_original_quic_connection_id = original_cid;
  this->_quic_connection_id.randomize();

  this->_update_cids();

  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char dcid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    char scid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    this->_peer_quic_connection_id.hex(dcid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    this->_quic_connection_id.hex(scid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

    QUICConDebug("dcid=%s scid=%s", dcid_hex_str, scid_hex_str);
  }
}

// Initialize QUICNetVC for in coming connection (NET_VCONNECTION_IN)
void
QUICNetVConnection::init(QUICConnectionId peer_cid, QUICConnectionId original_cid, QUICConnectionId first_cid,
                         UDPConnection *udp_con, QUICPacketHandler *packet_handler, QUICConnectionTable *ctable)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::acceptEvent);
  this->_udp_con                     = udp_con;
  this->_packet_handler              = packet_handler;
  this->_peer_quic_connection_id     = peer_cid;
  this->_original_quic_connection_id = original_cid;
  this->_first_quic_connection_id    = first_cid;
  this->_quic_connection_id.randomize();

  if (ctable) {
    this->_ctable = ctable;
    this->_ctable->insert(this->_quic_connection_id, this);
    this->_ctable->insert(this->_original_quic_connection_id, this);
  }

  this->_update_cids();

  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char dcid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    char scid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    this->_peer_quic_connection_id.hex(dcid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    this->_quic_connection_id.hex(scid_hex_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

    QUICConDebug("dcid=%s scid=%s", dcid_hex_str, scid_hex_str);
  }
}

bool
QUICNetVConnection::shouldDestroy()
{
  return this->refcount() == 0;
}

VIO *
QUICNetVConnection::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  ink_assert(false);
  return nullptr;
}

VIO *
QUICNetVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  ink_assert(false);
  return nullptr;
}

int
QUICNetVConnection::acceptEvent(int event, Event *e)
{
  EThread *t    = (e == nullptr) ? this_ethread() : e->ethread;
  NetHandler *h = get_NetHandler(t);

  MUTEX_TRY_LOCK(lock, h->mutex, t);
  if (!lock.is_locked()) {
    if (event == EVENT_NONE) {
      t->schedule_in(this, HRTIME_MSECONDS(net_retry_delay));
      return EVENT_DONE;
    } else {
      e->schedule_in(HRTIME_MSECONDS(net_retry_delay));
      return EVENT_CONT;
    }
  }

  // this->thread is already assigned by QUICPacketHandlerIn::_recv_packet
  ink_assert(this->thread == this_ethread());

  // Send this NetVC to NetHandler and start to polling read & write event.
  if (h->startIO(this) < 0) {
    free(t);
    return EVENT_DONE;
  }

  // FIXME: complete do_io_xxxx instead
  this->read.enabled = 1;

  // Handshake callback handler.
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_pre_handshake);

  // Send this netvc to InactivityCop.
  nh->startCop(this);

  if (inactivity_timeout_in) {
    set_inactivity_timeout(inactivity_timeout_in);
  } else {
    set_inactivity_timeout(0);
  }

  if (active_timeout_in) {
    set_active_timeout(active_timeout_in);
  }

  this->start();

  action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  this->_schedule_packet_write_ready();

  return EVENT_DONE;
}

int
QUICNetVConnection::startEvent(int event, Event *e)
{
  ink_assert(event == EVENT_IMMEDIATE);
  MUTEX_TRY_LOCK(lock, get_NetHandler(e->ethread)->mutex, e->ethread);
  if (!lock.is_locked()) {
    e->schedule_in(HRTIME_MSECONDS(net_retry_delay));
    return EVENT_CONT;
  }

  if (!action_.cancelled) {
    this->connectUp(e->ethread, NO_FD);
  } else {
    this->free(e->ethread);
  }

  return EVENT_DONE;
}

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::start()
{
  ink_release_assert(this->thread != nullptr);

  this->_five_tuple.update(this->local_addr, this->remote_addr, SOCK_DGRAM);
  // Version 0x00000001 uses stream 0 for cryptographic handshake with TLS 1.3, but newer version may not
  if (this->direction() == NET_VCONNECTION_IN) {
    QUICCertConfig::scoped_config server_cert;

    this->_pp_key_info.set_context(QUICPacketProtectionKeyInfo::Context::SERVER);
    this->_ack_frame_manager.set_ack_delay_exponent(this->_quic_config->ack_delay_exponent_in());
    this->_reset_token = QUICStatelessResetToken(this->_quic_connection_id, this->_quic_config->instance_id());
    this->_hs_protocol = this->_setup_handshake_protocol(server_cert->ssl_default);
    this->_handshake_handler =
      new QUICHandshake(this, this->_hs_protocol, this->_reset_token, this->_quic_config->stateless_retry());
    this->_ack_frame_manager.set_max_ack_delay(this->_quic_config->max_ack_delay_in());
    this->_schedule_ack_manager_periodic(this->_quic_config->max_ack_delay_in());
  } else {
    QUICTPConfigQCP tp_config(this->_quic_config, NET_VCONNECTION_OUT);
    this->_pp_key_info.set_context(QUICPacketProtectionKeyInfo::Context::CLIENT);
    this->_ack_frame_manager.set_ack_delay_exponent(this->_quic_config->ack_delay_exponent_out());
    this->_hs_protocol       = this->_setup_handshake_protocol(this->_quic_config->client_ssl_ctx());
    this->_handshake_handler = new QUICHandshake(this, this->_hs_protocol);
    this->_handshake_handler->start(tp_config, &this->_packet_factory, this->_quic_config->vn_exercise_enabled());
    this->_handshake_handler->do_handshake();
    this->_ack_frame_manager.set_max_ack_delay(this->_quic_config->max_ack_delay_out());
    this->_schedule_ack_manager_periodic(this->_quic_config->max_ack_delay_out());
  }

  this->_application_map = new QUICApplicationMap();

  this->_frame_dispatcher = new QUICFrameDispatcher(this);

  // Create frame handlers
  QUICCCConfigQCP cc_config(this->_quic_config);
  QUICLDConfigQCP ld_config(this->_quic_config);
  this->_rtt_measure.init(ld_config);
  this->_congestion_controller = new QUICCongestionController(this->_rtt_measure, this, cc_config);
  this->_loss_detector         = new QUICLossDetector(this, this->_congestion_controller, &this->_rtt_measure, ld_config);
  this->_frame_dispatcher->add_handler(this->_loss_detector);

  this->_remote_flow_controller = new QUICRemoteConnectionFlowController(UINT64_MAX);
  this->_local_flow_controller  = new QUICLocalConnectionFlowController(&this->_rtt_measure, UINT64_MAX);
  this->_path_validator         = new QUICPathValidator();
  this->_stream_manager         = new QUICStreamManager(this, &this->_rtt_measure, this->_application_map);

  // Register frame generators
  this->_frame_generators.push_back(this->_handshake_handler);      // CRYPTO
  this->_frame_generators.push_back(this->_path_validator);         // PATH_CHALLENGE, PATH_RESPOSNE
  this->_frame_generators.push_back(this->_local_flow_controller);  // MAX_DATA
  this->_frame_generators.push_back(this->_remote_flow_controller); // DATA_BLOCKED
  this->_frame_generators.push_back(this);                          // NEW_TOKEN
  this->_frame_generators.push_back(this->_stream_manager);         // STREAM, MAX_STREAM_DATA, STREAM_DATA_BLOCKED
  this->_frame_generators.push_back(&this->_ack_frame_manager);     // ACK
  this->_frame_generators.push_back(&this->_pinger);                // PING

  // Register frame handlers
  this->_frame_dispatcher->add_handler(this);
  this->_frame_dispatcher->add_handler(this->_stream_manager);
  this->_frame_dispatcher->add_handler(this->_path_validator);
  this->_frame_dispatcher->add_handler(this->_handshake_handler);
}

void
QUICNetVConnection::free(EThread *t)
{
  QUICConDebug("Free connection");

  /* TODO: Uncmment these blocks after refactoring read / write process
    this->_udp_con        = nullptr;
    this->_packet_handler = nullptr;

    _unschedule_packet_write_ready();

    delete this->_handshake_handler;
    delete this->_application_map;
    delete this->_hs_protocol;
    delete this->_loss_detector;
    delete this->_frame_dispatcher;
    delete this->_stream_manager;
    delete this->_congestion_controller;
    if (this->_alt_con_manager) {
      delete this->_alt_con_manager;
    }

    super::clear();
  */
  ALPNSupport::clear();
  this->_packet_handler->close_connection(this);
}

void
QUICNetVConnection::free()
{
  this->free(this_ethread());
}

// called by ET_UDP
void
QUICNetVConnection::remove_connection_ids()
{
  if (this->_ctable) {
    this->_ctable->erase(this->_original_quic_connection_id, this);
    this->_ctable->erase(this->_quic_connection_id, this);
  }

  if (this->_alt_con_manager) {
    this->_alt_con_manager->invalidate_alt_connections();
  }
}

// called by ET_UDP
void
QUICNetVConnection::destroy(EThread *t)
{
  QUICConDebug("Destroy connection");
  /*  TODO: Uncmment these blocks after refactoring read / write process
    if (from_accept_thread) {
      quicNetVCAllocator.free(this);
    } else {
      THREAD_FREE(this, quicNetVCAllocator, t);
    }
  */
}

void
QUICNetVConnection::reenable(VIO *vio)
{
  return;
}

int
QUICNetVConnection::connectUp(EThread *t, int fd)
{
  int res        = 0;
  NetHandler *nh = get_NetHandler(t);
  this->thread   = this_ethread();
  ink_assert(nh->mutex->thread_holding == this->thread);

  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_pre_handshake);

  if ((res = nh->startIO(this)) < 0) {
    // FIXME: startIO only return 0 now! what should we do if it failed ?
  }

  nh->startCop(this);

  // FIXME: complete do_io_xxxx instead
  this->read.enabled = 1;

  this->start();

  // start QUIC handshake
  this->_schedule_packet_write_ready();

  return CONNECT_SUCCESS;
}

QUICConnectionId
QUICNetVConnection::peer_connection_id() const
{
  return this->_peer_quic_connection_id;
}

QUICConnectionId
QUICNetVConnection::original_connection_id() const
{
  return this->_original_quic_connection_id;
}

QUICConnectionId
QUICNetVConnection::first_connection_id() const
{
  return this->_first_quic_connection_id;
}

QUICConnectionId
QUICNetVConnection::connection_id() const
{
  return this->_quic_connection_id;
}

/*
 Return combination of dst connection id and src connection id for debug log
 e.g. "aaaaaaaa-bbbbbbbb"
   - "aaaaaaaa" : high 32 bit of dst connection id
   - "bbbbbbbb" : high 32 bit of src connection id
 */
std::string_view
QUICNetVConnection::cids() const
{
  return this->_cids;
}

const QUICFiveTuple
QUICNetVConnection::five_tuple() const
{
  return this->_five_tuple;
}

uint32_t
QUICNetVConnection::pmtu() const
{
  return this->_pmtu;
}

NetVConnectionContext_t
QUICNetVConnection::direction() const
{
  return this->netvc_context;
}

uint32_t
QUICNetVConnection::_minimum_quic_packet_size()
{
  if (netvc_context == NET_VCONNECTION_OUT) {
    // FIXME Only the first packet need to be 1200 bytes at least
    return MINIMUM_INITIAL_PACKET_SIZE;
  } else {
    // FIXME This size should be configurable and should have some randomness
    // This is just for providing protection against packet analysis for protected packets
    return 32 + (this->_rnd() & 0x3f); // 32 to 96
  }
}

uint32_t
QUICNetVConnection::_maximum_quic_packet_size() const
{
  if (this->options.ip_family == PF_INET6) {
    return this->_pmtu - UDP_HEADER_SIZE - IPV6_HEADER_SIZE;
  } else {
    return this->_pmtu - UDP_HEADER_SIZE - IPV4_HEADER_SIZE;
  }
}

uint64_t
QUICNetVConnection::_maximum_stream_frame_data_size()
{
  return this->_maximum_quic_packet_size() - MAX_STREAM_FRAME_OVERHEAD - MAX_PACKET_OVERHEAD;
}

QUICStreamManager *
QUICNetVConnection::stream_manager()
{
  return this->_stream_manager;
}

void
QUICNetVConnection::handle_received_packet(UDPPacket *packet)
{
  this->_packet_recv_queue.enqueue(packet);
}

void
QUICNetVConnection::ping()
{
  this->_pinger.request(QUICEncryptionLevel::ONE_RTT);
}

void
QUICNetVConnection::close(QUICConnectionErrorUPtr error)
{
  if (this->handler == reinterpret_cast<ContinuationHandler>(&QUICNetVConnection::state_connection_closed) ||
      this->handler == reinterpret_cast<ContinuationHandler>(&QUICNetVConnection::state_connection_closing)) {
    // do nothing
  } else {
    this->_switch_to_closing_state(std::move(error));
  }
}

std::vector<QUICFrameType>
QUICNetVConnection::interests()
{
  return {QUICFrameType::CONNECTION_CLOSE, QUICFrameType::DATA_BLOCKED, QUICFrameType::MAX_DATA};
}

QUICConnectionErrorUPtr
QUICNetVConnection::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;

  switch (frame.type()) {
  case QUICFrameType::MAX_DATA:
    this->_remote_flow_controller->forward_limit(static_cast<const QUICMaxDataFrame &>(frame).maximum_data());
    QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                this->_remote_flow_controller->current_limit());
    this->_schedule_packet_write_ready();
    break;
  case QUICFrameType::DATA_BLOCKED:
    // DATA_BLOCKED frame is for debugging. Nothing to do here.
    break;
  case QUICFrameType::CONNECTION_CLOSE:
    if (this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_closed) ||
        this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_draining)) {
      return error;
    }

    // 7.9.1. Closing and Draining Connection States
    // An endpoint MAY transition from the closing period to the draining period if it can confirm that its peer is also closing or
    // draining. Receiving a closing frame is sufficient confirmation, as is receiving a stateless reset.
    {
      uint16_t error_code = static_cast<const QUICConnectionCloseFrame &>(frame).error_code();
      this->_switch_to_draining_state(
        QUICConnectionErrorUPtr(std::make_unique<QUICConnectionError>(static_cast<QUICTransErrorCode>(error_code))));
    }
    break;
  default:
    QUICConDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

// XXX Setup QUICNetVConnection on regular EThread.
// QUICNetVConnection::init() might be called on ET_UDP EThread.
int
QUICNetVConnection::state_pre_handshake(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  // this->thread should be assigned on any direction
  ink_assert(this->thread == this_ethread());

  if (!this->nh) {
    this->nh = get_NetHandler(this_ethread());
  }

  // FIXME: Should be accept_no_activity_timeout?
  if (this->get_context() == NET_VCONNECTION_IN) {
    this->set_inactivity_timeout(HRTIME_MSECONDS(this->_quic_config->no_activity_timeout_in()));
  } else {
    this->set_inactivity_timeout(HRTIME_MSECONDS(this->_quic_config->no_activity_timeout_out()));
  }

  this->add_to_active_queue();

  this->_switch_to_handshake_state();
  return this->handleEvent(event, data);
}

// TODO: Timeout by active_timeout
int
QUICNetVConnection::state_handshake(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
    this->_switch_to_established_state();
    return this->handleEvent(event, data);
  }

  QUICConnectionErrorUPtr error = nullptr;

  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    QUICPacketCreationResult result;
    net_activity(this, this_ethread());
    do {
      QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
      if (result == QUICPacketCreationResult::NOT_READY) {
        error = nullptr;
      } else if (result == QUICPacketCreationResult::FAILED) {
        // Don't make this error, and discard the packet.
        // Because:
        // - Attacker can terminate connections
        // - It could be just an errora on lower layer
        error = nullptr;
      } else if (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::UNSUPPORTED) {
        error = this->_state_handshake_process_packet(*packet);
      }

      // if we complete handshake, switch to establish state
      if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
        this->_switch_to_established_state();
        return this->handleEvent(event, data);
      }

    } while (error == nullptr && (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::IGNORED));
    break;
  }
  case QUIC_EVENT_ACK_PERIODIC:
    this->_handle_periodic_ack_event();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    // TODO: support RETRY packet
    error = this->_state_common_send_packet();
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);
    break;
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_handle_path_validation_timeout(data);
    break;
  case EVENT_IMMEDIATE:
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
  }

  if (error != nullptr) {
    this->_handle_error(std::move(error));
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_established(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  QUICConnectionErrorUPtr error = nullptr;
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    error = this->_state_connection_established_receive_packet();
    break;
  case QUIC_EVENT_ACK_PERIODIC:
    this->_handle_periodic_ack_event();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    error = this->_state_common_send_packet();
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);
    break;
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_handle_path_validation_timeout(data);
    break;
  case EVENT_IMMEDIATE:
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
  }

  if (error != nullptr) {
    QUICConDebug("QUICError: cls=%u, code=0x%" PRIx16, static_cast<unsigned int>(error->cls), error->code);
    this->_handle_error(std::move(error));
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_closing(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  QUICConnectionErrorUPtr error = nullptr;
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    error = this->_state_closing_receive_packet();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    this->_state_closing_send_packet();
    break;
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_handle_path_validation_timeout(data);
    break;
  case QUIC_EVENT_CLOSING_TIMEOUT:
    this->_close_closing_timeout(data);
    this->_switch_to_close_state();
    break;
  case QUIC_EVENT_ACK_PERIODIC:
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
    ink_assert(false);
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_connection_draining(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  QUICConnectionErrorUPtr error = nullptr;
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    error = this->_state_draining_receive_packet();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    // Do not send any packets in this state.
    // This should be the only difference between this and closing_state.
    this->_close_packet_write_ready(data);
    break;
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_handle_path_validation_timeout(data);
    break;
  case QUIC_EVENT_CLOSING_TIMEOUT:
    this->_close_closing_timeout(data);
    this->_switch_to_close_state();
    break;
  case QUIC_EVENT_ACK_PERIODIC:
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
    ink_assert(false);
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_connection_closed(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  switch (event) {
  case QUIC_EVENT_SHUTDOWN: {
    this->_unschedule_ack_manager_periodic();
    this->_unschedule_packet_write_ready();
    this->_unschedule_closing_timeout();
    this->_unschedule_path_validation_timeout();
    this->_close_closed_event(data);
    this->next_inactivity_timeout_at = 0;
    this->next_activity_timeout_at   = 0;

    this->inactivity_timeout_in = 0;
    this->active_timeout_in     = 0;

    // TODO: Drop record from Connection-ID - QUICNetVConnection table in QUICPacketHandler
    // Shutdown loss detector
    SCOPED_MUTEX_LOCK(lock2, this->_loss_detector->mutex, this_ethread());
    this->_loss_detector->handleEvent(QUIC_EVENT_LD_SHUTDOWN, nullptr);

    // FIXME I'm not sure whether we can block here, but it's needed to not crash.
    SCOPED_MUTEX_LOCK(lock, this->nh->mutex, this_ethread());
    if (this->nh) {
      this->nh->free_netvc(this);
    } else {
      this->free(this->mutex->thread_holding);
    }
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);
    break;
  }
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
  }

  return EVENT_DONE;
}

UDPConnection *
QUICNetVConnection::get_udp_con()
{
  return this->_udp_con;
}

void
QUICNetVConnection::net_read_io(NetHandler *nh, EThread *lthread)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  this->handleEvent(QUIC_EVENT_PACKET_READ_READY, nullptr);

  return;
}

int64_t
QUICNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  ink_assert(false);

  return 0;
}

int
QUICNetVConnection::populate_protocol(std::string_view *results, int n) const
{
  int retval = 0;
  if (n > retval) {
    results[retval++] = IP_PROTO_TAG_QUIC;
    if (n > retval) {
      retval += super::populate_protocol(results + retval, n - retval);
    }
  }
  return retval;
}

const char *
QUICNetVConnection::protocol_contains(std::string_view prefix) const
{
  const char *retval   = nullptr;
  std::string_view tag = IP_PROTO_TAG_QUIC;
  if (prefix.size() <= tag.size() && strncmp(tag.data(), prefix.data(), prefix.size()) == 0) {
    retval = tag.data();
  } else {
    retval = super::protocol_contains(prefix);
  }
  return retval;
}

// ALPN TLS extension callback. Given the client's set of offered
// protocols, we have to select a protocol to use for this session.
int
QUICNetVConnection::select_next_protocol(SSL *ssl, const unsigned char **out, unsigned char *outlen, const unsigned char *in,
                                         unsigned inlen) const
{
  const unsigned char *npnptr = nullptr;
  unsigned int npnsize        = 0;
  if (this->getNPN(&npnptr, &npnsize)) {
    // SSL_select_next_proto chooses the first server-offered protocol that appears in the clients protocol set, ie. the
    // server selects the protocol. This is a n^2 search, so it's preferable to keep the protocol set short.
    if (SSL_select_next_proto((unsigned char **)out, outlen, npnptr, npnsize, in, inlen) == OPENSSL_NPN_NEGOTIATED) {
      Debug("ssl", "selected ALPN protocol %.*s", (int)(*outlen), *out);
      return SSL_TLSEXT_ERR_OK;
    }
  }

  *out    = nullptr;
  *outlen = 0;
  return SSL_TLSEXT_ERR_NOACK;
}

bool
QUICNetVConnection::is_closed() const
{
  return this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_closed);
}

QUICPacketNumber
QUICNetVConnection::_largest_acked_packet_number(QUICEncryptionLevel level) const
{
  auto index = QUICTypeUtil::pn_space(level);

  return this->_loss_detector->largest_acked_packet_number(index);
}

std::string_view
QUICNetVConnection::negotiated_application_name() const
{
  const uint8_t *name;
  unsigned int name_len = 0;

  this->_hs_protocol->negotiated_application_name(&name, &name_len);

  return std::string_view(reinterpret_cast<const char *>(name), name_len);
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_packet(const QUICPacket &packet)
{
  QUICConnectionErrorUPtr error = nullptr;
  switch (packet.type()) {
  case QUICPacketType::VERSION_NEGOTIATION:
    error = this->_state_handshake_process_version_negotiation_packet(packet);
    break;
  case QUICPacketType::INITIAL:
    error = this->_state_handshake_process_initial_packet(packet);
    break;
  case QUICPacketType::RETRY:
    error = this->_state_handshake_process_retry_packet(packet);
    break;
  case QUICPacketType::HANDSHAKE:
    error = this->_state_handshake_process_handshake_packet(packet);
    if (this->_pp_key_info.is_decryption_key_available(QUICKeyPhase::INITIAL) && this->netvc_context == NET_VCONNECTION_IN) {
      this->_pp_key_info.drop_keys(QUICKeyPhase::INITIAL);
      this->_minimum_encryption_level = QUICEncryptionLevel::HANDSHAKE;
    }
    break;
  case QUICPacketType::ZERO_RTT_PROTECTED:
    error = this->_state_handshake_process_zero_rtt_protected_packet(packet);
    break;
  case QUICPacketType::PROTECTED:
  default:
    QUICConDebug("Ignore %s(%" PRIu8 ") packet", QUICDebugNames::packet_type(packet.type()), static_cast<uint8_t>(packet.type()));

    error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::INTERNAL_ERROR);
    break;
  }
  return error;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_version_negotiation_packet(const QUICPacket &packet)
{
  QUICConnectionErrorUPtr error = nullptr;

  if (packet.destination_cid() != this->connection_id()) {
    QUICConDebug("Ignore Version Negotiation packet");
    return error;
  }

  if (this->_handshake_handler->is_version_negotiated()) {
    QUICConDebug("ignore VN - already negotiated");
  } else {
    error = this->_handshake_handler->negotiate_version(packet, &this->_packet_factory);

    // discard all transport state except packet number
    this->_loss_detector->reset();

    this->_congestion_controller->reset();

    // start handshake over
    this->_handshake_handler->reset();
    this->_handshake_handler->do_handshake();
    this->_schedule_packet_write_ready();
  }

  return error;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_initial_packet(const QUICPacket &packet)
{
  // QUIC packet could be smaller than MINIMUM_INITIAL_PACKET_SIZE when coalescing packets
  // if (packet->size() < MINIMUM_INITIAL_PACKET_SIZE) {
  //   QUICConDebug("Packet size is smaller than the minimum initial packet size");
  //   // Ignore the packet
  //   return QUICErrorUPtr(new QUICNoError());
  // }

  QUICConnectionErrorUPtr error = nullptr;

  // Start handshake
  if (this->netvc_context == NET_VCONNECTION_IN) {
    if (!this->_alt_con_manager) {
      this->_alt_con_manager =
        new QUICAltConnectionManager(this, *this->_ctable, this->_peer_quic_connection_id, this->_quic_config->instance_id(),
                                     this->_quic_config->num_alt_connection_ids(), this->_quic_config->preferred_address_ipv4(),
                                     this->_quic_config->preferred_address_ipv6());
      this->_frame_generators.push_back(this->_alt_con_manager);
      this->_frame_dispatcher->add_handler(this->_alt_con_manager);
    }
    QUICTPConfigQCP tp_config(this->_quic_config, NET_VCONNECTION_IN);
    error = this->_handshake_handler->start(tp_config, packet, &this->_packet_factory, this->_alt_con_manager->preferred_address());

    // If version negotiation was failed and VERSION NEGOTIATION packet was sent, nothing to do.
    if (this->_handshake_handler->is_version_negotiated()) {
      error = this->_recv_and_ack(packet);

      if (error == nullptr && !this->_handshake_handler->has_remote_tp()) {
        error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
      }
    }
  } else {
    // on client side, _handshake_handler is already started. Just process packet like _state_handshake_process_handshake_packet()
    error = this->_recv_and_ack(packet);
  }

  return error;
}

/**
   This doesn't call this->_recv_and_ack(), because RETRY packet doesn't have any frames.
 */
QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_retry_packet(const QUICPacket &packet)
{
  ink_assert(this->netvc_context == NET_VCONNECTION_OUT);

  if (this->_av_token) {
    QUICConDebug("Ignore RETRY packet - already processed before");
    return nullptr;
  }

  // TODO: move packet->payload to _av_token
  this->_av_token_len = packet.payload_length();
  this->_av_token     = ats_unique_malloc(this->_av_token_len);
  memcpy(this->_av_token.get(), packet.payload(), this->_av_token_len);

  // discard all transport state
  this->_handshake_handler->reset();
  this->_packet_factory.reset();
  this->_loss_detector->reset();

  this->_congestion_controller->reset();
  this->_packet_recv_queue.reset();

  // Initialize Key Materials with peer CID. Because peer CID is DCID of (second) INITIAL packet from client which reply to RETRY
  // packet from server
  this->_hs_protocol->initialize_key_materials(this->_peer_quic_connection_id);

  // start handshake over
  this->_handshake_handler->do_handshake();
  this->_schedule_packet_write_ready();

  return nullptr;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_handshake_packet(const QUICPacket &packet)
{
  // Source address is verified by receiving any message from the client encrypted using the
  // Handshake keys.
  if (this->netvc_context == NET_VCONNECTION_IN && !this->_verfied_state.is_verified()) {
    this->_verfied_state.set_addr_verifed();
  }
  return this->_recv_and_ack(packet);
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_handshake_process_zero_rtt_protected_packet(const QUICPacket &packet)
{
  this->_stream_manager->init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                                  this->_handshake_handler->remote_transport_parameters());
  this->_start_application();
  return this->_recv_and_ack(packet);
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_connection_established_process_protected_packet(const QUICPacket &packet)
{
  QUICConnectionErrorUPtr error = nullptr;
  bool has_non_probing_frame    = false;

  error = this->_recv_and_ack(packet, &has_non_probing_frame);
  if (error != nullptr) {
    return error;
  }

  // Migrate connection if required
  // FIXME Connection migration will be initiated when a peer sent non-probing frames.
  // We need to two or more paths because we need to respond to probing packets on a new path and also need to send other frames
  // on the old path until they initiate migration.
  // if (packet.destination_cid() == this->_quic_connection_id && has_non_probing_frame) {
  if (this->_alt_con_manager != nullptr) {
    if (packet.destination_cid() != this->_quic_connection_id || !ats_ip_addr_port_eq(packet.from(), this->remote_addr)) {
      if (!has_non_probing_frame) {
        QUICConDebug("FIXME: Connection migration has been initiated without non-probing frames");
      }
      error = this->_state_connection_established_migrate_connection(packet);
      if (error != nullptr) {
        return error;
      }
    }
  }

  // For Connection Migration excercise
  if (this->netvc_context == NET_VCONNECTION_OUT && this->_quic_config->cm_exercise_enabled()) {
    this->_state_connection_established_initiate_connection_migration();
  }

  return error;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_connection_established_receive_packet()
{
  QUICConnectionErrorUPtr error = nullptr;
  QUICPacketCreationResult result;

  // Receive a QUIC packet
  net_activity(this, this_ethread());
  do {
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::FAILED) {
      // Don't make this error, and discard the packet.
      // Because:
      // - Attacker can terminate connections
      // - It could be just an errora on lower layer
      continue;
    } else if (result == QUICPacketCreationResult::NO_PACKET) {
      return error;
    } else if (result == QUICPacketCreationResult::NOT_READY) {
      return error;
    } else if (result == QUICPacketCreationResult::IGNORED) {
      continue;
    }

    // Process the packet
    switch (packet->type()) {
    case QUICPacketType::PROTECTED:
      error = this->_state_connection_established_process_protected_packet(*packet);
      break;
    case QUICPacketType::INITIAL:
    case QUICPacketType::HANDSHAKE:
    case QUICPacketType::ZERO_RTT_PROTECTED:
      // Pass packet to _recv_and_ack to send ack to the packet. Stream data will be discarded by offset mismatch.
      error = this->_recv_and_ack(*packet);
      break;
    default:
      QUICConDebug("Unknown packet type: %s(%" PRIu8 ")", QUICDebugNames::packet_type(packet->type()),
                   static_cast<uint8_t>(packet->type()));

      error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::INTERNAL_ERROR);
      break;
    }

  } while (error == nullptr && (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::IGNORED));
  return error;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_closing_receive_packet()
{
  while (this->_packet_recv_queue.size() > 0) {
    QUICPacketCreationResult result;
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::SUCCESS) {
      switch (packet->type()) {
      case QUICPacketType::VERSION_NEGOTIATION:
        // Ignore VN packets on closing state
        break;
      default:
        this->_recv_and_ack(*packet);
        break;
      }
    }
    ++this->_state_closing_recv_packet_count;

    if (this->_state_closing_recv_packet_window < STATE_CLOSING_MAX_RECV_PKT_WIND &&
        this->_state_closing_recv_packet_count >= this->_state_closing_recv_packet_window) {
      this->_state_closing_recv_packet_count = 0;
      this->_state_closing_recv_packet_window <<= 1;

      this->_schedule_packet_write_ready(true);
      break;
    }
  }

  return nullptr;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_draining_receive_packet()
{
  while (this->_packet_recv_queue.size() > 0) {
    QUICPacketCreationResult result;
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::SUCCESS) {
      this->_recv_and_ack(*packet);
      // Do NOT schedule WRITE_READY event from this point.
      // An endpoint in the draining state MUST NOT send any packets.
    }
  }

  return nullptr;
}

/**
 * 1. Check congestion window
 * 2. Allocate buffer for UDP Payload
 * 3. Generate QUIC Packet
 * 4. Store data to the paylaod
 * 5. Send UDP Packet
 */
QUICConnectionErrorUPtr
QUICNetVConnection::_state_common_send_packet()
{
  uint32_t packet_count = 0;
  uint32_t error        = 0;
  while (error == 0 && packet_count < PACKET_PER_EVENT) {
    uint32_t window = this->_congestion_controller->open_window();

    if (window == 0) {
      break;
    }

    Ptr<IOBufferBlock> udp_payload(new_IOBufferBlock());
    uint32_t udp_payload_len = std::min(window, this->_pmtu);
    udp_payload->alloc(iobuffer_size_to_index(udp_payload_len));

    uint32_t written = 0;
    for (int i = static_cast<int>(this->_minimum_encryption_level); i <= static_cast<int>(QUICEncryptionLevel::ONE_RTT); ++i) {
      auto level = QUIC_ENCRYPTION_LEVELS[i];
      if (level == QUICEncryptionLevel::ONE_RTT && !this->_handshake_handler->is_completed()) {
        continue;
      }

      uint32_t max_packet_size = udp_payload_len - written;
      if (this->netvc_context == NET_VCONNECTION_IN && !this->_verfied_state.is_verified()) {
        max_packet_size = std::min(max_packet_size, this->_verfied_state.windows());
      }

      QUICPacketInfoUPtr packet_info = std::make_unique<QUICPacketInfo>();
      QUICPacketUPtr packet          = this->_packetize_frames(level, max_packet_size, packet_info->frames);

      if (packet) {
        packet_info->packet_number    = packet->packet_number();
        packet_info->time_sent        = Thread::get_hrtime();
        packet_info->ack_eliciting    = packet->is_ack_eliciting();
        packet_info->is_crypto_packet = packet->is_crypto_packet();
        packet_info->in_flight        = true;
        if (packet_info->ack_eliciting) {
          packet_info->sent_bytes = packet->size();
        } else {
          packet_info->sent_bytes = 0;
        }
        packet_info->type     = packet->type();
        packet_info->pn_space = QUICTypeUtil::pn_space(level);

        if (this->netvc_context == NET_VCONNECTION_IN && !this->_verfied_state.is_verified()) {
          QUICConDebug("send to unverified window: %u", this->_verfied_state.windows());
          this->_verfied_state.consume(packet->size());
        }

        // TODO: do not write two QUIC Short Header Packets
        uint8_t *buf = reinterpret_cast<uint8_t *>(udp_payload->end());
        size_t len   = 0;
        packet->store(buf, &len);
        udp_payload->fill(len);
        written += len;

        int dcil = (this->_peer_quic_connection_id == QUICConnectionId::ZERO()) ? 0 : this->_peer_quic_connection_id.length();
        this->_ph_protector.protect(buf, len, dcil);

        QUICConDebug("[TX] %s packet #%" PRIu64 " size=%zu", QUICDebugNames::packet_type(packet->type()), packet->packet_number(),
                     len);

        if (this->_pp_key_info.is_encryption_key_available(QUICKeyPhase::INITIAL) && packet->type() == QUICPacketType::HANDSHAKE &&
            this->netvc_context == NET_VCONNECTION_OUT) {
          this->_pp_key_info.drop_keys(QUICKeyPhase::INITIAL);
          this->_minimum_encryption_level = QUICEncryptionLevel::HANDSHAKE;
        }

        this->_loss_detector->on_packet_sent(std::move(packet_info));
        packet_count++;
      }
    }

    if (written) {
      this->_packet_handler->send_packet(this, udp_payload);
    } else {
      udp_payload->dealloc();
      break;
    }
  }

  if (packet_count) {
    QUIC_INCREMENT_DYN_STAT_EX(QUICStats::total_packets_sent_stat, packet_count);
    net_activity(this, this_ethread());
  }

  return nullptr;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_closing_send_packet()
{
  this->_packetize_closing_frame();

  // TODO: should credit of congestion controller be checked?

  // During the closing period, an endpoint that sends a
  // closing frame SHOULD respond to any packet that it receives with
  // another packet containing a closing frame.  To minimize the state
  // that an endpoint maintains for a closing connection, endpoints MAY
  // send the exact same packet.
  if (this->_the_final_packet) {
    this->_packet_handler->send_packet(*this->_the_final_packet, this, this->_ph_protector);
  }

  return nullptr;
}

Ptr<IOBufferBlock>
QUICNetVConnection::_store_frame(Ptr<IOBufferBlock> parent_block, size_t &size_added, uint64_t &max_frame_size, QUICFrame &frame,
                                 std::vector<QUICFrameInfo> &frames)
{
  Ptr<IOBufferBlock> new_block = frame.to_io_buffer_block(max_frame_size);

  size_added             = 0;
  Ptr<IOBufferBlock> tmp = new_block;
  while (tmp) {
    size_added += tmp->size();
    tmp = tmp->next;
  }

  if (parent_block == nullptr) {
    parent_block = new_block;
  } else {
    parent_block->next = new_block;
  }

  // frame should be stored because it's created with max_frame_size
  ink_assert(size_added != 0);

  max_frame_size -= size_added;

  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char msg[1024];
    frame.debug_msg(msg, sizeof(msg));
    QUICConDebug("[TX] | %s", msg);
  }

  frames.emplace_back(frame.id(), frame.generated_by());

  while (parent_block->next) {
    parent_block = parent_block->next;
  }
  return parent_block;
}

// FIXME QUICNetVConnection should not know the actual type value of PADDING frame
Ptr<IOBufferBlock>
QUICNetVConnection::_generate_padding_frame(size_t frame_size)
{
  Ptr<IOBufferBlock> block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  block->alloc(iobuffer_size_to_index(frame_size));
  memset(block->start(), 0, frame_size);
  block->fill(frame_size);

  ink_assert(frame_size == static_cast<size_t>(block->size()));

  return block;
}

QUICPacketUPtr
QUICNetVConnection::_packetize_frames(QUICEncryptionLevel level, uint64_t max_packet_size, std::vector<QUICFrameInfo> &frames)
{
  ink_hrtime timestamp = Thread::get_hrtime();

  QUICPacketUPtr packet = QUICPacketFactory::create_null_packet();
  if (max_packet_size <= MAX_PACKET_OVERHEAD) {
    return packet;
  }

  // TODO: adjust MAX_PACKET_OVERHEAD for each encryption level
  uint64_t max_frame_size = max_packet_size - MAX_PACKET_OVERHEAD;
  if (level == QUICEncryptionLevel::INITIAL && this->_av_token) {
    max_frame_size = max_frame_size - (QUICVariableInt::size(this->_av_token_len) + this->_av_token_len);
  }
  max_frame_size = std::min(max_frame_size, this->_maximum_stream_frame_data_size());

  bool probing                   = false;
  int frame_count                = 0;
  size_t len                     = 0;
  Ptr<IOBufferBlock> first_block = make_ptr<IOBufferBlock>(new_IOBufferBlock());
  Ptr<IOBufferBlock> last_block  = first_block;
  first_block->alloc(iobuffer_size_to_index(0));
  first_block->fill(0);

  if (!this->_has_ack_eliciting_packet_out) {
    // Sent too much ack_only packet. At this moment we need to packetize a ping frame
    // to force peer send ack frame.
    this->_pinger.request(level);
  }

  size_t size_added  = 0;
  bool ack_eliciting = false;
  bool crypto        = false;
  uint8_t frame_instance_buffer[QUICFrame::MAX_INSTANCE_SIZE]; // This is for a frame instance but not serialized frame data
  QUICFrame *frame = nullptr;
  for (auto g : this->_frame_generators) {
    while (g->will_generate_frame(level, timestamp)) {
      // FIXME will_generate_frame should receive more parameters so we don't need extra checks
      if (g == this->_remote_flow_controller && !this->_stream_manager->will_generate_frame(level, timestamp)) {
        break;
      }
      if (g == this->_stream_manager && this->_path_validator->is_validating()) {
        break;
      }

      // Common block
      frame = g->generate_frame(frame_instance_buffer, level, this->_remote_flow_controller->credit(), max_frame_size, timestamp);
      if (frame) {
        ++frame_count;
        probing |= frame->is_probing_frame();
        if (frame->is_flow_controlled()) {
          int ret = this->_remote_flow_controller->update(this->_stream_manager->total_offset_sent());
          QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                      this->_remote_flow_controller->current_limit());
          ink_assert(ret == 0);
        }
        last_block = this->_store_frame(last_block, size_added, max_frame_size, *frame, frames);
        len += size_added;

        // FIXME ACK frame should have priority
        if (frame->type() == QUICFrameType::STREAM) {
          if (++this->_stream_frames_sent % MAX_CONSECUTIVE_STREAMS == 0) {
            break;
          }
        }

        if (!ack_eliciting && frame->type() != QUICFrameType::ACK) {
          ack_eliciting = true;
          this->_pinger.cancel(level);
        }

        if (frame->type() == QUICFrameType::CRYPTO &&
            (level == QUICEncryptionLevel::INITIAL || level == QUICEncryptionLevel::HANDSHAKE)) {
          crypto = true;
        }

        frame->~QUICFrame();
      } else {
        // Move to next generator
        break;
      }
    }
  }

  // Schedule a packet
  if (len != 0) {
    if (level == QUICEncryptionLevel::INITIAL && this->netvc_context == NET_VCONNECTION_OUT) {
      // Pad with PADDING frames
      uint64_t min_size = this->_minimum_quic_packet_size();
      if (this->_av_token) {
        min_size = min_size - this->_av_token_len;
      }
      min_size = std::min(min_size, max_packet_size);

      if (min_size > len) {
        Ptr<IOBufferBlock> pad_block = _generate_padding_frame(min_size - len);
        last_block->next             = pad_block;
        len += pad_block->size();
      }
    } else {
      // Pad with PADDING frames to make sure payload length satisfy the minimum length for sampling for header protection
      if (MIN_PKT_PAYLOAD_LEN > len) {
        Ptr<IOBufferBlock> pad_block = _generate_padding_frame(MIN_PKT_PAYLOAD_LEN - len);
        last_block->next             = pad_block;
        len += pad_block->size();
      }
    }

    // Packet is retransmittable if it's not ack only packet
    packet                              = this->_build_packet(level, first_block, ack_eliciting, probing, crypto);
    this->_has_ack_eliciting_packet_out = ack_eliciting;
  }

  return packet;
}

void
QUICNetVConnection::_packetize_closing_frame()
{
  if (this->_connection_error == nullptr || this->_the_final_packet) {
    return;
  }

  QUICFrame *frame = nullptr;

  // CONNECTION_CLOSE
  uint8_t frame_buf[QUICFrame::MAX_INSTANCE_SIZE];
  frame = QUICFrameFactory::create_connection_close_frame(frame_buf, *this->_connection_error);

  uint32_t max_size = this->_maximum_quic_packet_size();

  size_t size_added       = 0;
  uint64_t max_frame_size = static_cast<uint64_t>(max_size);
  std::vector<QUICFrameInfo> frames;
  Ptr<IOBufferBlock> parent_block;
  parent_block = nullptr;
  parent_block = this->_store_frame(parent_block, size_added, max_frame_size, *frame, frames);

  QUICEncryptionLevel level = this->_hs_protocol->current_encryption_level();
  ink_assert(level != QUICEncryptionLevel::ZERO_RTT);
  this->_the_final_packet = this->_build_packet(level, parent_block, true, false, false);
}

QUICConnectionErrorUPtr
QUICNetVConnection::_recv_and_ack(const QUICPacket &packet, bool *has_non_probing_frame)
{
  ink_assert(packet.type() != QUICPacketType::RETRY);

  const uint8_t *payload      = packet.payload();
  uint16_t size               = packet.payload_length();
  QUICPacketNumber packet_num = packet.packet_number();
  QUICEncryptionLevel level   = QUICTypeUtil::encryption_level(packet.type());

  bool ack_only;
  bool is_flow_controlled;

  QUICConnectionErrorUPtr error = nullptr;
  if (has_non_probing_frame) {
    *has_non_probing_frame = false;
  }

  error = this->_frame_dispatcher->receive_frames(level, payload, size, ack_only, is_flow_controlled, has_non_probing_frame);
  if (error != nullptr) {
    return error;
  }

  if (is_flow_controlled) {
    int ret = this->_local_flow_controller->update(this->_stream_manager->total_offset_received());
    QUICFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
                this->_local_flow_controller->current_limit());

    if (ret != 0) {
      return std::make_unique<QUICConnectionError>(QUICTransErrorCode::FLOW_CONTROL_ERROR);
    }

    this->_local_flow_controller->forward_limit(this->_stream_manager->total_reordered_bytes() + this->_flow_control_buffer_size);
    QUICFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
                this->_local_flow_controller->current_limit());
  }

  this->_ack_frame_manager.update(level, packet_num, size, ack_only);

  return error;
}

QUICPacketUPtr
QUICNetVConnection::_build_packet(QUICEncryptionLevel level, Ptr<IOBufferBlock> parent_block, bool ack_eliciting, bool probing,
                                  bool crypto)
{
  QUICPacketType type   = QUICTypeUtil::packet_type(level);
  QUICPacketUPtr packet = QUICPacketFactory::create_null_packet();

  // FIXME Pass parent_block to create_x_packet
  // No need to make a unique buf here
  ats_unique_buf buf = ats_unique_malloc(2048);
  uint8_t *raw_buf   = buf.get();
  size_t len         = 0;
  while (parent_block) {
    memcpy(raw_buf + len, parent_block->start(), parent_block->size());
    len += parent_block->size();
    parent_block = parent_block->next;
  }

  switch (type) {
  case QUICPacketType::INITIAL: {
    QUICConnectionId dcid = this->_peer_quic_connection_id;
    ats_unique_buf token  = {nullptr};
    size_t token_len      = 0;

    if (this->netvc_context == NET_VCONNECTION_OUT) {
      // TODO: Add a case of using token which is advertized by NEW_TOKEN frame
      if (this->_av_token) {
        token     = ats_unique_malloc(this->_av_token_len);
        token_len = this->_av_token_len;
        memcpy(token.get(), this->_av_token.get(), token_len);
      } else {
        dcid = this->_original_quic_connection_id;
      }
    }

    packet = this->_packet_factory.create_initial_packet(
      dcid, this->_quic_connection_id, this->_largest_acked_packet_number(QUICEncryptionLevel::INITIAL), std::move(buf), len,
      ack_eliciting, probing, crypto, std::move(token), token_len);
    break;
  }
  case QUICPacketType::HANDSHAKE: {
    packet = this->_packet_factory.create_handshake_packet(this->_peer_quic_connection_id, this->_quic_connection_id,
                                                           this->_largest_acked_packet_number(QUICEncryptionLevel::HANDSHAKE),
                                                           std::move(buf), len, ack_eliciting, probing, crypto);
    break;
  }
  case QUICPacketType::ZERO_RTT_PROTECTED: {
    packet = this->_packet_factory.create_zero_rtt_packet(this->_original_quic_connection_id, this->_quic_connection_id,
                                                          this->_largest_acked_packet_number(QUICEncryptionLevel::ZERO_RTT),
                                                          std::move(buf), len, ack_eliciting, probing);
    break;
  }
  case QUICPacketType::PROTECTED: {
    packet = this->_packet_factory.create_protected_packet(this->_peer_quic_connection_id,
                                                           this->_largest_acked_packet_number(QUICEncryptionLevel::ONE_RTT),
                                                           std::move(buf), len, ack_eliciting, probing);
    break;
  }
  default:
    // should not be here
    ink_assert(false);
    break;
  }

  return packet;
}

void
QUICNetVConnection::_init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                              const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
  this->_stream_manager->init_flow_control_params(local_tp, remote_tp);

  uint64_t local_initial_max_data  = 0;
  uint64_t remote_initial_max_data = 0;
  if (local_tp) {
    local_initial_max_data          = local_tp->getAsUInt(QUICTransportParameterId::INITIAL_MAX_DATA);
    this->_flow_control_buffer_size = local_initial_max_data;
  }
  if (remote_tp) {
    remote_initial_max_data = remote_tp->getAsUInt(QUICTransportParameterId::INITIAL_MAX_DATA);
  }

  this->_local_flow_controller->set_limit(local_initial_max_data);
  this->_remote_flow_controller->set_limit(remote_initial_max_data);
  QUICFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
              this->_local_flow_controller->current_limit());
  QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
              this->_remote_flow_controller->current_limit());
}

void
QUICNetVConnection::_handle_error(QUICConnectionErrorUPtr error)
{
  QUICError("QUICError: %s (%u), %s (0x%" PRIx16 ")", QUICDebugNames::error_class(error->cls),
            static_cast<unsigned int>(error->cls), QUICDebugNames::error_code(error->code), error->code);

  // Connection Error
  this->close(std::move(error));
}

QUICPacketUPtr
QUICNetVConnection::_dequeue_recv_packet(QUICPacketCreationResult &result)
{
  QUICPacketUPtr packet = this->_packet_recv_queue.dequeue(result);

  if (result == QUICPacketCreationResult::SUCCESS) {
    if (this->direction() == NET_VCONNECTION_OUT) {
      // Reset CID if a server sent back a new CID
      // FIXME This should happen only once
      QUICConnectionId src_cid = packet->source_cid();
      // FIXME src connection id could be zero ? if so, check packet header type.
      if (src_cid != QUICConnectionId::ZERO()) {
        if (this->_peer_quic_connection_id != src_cid) {
          this->_update_peer_cid(src_cid);
        }
      }
    }

    if (!this->_verfied_state.is_verified()) {
      this->_verfied_state.fill(packet->size());
    }
  }

  // Debug prints
  switch (result) {
  case QUICPacketCreationResult::NO_PACKET:
    break;
  case QUICPacketCreationResult::NOT_READY:
    QUICConDebug("Not ready to decrypt the packet");
    break;
  case QUICPacketCreationResult::IGNORED:
    QUICConDebug("Ignored");
    break;
  case QUICPacketCreationResult::UNSUPPORTED:
    QUICConDebug("Unsupported version");
    break;
  case QUICPacketCreationResult::SUCCESS:
    if (packet->type() == QUICPacketType::VERSION_NEGOTIATION) {
      QUICConDebug("[RX] %s packet size=%u", QUICDebugNames::packet_type(packet->type()), packet->size());
    } else {
      QUICConDebug("[RX] %s packet #%" PRIu64 " size=%u header_len=%u payload_len=%u", QUICDebugNames::packet_type(packet->type()),
                   packet->packet_number(), packet->size(), packet->header_size(), packet->payload_length());
    }
    break;
  default:
    QUICConDebug("Failed to decrypt the packet");
    break;
  }

  return packet;
}

void
QUICNetVConnection::_schedule_packet_write_ready(bool delay)
{
  if (!this->_packet_write_ready) {
    QUICConVVVDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_PACKET_WRITE_READY));
    if (delay) {
      this->_packet_write_ready = this->thread->schedule_in(this, WRITE_READY_INTERVAL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
    } else {
      this->_packet_write_ready = this->thread->schedule_imm(this, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
    }
  }
}

void
QUICNetVConnection::_unschedule_packet_write_ready()
{
  if (this->_packet_write_ready) {
    this->_packet_write_ready->cancel();
    this->_packet_write_ready = nullptr;
  }
}

void
QUICNetVConnection::_close_packet_write_ready(Event *data)
{
  ink_assert(this->_packet_write_ready == data);
  this->_packet_write_ready = nullptr;
}

void
QUICNetVConnection::_schedule_closing_timeout(ink_hrtime interval)
{
  if (!this->_closing_timeout) {
    QUICConDebug("Schedule %s event in %" PRIu64 "ms", QUICDebugNames::quic_event(QUIC_EVENT_CLOSING_TIMEOUT),
                 interval / HRTIME_MSECOND);
    this->_closing_timeout = this->thread->schedule_in_local(this, interval, QUIC_EVENT_CLOSING_TIMEOUT);
  }
}

void
QUICNetVConnection::_unschedule_closing_timeout()
{
  if (this->_closing_timeout) {
    QUICConDebug("Unschedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_CLOSING_TIMEOUT));
    this->_closing_timeout->cancel();
    this->_closing_timeout = nullptr;
  }
}

void
QUICNetVConnection::_schedule_ack_manager_periodic(ink_hrtime interval)
{
  this->_ack_manager_periodic = this->thread->schedule_every(this, interval, QUIC_EVENT_ACK_PERIODIC);
}

void
QUICNetVConnection::_unschedule_ack_manager_periodic()
{
  if (this->_ack_manager_periodic) {
    QUICConDebug("Unschedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_ACK_PERIODIC));
    this->_ack_manager_periodic->cancel();
    this->_ack_manager_periodic = nullptr;
  }
}

void
QUICNetVConnection::_close_closing_timeout(Event *data)
{
  ink_assert(this->_closing_timeout == data);
  this->_closing_timeout = nullptr;
}

void
QUICNetVConnection::_schedule_closed_event()
{
  if (!this->_closed_event) {
    QUICConDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_SHUTDOWN));
    this->_closed_event = this->thread->schedule_imm(this, QUIC_EVENT_SHUTDOWN, nullptr);
  }
}

void
QUICNetVConnection::_unschedule_closed_event()
{
  if (!this->_closed_event) {
    QUICConDebug("Unschedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_SHUTDOWN));
    this->_closed_event->cancel();
    this->_closed_event = nullptr;
  }
}

void
QUICNetVConnection::_close_closed_event(Event *data)
{
  ink_assert(this->_closed_event == data);
  this->_closed_event = nullptr;
}

int
QUICNetVConnection::_complete_handshake_if_possible()
{
  if (this->handler != reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_handshake)) {
    return 0;
  }

  if (!(this->_handshake_handler && this->_handshake_handler->is_completed())) {
    return -1;
  }

  if (this->netvc_context == NET_VCONNECTION_OUT && !this->_handshake_handler->has_remote_tp()) {
    return -1;
  }

  this->_init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                  this->_handshake_handler->remote_transport_parameters());

  // PN space doesn't matter but seems like this is the way to pick the LossDetector for 0-RTT and Short packet
  uint64_t ack_delay_exponent =
    this->_handshake_handler->remote_transport_parameters()->getAsUInt(QUICTransportParameterId::ACK_DELAY_EXPONENT);
  this->_loss_detector->update_ack_delay_exponent(ack_delay_exponent);

  this->_start_application();

  return 0;
}

void
QUICNetVConnection::_schedule_path_validation_timeout(ink_hrtime interval)
{
  if (!this->_path_validation_timeout) {
    QUICConDebug("Schedule %s event in %" PRIu64 "ms", QUICDebugNames::quic_event(QUIC_EVENT_PATH_VALIDATION_TIMEOUT),
                 interval / HRTIME_MSECOND);
    this->_path_validation_timeout = this->thread->schedule_in_local(this, interval, QUIC_EVENT_PATH_VALIDATION_TIMEOUT);
  }
}

void
QUICNetVConnection::_unschedule_path_validation_timeout()
{
  if (this->_path_validation_timeout) {
    QUICConDebug("Unschedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_PATH_VALIDATION_TIMEOUT));
    this->_path_validation_timeout->cancel();
    this->_path_validation_timeout = nullptr;
  }
}

void
QUICNetVConnection::_close_path_validation_timeout(Event *data)
{
  ink_assert(this->_path_validation_timeout == data);
  this->_path_validation_timeout = nullptr;
}

void
QUICNetVConnection::_start_application()
{
  if (!this->_application_started) {
    this->_application_started = true;

    const uint8_t *app_name;
    unsigned int app_name_len = 0;
    this->_handshake_handler->negotiated_application_name(&app_name, &app_name_len);
    if (app_name == nullptr) {
      app_name     = reinterpret_cast<const uint8_t *>(IP_PROTO_TAG_HTTP_QUIC.data());
      app_name_len = IP_PROTO_TAG_HTTP_QUIC.size();
    }

    if (netvc_context == NET_VCONNECTION_IN) {
      if (!this->setSelectedProtocol(app_name, app_name_len)) {
        this->_handle_error(std::make_unique<QUICConnectionError>(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR));
      } else {
        this->endpoint()->handleEvent(NET_EVENT_ACCEPT, this);
      }
    } else {
      this->action_.continuation->handleEvent(NET_EVENT_OPEN, this);
    }
  }
}

void
QUICNetVConnection::_switch_to_handshake_state()
{
  QUICConDebug("Enter state_handshake");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);
}

void
QUICNetVConnection::_switch_to_established_state()
{
  if (this->_complete_handshake_if_possible() == 0) {
    QUICConDebug("Enter state_connection_established");
    QUICConDebug("Negotiated cipher suite: %s", this->_handshake_handler->negotiated_cipher_suite());

    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_established);

    if (this->direction() == NET_VCONNECTION_OUT) {
      std::shared_ptr<const QUICTransportParameters> remote_tp = this->_handshake_handler->remote_transport_parameters();
      const uint8_t *pref_addr_buf;
      uint16_t len;
      pref_addr_buf          = remote_tp->getAsBytes(QUICTransportParameterId::PREFERRED_ADDRESS, len);
      this->_alt_con_manager = new QUICAltConnectionManager(this, *this->_ctable, this->_peer_quic_connection_id,
                                                            this->_quic_config->instance_id(), 1, {pref_addr_buf, len});
      this->_frame_generators.push_back(this->_alt_con_manager);
      this->_frame_dispatcher->add_handler(this->_alt_con_manager);
    }
  } else {
    // Illegal state change
    ink_assert(!"Handshake has to be completed");
  }
}

void
QUICNetVConnection::_switch_to_closing_state(QUICConnectionErrorUPtr error)
{
  if (this->_complete_handshake_if_possible() != 0) {
    QUICConDebug("Switching state without handshake completion");
  }
  if (error->msg) {
    QUICConDebug("Reason: %.*s", static_cast<int>(strlen(error->msg)), error->msg);
  }

  // Once we are in closing or draining state, the ack_manager is not needed anymore. Because we don't send
  // any frame other than close_frame.
  this->_unschedule_ack_manager_periodic();

  this->_connection_error = std::move(error);
  this->_schedule_packet_write_ready();

  this->remove_from_active_queue();
  this->set_inactivity_timeout(0);

  ink_hrtime rto = this->_rtt_measure.current_pto_period();

  QUICConDebug("Enter state_connection_closing");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closing);

  // This states SHOULD persist for three times the
  // current Retransmission Timeout (RTO) interval as defined in
  // [QUIC-RECOVERY].
  this->_schedule_closing_timeout(3 * rto);
}

void
QUICNetVConnection::_switch_to_draining_state(QUICConnectionErrorUPtr error)
{
  if (this->_complete_handshake_if_possible() != 0) {
    QUICConDebug("Switching state without handshake completion");
  }
  if (error->msg) {
    QUICConDebug("Reason: %.*s", static_cast<int>(strlen(error->msg)), error->msg);
  }

  // Once we are in closing or draining state, the ack_manager is not needed anymore. Because we don't send
  // any frame other than close_frame.
  this->_unschedule_ack_manager_periodic();

  this->remove_from_active_queue();
  this->set_inactivity_timeout(0);

  ink_hrtime rto = this->_rtt_measure.current_pto_period();

  QUICConDebug("Enter state_connection_draining");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_draining);

  // This states SHOULD persist for three times the
  // current Retransmission Timeout (RTO) interval as defined in
  // [QUIC-RECOVERY].

  this->_schedule_closing_timeout(3 * rto);
}

void
QUICNetVConnection::_switch_to_close_state()
{
  this->_unschedule_closing_timeout();
  this->_unschedule_path_validation_timeout();

  if (this->_complete_handshake_if_possible() != 0) {
    QUICConDebug("Switching state without handshake completion");
  }
  QUICConDebug("Enter state_connection_closed");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closed);
  this->_schedule_closed_event();
}

void
QUICNetVConnection::_handle_idle_timeout()
{
  this->remove_from_active_queue();
  this->_switch_to_draining_state(std::make_unique<QUICConnectionError>(QUICTransErrorCode::NO_ERROR, "Idle Timeout"));

  // TODO: signal VC_EVENT_ACTIVE_TIMEOUT/VC_EVENT_INACTIVITY_TIMEOUT to application
}

void
QUICNetVConnection::_validate_new_path()
{
  this->_path_validator->validate();
  // Not sure how long we should wait. The spec says just "enough time".
  // Use the same time amount as the closing timeout.
  ink_hrtime rto = this->_rtt_measure.current_pto_period();
  this->_schedule_path_validation_timeout(3 * rto);
}

void
QUICNetVConnection::_update_cids()
{
  snprintf(this->_cids_data, sizeof(this->_cids_data), "%08" PRIx32 "-%08" PRIx32 "", this->_peer_quic_connection_id.h32(),
           this->_quic_connection_id.h32());

  this->_cids = {this->_cids_data, sizeof(this->_cids_data)};
}

void
QUICNetVConnection::_update_peer_cid(const QUICConnectionId &new_cid)
{
  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char old_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    char new_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    this->_peer_quic_connection_id.hex(old_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    new_cid.hex(new_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

    QUICConDebug("dcid: %s -> %s", old_cid_str, new_cid_str);
  }

  this->_peer_old_quic_connection_id = this->_peer_quic_connection_id;
  this->_peer_quic_connection_id     = new_cid;
  this->_update_cids();
}

void
QUICNetVConnection::_update_local_cid(const QUICConnectionId &new_cid)
{
  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char old_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    char new_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    this->_quic_connection_id.hex(old_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    new_cid.hex(new_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

    QUICConDebug("scid: %s -> %s", old_cid_str, new_cid_str);
  }

  this->_quic_connection_id = new_cid;
  this->_update_cids();
}

void
QUICNetVConnection::_rerandomize_original_cid()
{
  QUICConnectionId tmp = this->_original_quic_connection_id;
  this->_original_quic_connection_id.randomize();

  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char old_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    char new_cid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
    tmp.hex(old_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
    this->_original_quic_connection_id.hex(new_cid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);

    QUICConDebug("original cid: %s -> %s", old_cid_str, new_cid_str);
  }
}

QUICHandshakeProtocol *
QUICNetVConnection::_setup_handshake_protocol(shared_SSL_CTX ctx)
{
  // Initialize handshake protocol specific stuff
  // For QUICv1 TLS is the only option
  QUICTLS *tls = new QUICTLS(this->_pp_key_info, ctx.get(), this->direction(), this->options,
                             this->_quic_config->client_session_file(), this->_quic_config->client_keylog_file());
  SSL_set_ex_data(tls->ssl_handle(), QUIC::ssl_quic_qc_index, static_cast<QUICConnection *>(this));

  return tls;
}

QUICConnectionErrorUPtr
QUICNetVConnection::_state_connection_established_migrate_connection(const QUICPacket &p)
{
  ink_assert(this->_handshake_handler->is_completed());

  QUICConnectionErrorUPtr error = nullptr;
  QUICConnectionId dcid         = p.destination_cid();

  if (this->netvc_context == NET_VCONNECTION_IN) {
    if (!this->_alt_con_manager->is_ready_to_migrate()) {
      // TODO: Should endpoint send connection error when remote endpoint doesn't send NEW_CONNECTION_ID frames before initiating
      // connection migration ?
      QUICConDebug("Ignore connection migration - remote endpoint initiated CM before sending NEW_CONNECTION_ID frames");
      return error;
    }
    QUICConDebug("Connection migration is initiated by remote");
  }

  if (this->connection_id() == dcid) {
    // On client side (NET_VCONNECTION_OUT), nothing to do any more
    if (this->netvc_context == NET_VCONNECTION_IN) {
      Connection con;
      con.setRemote(&(p.from().sa));
      this->con.move(con);
      this->set_remote_addr();
      this->_udp_con = p.udp_con();
      this->_validate_new_path();
    }
  } else {
    if (this->_alt_con_manager->migrate_to(dcid, this->_reset_token)) {
      // DCID of received packet is local cid
      this->_update_local_cid(dcid);

      // On client side (NET_VCONNECTION_OUT), nothing to do any more
      if (this->netvc_context == NET_VCONNECTION_IN) {
        Connection con;
        con.setRemote(&(p.from().sa));
        this->con.move(con);
        this->set_remote_addr();
        this->_udp_con = p.udp_con();

        this->_update_peer_cid(this->_alt_con_manager->migrate_to_alt_cid());
        this->_validate_new_path();
      }
    } else {
      char dcid_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
      dcid.hex(dcid_str, QUICConnectionId::MAX_HEX_STR_LENGTH);
      QUICConDebug("Connection migration failed cid=%s", dcid_str);
    }
  }

  return error;
}

/**
 * Connection Migration Excercise from client
 */
QUICConnectionErrorUPtr
QUICNetVConnection::_state_connection_established_initiate_connection_migration()
{
  ink_assert(this->_handshake_handler->is_completed());
  ink_assert(this->netvc_context == NET_VCONNECTION_OUT);

  QUICConnectionErrorUPtr error = nullptr;
  ink_hrtime timestamp          = Thread::get_hrtime();

  std::shared_ptr<const QUICTransportParameters> remote_tp = this->_handshake_handler->remote_transport_parameters();

  if (this->_connection_migration_initiated || remote_tp->contains(QUICTransportParameterId::DISABLE_MIGRATION) ||
      !this->_alt_con_manager->is_ready_to_migrate() ||
      this->_alt_con_manager->will_generate_frame(QUICEncryptionLevel::ONE_RTT, timestamp)) {
    return error;
  }

  QUICConDebug("Initiated connection migration");
  this->_connection_migration_initiated = true;

  this->_update_peer_cid(this->_alt_con_manager->migrate_to_alt_cid());

  this->_validate_new_path();

  return error;
}

void
QUICNetVConnection::_handle_periodic_ack_event()
{
  ink_hrtime timestamp = Thread::get_hrtime();
  bool need_schedule   = false;
  for (int i = static_cast<int>(this->_minimum_encryption_level); i <= static_cast<int>(QUICEncryptionLevel::ONE_RTT); ++i) {
    if (this->_ack_frame_manager.will_generate_frame(QUIC_ENCRYPTION_LEVELS[i], timestamp)) {
      need_schedule = true;
      break;
    }
  }

  if (need_schedule) {
    // we have ack to send
    // FIXME: should sent depend on socket event.
    this->_schedule_packet_write_ready();
  }
}

void
QUICNetVConnection::_handle_path_validation_timeout(Event *data)
{
  this->_close_path_validation_timeout(data);
  if (this->_path_validator->is_validated()) {
    QUICConDebug("Path validated");
    this->_alt_con_manager->drop_cid(this->_peer_old_quic_connection_id);
    // FIXME This is a kind of workaround for connection migration.
    // This PING make peer to send an ACK frame so that ATS can detect packet loss.
    // It would be better if QUICLossDetector could detect the loss in another way.
    this->ping();
  } else {
    QUICConDebug("Path validation failed");
    this->_switch_to_close_state();
  }
}

// QUICFrameGenerator
bool
QUICNetVConnection::will_generate_frame(QUICEncryptionLevel level, ink_hrtime timestamp)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return !this->_is_resumption_token_sent;
}

QUICFrame *
QUICNetVConnection::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                                   ink_hrtime timestamp)
{
  QUICFrame *frame = nullptr;

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  if (this->_is_resumption_token_sent) {
    return frame;
  }

  if (this->direction() == NET_VCONNECTION_IN) {
    // TODO Make expiration period configurable
    QUICResumptionToken token(this->get_remote_endpoint(), this->connection_id(), Thread::get_hrtime() + HRTIME_HOURS(24));
    frame = QUICFrameFactory::create_new_token_frame(buf, token, this->_issue_frame_id(), this);
    if (frame) {
      if (frame->size() < maximum_frame_size) {
        this->_is_resumption_token_sent = true;
      } else {
        // Cancel generating frame
        frame = nullptr;
      }
    }
  }

  return frame;
}

void
QUICNetVConnection::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  this->_is_resumption_token_sent = false;
}
