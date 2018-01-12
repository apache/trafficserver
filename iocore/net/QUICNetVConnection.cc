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

#include "QUICConfig.h"
#include "QUICDebugNames.h"
#include "QUICEvents.h"
#include "QUICConfig.h"

#define STATE_FROM_VIO(_x) ((NetState *)(((char *)(_x)) - STATE_VIO_OFFSET))
#define STATE_VIO_OFFSET ((uintptr_t) & ((NetState *)0)->vio)

#define QUICConDebug(fmt, ...) \
  Debug("quic_net", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_quic_connection_id), ##__VA_ARGS__)

#define QUICError(fmt, ...)                                                                                 \
  Debug("quic_net", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_quic_connection_id), ##__VA_ARGS__); \
  Error("quic_net [%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_quic_connection_id), ##__VA_ARGS__)

static constexpr uint32_t MAX_PACKET_OVERHEAD                = 17; // Max long header len(17)
static constexpr uint32_t MAX_STREAM_FRAME_OVERHEAD          = 15;
static constexpr uint32_t MINIMUM_INITIAL_CLIENT_PACKET_SIZE = 1200;

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::init(QUICConnectionId original_cid, UDPConnection *udp_con, QUICPacketHandler *packet_handler)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_pre_handshake);
  this->_packet_transmitter_mutex    = new_ProxyMutex();
  this->_frame_transmitter_mutex     = new_ProxyMutex();
  this->_udp_con                     = udp_con;
  this->_packet_handler              = packet_handler;
  this->_original_quic_connection_id = original_cid;
  this->_quic_connection_id.randomize();
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
QUICNetVConnection::startEvent(int /*event ATS_UNUSED */, Event *e)
{
  return EVENT_DONE;
}

// XXX This might be called on ET_UDP thread
void
QUICNetVConnection::start(SSL_CTX *ssl_ctx)
{
  // Version 0x00000001 uses stream 0 for cryptographic handshake with TLS 1.3, but newer version may not
  if (this->direction() == NET_VCONNECTION_IN) {
    QUICConfig::scoped_config params;
    this->_reset_token.generate(this->_quic_connection_id, params->server_id());
    this->_handshake_handler = new QUICHandshake(this, ssl_ctx, this->_reset_token);
  } else {
    this->_handshake_handler = new QUICHandshake(this, ssl_ctx);
    this->_handshake_handler->start(&this->_packet_factory);
  }
  this->_application_map = new QUICApplicationMap();
  this->_application_map->set(STREAM_ID_FOR_HANDSHAKE, this->_handshake_handler);

  this->_crypto           = this->_handshake_handler->crypto_module();
  this->_frame_dispatcher = new QUICFrameDispatcher();
  this->_packet_factory.set_crypto_module(this->_crypto);

  // Create frame handlers
  this->_stream_manager         = new QUICStreamManager(this->connection_id(), this, this->_application_map);
  this->_congestion_controller  = new QUICCongestionController();
  this->_loss_detector          = new QUICLossDetector(this, this->_congestion_controller);
  this->_remote_flow_controller = new QUICRemoteConnectionFlowController(0, this);
  this->_local_flow_controller  = new QUICLocalConnectionFlowController(0, this);

  this->_frame_dispatcher->add_handler(this);
  this->_frame_dispatcher->add_handler(this->_stream_manager);
  this->_frame_dispatcher->add_handler(this->_loss_detector);
}

void
QUICNetVConnection::free(EThread *t)
{
  QUICConDebug("Free connection");

  this->_udp_con        = nullptr;
  this->_packet_handler = nullptr;
  _unschedule_packet_write_ready();

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

int
QUICNetVConnection::connectUp(EThread *t, int fd)
{
  // create stream 0
  // FIXME: integration w/ QUICStreamManager
  QUICStream *stream = new (THREAD_ALLOC(quicStreamAllocator, this_ethread())) QUICStream();
  stream->init(this, this->connection_id(), STREAM_ID_FOR_HANDSHAKE);
  if (!this->_handshake_handler->is_stream_set(stream)) {
    this->_handshake_handler->set_stream(stream);
  }

  // start QUIC handshake
  this->_handshake_handler->handleEvent(VC_EVENT_WRITE_READY, nullptr);

  // action_.continuation->handleEvent(NET_EVENT_OPEN, this);
  return CONNECT_SUCCESS;
}

QUICConnectionId
QUICNetVConnection::original_connection_id()
{
  return this->_original_quic_connection_id;
}

QUICConnectionId
QUICNetVConnection::connection_id()
{
  return this->_quic_connection_id;
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
    QUICConDebug("Packet Number=%" PRIu64 " Type=%s Size=%hu", packet->packet_number(), QUICDebugNames::packet_type(packet->type()),
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
  ink_assert(packet.type() != QUICPacketType::VERSION_NEGOTIATION && packet.type() != QUICPacketType::UNINITIALIZED);

  // Get payload from a header because packet.payload() is encrypted
  uint16_t size          = packet.header()->payload_size();
  const uint8_t *payload = packet.header()->payload();

  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();
  uint16_t cursor     = 0;

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
QUICNetVConnection::get_packet_transmitter_mutex()
{
  return this->_packet_transmitter_mutex;
}

void
QUICNetVConnection::push_packet(UDPPacket *packet)
{
  this->_packet_recv_queue.enqueue(packet);
}

void
QUICNetVConnection::_transmit_frame(QUICFrameUPtr frame)
{
  SCOPED_MUTEX_LOCK(frame_transmitter_lock, this->_frame_transmitter_mutex, this_ethread());

  if (frame) {
    QUICConDebug("Frame Type=%s Size=%zu", QUICDebugNames::frame_type(frame->type()), frame->size());
    if (frame->type() == QUICFrameType::STREAM) {
      QUICStreamFrame &stream_frame = static_cast<QUICStreamFrame &>(*frame);
      // XXX: Stream 0 is exempt from the connection-level flow control window.
      if (stream_frame.stream_id() == STREAM_ID_FOR_HANDSHAKE) {
        this->_frame_send_queue.push(std::move(frame));
      } else {
        this->_stream_frame_send_queue.push(std::move(frame));
      }
    } else {
      this->_frame_send_queue.push(std::move(frame));
    }
  }
}

void
QUICNetVConnection::transmit_frame(QUICFrameUPtr frame)
{
  this->_transmit_frame(std::move(frame));
  this->_schedule_packet_write_ready();
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
    Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
          static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
          this->_remote_flow_controller->current_limit());
    this->_schedule_packet_write_ready();

    break;
  case QUICFrameType::BLOCKED:
    // BLOCKED frame is for debugging. Nothing to do here.
    break;
  case QUICFrameType::CONNECTION_CLOSE:
    this->_switch_to_close_state();
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

  this->set_inactivity_timeout(HRTIME_SECONDS(params->no_activity_timeout_in()));
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
    QUICPacketUPtr packet = this->_dequeue_recv_packet(result);
    if (result == QUICPacketCreationResult::NOT_READY) {
      error = QUICErrorUPtr(new QUICNoError());
    } else if (result == QUICPacketCreationResult::FAILED) {
      error = QUICConnectionErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_FATAL_ALERT_GENERATED));
    } else {
      error = this->_state_handshake_process_packet(std::move(packet));
    }
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);
    error = this->_state_common_send_packet();
    break;
  }
  case EVENT_IMMEDIATE:
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  default:
    QUICConDebug("Unexpected event: %s", QUICDebugNames::quic_event(event));
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
    break;
  }
  case EVENT_IMMEDIATE: {
    // Start Immediate Close because of Idle Timeout
    this->_handle_idle_timeout();
    break;
  }
  default:
    QUICConDebug("Unexpected event: %s", QUICDebugNames::quic_event(event));
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
  case QUIC_EVENT_PACKET_READ_READY: {
    if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
      error = this->_state_common_receive_packet();
    } else {
      // FIXME Just ignore for now but it has to be acked (GitHub#2609)
    }
    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);
    this->_state_common_send_packet();
    break;
  }
  default:
    QUICConDebug("Unexpected event: %s", QUICDebugNames::quic_event(event));
  }

  // FIXME Enter closed state if CONNECTION_CLOSE was ACKed and draining period end
  if (true) {
    this->_switch_to_close_state();
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
    this->next_inactivity_timeout_at = 0;
    this->next_activity_timeout_at   = 0;

    this->inactivity_timeout_in = 0;
    this->active_timeout_in     = 0;

    // TODO: Drop record from Connection-ID - QUICNetVConnection table in QUICPacketHandler
    // Shutdown loss detector
    this->_loss_detector->handleEvent(QUIC_EVENT_LD_SHUTDOWN, nullptr);

    break;
  }
  case QUIC_EVENT_PACKET_WRITE_READY: {
    this->_close_packet_write_ready(data);
    break;
  }
  default:
    QUICConDebug("Unexpected event: %s", QUICDebugNames::quic_event(event));
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

int
QUICNetVConnection::populate_protocol(ts::string_view *results, int n) const
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
QUICNetVConnection::protocol_contains(ts::string_view prefix) const
{
  const char *retval  = nullptr;
  ts::string_view tag = IP_PROTO_TAG_QUIC;
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
QUICNetVConnection::is_closed()
{
  return this->handler == reinterpret_cast<NetVConnHandler>(&QUICNetVConnection::state_connection_closed);
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

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_packet(QUICPacketUPtr packet)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (packet->type()) {
  case QUICPacketType::INITIAL:
    error = this->_state_handshake_process_initial_client_packet(std::move(packet));
    break;
  case QUICPacketType::HANDSHAKE:
    error = this->_state_handshake_process_client_cleartext_packet(std::move(packet));
    break;
  case QUICPacketType::ZERO_RTT_PROTECTED:
    error = this->_state_handshake_process_zero_rtt_protected_packet(std::move(packet));
    break;
  default:
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
    break;
  }
  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_initial_client_packet(QUICPacketUPtr packet)
{
  if (packet->size() < MINIMUM_INITIAL_CLIENT_PACKET_SIZE) {
    QUICConDebug("Packet size is smaller than the minimum initial client packet size");
    // Ignore the packet
    return QUICErrorUPtr(new QUICNoError());
  }

  // Start handshake
  QUICErrorUPtr error = this->_handshake_handler->start(packet.get(), &this->_packet_factory);
  if (this->_handshake_handler->is_version_negotiated()) {
    error = this->_recv_and_ack(packet->payload(), packet->payload_size(), packet->packet_number());
  } else {
    // Perhaps response packets for initial client packet were lost, but no need to start handshake again because loss detector will
    // retransmit the packets.
  }
  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_client_cleartext_packet(QUICPacketUPtr packet)
{
  return this->_recv_and_ack(packet->payload(), packet->payload_size(), packet->packet_number());
}

QUICErrorUPtr
QUICNetVConnection::_state_handshake_process_zero_rtt_protected_packet(QUICPacketUPtr packet)
{
  // TODO: Not sure what we have to do
  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICNetVConnection::_state_connection_established_process_packet(QUICPacketUPtr packet)
{
  return this->_recv_and_ack(packet->payload(), packet->payload_size(), packet->packet_number());
}

QUICErrorUPtr
QUICNetVConnection::_state_common_receive_packet()
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  QUICPacketCreationResult result;

  // Receive a QUIC packet
  QUICPacketUPtr p = this->_dequeue_recv_packet(result);
  if (result == QUICPacketCreationResult::FAILED) {
    return QUICConnectionErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_FATAL_ALERT_GENERATED));
  } else if (result == QUICPacketCreationResult::NOT_READY) {
    return QUICErrorUPtr(new QUICNoError());
  }
  net_activity(this, this_ethread());

  // Check connection migration
  if (p->connection_id() != this->_quic_connection_id) {
    for (unsigned int i = 0; i < countof(this->_alt_quic_connection_ids); ++i) {
      AltConnectionInfo &info = this->_alt_quic_connection_ids[i];
      if (info.id == p->connection_id()) {
        // Migrate connection
        // TODO Address Validation
        // TODO Adjust expected packet number with a gap computed based on info.seq_num
        // TODO Unregister the old connection id (Should we wait for a while?)
        this->_quic_connection_id = info.id;
        this->_reset_token        = info.token;
        this->_update_alt_connection_ids(i);
        break;
      }
    }
    ink_assert(p->connection_id() == this->_quic_connection_id);
  }

  // Process the packet
  switch (p->type()) {
  case QUICPacketType::PROTECTED:
    error = this->_state_connection_established_process_packet(std::move(p));
    break;
  case QUICPacketType::HANDSHAKE:
    // FIXME Just ignore for now but it has to be acked (GitHub#2609)
    break;
  default:
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::INTERNAL_ERROR));
    break;
  }
  return error;
}

QUICErrorUPtr
QUICNetVConnection::_state_common_send_packet()
{
  this->_packetize_frames();

  QUICPacket *packet;

  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  while ((packet = this->_packet_send_queue.dequeue()) != nullptr) {
    this->_packet_handler->send_packet(*packet, this);
    this->_loss_detector->on_packet_sent(QUICPacketUPtr(packet, &QUICPacketDeleter::delete_packet));
  }

  net_activity(this, this_ethread());

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
  } else {
    current_packet_type = QUICPacketType::UNINITIALIZED;
  }

  if (len + frame->size() + MAX_PACKET_OVERHEAD > max_size || (previous_packet_type != current_packet_type && len > 0)) {
    this->_transmit_packet(this->_build_packet(std::move(buf), len, retransmittable, previous_packet_type));
    retransmittable = false;
    len             = 0;
  }

  retransmittable = retransmittable || (frame->type() != QUICFrameType::ACK && frame->type() != QUICFrameType::PADDING);

  if (buf == nullptr) {
    buf = ats_unique_malloc(max_size);
  }

  size_t l = 0;
  QUICConDebug("type=%s", QUICDebugNames::frame_type(frame->type()));
  frame->store(buf.get() + len, &l);
  len += l;

  return;
}

// 1. Dequeue frame from _stream_frame_send_queue and _frame_send_queue
// 2. Put frames into buffer as many as possible
// 3. Build packet with the buffer
// 4. Enqueue the packet via transmit_packet
void
QUICNetVConnection::_packetize_frames()
{
  size_t len = 0;
  ats_unique_buf buf(nullptr, [](void *p) { ats_free(p); });
  QUICPacketType current_packet_type = QUICPacketType::UNINITIALIZED;

  QUICFrameUPtr frame(nullptr, nullptr);
  bool retransmittable = false;

  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  SCOPED_MUTEX_LOCK(frame_transmitter_lock, this->_frame_transmitter_mutex, this_ethread());

  QUICFrameUPtr ack_frame = QUICFrameFactory::create_null_ack_frame();
  if (this->_frame_send_queue.size() || this->_stream_frame_send_queue.size()) {
    ack_frame = this->_ack_frame_creator.create();
  } else {
    ack_frame = this->_ack_frame_creator.create_if_needed();
  }
  if (ack_frame != nullptr) {
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(ack_frame));
  }

  while (this->_frame_send_queue.size() > 0) {
    frame = std::move(this->_frame_send_queue.front());
    this->_frame_send_queue.pop();
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
  }

  while (this->_stream_frame_send_queue.size() > 0) {
    const QUICFrameUPtr &f = this->_stream_frame_send_queue.front();
    uint32_t frame_size    = f->size();

    int ret = this->_remote_flow_controller->update((this->_stream_manager->total_offset_sent() + frame_size));
    Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
          static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
          this->_remote_flow_controller->current_limit());

    if (ret != 0) {
      QUICConDebug("Flow Controller blocked sending a STREAM frame");
      break;
    }

    frame = std::move(this->_stream_frame_send_queue.front());
    this->_stream_frame_send_queue.pop();
    this->_store_frame(buf, len, retransmittable, current_packet_type, std::move(frame));
    this->_stream_manager->add_total_offset_sent(frame_size);
  }

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

QUICErrorUPtr
QUICNetVConnection::_recv_and_ack(const uint8_t *payload, uint16_t size, QUICPacketNumber packet_num)
{
  if (packet_num > this->_largest_received_packet_number) {
    this->_largest_received_packet_number = packet_num;
  }

  bool should_send_ack;

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  error = this->_frame_dispatcher->receive_frames(payload, size, should_send_ack);
  if (error->cls != QUICErrorClass::NONE) {
    return error;
  }

  int ret = this->_local_flow_controller->update(this->_stream_manager->total_offset_received());
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [LOCAL] %" PRIu64 "/%" PRIu64, static_cast<uint64_t>(this->_quic_connection_id),
        this->_local_flow_controller->current_offset(), this->_local_flow_controller->current_limit());
  if (ret != 0) {
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::FLOW_CONTROL_ERROR));
  }

  this->_ack_frame_creator.update(packet_num, should_send_ack);
  static_cast<QUICConnection *>(this)->transmit_frame();

  return error;
}

QUICPacketUPtr
QUICNetVConnection::_build_packet(ats_unique_buf buf, size_t len, bool retransmittable, QUICPacketType type)
{
  QUICPacketUPtr packet = QUICPacketFactory::create_null_packet();

  switch (type) {
  case QUICPacketType::INITIAL:
    ink_assert(this->get_context() == NET_VCONNECTION_OUT);
    packet = this->_packet_factory.create_initial_packet(this->_original_quic_connection_id, this->largest_acked_packet_number(),
                                                         QUIC_SUPPORTED_VERSIONS[0], std::move(buf), len);
    break;
  case QUICPacketType::HANDSHAKE:
    packet = this->_packet_factory.create_handshake_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                           std::move(buf), len, retransmittable);
    break;
  default:
    if (this->_handshake_handler && this->_handshake_handler->is_completed()) {
      packet = this->_packet_factory.create_server_protected_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                                    std::move(buf), len, retransmittable);
    } else {
      // FIXME: remove this flag
      if (this->get_context() == NET_VCONNECTION_OUT && this->_is_initial) {
        this->_is_initial = false;
        packet            = this->_packet_factory.create_initial_packet(
          this->_original_quic_connection_id, this->largest_acked_packet_number(), QUIC_SUPPORTED_VERSIONS[0], std::move(buf), len);
      } else {
        packet = this->_packet_factory.create_handshake_packet(this->_quic_connection_id, this->largest_acked_packet_number(),
                                                               std::move(buf), len, retransmittable);
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

  this->_local_flow_controller->forward_limit(local_initial_max_data * 1024);
  this->_remote_flow_controller->forward_limit(remote_initial_max_data * 1024);
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [LOCAL] %" PRIu64 "/%" PRIu64, static_cast<uint64_t>(this->_quic_connection_id),
        this->_local_flow_controller->current_offset(), this->_local_flow_controller->current_limit());
  Debug("quic_flow_ctrl", "Connection [%" PRIx64 "] [REMOTE] %" PRIu64 "/%" PRIu64,
        static_cast<uint64_t>(this->_quic_connection_id), this->_remote_flow_controller->current_offset(),
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
    serror->stream->reset(QUICStreamErrorUPtr(serror));
  } else {
    // Connection Error
    QUICConnectionError *cerror = static_cast<QUICConnectionError *>(error.release());
    this->close(QUICConnectionErrorUPtr(cerror));
  }
}

QUICPacketUPtr
QUICNetVConnection::_dequeue_recv_packet(QUICPacketCreationResult &result)
{
  QUICPacketUPtr quic_packet = QUICPacketFactory::create_null_packet();
  UDPPacket *udp_packet      = this->_packet_recv_queue.dequeue();
  if (!udp_packet) {
    result = QUICPacketCreationResult::NOT_READY;
    return quic_packet;
  }
  net_activity(this, this_ethread());

  // Create a QUIC packet
  ats_unique_buf pkt = ats_unique_malloc(udp_packet->getPktLength());
  IOBufferBlock *b   = udp_packet->getIOBlockChain();
  size_t written     = 0;
  while (b) {
    memcpy(pkt.get() + written, b->buf(), b->read_avail());
    written += b->read_avail();
    b = b->next.get();
  }
  quic_packet = this->_packet_factory.create(std::move(pkt), written, this->largest_received_packet_number(), result);
  if (result == QUICPacketCreationResult::NOT_READY) {
    QUICConDebug("Not ready to decrypt the packet");
    // Retry later
    this->_packet_recv_queue.enqueue(udp_packet);
    this_ethread()->schedule_in_local(this, HRTIME_MSECONDS(10), QUIC_EVENT_PACKET_READ_READY);
  } else {
    udp_packet->free();
    if (result == QUICPacketCreationResult::SUCCESS) {
      QUICConDebug("type=%s pkt_num=%" PRIu64 " size=%u", QUICDebugNames::packet_type(quic_packet->type()),
                   quic_packet->packet_number(), quic_packet->size());
    } else {
      QUICConDebug("Failed to decrypt the packet");
    }
  }

  return quic_packet;
}

void
QUICNetVConnection::_schedule_packet_write_ready()
{
  SCOPED_MUTEX_LOCK(packet_transmitter_lock, this->_packet_transmitter_mutex, this_ethread());
  if (!this->_packet_write_ready) {
    QUICConDebug("Schedule %s event", QUICDebugNames::quic_event(QUIC_EVENT_PACKET_WRITE_READY));
    this->_packet_write_ready = eventProcessor.schedule_imm(this, ET_CALL, QUIC_EVENT_PACKET_WRITE_READY, nullptr);
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

  const uint8_t *app_name;
  unsigned int app_name_len = 0;
  this->_handshake_handler->negotiated_application_name(&app_name, &app_name_len);
  if (app_name == nullptr) {
    app_name     = reinterpret_cast<const uint8_t *>(IP_PROTO_TAG_HTTP_QUIC.data());
    app_name_len = IP_PROTO_TAG_HTTP_QUIC.size();
  }

  Continuation *endpoint = this->_next_protocol_set->findEndpoint(app_name, app_name_len);
  if (endpoint == nullptr) {
    this->_handle_error(QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR)));
  } else {
    endpoint->handleEvent(NET_EVENT_ACCEPT, this);
  }

  return 0;
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
    QUICConDebug("%s", this->_handshake_handler->negotiated_cipher_suite());
    SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_established);

    this->_update_alt_connection_ids(countof(this->_alt_quic_connection_ids) - 1);
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
  if (error->cls == QUICErrorClass::APPLICATION) {
    this->transmit_frame(QUICFrameFactory::create_application_close_frame(std::move(error)));
  } else {
    this->transmit_frame(QUICFrameFactory::create_connection_close_frame(std::move(error)));
  }
  QUICConDebug("Enter state_connection_closing");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closing);
}

void
QUICNetVConnection::_switch_to_close_state()
{
  if (this->_complete_handshake_if_possible() != 0) {
    QUICConDebug("Switching state without handshake completion");
  }
  QUICConDebug("Enter state_connection_closed");
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_connection_closed);
  this_ethread()->schedule_imm(this, QUIC_EVENT_SHUTDOWN, nullptr);
}

void
QUICNetVConnection::_handle_idle_timeout()
{
  this->remove_from_active_queue();
  this->close(std::make_unique<QUICConnectionError>(QUICTransErrorCode::NO_ERROR, "Idle Timeout"));

  // TODO: signal VC_EVENT_ACTIVE_TIMEOUT/VC_EVENT_INACTIVITY_TIMEOUT to application
}

void
QUICNetVConnection::_update_alt_connection_ids(uint8_t chosen)
{
  QUICConfig::scoped_config params;
  int n       = sizeof(this->_alt_quic_connection_ids);
  int current = this->_alt_quic_connection_id_seq_num % n;
  int delta   = chosen - current;
  int count   = (n + delta) % n + 1;

  for (int i = 0; i < count; ++i) {
    int index = (current + i) % n;
    QUICConnectionId conn_id;
    QUICStatelessResetToken token;

    conn_id.randomize();
    token.generate(conn_id, params->server_id());
    this->_alt_quic_connection_ids[index] = {this->_alt_quic_connection_id_seq_num + i, conn_id, token};
    this->_packet_handler->registerAltConnectionId(conn_id, this);
    this->transmit_frame(QUICFrameFactory::create_new_connection_id_frame(this->_alt_quic_connection_ids[index].seq_num,
                                                                          this->_alt_quic_connection_ids[index].id,
                                                                          this->_alt_quic_connection_ids[index].token));
  }
  this->_alt_quic_connection_id_seq_num += count;
}
