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

#include "ts/ink_config.h"
#include "records/I_RecHttp.h"
#include "ts/Diags.h"

#include "P_Net.h"
#include "InkAPIInternal.h" // Added to include the quic_hook definitions
#include "Log.h"

#include "P_SSLNextProtocolSet.h"

#include "QUICStats.h"
#include "QUICConfig.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICConfig.h"

#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))
#define STATE_VIO_OFFSET ((uintptr_t) & ((NetState *)0)->vio)

static constexpr std::string_view QUIC_DEBUG_TAG = "quic_net"sv;

#define QUICConDebug(fmt, ...) Debug(QUIC_DEBUG_TAG.data(), "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICConVDebug(fmt, ...) Debug("v_quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICFCDebug(fmt, ...) Debug("quic_flow_ctrl", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

#define QUICError(fmt, ...)                                           \
  Debug("quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__); \
  Error("quic_net [%s] " fmt, this->cids().data(), ##__VA_ARGS__)

static constexpr uint32_t IPV4_HEADER_SIZE            = 20;
static constexpr uint32_t IPV6_HEADER_SIZE            = 40;
static constexpr uint32_t UDP_HEADER_SIZE             = 8;
static constexpr uint32_t MAX_PACKET_OVERHEAD         = 54; // Max long header len
static constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD   = 24;
static constexpr uint32_t MINIMUM_INITIAL_PACKET_SIZE = 1200;
static constexpr ink_hrtime WRITE_READY_INTERVAL      = HRTIME_MSECONDS(20);
static constexpr int FRAME_PER_EVENT                  = 64;

static constexpr uint32_t STATE_CLOSING_MAX_SEND_PKT_NUM  = 8; // Max number of sending packets which contain a closing frame.
static constexpr uint32_t STATE_CLOSING_MAX_RECV_PKT_WIND = 1 << STATE_CLOSING_MAX_SEND_PKT_NUM;

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::init(QUICConnectionId peer_cid, QUICConnectionId original_cid, UDPConnection *udp_con,
                         QUICPacketHandler *packet_handler, QUICConnectionTable *ctable)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::acceptEvent);
  this->_packet_transmitter_mutex    = new_ProxyMutex();
  this->_frame_transmitter_mutex     = new_ProxyMutex();
  this->_udp_con                     = udp_con;
  this->_packet_handler              = packet_handler;
  this->_peer_quic_connection_id     = peer_cid;
  this->_original_quic_connection_id = original_cid;
  this->_quic_connection_id.randomize();
  // PacketHandler for out going connection doesn't have connection table
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

  thread = t;

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
  QUICConfig::scoped_config params;

  this->_five_tuple.update(this->local_addr, this->remote_addr, SOCK_DGRAM);
  // Version 0x00000001 uses stream 0 for cryptographic handshake with TLS 1.3, but newer version may not
  if (this->direction() == NET_VCONNECTION_IN) {
    this->_reset_token.generate(this->_quic_connection_id, params->server_id());
    this->_handshake_handler = new QUICHandshake(this, params->server_ssl_ctx(), this->_reset_token, params->stateless_retry());
  } else {
    this->_handshake_handler = new QUICHandshake(this, params->client_ssl_ctx());
    this->_handshake_handler->start(&this->_packet_factory, params->vn_exercise_enabled());
  }

  this->_application_map = new QUICApplicationMap();
  this->_application_map->set(STREAM_ID_FOR_HANDSHAKE, this->_handshake_handler);

  this->_hs_protocol      = this->_handshake_handler->protocol();
  this->_frame_dispatcher = new QUICFrameDispatcher(this);
  this->_packet_factory.set_hs_protocol(this->_hs_protocol);

  // Create frame handlers
  this->_congestion_controller  = new QUICCongestionController(this);
  this->_loss_detector          = new QUICLossDetector(this, this, this->_congestion_controller);
  this->_remote_flow_controller = new QUICRemoteConnectionFlowController(UINT64_MAX);
  this->_local_flow_controller  = new QUICLocalConnectionFlowController(this->_loss_detector, UINT64_MAX);
  this->_path_validator         = new QUICPathValidator();
  this->_stream_manager         = new QUICStreamManager(this, this->_loss_detector, this->_application_map);

  this->_frame_dispatcher->add_handler(this);
  this->_frame_dispatcher->add_handler(this->_stream_manager);
  this->_frame_dispatcher->add_handler(this->_loss_detector);
  this->_frame_dispatcher->add_handler(this->_path_validator);

  if (this->direction() == NET_VCONNECTION_IN) {
    this->_validate_new_path();
  }
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
  this->_packet_handler->close_conenction(this);
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

  // create stream for handshake
  QUICErrorUPtr error = this->_stream_manager->create_stream(STREAM_ID_FOR_HANDSHAKE);
  if (error->cls != QUICErrorClass::NONE) {
    QUICConDebug("Couldn't create stream for handshake");

    return CONNECT_FAILURE;
  }

  nh->startCop(this);

  // FIXME: complete do_io_xxxx instead
  this->read.enabled = 1;

  // start QUIC handshake
  this->_handshake_handler->handleEvent(VC_EVENT_WRITE_READY, nullptr);
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
QUICNetVConnection::minimum_quic_packet_size()
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
QUICNetVConnection::maximum_quic_packet_size() const
{
  if (this->options.ip_family == PF_INET6) {
    return this->_pmtu - UDP_HEADER_SIZE - IPV6_HEADER_SIZE;
  } else {
    return this->_pmtu - UDP_HEADER_SIZE - IPV4_HEADER_SIZE;
  }
}

uint32_t
QUICNetVConnection::_maximum_stream_frame_data_size()
{
  return this->maximum_quic_packet_size() - MAX_STREAM_FRAME_OVERHEAD - MAX_PACKET_OVERHEAD;
}

QUICStreamManager *
QUICNetVConnection::stream_manager()
{
  return this->_stream_manager;
}

uint32_t
QUICNetVConnection::_transmit_packet(QUICPacketUPtr packet)
{
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());

  if (packet) {
    QUICConDebug("Enqueue %s pkt_num=%" PRIu64 " size=%hu", QUICDebugNames::packet_type(packet->type()), packet->packet_number(),
                 packet->size());
    // TODO Remove const_cast
    this->_packet_send_queue.enqueue(const_cast<QUICPacket *>(packet.release()));
  }
  return this->_packet_send_queue.size;
}

uint32_t
QUICNetVConnection::transmit_packet(QUICPacketUPtr packet)
{
  uint32_t npackets = this->_transmit_packet(std::move(packet));
  this->_schedule_packet_write_ready();
  return npackets;
}

void
QUICNetVConnection::retransmit_packet(const QUICPacket &packet)
{
  QUICConDebug("Retransmit packet #%" PRIu64 " type %s", packet.packet_number(), QUICDebugNames::packet_type(packet.type()));

  this->_packet_retransmitter.retransmit_packet(packet);
}

Ptr<ProxyMutex>
QUICNetVConnection::get_packet_transmitter_mutex()
{
  return this->_packet_transmitter_mutex;
}

void
QUICNetVConnection::handle_received_packet(UDPPacket *packet)
{
  this->_packet_recv_queue.enqueue(packet);
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
  return {QUICFrameType::CONNECTION_CLOSE, QUICFrameType::BLOCKED, QUICFrameType::MAX_DATA};
}

QUICErrorUPtr
QUICNetVConnection::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::MAX_DATA:
    this->_remote_flow_controller->forward_limit(std::static_pointer_cast<const QUICMaxDataFrame>(frame)->maximum_data());
    QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                this->_remote_flow_controller->current_limit());
    this->_schedule_packet_write_ready();

    break;
  case QUICFrameType::PING:
    // Nothing to do
    break;
  case QUICFrameType::BLOCKED:
    // BLOCKED frame is for debugging. Nothing to do here.
    break;
  case QUICFrameType::APPLICATION_CLOSE:
  case QUICFrameType::CONNECTION_CLOSE:
    if (this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_closed) ||
        this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_draining)) {
      return error;
    }

    // 7.9.1. Closing and Draining Connection States
    // An endpoint MAY transition from the closing period to the draining period if it can confirm that its peer is also closing or
    // draining. Receiving a closing frame is sufficient confirmation, as is receiving a stateless reset.
    this->_switch_to_draining_state(QUICConnectionErrorUPtr(
      new QUICConnectionError(std::static_pointer_cast<const QUICApplicationCloseFrame>(frame)->error_code())));
    break;
  default:
    QUICConDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  return error;
}

// XXX Setup QUICNetVConnection on regular EThread.
// QUICNetVConnection::init() and QUICNetVConnection::start() might be called on ET_UDP EThread.
int
QUICNetVConnection::state_pre_handshake(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  if (!this->thread) {
    this->thread = this_ethread();
  }

  if (!this->nh) {
    this->nh = get_NetHandler(this_ethread());
  }

  // FIXME: Should be accept_no_activity_timeout?
  QUICConfig::scoped_config params;

  if (this->get_context() == NET_VCONNECTION_IN) {
    this->set_inactivity_timeout(HRTIME_SECONDS(params->no_activity_timeout_in()));
  } else {
    this->set_inactivity_timeout(HRTIME_SECONDS(params->no_activity_timeout_out()));
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

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    QUICPacketCreationResult result;
    net_activity(this, this_ethread());
    do {
      QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
      if (result == QUICPacketCreationResult::NOT_READY) {
        error = QUICErrorUPtr(new QUICNoError());
      } else if (result == QUICPacketCreationResult::FAILED) {
        error = QUICConnectionErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_FATAL_ALERT_GENERATED));
      } else if (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::UNSUPPORTED) {
        error = this->_state_handshake_process_packet(std::move(packet));
      }

      // if we complete handshake, switch to establish state
      if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
        this->_switch_to_established_state();
        return this->handleEvent(event, data);
      }

    } while (error->cls == QUICErrorClass::NONE &&
             (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::IGNORED));
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);

    if (this->_handshake_handler && this->_handshake_handler->msg_type() == QUICHandshakeMsgType::RETRY) {
      error = this->_state_handshake_send_retry_packet();
      if (this->_handshake_handler->is_stateless_retry_enabled()) {
        this->_switch_to_close_state();
      }
    } else {
      error = this->_state_common_send_packet();
    }
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);

    break;
  }
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_close_path_validation_timeout(data);
    if (!this->_path_validator->is_validated()) {
      this->_switch_to_close_state();
    }
    break;
  case EVENT_IMMEDIATE:
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
  }

  if (error->cls != QUICErrorClass::NONE) {
    this->_handle_error(std::move(error));
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_established(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    error = this->_state_common_receive_packet();
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);
    error = this->_state_common_send_packet();
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);
    break;
  }
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_close_path_validation_timeout(data);
    if (!this->_path_validator->is_validated()) {
      this->_switch_to_close_state();
    }
    break;
  case EVENT_IMMEDIATE: {
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  }
  default:
    QUICConDebug("Unexpected event: %s (%d)", QUICDebugNames::quic_event(event), event);
  }

  if (error->cls != QUICErrorClass::NONE) {
    QUICConDebug("QUICError: cls=%u, code=0x%" PRIu16, static_cast<unsigned int>(error->cls), error->code());
    this->_handle_error(std::move(error));
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_closing(int event, Event *data)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    error = this->_state_closing_receive_packet();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    this->_state_closing_send_packet();
    break;
  case QUIC_EVENT_PATH_VALIDATION_TIMEOUT:
    this->_close_path_validation_timeout(data);
    if (!this->_path_validator->is_validated()) {
      this->_switch_to_close_state();
    }
    break;
  case QUIC_EVENT_CLOSING_TIMEOUT:
    this->_close_closing_timeout(data);
    this->_switch_to_close_state();
    break;
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

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
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
    this->_close_path_validation_timeout(data);
    if (!this->_path_validator->is_validated()) {
      this->_switch_to_close_state();
    }
    break;
  case QUIC_EVENT_CLOSING_TIMEOUT:
    this->_close_closing_timeout(data);
    this->_switch_to_close_state();
    break;
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
    this->_loss_detector->handleEvent(QUIC_EVENT_LD_SHUTDOWN, nullptr);

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

void
QUICNetVConnection::registerNextProtocolSet(SSLNextProtocolSet *s)
{
  this->_next_protocol_set = s;
}

bool
QUICNetVConnection::is_closed() const
{
  return this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_closed);
}

SSLNextProtocolSet *
QUICNetVConnection::next_protocol_set() const
{
  return this->_next_protocol_set;
}

QUICPacketNumber
QUICNetVConnection::largest_acked_packet_number() const
{
  return this->_loss_detector->largest_acked_packet_number();
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_packet(QUICPacketUPtr packet)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (packet->type()) {
  case QUICPacketType::VERSION_NEGOTIATION:
    error = this->_state_handshake_process_version_negotiation_packet(std::move(packet));
    break;
  case QUICPacketType::INITIAL:
    error = this->_state_handshake_process_initial_packet(std::move(packet));
    break;
  case QUICPacketType::RETRY:
    error = this->_state_handshake_process_retry_packet(std::move(packet));
    break;
  case QUICPacketType::HANDSHAKE:
    error = this->_state_handshake_process_handshake_packet(std::move(packet));
    break;
  case QUICPacketType::ZERO_RTT_PROTECTED:
    error = this->_state_handshake_process_zero_rtt_protected_packet(std::move(packet));
    break;
  case QUICPacketType::PROTECTED:
  default:
    QUICConDebug("Ignore %s(%" PRIu8 ") packet", QUICDebugNames::packet_type(packet->type()), static_cast<uint8_t>(packet->type()));

    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
    break;
  }
  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_version_negotiation_packet(QUICPacketUPtr packet)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  if (packet->destination_cid() != this->connection_id()) {
    QUICConDebug("Ignore Version Negotiation packet");
    return error;
  }

  if (this->_handshake_handler->is_version_negotiated()) {
    QUICConDebug("ignore VN - already negotiated");
  } else {
    error = this->_handshake_handler->negotiate_version(packet.get(), &this->_packet_factory);

    // discard all transport state except packet number
    this->_stream_manager->reset_send_offset();
    this->_stream_manager->reset_recv_offset();
    this->_loss_detector->reset();
    SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
    this->_packet_retransmitter.reset();

    // start handshake over
    this->_handshake_handler->reset();
    this->_handshake_handler->handleEvent(VC_EVENT_WRITE_READY, nullptr);
    this->_schedule_packet_write_ready();
  }

  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_initial_packet(QUICPacketUPtr packet)
{
  // QUIC packet could be smaller than MINIMUM_INITIAL_PACKET_SIZE when coalescing packets
  // if (packet->size() < MINIMUM_INITIAL_PACKET_SIZE) {
  //   QUICConDebug("Packet size is smaller than the minimum initial packet size");
  //   // Ignore the packet
  //   return QUICErrorUPtr(new QUICNoError());
  // }

  // Start handshake
  QUICErrorUPtr error = this->_handshake_handler->start(packet.get(), &this->_packet_factory);

  // If version negotiation was failed and VERSION NEGOTIATION packet was sent, nothing to do.
  if (this->_handshake_handler->is_version_negotiated()) {
    error = this->_recv_and_ack(std::move(packet));
  }

  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_retry_packet(QUICPacketUPtr packet)
{
  // discard all transport state
  this->_stream_manager->reset_send_offset();
  this->_loss_detector->reset();
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  this->_packet_retransmitter.reset();

  QUICErrorUPtr error = this->_recv_and_ack(std::move(packet));

  // Packet number of RETRY packet is echo of INITIAL packet
  this->_packet_recv_queue.reset();
  this->_stream_manager->reset_recv_offset();

  // Generate new Connection ID
  this->_rerandomize_original_cid();

  this->_hs_protocol->initialize_key_materials(this->_original_quic_connection_id);

  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_handshake_packet(QUICPacketUPtr packet)
{
  return this->_recv_and_ack(std::move(packet));
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_zero_rtt_protected_packet(QUICPacketUPtr packet)
{
  this->_stream_manager->init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                                  this->_handshake_handler->remote_transport_parameters());
  this->_start_application();
  return this->_recv_and_ack(std::move(packet));
}

QUICErrorUPtr
QUICNetVConnection::_state_connection_established_process_packet(QUICPacketUPtr packet)
{
  return this->_recv_and_ack(std::move(packet));
}

QUICErrorUPtr
QUICNetVConnection::_state_common_receive_packet()
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  QUICPacketCreationResult result;

  // Receive a QUIC packet
  net_activity(this, this_ethread());
  do {
    QUICPacketUPtr p = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::FAILED) {
      return QUICConnectionErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_FATAL_ALERT_GENERATED));
    } else if (result == QUICPacketCreationResult::NO_PACKET) {
      return QUICErrorUPtr(new QUICNoError());
    } else if (result == QUICPacketCreationResult::NOT_READY) {
      return QUICErrorUPtr(new QUICNoError());
    } else if (result == QUICPacketCreationResult::IGNORED) {
      continue;
    }

    // Process the packet
    switch (p->type()) {
    case QUICPacketType::PROTECTED:
      // Check connection migration
      if (this->_handshake_handler->is_completed() && p->destination_cid() != this->_quic_connection_id) {
        if (this->_alt_con_manager->migrate_to(p->destination_cid(), this->_reset_token)) {
          // Migrate connection
          // TODO Address Validation
          // TODO Adjust expected packet number with a gap computed based on info.seq_num
          this->_update_local_cid(p->destination_cid());
          Connection con;
          con.setRemote(&p->from().sa);
          this->con.move(con);
          this->_validate_new_path();
        } else {
          // TODO Send some error?
        }
      }
      error = this->_state_connection_established_process_packet(std::move(p));
      break;
    case QUICPacketType::INITIAL:
    case QUICPacketType::HANDSHAKE:
      // Pass packet to _recv_and_ack to send ack to the packet. Stream data will be discarded by offset mismatch.
      error = this->_recv_and_ack(std::move(p));
      break;
    default:
      QUICConDebug("Unknown packet type: %s(%" PRIu8 ")", QUICDebugNames::packet_type(p->type()), static_cast<uint8_t>(p->type()));

      error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
      break;
    }

  } while (error->cls == QUICErrorClass::NONE &&
           (result == QUICPacketCreationResult::SUCCESS || result == QUICPacketCreationResult::IGNORED));
  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_closing_receive_packet()
{
  while (this->_packet_recv_queue.size() > 0) {
    QUICPacketCreationResult result;
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::SUCCESS) {
      this->_recv_and_ack(std::move(packet));
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

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICNetVConnection::_state_draining_receive_packet()
{
  while (this->_packet_recv_queue.size() > 0) {
    QUICPacketCreationResult result;
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::SUCCESS) {
      this->_recv_and_ack(std::move(packet));
      // Do NOT schedule WRITE_READY event from this point.
      // An endpoint in the draining state MUST NOT send any packets.
    }
  }

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICNetVConnection::_state_common_send_packet()
{
  this->_packetize_frames();

  QUICPacket *packet;

  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  uint32_t packet_count = 0;
  while ((packet = this->_packet_send_queue.dequeue()) != nullptr) {
    if (packet->type() == QUICPacketType::HANDSHAKE && !this->_path_validator->is_validated() &&
        this->_handshake_packets_sent >= 3) {
      this->_packet_send_queue.push(packet);
      break;
    }
    if (!this->_congestion_controller->check_credit()) {
      this->_packet_send_queue.push(packet);
      break;
    }

    this->_packet_handler->send_packet(*packet, this);
    if (packet->type() == QUICPacketType::HANDSHAKE) {
      ++this->_handshake_packets_sent;
    }
    this->_loss_detector->on_packet_sent(QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet));
    packet_count++;
  }
  QUIC_INCREMENT_DYN_STAT_EX(QUICStats::total_packets_sent_stat, packet_count);

  if (packet_count) {
    net_activity(this, this_ethread());
  }

  return QUICErrorUPtr(new QUICNoError());
}

// RETRY packet contains ONLY a single STREAM frame
QUICErrorUPtr
QUICNetVConnection::_state_handshake_send_retry_packet()
{
  size_t len = 0;
  ats_unique_buf buf(nullptr, [](void *p) { ats_free(p); });
  QUICPacketType current_packet_type = QUICPacketType::UNINITIALIZED;

  QUICFrameUPtr frame(nullptr, nullptr);
  bool retransmittable = this->_handshake_handler->is_stateless_retry_enabled() ? false : true;

  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  SCOPED_MUTEX_LOCK(frame_transmitter_lock, this->_frame_transmitter_mutex, this_ethread());

  frame = this->_stream_manager->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
  ink_assert(frame);
  ink_assert(frame->type() == QUICFrameType::STREAM);
  this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
  if (len == 0) {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
  }

  QUICPacketUPtr packet = this->_build_packet(std::move(buf), len, retransmittable, QUICPacketType::RETRY);
  this->_packet_handler->send_packet(*packet, this);
  this->_loss_detector->on_packet_sent(std::move(packet));

  QUIC_INCREMENT_DYN_STAT_EX(QUICStats::total_packets_sent_stat, 1);

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
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
    this->_packet_handler->send_packet(*this->_the_final_packet, this);
  }
  return QUICErrorUPtr(new QUICNoError());
}

// Store frame data to buffer for packet. When remaining buffer is too small to store frame data or packet type is different from
// previous one, build packet and transmit it. After that, allocate new buffer.
void
QUICNetVConnection::_store_frame(ats_unique_buf &buf, size_t &len, bool &retransmittable, QUICPacketType &current_packet_type,
                                 QUICFrameUPtr frame)
{
  uint32_t max_size = this->maximum_quic_packet_size();

  QUICPacketType previous_packet_type = current_packet_type;
  QUICRetransmissionFrame *rf         = dynamic_cast<QUICRetransmissionFrame *>(frame.get());
  if (rf) {
    current_packet_type = rf->packet_type();
  } else if (frame->is_protected()) {
    current_packet_type = QUICPacketType::PROTECTED;
  } else {
    current_packet_type = QUICPacketType::UNINITIALIZED;
  }

  if (len + frame->size() + MAX_PACKET_OVERHEAD > max_size || (previous_packet_type != current_packet_type && len > 0)) {
    this->_transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, previous_packet_type));
    retransmittable = false;
    len             = 0;
    buf             = nullptr;
  }

  retransmittable = retransmittable || (frame->type() != QUICFrameType::ACK && frame->type() != QUICFrameType::PADDING);

  if (buf == nullptr) {
    buf = ats_unique_malloc(max_size);
  }

  ink_assert(max_size > len);

  size_t l = 0;
  size_t n = frame->store(buf.get() + len, &l, max_size - len);
  if (n > 0) {
    if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
      char msg[1024];
      frame->debug_msg(msg, sizeof(msg));
      QUICConDebug("[TX] %s", msg);
    }

    len += l;
    return;
  }

  // split frame
  auto new_frame = QUICFrameFactory::split_frame(frame.get(), max_size - len);

  if (is_debug_tag_set(QUIC_DEBUG_TAG.data())) {
    char msg[1024];
    frame->debug_msg(msg, sizeof(msg));
    QUICConDebug("[TX] %s", msg);
  }

  ink_assert(frame->store(buf.get() + len, &l, max_size - len) > 0);
  ink_assert(new_frame != nullptr);

  this->_transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, current_packet_type));
  len = 0;
  buf = ats_unique_malloc(max_size);
  this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(new_frame));
  return;
}

void
QUICNetVConnection::_packetize_frames()
{
  int frame_count = 0;
  size_t len      = 0;
  ats_unique_buf buf(nullptr, [](void *p) { ats_free(p); });
  QUICPacketType current_packet_type = QUICPacketType::UNINITIALIZED;

  QUICFrameUPtr frame(nullptr, nullptr);
  bool retransmittable = false;

  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  SCOPED_MUTEX_LOCK(frame_transmitter_lock, this->_frame_transmitter_mutex, this_ethread());

  bool will_be_ack_only = true;
  if (this->_connection_error || this->_packet_retransmitter.will_generate_frame() ||
      this->_stream_manager->will_generate_frame() || this->_path_validator->will_generate_frame()) {
    will_be_ack_only = false;
  }

  // ACK
  if (will_be_ack_only) {
    if (this->_ack_frame_creator.will_generate_frame()) {
      frame = this->_ack_frame_creator.generate_frame(UINT16_MAX, this->_maximum_stream_frame_data_size());
    }
  } else {
    frame = this->_ack_frame_creator.generate_frame(UINT16_MAX, this->_maximum_stream_frame_data_size());
  }

  if (frame != nullptr) {
    ++frame_count;
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
  }

  // PATH_CHALLENGE, PATH_RESPOSNE
  if (this->_stream_manager->will_generate_frame()) {
    frame = this->_path_validator->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
    if (frame) {
      ++frame_count;
      this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
    }
  }

  // NEW_CONNECTION_ID
  if (this->_alt_con_manager && this->_alt_con_manager->will_generate_frame()) {
    frame =
      this->_alt_con_manager->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
    while (frame) {
      ++frame_count;
      this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
      if (frame_count >= FRAME_PER_EVENT) {
        break;
      }
      frame =
        this->_alt_con_manager->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
    }
  }

  // Lost frames
  frame =
    this->_packet_retransmitter.generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
  while (frame) {
    ++frame_count;
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
    if (frame_count >= FRAME_PER_EVENT) {
      break;
    }
    frame =
      this->_packet_retransmitter.generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
  }

  // STREAM, MAX_STREAM_DATA, STREAM_BLOCKED
  frame = this->_stream_manager->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
  while (frame) {
    ++frame_count;
    if (frame->type() == QUICFrameType::STREAM) {
      int ret = this->_remote_flow_controller->update(this->_stream_manager->total_offset_sent());
      QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
                  this->_remote_flow_controller->current_limit());
      ink_assert(ret == 0);
    }
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));

    if (frame_count >= FRAME_PER_EVENT) {
      break;
    }
    frame = this->_stream_manager->generate_frame(this->_remote_flow_controller->credit(), this->_maximum_stream_frame_data_size());
  }

  // Schedule a packet
  if (len != 0) {
    // Pad with PADDING frames
    uint32_t min_size = this->minimum_quic_packet_size();
    if (min_size > len) {
      // FIXME QUICNetVConnection should not know the actual type value of PADDING frame
      memset(buf.get() + len, 0, min_size - len);
      len += min_size - len;
    }
    this->_transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, current_packet_type));
  }
}

void
QUICNetVConnection::_packetize_closing_frame()
{
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  SCOPED_MUTEX_LOCK(frame_transmitter_lock, this->_frame_transmitter_mutex, this_ethread());

  if (this->_connection_error == nullptr) {
    return;
  }

  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  if (this->_connection_error->cls == QUICErrorClass::APPLICATION) {
    frame = QUICFrameFactory::create_application_close_frame(std::move(this->_connection_error));
  } else {
    frame = QUICFrameFactory::create_connection_close_frame(std::move(this->_connection_error));
  }

  ats_unique_buf buf(nullptr, [](void *p) { ats_free(p); });
  size_t len                         = 0;
  bool retransmittable               = false;
  QUICPacketType current_packet_type = QUICPacketType::UNINITIALIZED;
  this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));

  this->_the_final_packet = this->_build_packet(std::move(buf), len, false);
}

QUICErrorUPtr
QUICNetVConnection::_recv_and_ack(QUICPacketUPtr packet)
{
  const uint8_t *payload      = packet->payload();
  uint16_t size               = packet->payload_length();
  QUICPacketNumber packet_num = packet->packet_number();

  bool should_send_ack;

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  error = this->_frame_dispatcher->receive_frames(payload, size, should_send_ack);
  if (error->cls != QUICErrorClass::NONE) {
    return error;
  }

  if (packet->type() == QUICPacketType::RETRY) {
    should_send_ack = false;
  }

  int ret = this->_local_flow_controller->update(this->_stream_manager->total_offset_received());
  QUICFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
              this->_local_flow_controller->current_limit());
  if (ret != 0) {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::FLOW_CONTROL_ERROR));
  }

  bool protection = packet->type() == QUICPacketType::PROTECTED || packet->type() == QUICPacketType::ZERO_RTT_PROTECTED;
  this->_ack_frame_creator.update(packet_num, protection, should_send_ack);

  return error;
}

QUICPacketUPtr
QUICNetVConnection::_build_packet(ats_unique_buf buf, size_t len, bool retransmittable, QUICPacketType type)
{
  QUICPacketUPtr packet = QUICPacketFactory::create_null_packet();

  // TODO: support NET_VCONNECTION_IN
  if (this->get_context() == NET_VCONNECTION_OUT && type == QUICPacketType::UNINITIALIZED) {
    if (this->_last_received_packet_type == QUICPacketType::UNINITIALIZED ||
        this->_last_received_packet_type == QUICPacketType::VERSION_NEGOTIATION ||
        this->_last_received_packet_type == QUICPacketType::RETRY) {
      type = QUICPacketType::INITIAL;
    } else if (_last_received_packet_type == QUICPacketType::HANDSHAKE) {
      type = QUICPacketType::HANDSHAKE;
    } else if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
      type = QUICPacketType::PROTECTED;
    } else {
      Error("Unsupported case");
    }
  }

  switch (type) {
  case QUICPacketType::INITIAL:
    ink_assert(this->get_context() == NET_VCONNECTION_OUT);
    packet = this->_packet_factory.create_initial_packet(this->_original_quic_connection_id, this->_quic_connection_id,
                                                         this->largest_acked_packet_number(), std::move(buf), len);
    this->_handshake_handler->handleEvent(QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE, nullptr);

    break;
  case QUICPacketType::RETRY:
    // Echo "_largest_received_packet_number" as packet number. Probably this is the packet number from triggering client packet.
    packet = this->_packet_factory.create_retry_packet(this->_peer_quic_connection_id, this->_quic_connection_id,
                                                       this->_packet_recv_queue.largest_received_packet_number(), std::move(buf),
                                                       len, retransmittable);
    break;
  case QUICPacketType::HANDSHAKE:
    packet =
      this->_packet_factory.create_handshake_packet(this->_peer_quic_connection_id, this->_quic_connection_id,
                                                    this->largest_acked_packet_number(), std::move(buf), len, retransmittable);
    this->_handshake_handler->handleEvent(QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE, nullptr);

    break;
  case QUICPacketType::PROTECTED:
    packet = this->_packet_factory.create_server_protected_packet(
      this->_peer_quic_connection_id, this->largest_acked_packet_number(), std::move(buf), len, retransmittable);
    break;
  default:
    if (this->get_context() == NET_VCONNECTION_IN) {
      if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
        packet = this->_packet_factory.create_server_protected_packet(
          this->_peer_quic_connection_id, this->largest_acked_packet_number(), std::move(buf), len, retransmittable);
      } else {
        packet =
          this->_packet_factory.create_handshake_packet(this->_peer_quic_connection_id, this->_quic_connection_id,
                                                        this->largest_acked_packet_number(), std::move(buf), len, retransmittable);
      }
    }
    break;
  }

  return packet;
}

void
QUICNetVConnection::_init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                              const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
  this->_stream_manager->init_flow_control_params(local_tp, remote_tp);

  uint32_t local_initial_max_data  = 0;
  uint32_t remote_initial_max_data = 0;
  if (local_tp) {
    local_initial_max_data = local_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_DATA);
  }
  if (remote_tp) {
    remote_initial_max_data = remote_tp->getAsUInt32(QUICTransportParameterId::INITIAL_MAX_DATA);
  }

  this->_local_flow_controller->set_limit(local_initial_max_data);
  this->_remote_flow_controller->set_limit(remote_initial_max_data);
  QUICFCDebug("[LOCAL] %" PRIu64 "/%" PRIu64, this->_local_flow_controller->current_offset(),
              this->_local_flow_controller->current_limit());
  QUICFCDebug("[REMOTE] %" PRIu64 "/%" PRIu64, this->_remote_flow_controller->current_offset(),
              this->_remote_flow_controller->current_limit());
}

void
QUICNetVConnection::_handle_error(QUICErrorUPtr error)
{
  if (error->cls == QUICErrorClass::APPLICATION) {
    QUICError("QUICError: %s (%u), APPLICATION ERROR (0x%" PRIu16 ")", QUICDebugNames::error_class(error->cls),
              static_cast<unsigned int>(error->cls), error->code());
  } else {
    QUICError("QUICError: %s (%u), %s (0x%" PRIu16 ")", QUICDebugNames::error_class(error->cls),
              static_cast<unsigned int>(error->cls), QUICDebugNames::error_code(error->trans_error_code), error->code());
  }

  if (dynamic_cast<QUICStreamError *>(error.get()) != nullptr) {
    // Stream Error
    QUICStreamError *serror = static_cast<QUICStreamError *>(error.release());
    this->_stream_manager->reset_stream(serror->stream->id(), QUICStreamErrorUPtr(serror));
  } else {
    // Connection Error
    QUICConnectionError *cerror = static_cast<QUICConnectionError *>(error.release());
    this->close(QUICConnectionErrorUPtr(cerror));
  }
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

    this->_last_received_packet_type = packet->type();
    this->_packet_factory.set_dcil(packet->destination_cid().length());
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
      QUICConDebug("Dequeue %s size=%u", QUICDebugNames::packet_type(packet->type()), packet->size());
    } else {
      QUICConDebug("Dequeue %s pkt_num=%" PRIu64 " size=%u", QUICDebugNames::packet_type(packet->type()), packet->packet_number(),
                   packet->size());
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
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  if (!this->_packet_write_ready) {
    QUICConVDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_PACKET_WRITE_READY));
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
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  if (this->_packet_write_ready) {
    this->_packet_write_ready->cancel();
    this->_packet_write_ready = nullptr;
  }
}

void
QUICNetVConnection::_close_packet_write_ready(Event *data)
{
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  ink_assert(this->_packet_write_ready == data);
  this->_packet_write_ready = nullptr;
}

void
QUICNetVConnection::_schedule_closing_timeout(ink_hrtime interval)
{
  if (!this->_closing_timeout) {
    QUICConDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_CLOSING_TIMEOUT));
    this->_closing_timeout = this->thread->schedule_in_local(this, interval, QUIC_EVENT_CLOSING_TIMEOUT);
  }
}

void
QUICNetVConnection::_unschedule_closing_timeout()
{
  if (this->_closing_timeout) {
    this->_closing_timeout->cancel();
    this->_closing_timeout = nullptr;
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

  this->_init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                  this->_handshake_handler->remote_transport_parameters());

  this->_start_application();

  return 0;
}

void
QUICNetVConnection::_schedule_path_validation_timeout(ink_hrtime interval)
{
  if (!this->_path_validation_timeout) {
    QUICConDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_PATH_VALIDATION_TIMEOUT));
    this->_path_validation_timeout = this->thread->schedule_in_local(this, interval, QUIC_EVENT_PATH_VALIDATION_TIMEOUT);
  }
}

void
QUICNetVConnection::_unschedule_path_validation_timeout()
{
  if (this->_path_validation_timeout) {
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
      Continuation *endpoint = this->_next_protocol_set->findEndpoint(app_name, app_name_len);
      if (endpoint == nullptr) {
        this->_handle_error(QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR)));
      } else {
        endpoint->handleEvent(NET_EVENT_ACCEPT, this);
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
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_established);

    if (netvc_context == NET_VCONNECTION_IN) {
      this->_alt_con_manager = new QUICAltConnectionManager(this, *this->_ctable);
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
  } else {
    QUICConDebug("Reason was not provided");
  }
  this->_connection_error = std::move(error);
  this->_schedule_packet_write_ready();

  this->remove_from_active_queue();
  this->set_inactivity_timeout(0);

  ink_hrtime rto = this->_loss_detector->current_rto_period();

  QUICConDebug("Enter state_connection_closing %" PRIu64 "ms", 3 * rto / HRTIME_MSECOND);
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
  } else {
    QUICConDebug("Reason was not provided");
  }

  this->remove_from_active_queue();
  this->set_inactivity_timeout(0);

  ink_hrtime rto = this->_loss_detector->current_rto_period();

  QUICConDebug("Enter state_connection_draining %" PRIu64 "ms", 3 * rto / HRTIME_MSECOND);
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
  ink_hrtime rto = this->_loss_detector->current_rto_period();
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

  this->_peer_quic_connection_id = new_cid;
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
