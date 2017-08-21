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

#include "QUICEchoApp.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICConfig.h"

#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))
#define STATE_VIO_OFFSET ((uintptr_t) & ((NetState *)0)->vio)

#define DebugQUICCon(fmt, ...) \
  Debug("quic_net", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_quic_connection_id), ##__VA_ARGS__)

const static uint32_t MINIMUM_MTU               = 1280;
const static uint32_t MAX_PACKET_OVERHEAD       = 25; // Max long header len(17) + FNV-1a hash len(8)
const static uint32_t MAX_STREAM_FRAME_OVERHEAD = 15;

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

QUICNetVConnection::QUICNetVConnection() : UnixNetVConnection()
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);
}

void
QUICNetVConnection::init(UDPConnection *udp_con, QUICPacketHandler *packet_handler)
{
  this->_transmitter_mutex = new_ProxyMutex();
  this->_udp_con           = udp_con;
  this->_transmitter_mutex = new_ProxyMutex();
  this->_packet_handler    = packet_handler;
  this->_quic_connection_id.randomize();
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
  this->_version_negotiator = new QUICVersionNegotiator(&this->_packet_factory, this);
  this->_crypto             = new QUICCrypto(ssl_ctx, this);
  this->_packet_factory.set_crypto_module(this->_crypto);

  // FIXME Should these have to be shared_ptr?
  this->_loss_detector  = std::make_shared<QUICLossDetector>(this);
  this->_stream_manager = std::make_shared<QUICStreamManager>();
  this->_stream_manager->init(this);
  this->_stream_manager->set_connection(this); // FIXME Want to remove;

  std::shared_ptr<QUICFlowController> flowController             = std::make_shared<QUICFlowController>();
  std::shared_ptr<QUICCongestionController> congestionController = std::make_shared<QUICCongestionController>();
  this->_frame_dispatcher =
    new QUICFrameDispatcher(this, this->_stream_manager, flowController, congestionController, this->_loss_detector);

  // FIXME Fill appropriate values
  // MUSTs
  QUICTransportParametersInEncryptedExtensions *tp = new QUICTransportParametersInEncryptedExtensions();
  tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, {reinterpret_cast<const uint8_t *>("\x00\x00\x00\x00"), 4});
  tp->add(QUICTransportParameterId::INITIAL_MAX_DATA, {reinterpret_cast<const uint8_t *>("\x00\x00\x00\x00"), 4});
  tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_ID, {reinterpret_cast<const uint8_t *>("\x00\x00\x00\x00"), 4});
  tp->add(QUICTransportParameterId::IDLE_TIMEOUT, {reinterpret_cast<const uint8_t *>("\x00\x00"), 2});
  tp->add_version(QUIC_SUPPORTED_VERSIONS[0]);
  // MAYs
  // this->_local_transport_parameters.add(QUICTransportParameterId::TRUNCATE_CONNECTION_ID, {});
  // this->_local_transport_parameters.add(QUICTransportParameterId::MAX_PACKET_SIZE, {{0x00, 0x00}, 2});
  this->_local_transport_parameters = std::unique_ptr<QUICTransportParameters>(tp);
}

void
QUICNetVConnection::free(EThread *t)
{
  DebugQUICCon("Free connection");

  this->_udp_con        = nullptr;
  this->_packet_handler = nullptr;

  delete this->_version_negotiator;
  delete this->_handshake_handler;
  delete this->_application;
  delete this->_crypto;
  delete this->_frame_dispatcher;
  // XXX _loss_detector and _stream_manager are std::shared_ptr

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

void
QUICNetVConnection::set_transport_parameters(std::unique_ptr<QUICTransportParameters> tp)
{
  this->_remote_transport_parameters = std::move(tp);

  const QUICTransportParametersInClientHello *tp_in_ch =
    dynamic_cast<QUICTransportParametersInClientHello *>(this->_remote_transport_parameters.get());
  if (tp_in_ch) {
    // Version revalidation
    QUICVersion version = tp_in_ch->negotiated_version();
    if (this->_version_negotiator->revalidate(version) != QUICVersionNegotiationStatus::REVALIDATED) {
      this->close({QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_VERSION_NEGOTIATION_MISMATCH});
      return;
    }
    if (tp_in_ch->negotiated_version() != tp_in_ch->initial_version()) {
      // FIXME Check initial_version
      /* If the initial version is different from the negotiated_version, a
       * stateless server MUST check that it would have sent a version
       * negotiation packet if it had received a packet with the indicated
       * initial_version. (Draft-04 7.3.4. Version Negotiation Validation)
       */
      this->close({QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_VERSION_NEGOTIATION_MISMATCH});
      return;
    }
    DebugQUICCon("Version negotiation revalidated: %x", packet->version());
    return;
  }

  const QUICTransportParametersInEncryptedExtensions *tp_in_ee =
    dynamic_cast<QUICTransportParametersInEncryptedExtensions *>(this->_remote_transport_parameters.get());
  if (tp_in_ee) {
    // TODO Add client side implementation
    return;
  }
}

const QUICTransportParameters &
QUICNetVConnection::local_transport_parameters()
{
  return *this->_local_transport_parameters;
}

uint32_t
QUICNetVConnection::minimum_quic_packet_size()
{
  if (this->options.ip_family == PF_INET6) {
    return MINIMUM_MTU - 48;
  } else {
    return MINIMUM_MTU - 28;
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
QUICNetVConnection::transmit_packet(std::unique_ptr<const QUICPacket> packet)
{
  // TODO Remove const_cast
  this->_packet_send_queue.enqueue(const_cast<QUICPacket *>(packet.release()));
  eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
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
QUICNetVConnection::push_packet(std::unique_ptr<QUICPacket const> packet)
{
  DebugQUICCon("Type=%s Size=%u", QUICDebugNames::packet_type(packet->type()), packet->size());
  this->_packet_recv_queue.enqueue(const_cast<QUICPacket *>(packet.release()));
}

void
QUICNetVConnection::transmit_frame(std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> frame)
{
  DebugQUICCon("Type=%s Size=%zu", QUICDebugNames::frame_type(frame->type()), frame->size());
  this->_frame_buffer.push(std::move(frame));
  eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
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

void
QUICNetVConnection::handle_frame(std::shared_ptr<const QUICFrame> frame)
{
  switch (frame->type()) {
  case QUICFrameType::CONNECTION_CLOSE:
    DebugQUICCon("Enter state_connection_closed");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closed);
    break;
  default:
    DebugQUICCon("Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }
}

// TODO: Timeout by active_timeout / inactive_timeout
int
QUICNetVConnection::state_handshake(int event, Event *data)
{
  QUICError error;

  if (!thread) {
    thread = this_ethread();
  }

  if (!nh) {
    nh = get_NetHandler(this_ethread());
  }

  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY: {
    std::unique_ptr<const QUICPacket> p = std::unique_ptr<const QUICPacket>(this->_packet_recv_queue.dequeue());
    net_activity(this, this_ethread());

    switch (p->type()) {
    case QUICPacketType::CLIENT_INITIAL:
      error = this->_state_handshake_process_initial_client_packet(std::move(p));
      break;
    case QUICPacketType::CLIENT_CLEARTEXT:
      error = this->_state_handshake_process_client_cleartext_packet(std::move(p));
      break;
    case QUICPacketType::ZERO_RTT_PROTECTED:
      error = this->_state_handshake_process_zero_rtt_protected_packet(std::move(p));
      break;
    default:
      error = QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
      break;
    }

    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    error = this->_state_common_send_packet();
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
    DebugQUICCon("Enter state_connection_established");
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_established);

    QUICConfig::scoped_config params;

    // TODO:  use idle_timeout from negotiated Transport Prameters
    ink_hrtime idle_timeout = HRTIME_SECONDS(params->no_activity_timeout_in());
    this->set_inactivity_timeout(idle_timeout);
    this->add_to_active_queue();
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
    error = this->_state_common_send_packet();
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

QUICApplication *
QUICNetVConnection::get_application(QUICStreamId stream_id)
{
  if (stream_id == STREAM_ID_FOR_HANDSHAKE) {
    return static_cast<QUICApplication *>(this->_handshake_handler);
  } else {
    if (!this->_application) {
      DebugQUICCon("setup quic application");
      // TODO: Instantiate negotiated application
      const uint8_t *application = this->_handshake_handler->negotiated_application_name();
      if (memcmp(application, "hq", 2) == 0) {
        QUICEchoApp *echo_app = new QUICEchoApp(new_ProxyMutex(), this);
        this->_application    = echo_app;
      }
    }
  }
  return this->_application;
}

QUICCrypto *
QUICNetVConnection::get_crypto()
{
  return this->_crypto;
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

QUICError
QUICNetVConnection::_state_handshake_process_initial_client_packet(std::unique_ptr<const QUICPacket> packet)
{
  if (packet->size() < this->minimum_quic_packet_size()) {
    DebugQUICCon("%" PRId32 ", %" PRId32, packet->size(), this->minimum_quic_packet_size());

    return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
  }

  // Negotiate version
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED) {
    if (packet->type() != QUICPacketType::CLIENT_INITIAL) {
      return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
    }
    if (packet->version()) {
      if (this->_version_negotiator->negotiate(packet.get()) == QUICVersionNegotiationStatus::NEGOTIATED) {
        DebugQUICCon("Version negotiation succeeded: %x", packet->version());
        this->_packet_factory.set_version(packet->version());
        // Check integrity (QUIC-TLS-04: 6.1. Integrity Check Processing)
        if (packet->has_valid_fnv1a_hash()) {
          this->_handshake_handler = new QUICHandshake(new_ProxyMutex(), this);
          this->_frame_dispatcher->receive_frames(packet->payload(), packet->payload_size());
        } else {
          DebugQUICCon("Invalid FNV-1a hash value");
        }
      } else {
        DebugQUICCon("Version negotiation failed: %x", packet->version());
      }
    } else {
      return QUICError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::QUIC_INTERNAL_ERROR);
    }
  }

  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICNetVConnection::_state_handshake_process_client_cleartext_packet(std::unique_ptr<const QUICPacket> packet)
{
  // The payload of this packet contains STREAM frames and could contain PADDING and ACK frames
  if (packet->has_valid_fnv1a_hash()) {
    this->_recv_and_ack(packet->payload(), packet->payload_size(), packet->packet_number());
  } else {
    DebugQUICCon("Invalid FNV-1a hash value");
  }
  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICNetVConnection::_state_handshake_process_zero_rtt_protected_packet(std::unique_ptr<const QUICPacket> packet)
{
  // TODO: Decrypt the packet
  // decrypt(payload, p);
  // TODO: Not sure what we have to do
  return QUICError(QUICErrorClass::NONE);
}

QUICError
QUICNetVConnection::_state_connection_established_process_packet(std::unique_ptr<const QUICPacket> packet)
{
  // TODO: fix size
  size_t max_plain_txt_len = 2048;
  ats_unique_buf plain_txt = ats_unique_malloc(max_plain_txt_len);
  size_t plain_txt_len     = 0;

  if (this->_crypto->decrypt(plain_txt.get(), plain_txt_len, max_plain_txt_len, packet->payload(), packet->payload_size(),
                             packet->packet_number(), packet->header(), packet->header_size(), packet->key_phase())) {
    DebugQUICCon("Decrypt Packet, pkt_num: %" PRIu64 ", header_len: %hu, payload_len: %zu", packet->packet_number(),
                 packet->header_size(), plain_txt_len);

    this->_recv_and_ack(plain_txt.get(), plain_txt_len, packet->packet_number());

    return QUICError(QUICErrorClass::NONE);
  } else {
    DebugQUICCon("CRYPTOGRAPHIC Error");

    return QUICError(QUICErrorClass::CRYPTOGRAPHIC);
  }
}

QUICError
QUICNetVConnection::_state_common_receive_packet()
{
  QUICError error;
  std::unique_ptr<const QUICPacket> p = std::unique_ptr<const QUICPacket>(this->_packet_recv_queue.dequeue());
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

  const QUICPacket *packet;
  while ((packet = this->_packet_send_queue.dequeue()) != nullptr) {
    this->_packet_handler->send_packet(*packet, this);
    this->_loss_detector->on_packet_sent(std::unique_ptr<const QUICPacket>(packet));
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

void
QUICNetVConnection::_recv_and_ack(const uint8_t *payload, uint16_t size, QUICPacketNumber packet_num)
{
  bool should_send_ack = this->_frame_dispatcher->receive_frames(payload, size);
  this->_ack_frame_creator.update(packet_num, should_send_ack);
  std::unique_ptr<QUICFrame, QUICFrameDeleterFunc> ack_frame = this->_ack_frame_creator.create_if_needed();
  if (ack_frame != nullptr) {
    this->transmit_frame(std::move(ack_frame));
    eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
  }
}

std::unique_ptr<QUICPacket>
QUICNetVConnection::_build_packet(ats_unique_buf buf, size_t len, bool retransmittable, QUICPacketType type)
{
  std::unique_ptr<QUICPacket> packet;
  DebugQUICCon("retransmittable %u", retransmittable);

  switch (type) {
  case QUICPacketType::SERVER_CLEARTEXT:
    packet = this->_packet_factory.create_server_cleartext_packet(this->_quic_connection_id, std::move(buf), len, retransmittable);
    break;
  default:
    if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
      packet =
        this->_packet_factory.create_server_protected_packet(this->_quic_connection_id, std::move(buf), len, retransmittable);
    } else {
      packet =
        this->_packet_factory.create_server_cleartext_packet(this->_quic_connection_id, std::move(buf), len, retransmittable);
    }
    break;
  }

  return packet;
}
