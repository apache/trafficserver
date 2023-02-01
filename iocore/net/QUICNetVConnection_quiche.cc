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

#include "P_QUICNetVConnection_quiche.h"
#include "P_QUICPacketHandler_quiche.h"
#include "quic/QUICStream_quiche.h"
#include <quiche.h>

static constexpr ink_hrtime WRITE_READY_INTERVAL = HRTIME_MSECONDS(2);

#define QUICConDebug(fmt, ...)  Debug("quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)
#define QUICConVDebug(fmt, ...) Debug("v_quic_net", "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

QUICNetVConnection::QUICNetVConnection() {}

QUICNetVConnection::~QUICNetVConnection() {}

void
QUICNetVConnection::init(QUICVersion version, QUICConnectionId peer_cid, QUICConnectionId original_cid, UDPConnection *,
                         QUICPacketHandler *)
{
}

void
QUICNetVConnection::init(QUICVersion version, QUICConnectionId peer_cid, QUICConnectionId original_cid, QUICConnectionId first_cid,
                         QUICConnectionId retry_cid, UDPConnection *udp_con, quiche_conn *quiche_con,
                         QUICPacketHandler *packet_handler, QUICConnectionTable *ctable)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::acceptEvent);
  this->_udp_con                     = udp_con;
  this->_quiche_con                  = quiche_con;
  this->_packet_handler              = packet_handler;
  this->_original_quic_connection_id = original_cid;
  this->_quic_connection_id.randomize();
  this->_initial_source_connection_id = this->_quic_connection_id;

  if (ctable) {
    this->_ctable = ctable;
    this->_ctable->insert(this->_quic_connection_id, this);
    this->_ctable->insert(this->_original_quic_connection_id, this);
  }
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
}

// called by ET_UDP
void
QUICNetVConnection::destroy(EThread *t)
{
  QUICConDebug("Destroy connection");
  if (from_accept_thread) {
    quicNetVCAllocator.free(this);
  } else {
    THREAD_FREE(this, quicNetVCAllocator, t);
  }
}

void
QUICNetVConnection::set_local_addr()
{
}

void
QUICNetVConnection::free(EThread *t)
{
  QUICConDebug("Free connection");

  this->_udp_con = nullptr;

  quiche_conn_free(this->_quiche_con);

  delete this->_application_map;
  this->_application_map = nullptr;
  delete this->_stream_manager;
  this->_stream_manager = nullptr;

  super::clear();
  this->_context->trigger(QUICContext::CallbackEvent::CONNECTION_CLOSE);
  ALPNSupport::clear();
  TLSBasicSupport::clear();

  this->_packet_handler->close_connection(this);
  this->_packet_handler = nullptr;
}

void
QUICNetVConnection::reenable(VIO *vio)
{
}

int
QUICNetVConnection::state_handshake(int event, Event *data)
{
  if (quiche_conn_is_established(this->_quiche_con)) {
    this->_switch_to_established_state();
    return this->handleEvent(event, data);
  }

  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    this->_handle_read_ready();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    this->_handle_write_ready();
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);
    break;
  case EVENT_INTERVAL:
    this->_handle_interval();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    _unschedule_packet_write_ready();
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandleed event: %d", event);
    break;
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_established(int event, Event *data)
{
  switch (event) {
  case QUIC_EVENT_PACKET_READ_READY:
    this->_handle_read_ready();
    break;
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_close_packet_write_ready(data);
    this->_handle_write_ready();
    // Reschedule WRITE_READY
    this->_schedule_packet_write_ready(true);
    break;
  case EVENT_INTERVAL:
    this->_handle_interval();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    _unschedule_packet_write_ready();
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandleed event: %d", event);
    break;
  }
  return EVENT_DONE;
}

void
QUICNetVConnection::_switch_to_established_state()
{
  QUICConDebug("Enter state_connection_established");
  this->_record_tls_handshake_end_time();
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_established);
  this->_start_application();
  this->_handshake_completed = true;
}

void
QUICNetVConnection::_start_application()
{
  if (!this->_application_started) {
    this->_application_started = true;

    const uint8_t *app_name;
    size_t app_name_len = 0;
    quiche_conn_application_proto(this->_quiche_con, &app_name, &app_name_len);
    if (app_name == nullptr) {
      app_name     = reinterpret_cast<const uint8_t *>(IP_PROTO_TAG_HTTP_QUIC.data());
      app_name_len = IP_PROTO_TAG_HTTP_QUIC.size();
    }

    this->set_negotiated_protocol_id({reinterpret_cast<const char *>(app_name), static_cast<size_t>(app_name_len)});

    if (netvc_context == NET_VCONNECTION_IN) {
      if (!this->setSelectedProtocol(app_name, app_name_len)) {
        // this->_handle_error(std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION));
      } else {
        this->endpoint()->handleEvent(NET_EVENT_ACCEPT, this);
      }
    } else {
      this->action_.continuation->handleEvent(NET_EVENT_OPEN, this);
    }
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

  this->_context         = std::make_unique<QUICContext>(this);
  this->_application_map = new QUICApplicationMap();
  this->_stream_manager  = new QUICStreamManagerImpl(this->_context.get(), this->_application_map);

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
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);

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

  this->thread->schedule_in(this, HRTIME_MSECONDS(quiche_conn_timeout_as_millis(this->_quiche_con)));

  return EVENT_DONE;
}

int
QUICNetVConnection::connectUp(EThread *t, int fd)
{
  return 0;
}

QUICStreamManager *
QUICNetVConnection::stream_manager()
{
  return this->_stream_manager;
}

void
QUICNetVConnection::close_quic_connection(QUICConnectionErrorUPtr error)
{
}

void
QUICNetVConnection::reset_quic_connection()
{
}

void
QUICNetVConnection::handle_received_packet(UDPPacket *packet)
{
  IOBufferBlock *block = packet->getIOBlockChain();
  uint8_t *buf         = reinterpret_cast<uint8_t *>(block->buf());
  uint64_t buf_len     = block->size();

  net_activity(this, this_ethread());
  quiche_recv_info recv_info = {
    &packet->from.sa,
    static_cast<socklen_t>(packet->from.isIp4() ? sizeof(packet->from.sin) : sizeof(packet->from.sin6)),
#ifdef HAVE_QUICHE_CONFIG_SET_ACTIVE_CONNECTION_ID_LIMIT
    &packet->to.sa,
    static_cast<socklen_t>(packet->to.isIp4() ? sizeof(packet->to.sin) : sizeof(packet->to.sin6)),
#endif
  };

  ssize_t done = quiche_conn_recv(this->_quiche_con, buf, buf_len, &recv_info);
  if (done < 0) {
    QUICConVDebug("failed to process packet: %zd", done);
    return;
  }
}

void
QUICNetVConnection::ping()
{
}

QUICConnectionId
QUICNetVConnection::peer_connection_id() const
{
  return {};
}

QUICConnectionId
QUICNetVConnection::original_connection_id() const
{
  return {};
}

QUICConnectionId
QUICNetVConnection::first_connection_id() const
{
  return {};
}

QUICConnectionId
QUICNetVConnection::retry_source_connection_id() const
{
  return {};
}

QUICConnectionId
QUICNetVConnection::initial_source_connection_id() const
{
  return {};
}

QUICConnectionId
QUICNetVConnection::connection_id() const
{
  return {};
}

std::string_view
QUICNetVConnection::cids() const
{
  return "";
}

const QUICFiveTuple
QUICNetVConnection::five_tuple() const
{
  return {};
}

uint32_t
QUICNetVConnection::pmtu() const
{
  return 0;
}

NetVConnectionContext_t
QUICNetVConnection::direction() const
{
  return NET_VCONNECTION_IN;
}

QUICVersion
QUICNetVConnection::negotiated_version() const
{
  return 0;
}

std::string_view
QUICNetVConnection::negotiated_application_name() const
{
  const uint8_t *name;
  size_t name_len = 0;
  quiche_conn_application_proto(this->_quiche_con, &name, &name_len);

  return std::string_view(reinterpret_cast<const char *>(name), name_len);
}

bool
QUICNetVConnection::is_closed() const
{
  return quiche_conn_is_closed(this->_quiche_con);
}

bool
QUICNetVConnection::is_at_anti_amplification_limit() const
{
  return false;
}

bool
QUICNetVConnection::is_address_validation_completed() const
{
  return false;
}

bool
QUICNetVConnection::is_handshake_completed() const
{
  return false;
}

bool
QUICNetVConnection::has_keys_for(QUICPacketNumberSpace space) const
{
  return false;
}

std::vector<QUICFrameType>
QUICNetVConnection::interests()
{
  return {};
}

QUICConnectionErrorUPtr
QUICNetVConnection::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  return nullptr;
}

void
QUICNetVConnection::net_read_io(NetHandler *nh, EThread *lthread)
{
  if (quiche_conn_is_readable(this->_quiche_con)) {
    SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
    this->handleEvent(QUIC_EVENT_PACKET_READ_READY, nullptr);
  }
}

int64_t
QUICNetVConnection::load_buffer_and_write(int64_t towrite, MIOBufferAccessor &buf, int64_t &total_written, int &needs)
{
  return 0;
}

void
QUICNetVConnection::_schedule_packet_write_ready(bool delay)
{
  if (!this->_packet_write_ready) {
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
QUICNetVConnection::_handle_read_ready()
{
  quiche_stream_iter *readable = quiche_conn_readable(this->_quiche_con);
  uint64_t s                   = 0;
  while (quiche_stream_iter_next(readable, &s)) {
    QUICStreamImpl *stream;
    QUICConVDebug("stream %" PRIu64 " is readable\n", s);
    stream = static_cast<QUICStreamImpl *>(quiche_conn_stream_application_data(this->_quiche_con, s));
    if (stream == nullptr) {
      this->_stream_manager->create_stream(s);
      stream = static_cast<QUICStreamImpl *>(this->_stream_manager->find_stream(s));
      quiche_conn_stream_init_application_data(this->_quiche_con, s, stream);
    }
    stream->receive_data(this->_quiche_con);
  }
  quiche_stream_iter_free(readable);
}

void
QUICNetVConnection::_handle_write_ready()
{
  if (quiche_conn_is_established(this->_quiche_con)) {
    quiche_stream_iter *writable = quiche_conn_writable(this->_quiche_con);
    uint64_t s                   = 0;
    while (quiche_stream_iter_next(writable, &s)) {
      QUICStreamImpl *stream;
      stream = static_cast<QUICStreamImpl *>(quiche_conn_stream_application_data(this->_quiche_con, s));
      stream->send_data(this->_quiche_con);
    }
    quiche_stream_iter_free(writable);
  }

  Ptr<IOBufferBlock> udp_payload;
  quiche_send_info send_info;
  ssize_t res;
  ssize_t written = 0;

  size_t quantum              = quiche_conn_send_quantum(this->_quiche_con);
  size_t max_udp_payload_size = quiche_conn_max_send_udp_payload_size(this->_quiche_con);

  // This buffer size must be less than 64KB because it can be used for UDP GSO (UDP_SEGMENT)
  udp_payload = new_IOBufferBlock();
  udp_payload->alloc(buffer_size_to_index(quantum, BUFFER_SIZE_INDEX_32K));
  quantum = std::min(static_cast<int64_t>(quantum), udp_payload->write_avail());
  while (written + max_udp_payload_size <= quantum) {
    res = quiche_conn_send(this->_quiche_con, reinterpret_cast<uint8_t *>(udp_payload->end()) + written, max_udp_payload_size,
                           &send_info);
    if (res > 0) {
      written += res;
    }
    if (static_cast<size_t>(res) != max_udp_payload_size) {
      break;
    }
  }
  if (written > 0) {
    udp_payload->fill(written);
    int segment_size = 0;
    if (static_cast<size_t>(written) > max_udp_payload_size) {
      segment_size = max_udp_payload_size;
    }
    this->_packet_handler->send_packet(this->_udp_con, this->con.addr, udp_payload, segment_size);
    net_activity(this, this_ethread());
  }
}

void
QUICNetVConnection::_handle_interval()
{
  quiche_conn_on_timeout(this->_quiche_con);

  if (quiche_conn_is_closed(this->_quiche_con)) {
    this->_ctable->erase(this->_quic_connection_id, this);
    this->_ctable->erase(this->_original_quic_connection_id, this);

    if (quiche_conn_is_timed_out(this->_quiche_con)) {
      this->thread->schedule_imm(this, VC_EVENT_INACTIVITY_TIMEOUT);
      return;
    }

    bool is_app;
    uint64_t error_code;
    const uint8_t *reason;
    size_t reason_len;
    bool has_error = quiche_conn_peer_error(this->_quiche_con, &is_app, &error_code, &reason, &reason_len) ||
                     quiche_conn_local_error(this->_quiche_con, &is_app, &error_code, &reason, &reason_len);
    if (has_error && error_code != static_cast<uint64_t>(QUICTransErrorCode::NO_ERROR)) {
      QUICConDebug("is_app=%d error_code=%" PRId64 " reason=%.*s", is_app, error_code, static_cast<int>(reason_len), reason);
      this->thread->schedule_imm(this, VC_EVENT_ERROR);
      return;
    }

    // If it's not timeout nor error, it's probably eos
    this->thread->schedule_imm(this, VC_EVENT_EOS);

  } else {
    // Just schedule timeout event again if the connection is still open
    this->thread->schedule_in(this, HRTIME_MSECONDS(quiche_conn_timeout_as_millis(this->_quiche_con)));
  }
}

int
QUICNetVConnection::populate_protocol(std::string_view *results, int n) const
{
  return 0;
}

const char *
QUICNetVConnection::protocol_contains(std::string_view tag) const
{
  return "";
}

SSL *
QUICNetVConnection::_get_ssl_object() const
{
  return nullptr;
}

ssl_curve_id
QUICNetVConnection::_get_tls_curve() const
{
  return 0;
}
