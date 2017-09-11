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
#include "ts/EventNotify.h"
#include "records/I_RecHttp.h"
#include "ts/Diags.h"

#include "P_Net.h"
#include "InkAPIInternal.h" // Added to include the quic_hook definitions
#include "BIO_fastopen.h"
#include "Log.h"

#include "P_SSLNextProtocolSet.h"

#include "QUICEchoApp.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICConfig.h"

#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))
#define STATE_VIO_OFFSET ((uintptr_t) & ((NetState *)0)->vio)

#define DebugQUICCon(fmt, ...) \
  Debug("quic_net", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_quic_connection_id), ##__VA_ARGS__)

static constexpr uint32_t MAX_PACKET_OVERHEAD                = 25; // Max long header len(17) + FNV-1a hash len(8)
static constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD          = 15;
static constexpr uint32_t MINIMUM_INITIAL_CLIENT_PACKET_SIZE = 1200;
static constexpr char STATELESS_RETRY_TOKEN_KEY[]            = "stateless_token_retry_key";

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

QUICNetVConnection::QUICNetVConnection() : UnixNetVConnection()
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_pre_handshake);
}

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::init(UDPConnection *udp_con, QUICPacketHandler *packet_handler)
{
  this->_transmitter_mutex = new_ProxyMutex();
  this->_udp_con           = udp_con;
  this->_packet_handler    = packet_handler;
  this->_quic_connection_id.randomize();

  // FIXME These should be done by HttpProxyServerMain
  SSLNextProtocolSet *next_protocol_set = new SSLNextProtocolSet();
  next_protocol_set->registerEndpoint(TS_ALPN_PROTOCOL_HTTP_QUIC, nullptr);
  this->registerNextProtocolSet(next_protocol_set);
}

VIO *
QUICNetVConnection::do_io_read(Continuation *c, int64_t nbytes, MIOBuffer *buf)
{
  return super::do_io_read(c, nbytes, buf);
}

VIO *
QUICNetVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool owner)
{
  return super::do_io_write(c, nbytes, buf, owner);
}

int
QUICNetVConnection::startEvent(int /*event ATS_UNUSED */, Event *e)
{
  return EVENT_DONE;
}

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::start(SSL_CTX *ssl_ctx)
{
  // Version 0x00000001 uses stream 0 for cryptographic handshake with TLS 1.3, but newer version may not
  this->_token.gen_token(STATELESS_RETRY_TOKEN_KEY, _quic_connection_id ^ id);

  this->_handshake_handler = new QUICHandshake(this, ssl_ctx, this->_token.get());
  this->_application_map   = new QUICApplicationMap();
  this->_application_map->set(STREAM_ID_FOR_HANDSHAKE, this->_handshake_handler);

  this->_crypto           = this->_handshake_handler->crypto_module();
  this->_frame_dispatcher = new QUICFrameDispatcher();
  this->_packet_factory.set_crypto_module(this->_crypto);

  // Create frame handlers
  this->_stream_manager         = new QUICStreamManager(this, this->_application_map);
  this->_congestion_controller  = new QUICCongestionController();
  this->_loss_detector          = new QUICLossDetector(this);
  this->_remote_flow_controller = new QUICRemoteConnectionFlowController(0, this);
  this->_local_flow_controller  = new QUICLocalConnectionFlowController(0, this);

  this->_frame_dispatcher->add_handler(this);
  this->_frame_dispatcher->add_handler(this->_stream_manager);
  this->_frame_dispatcher->add_handler(this->_congestion_controller);
  this->_frame_dispatcher->add_handler(this->_loss_detector);

  this->_init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                  this->_handshake_handler->remote_transport_parameters());
}

void
QUICNetVConnection::free(EThread *t)
{
  DebugQUICCon("Free connection");

  this->_udp_con        = nullptr;
  this->_packet_handler = nullptr;

  delete this->_version_negotiator;
  delete this->_handshake_handler;
  delete this->_application_map;
  delete this->_crypto;
  delete this->_loss_detector;
  delete this->_frame_dispatcher;
  delete this->_stream_manager;
  delete this->_congestion_controller;

  // TODO: clear member variables like `UnixNetVConnection::free(EThread *t)`
  this->mutex.clear();

  if (from_accept_thread) {
    quicNetVCAllocator.free(this);
  } else {
    THREAD_FREE(this, quicNetVCAllocator, t);
  }
}

void
QUICNetVConnection::reenable(VIO *vio)
{
  return;
}

uint32_t
QUICNetVConnection::pmtu()
{
  return this->_pmtu;
}

NetVConnectionContext_t
QUICNetVConnection::direction()
{
  return this->netvc_context;
}

uint32_t
QUICNetVConnection::minimum_quic_packet_size()
{
  if (netvc_context == NET_VCONNECTION_OUT) {
    // FIXME Only the first packet need to be 1200 bytes at least
    return MINIMUM_INITIAL_CLIENT_PACKET_SIZE;
  } else {
    // FIXME This size should be configurable and should have some randomness
    // This is just for providing protection against packet analysis for protected packets
    return 32 + (this->_rnd() & 0x3f); // 32 to 96
  }
}

uint32_t
QUICNetVConnection::maximum_quic_packet_size()
{
  if (this->options.ip_family == PF_INET6) {
    return this->_pmtu - 48;
  } else {
    return this->_pmtu - 28;
  }
}

uint32_t
QUICNetVConnection::maximum_stream_frame_data_size()
{
  return this->maximum_quic_packet_size() - MAX_STREAM_FRAME_OVERHEAD - MAX_PACKET_OVERHEAD;
}

void
QUICNetVConnection::transmit_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  // TODO Remove const_cast
  this->_packet_send_queue.enqueue(const_cast<QUICPacket *>(packet.release()));
  if (!this->_packet_write_ready) {
    this->_packet_write_ready = eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
  }
}

void
QUICNetVConnection::retransmit_packet(const QUICPacket &packet)
{
  uint16_t size          = packet.payload_size();
  const uint8_t *payload = packet.payload();

  std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame(nullptr, &QUICFrameDeleter::delete_null_frame);
  uint16_t cursor = 0;

  while (cursor < size) {
    frame = QUICFrameFactory::create(payload + cursor, size - cursor);
    cursor += frame->size();

    switch (frame->type()) {
    case QUICFrameType::PADDING:
    case QUICFrameType::ACK:
      break;
    default:
      frame = QUICFrameFactory::create_retransmission_frame(std::move(frame), packet);
      this->transmit_frame(std::move(frame));
      break;
    }
  }
}

Ptr<ProxyMutex>
QUICNetVConnection::get_transmitter_mutex()
{
  return this->_transmitter_mutex;
}

void
QUICNetVConnection::push_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  DebugQUICCon("type=%s pkt_num=%" PRIu64 " size=%u", QUICDebugNames::packet_type(packet->type()), packet->packet_number(),
               packet->size());
  this->_packet_recv_queue.enqueue(const_cast<QUICPacket *>(packet.release()));
}

void
QUICNetVConnection::transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame)
{
  DebugQUICCon("Type=%s Size=%zu", QUICDebugNames::frame_type(frame->type()), frame->size());
  this->_frame_buffer.push(std::move(frame));
  if (!this->_packet_write_ready) {
    this->_packet_write_ready = eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
  }
}

void
QUICNetVConnection::close(QUICError error)
{
  if (this->handler == reinterpret_cast<ContinuationHandler>(&QUICNetVConnection::state_connection_closed) ||
      this->handler == reinterpret_cast<ContinuationHandler>(&QUICNetVConnection::state_connection_closing)) {
    // do nothing
  } else {
    DebugQUICCon("Enter state_connection_closing");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closing);
    this->transmit_frame(QUICFrameFactory::create_connection_close_frame(error.code, 0, ""));
  }
}

std::vector<QUICFrameType>
QUICNetVConnection::interests()
{
  return {QUICFrameType::CONNECTION_CLOSE, QUICFrameType::BLOCKED, QUICFrameType::MAX_DATA};
}

QUICError
QUICNetVConnection::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  QUICError error = QUICError(QUICErrorClass::NONE);

  switch (frame->type()) {
  case QUICFrameType::MAX_DATA:
    this->_remote_flow_controller->forward_limit(std::static_pointer_cast<const QUICMaxDataFrame>(frame)->maximum_data());
    Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
          static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
          this->_remote_flow_controller->current_limit());
    break;
  case QUICFrameType::BLOCKED:
    // BLOCKED frame is for debugging. Nothing to do here.
    break;
  case QUICFrameType::CONNECTION_CLOSE:
    DebugQUICCon("Enter state_connection_closed");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closed);
    break;
  default:
    DebugQUICCon("Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
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
  if (!this->thread) {
    this->thread = this_ethread();
  }

  if (!this->nh) {
    this->nh = get_NetHandler(this_ethread());
  }

  // FIXME: Should be accept_no_activity_timeout?
  QUICConfig::scoped_config params;

  this->set_inactivity_timeout(HRTIME_SECONDS(params->no_activity_timeout_in()));
  this->add_to_active_queue();

  DebugQUICCon("Enter state_handshake");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);
  return this->handleEvent(event, data);
}

// TODO: Timeout by active_timeout
int
QUICNetVConnection::state_handshake(int event, Event *data)
{
  QUICError error;

  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> p =
      std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(this->_packet_recv_queue.dequeue(), &QUICPacketDeleter::delete_packet);
    net_activity(this, this_ethread());

    switch (p->type()) {
    case QUICPacketType::CLIENT_INITIAL: {
      error = this->_state_handshake_process_initial_client_packet(std::move(p));
      break;
    }
    case QUICPacketType::CLIENT_CLEARTEXT: {
      error = this->_state_handshake_process_client_cleartext_packet(std::move(p));
      break;
    }
    case QUICPacketType::ZERO_RTT_PROTECTED: {
      error = this->_state_handshake_process_zero_rtt_protected_packet(std::move(p));
      break;
    }
    default:
      error = QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
      break;
    }

    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    error                     = this->_state_common_send_packet();
    this->_packet_write_ready = nullptr;
    break;
  }
  case EVENT_IMMEDIATE: {
    // Start Implicit Shutdown. Because of no network activity for the duration of the idle timeout.
    this->remove_from_active_queue();
    this->close({});

    // TODO: signal VC_EVENT_ACTIVE_TIMEOUT/VC_EVENT_INACTIVITY_TIMEOUT to application
    break;
  }

  default:
    DebugQUICCon("Unexpected event: %u", event);
  }

  if (error.cls != QUICErrorClass::NONE) {
    // TODO: Send error if needed
    DebugQUICCon("QUICError: %s (%u), %s (0x%x)", QUICDebugNames::error_class(error.cls), static_cast<unsigned int>(error.cls),
                 QUICDebugNames::error_code(error.code), static_cast<unsigned int>(error.code));
  }

  if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
    this->_application_map->set_default(this->_create_application());
    this->_init_flow_control_params(this->_handshake_handler->local_transport_parameters(),
                                    this->_handshake_handler->remote_transport_parameters());

    DebugQUICCon("Enter state_connection_established");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_established);
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_established(int event, Event *data)
{
  QUICError error;
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    error = this->_state_common_receive_packet();
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    error                     = this->_state_common_send_packet();
    this->_packet_write_ready = nullptr;
    break;
  }

  case EVENT_IMMEDIATE: {
    // Start Implicit Shutdown. Because of no network activity for the duration of the idle timeout.
    this->remove_from_active_queue();
    this->close({});

    // TODO: signal VC_EVENT_ACTIVE_TIMEOUT/VC_EVENT_INACTIVITY_TIMEOUT to application
    break;
  }
  default:
    DebugQUICCon("Unexpected event: %u", event);
  }

  if (error.cls != QUICErrorClass::NONE) {
    // TODO: Send error if needed
    DebugQUICCon("QUICError: cls=%u, code=0x%x", static_cast<unsigned int>(error.cls), static_cast<unsigned int>(error.code));
  }

  return EVENT_CONT;
}

int
QUICNetVConnection::state_connection_closing(int event, Event *data)
{
  QUICError error;
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    error = this->_state_common_receive_packet();
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_state_common_send_packet();
    this->_packet_write_ready = nullptr;
    break;
  }
  default:
    DebugQUICCon("Unexpected event: %u", event);
  }

  // FIXME Enter closed state if CONNECTION_CLOSE was ACKed
  if (true) {
    DebugQUICCon("Enter state_connection_closed");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closed);
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_connection_closed(int event, Event *data)
{
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    // TODO: send GOAWAY frame
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_packet_write_ready        = nullptr;
    this->next_inactivity_timeout_at = 0;
    this->next_activity_timeout_at   = 0;

    this->inactivity_timeout_in = 0;
    this->active_timeout_in     = 0;

    // TODO: Drop record from Connection-ID - QUICNetVConnection table in QUICPacketHandler
    // Shutdown loss detector
    this->_loss_detector->handleEvent(QUIC_EVENT_LD_SHUTDOWN, nullptr);

    this->free(this_ethread());

    break;
  }
  default:
    DebugQUICCon("Unexpected event: %u", event);
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
  ink_assert(false);

  return;
}

int64_t
QUICNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  ink_assert(false);

  return 0;
}

void
QUICNetVConnection::registerNextProtocolSet(SSLNextProtocolSet *s)
{
  this->_next_protocol_set = s;
}

SSLNextProtocolSet *
QUICNetVConnection::next_protocol_set()
{
  return this->_next_protocol_set;
}

QUICPacketNumber
QUICNetVConnection::largest_received_packet_number()
{
  return this->_largest_received_packet_number;
}

QUICPacketNumber
QUICNetVConnection::largest_acked_packet_number()
{
  return this->_loss_detector->largest_acked_packet_number();
}

QUICError
QUICNetVConnection::_state_handshake_process_initial_client_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  if (packet->size() < MINIMUM_INITIAL_CLIENT_PACKET_SIZE) {
    DebugQUICCon("Packet size is smaller than the minimum initial client packet size");
    return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
  }

  // Start handshake
  QUICError error = this->_handshake_handler->start(packet.get(), &this->_packet_factory);
  if (this->_handshake_handler->is_version_negotiated()) {
    // Check integrity (QUIC-TLS-04: 6.1. Integrity Check Processing)
    if (packet->has_valid_fnv1a_hash()) {
      bool should_send_ack;
      error = this->_frame_dispatcher->receive_frames(packet->payload(), packet->payload_size(), should_send_ack);
      if (error.cls != QUICErrorClass::NONE) {
        return error;
      }
      error = this->_local_flow_controller->update(this->_stream_manager->total_offset_received());
      Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [LOCAL] %" PRIu64 "/%" PRIu64,
            static_cast<uint64_t>(this->_quic_connection_id), this->_local_flow_controller->current_offset(),
            this->_local_flow_controller->current_limit());
      if (error.cls != QUICErrorClass::NONE) {
        return error;
      }
    } else {
      DebugQUICCon("Invalid FNV-1a hash value");
      return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::CRYPTOGRAPHIC_ERROR);
    }
  }
  return error;
}

QUICError
QUICNetVConnection::_state_handshake_process_client_cleartext_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  QUICError error = QUICError(QUICErrorClass::NONE);

  // The payload of this packet contains STREAM frames and could contain PADDING and ACK frames
  if (packet->has_valid_fnv1a_hash()) {
    error = this->_recv_and_ack(packet->payload(), packet->payload_size(), packet->packet_number());
  } else {
    DebugQUICCon("Invalid FNV-1a hash value");
    return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::CRYPTOGRAPHIC_ERROR);
  }
  return error;
}

QUICError
QUICNetVConnection::_state_handshake_process_zero_rtt_protected_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  // TODO: Decrypt the packet
  // decrypt(payload, p);
  // TODO: Not sure what we have to do
  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICNetVConnection::_state_connection_established_process_packet(std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet)
{
  // TODO: fix size
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  if (this->_crypto->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, packet->payload(), packet->payload_size(),
                             packet->packet_number(), packet->header(), packet->header_size(), packet->key_phase())) {
    DebugQUICCon("Decrypt Packet, pkt_num: %" PRIu64 ", header_len: %hu, payload_len: %zu", packet->packet_number(),
                 packet->header_size(), plain_txt_len);

    return this->_recv_and_ack(plain_txt.get(), plain_txt_len, packet->packet_number());
  } else {
    DebugQUICCon("CRYPTOGRAPHIC Error");

    return QUICError(QUICErrorClass::CRYPTOGRAPHIC);
  }
}

QUICError
QUICNetVConnection::_state_common_receive_packet()
{
  QUICError error;
  std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> p =
    std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(this->_packet_recv_queue.dequeue(), &QUICPacketDeleter::delete_packet);
  net_activity(this, this_ethread());

  switch (p->type()) {
  case QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_0:
  case QUICPacketType::ONE_RTT_PROTECTED_KEY_PHASE_1:
    error = this->_state_connection_established_process_packet(std::move(p));
    break;
  default:
    error = QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
    break;
  }
  return error;
}

QUICError
QUICNetVConnection::_state_common_send_packet()
{
  this->_packetize_frames();

  QUICPacket *packet;
  QUICError error = this->_remote_flow_controller->update(this->_stream_manager->total_offset_sent());
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
        static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
        this->_remote_flow_controller->current_limit());
  if (error.cls != QUICErrorClass::NONE) {
    return error;
  }
  while ((packet = this->_packet_send_queue.dequeue()) != nullptr) {
    this->_packet_handler->send_packet(*packet, this);
    this->_loss_detector->on_packet_sent(
      std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>(packet, &QUICPacketDeleter::delete_packet));
  }

  net_activity(this, this_ethread());

  return QUICError(QUICErrorClass::NONE);
}

void
QUICNetVConnection::_packetize_frames()
{
  uint32_t max_size = this->maximum_quic_packet_size();
  uint32_t min_size = this->minimum_quic_packet_size();
  ats_unique_buf buf(nullptr, [](void *p) { ats_free(p); });
  size_t len = 0;

  // Put frames into buf as many as possible
  std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame(nullptr, nullptr);
  bool retransmittable                = false;
  QUICPacketType previous_packet_type = QUICPacketType::UNINITIALIZED;
  QUICPacketType current_packet_type  = QUICPacketType::UNINITIALIZED;

  while (this->_frame_buffer.size() > 0) {
    frame = std::move(this->_frame_buffer.front());
    this->_frame_buffer.pop();
    QUICRetransmissionFrame *rf = dynamic_cast<QUICRetransmissionFrame *>(frame.get());
    previous_packet_type        = current_packet_type;
    if (rf) {
      current_packet_type = rf->packet_type();
    } else {
      current_packet_type = QUICPacketType::UNINITIALIZED;
    }
    if (len + frame->size() + MAX_PACKET_OVERHEAD > max_size || (previous_packet_type != current_packet_type && len > 0)) {
      ink_assert(len > 0);
      SCOPED_MUTEX_LOCK(transmitter_lock, this->get_transmitter_mutex().get(), this_ethread());
      this->transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, previous_packet_type));
      len = 0;
    }
    retransmittable = retransmittable || (frame->type() != QUICFrameType::ACK && frame->type() != QUICFrameType::PADDING);

    if (buf == nullptr) {
      buf = ats_unique_malloc(max_size);
    }
    size_t l = 0;
    DebugQUICCon("type=%s", QUICDebugNames::frame_type(frame->type()));
    frame->store(buf.get() + len, &l);
    len += l;
  }

  if (len != 0) {
    // Pad with PADDING frames
    if (min_size > len) {
      // FIXME QUICNetVConnection should not know the actual type value of PADDING frame
      memset(buf.get() + len, 0, min_size - len);
      len += min_size - len;
    }
    SCOPED_MUTEX_LOCK(transmitter_lock, this->get_transmitter_mutex().get(), this_ethread());
    this->transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, current_packet_type));
  }
}

QUICError
QUICNetVConnection::_recv_and_ack(const uint8_t *payload, uint16_t size, QUICPacketNumber packet_num)
{
  if (packet_num > this->_largest_received_packet_number) {
    this->_largest_received_packet_number = packet_num;
  }

  bool should_send_ack;

  QUICError error;

  error = this->_frame_dispatcher->receive_frames(payload, size, should_send_ack);
  if (error.cls != QUICErrorClass::NONE) {
    return error;
  }

  error = this->_local_flow_controller->update(this->_stream_manager->total_offset_received());
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [LOCAL] %" PRIu64 "/%" PRIu64, static_cast<uint64_t>(this->_quic_connection_id),
        this->_local_flow_controller->current_offset(), this->_local_flow_controller->current_limit());
  if (error.cls != QUICErrorClass::NONE) {
    return error;
  }
  // this->_local_flow_controller->forward_limit();

  this->_ack_frame_creator.update(packet_num, should_send_ack);
  std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> ack_frame = this->_ack_frame_creator.create_if_needed();
  if (ack_frame != nullptr) {
    this->transmit_frame(std::move(ack_frame));
  }

  return error;
}

std::unique_ptr<QUICPacket, QUICPacketDeleterFunc>
QUICNetVConnection::_build_packet(ats_unique_buf buf, size_t len, bool retransmittable, QUICPacketType type)
{
  std::unique_ptr<QUICPacket, QUICPacketDeleterFunc> packet(nullptr, &QUICPacketDeleter::delete_null_packet);

  switch (type) {
  case QUICPacketType::SERVER_CLEARTEXT:
    packet = this->_packet_factory.create_server_cleartext_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                                  std::move(buf), len, retransmittable);
    break;
  default:
    if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
      packet = this->_packet_factory.create_server_protected_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                                    std::move(buf), len, retransmittable);
    } else {
      packet = this->_packet_factory.create_server_cleartext_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                                    std::move(buf), len, retransmittable);
    }
    break;
  }

  return packet;
}

QUICApplication *
QUICNetVConnection::_create_application()
{
  const uint8_t *app_name;
  unsigned int app_name_len = 0;
  this->_handshake_handler->negotiated_application_name(&app_name, &app_name_len);
  if (app_name) {
    DebugQUICCon("ALPN: %.*s", app_name_len, app_name);
    if (memcmp(TS_ALPN_PROTOCOL_HTTP_QUIC, app_name, app_name_len) == 0) {
      return new QUICEchoApp(this);
    } else {
      DebugQUICCon("Negotiated application is not available");
      ink_assert(false);
      return nullptr;
    }
  } else {
    DebugQUICCon("Failed to negotiate application");
    return nullptr;
  }
}

void
QUICNetVConnection::_init_flow_control_params(const std::shared_ptr<const QUICTransportParameters> &local_tp,
                                              const std::shared_ptr<const QUICTransportParameters> &remote_tp)
{
  this->_stream_manager->init_flow_control_params(local_tp, remote_tp);

  uint32_t local_initial_max_data  = 0;
  uint32_t remote_initial_max_data = 0;
  if (local_tp) {
    local_initial_max_data = local_tp->initial_max_data();
  }
  if (remote_tp) {
    remote_initial_max_data = remote_tp->initial_max_data();
  }

  this->_local_flow_controller->forward_limit(local_initial_max_data);
  this->_remote_flow_controller->forward_limit(remote_initial_max_data);
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [LOCAL] %" PRIu64 "/%" PRIu64, static_cast<uint64_t>(this->_quic_connection_id),
        this->_local_flow_controller->current_offset(), this->_local_flow_controller->current_limit());
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
        static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
        this->_remote_flow_controller->current_limit());
}
