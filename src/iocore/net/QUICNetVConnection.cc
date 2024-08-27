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

#include "P_SSLUtils.h"
#include "P_QUICNetVConnection.h"
#include "P_QUICPacketHandler.h"
#include "api/APIHook.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/net/QUICMultiCertConfigLoader.h"
#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICGlobals.h"
#include "iocore/net/SSLAPIHooks.h"

#include <netinet/in.h>
#include <quiche.h>

namespace
{
constexpr ink_hrtime WRITE_READY_INTERVAL = HRTIME_MSECONDS(2);

DbgCtl dbg_ctl_quic_net{"quic_net"};
DbgCtl dbg_ctl_v_quic_net{"v_quic_net"};

} // end anonymous namespace

#define QUICConDebug(fmt, ...)  Dbg(dbg_ctl_quic_net, "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)
#define QUICConVDebug(fmt, ...) Dbg(dbg_ctl_v_quic_net, "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

ClassAllocator<QUICNetVConnection> quicNetVCAllocator("quicNetVCAllocator");

QUICNetVConnection::QUICNetVConnection()
{
  this->_set_service(static_cast<ALPNSupport *>(this));
  this->_set_service(static_cast<TLSBasicSupport *>(this));
  this->_set_service(static_cast<TLSEventSupport *>(this));
  this->_set_service(static_cast<TLSCertSwitchSupport *>(this));
  this->_set_service(static_cast<TLSSNISupport *>(this));
  this->_set_service(static_cast<TLSSessionResumptionSupport *>(this));
  this->_set_service(static_cast<QUICSupport *>(this));
}

QUICNetVConnection::~QUICNetVConnection() {}

void
QUICNetVConnection::init(QUICVersion /* version ATS_UNUSED */, QUICConnectionId /* peer_cid ATS_UNUSED */,
                         QUICConnectionId /* original_cid ATS_UNUSED */, UDPConnection *, QUICPacketHandler *)
{
}

void
QUICNetVConnection::init(QUICVersion /* version ATS_UNUSED */, QUICConnectionId /* peer_cid ATS_UNUSED */,
                         QUICConnectionId original_cid, QUICConnectionId /* first_cid ATS_UNUSED */,
                         QUICConnectionId /* retry_cid ATS_UNUSED */, UDPConnection *udp_con, quiche_conn *quiche_con,
                         QUICPacketHandler *packet_handler, QUICConnectionTable *ctable, SSL *ssl)
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

  this->_ssl = ssl;
  SSL_set_ex_data(ssl, QUIC::ssl_quic_qc_index, static_cast<QUICConnection *>(this));
  this->_bindSSLObject();
}

void
QUICNetVConnection::free()
{
  this->free_thread(this_ethread());
}

// called by ET_UDP
void
QUICNetVConnection::remove_connection_ids()
{
  if (this->_ctable) {
    this->_ctable->erase(this->_quic_connection_id, this);
    this->_ctable->erase(this->_original_quic_connection_id, this);
  }
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
QUICNetVConnection::free_thread(EThread * /* t ATS_UNUSED */)
{
  QUICConDebug("Free connection");

  this->_udp_con = nullptr;

  quiche_conn_free(this->_quiche_con);
  this->_quiche_con = nullptr;

  this->_unschedule_quiche_timeout();
  this->_unschedule_packet_write_ready();

  delete this->_application_map;
  this->_application_map = nullptr;
  delete this->_stream_manager;
  this->_stream_manager = nullptr;

  super::clear();
  ALPNSupport::clear();
  TLSBasicSupport::clear();
  TLSEventSupport::clear();
  TLSCertSwitchSupport::_clear();

  this->_packet_handler->close_connection(this);
  this->_packet_handler = nullptr;
}

void
QUICNetVConnection::reenable(VIO * /* vio ATS_UNUSED */)
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
    this->_close_quiche_timeout(data);
    this->_handle_interval();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    _unschedule_packet_write_ready();
    this->_propagate_event(event);
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandled event: %d", event);
    break;
  }

  if (this->closed != 1 && (quiche_conn_is_closed(this->_quiche_con) || quiche_conn_is_draining(this->_quiche_con))) {
    this->_schedule_closing_event();
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_established(int event, Event *data)
{
  if (!this->_quiche_con) {
    // Connection has been closed.
    return EVENT_DONE;
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
    this->_close_quiche_timeout(data);
    this->_handle_interval();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    _unschedule_packet_write_ready();
    this->_propagate_event(event);
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandled event: %d", event);
    break;
  }

  if (this->closed != 1 && (quiche_conn_is_closed(this->_quiche_con) || quiche_conn_is_draining(this->_quiche_con))) {
    this->_schedule_closing_event();
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
    size_t         app_name_len = 0;
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

void
QUICNetVConnection::_propagate_event(int event)
{
  QUICConVDebug("Propagating: %d", event);
  if (this->read.vio.cont && this->read.vio.mutex == this->read.vio.cont->mutex) {
    this->read.vio.cont->handleEvent(event, &this->read.vio);
  } else if (this->write.vio.cont && this->write.vio.mutex == this->write.vio.cont->mutex) {
    this->write.vio.cont->handleEvent(event, &this->write.vio);
  } else {
    // Proxy Session does not exist
    QUICConVDebug("Session does not exist");
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
  auto vio           = super::do_io_read(c, nbytes, buf);
  this->read.enabled = 1;
  return vio;
}

VIO *
QUICNetVConnection::do_io_write(Continuation *c, int64_t nbytes, IOBufferReader *buf, bool /* owner ATS_UNUSED */)
{
  auto vio            = super::do_io_write(c, nbytes, buf);
  this->write.enabled = 1;
  return vio;
}

int
QUICNetVConnection::acceptEvent(int event, Event *e)
{
  EThread    *t = (e == nullptr) ? this_ethread() : e->ethread;
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
  this->_stream_manager  = new QUICStreamManager(this->_context.get(), this->_application_map);

  // this->thread is already assigned by QUICPacketHandlerIn::_recv_packet
  ink_assert(this->thread == this_ethread());

  // Send this NetVC to NetHandler and start to polling read & write event.
  if (h->startIO(this) < 0) {
    this->free_thread(t);
    return EVENT_DONE;
  }

  // FIXME: complete do_io_xxxx instead
  this->read.enabled = 1;

  // Handshake callback handler.
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);

  // Send this netvc to InactivityCop.
  // Note: even though we will set the timeouts to 0, we need this so we make sure the one gets freed and the IO is properly ended.
  nh->startCop(this);

  // We will take care of this by using `idle_timeout` configured by `proxy.config.quic.no_activity_timeout_in`.
  this->set_default_inactivity_timeout(0);

  if (inactivity_timeout_in) {
    this->set_inactivity_timeout(inactivity_timeout_in);
  }

  if (active_timeout_in) {
    set_active_timeout(active_timeout_in);
  }

  action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  this->_schedule_packet_write_ready();

  this->_schedule_quiche_timeout();

  return EVENT_DONE;
}

int
QUICNetVConnection::connectUp(EThread * /* t ATS_UNUSED */, int /* fd ATS_UNUSED */)
{
  return 0;
}

QUICStreamManager *
QUICNetVConnection::stream_manager()
{
  return this->_stream_manager;
}

void
QUICNetVConnection::close_quic_connection(QUICConnectionErrorUPtr /* error ATS_UNUSED */)
{
}

void
QUICNetVConnection::reset_quic_connection()
{
}

void
QUICNetVConnection::handle_received_packet(UDPPacket *packet)
{
  size_t   buf_len{0};
  uint8_t *buf = packet->get_entire_chain_buffer(&buf_len);
  net_activity(this, this_ethread());
  quiche_recv_info recv_info = {
    &packet->from.sa,
    static_cast<socklen_t>(packet->from.isIp4() ? sizeof(packet->from.sin) : sizeof(packet->from.sin6)),
    &packet->to.sa,
    static_cast<socklen_t>(packet->to.isIp4() ? sizeof(packet->to.sin) : sizeof(packet->to.sin6)),
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
  size_t         name_len = 0;
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

void
QUICNetVConnection::net_read_io(NetHandler * /* nh ATS_UNUSED */, EThread * /* lthread ATS_UNUSED */)
{
  SCOPED_MUTEX_LOCK(lock, this->mutex, this_ethread());
  this->handleEvent(QUIC_EVENT_PACKET_READ_READY, nullptr);
}

int64_t
QUICNetVConnection::load_buffer_and_write(int64_t /* towrite ATS_UNUSED */, MIOBufferAccessor & /* buf ATS_UNUSED */,
                                          int64_t & /* total_written ATS_UNUSED */, int & /* needs ATS_UNUSED */)
{
  return 0;
}

bool
QUICNetVConnection::getSSLHandShakeComplete() const
{
  return quiche_conn_is_established(this->_quiche_con);
}

void
QUICNetVConnection::_bindSSLObject()
{
  TLSBasicSupport::bind(this->_ssl, this);
  TLSEventSupport::bind(this->_ssl, this);
  ALPNSupport::bind(this->_ssl, this);
  TLSSessionResumptionSupport::bind(this->_ssl, this);
  TLSSNISupport::bind(this->_ssl, this);
  TLSCertSwitchSupport::bind(this->_ssl, this);
  QUICSupport::bind(this->_ssl, this);
}

void
QUICNetVConnection::_unbindSSLObject()
{
  TLSBasicSupport::unbind(this->_ssl);
  TLSEventSupport::unbind(this->_ssl);
  ALPNSupport::unbind(this->_ssl);
  TLSSessionResumptionSupport::unbind(this->_ssl);
  TLSSNISupport::unbind(this->_ssl);
  TLSCertSwitchSupport::unbind(this->_ssl);
  QUICSupport::unbind(this->_ssl);
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
QUICNetVConnection::_schedule_quiche_timeout()
{
  if (!this->_quiche_timeout) {
    this->_quiche_timeout = this->thread->schedule_in(this, HRTIME_MSECONDS(quiche_conn_timeout_as_millis(this->_quiche_con)));
  }
}

void
QUICNetVConnection::_unschedule_quiche_timeout()
{
  if (this->_quiche_timeout) {
    this->_quiche_timeout->cancel();
    this->_quiche_timeout = nullptr;
  }
}

void
QUICNetVConnection::_close_quiche_timeout(Event *data)
{
  ink_assert(this->_quiche_timeout == data);
  this->_quiche_timeout = nullptr;
}

void
QUICNetVConnection::_schedule_closing_event()
{
  QUICConDebug("Scheduling closing event");
  if (quiche_conn_is_timed_out(this->_quiche_con)) {
    QUICConDebug("QUIC Idle timeout detected");
    this->thread->schedule_imm(this, VC_EVENT_INACTIVITY_TIMEOUT);
    return;
  }

  bool           is_app;
  uint64_t       error_code;
  const uint8_t *reason;
  size_t         reason_len;
  bool           has_error = quiche_conn_peer_error(this->_quiche_con, &is_app, &error_code, &reason, &reason_len) ||
                   quiche_conn_local_error(this->_quiche_con, &is_app, &error_code, &reason, &reason_len);
  if (has_error && error_code != static_cast<uint64_t>(QUICTransErrorCode::NO_ERROR)) {
    QUICConDebug("is_app=%d error_code=%" PRId64 " reason=%.*s", is_app, error_code, static_cast<int>(reason_len), reason);
    this->thread->schedule_imm(this, VC_EVENT_ERROR);
    return;
  }

  // If it's not timeout nor error, it's probably eos
  this->thread->schedule_imm(this, VC_EVENT_EOS);
}

void
QUICNetVConnection::_handle_read_ready()
{
  quiche_stream_iter *readable = quiche_conn_readable(this->_quiche_con);
  uint64_t            s        = 0;
  while (quiche_stream_iter_next(readable, &s)) {
    QUICConVDebug("stream %" PRIu64 " is readable\n", s);
    QUICStream *stream = static_cast<QUICStream *>(this->_stream_manager->find_stream(s));
    if (stream == nullptr) {
      [[maybe_unused]] QUICConnectionError err;
      stream = this->_stream_manager->create_stream(s, err);
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
    uint64_t            s        = 0;
    while (quiche_stream_iter_next(writable, &s)) {
      QUICStream *stream = static_cast<QUICStream *>(this->_stream_manager->find_stream(s));
      if (stream == nullptr) {
        [[maybe_unused]] QUICConnectionError err;
        stream = this->_stream_manager->create_stream(s, err);
      }
      stream->send_data(this->_quiche_con);
    }
    quiche_stream_iter_free(writable);
  }

  Ptr<IOBufferBlock> udp_payload;
  quiche_send_info   send_info;
  struct timespec    send_at_hint;
  ssize_t            res;
  ssize_t            written = 0;

  size_t quantum              = quiche_conn_send_quantum(this->_quiche_con);
  size_t max_udp_payload_size = quiche_conn_max_send_udp_payload_size(this->_quiche_con);

  // This buffer size must be less than 64KB because it can be used for UDP GSO (UDP_SEGMENT)
  udp_payload = new_IOBufferBlock();
  udp_payload->alloc(buffer_size_to_index(quantum, BUFFER_SIZE_INDEX_32K));
  quantum = std::min(static_cast<int64_t>(quantum), udp_payload->write_avail());
  while (written + max_udp_payload_size <= quantum) {
    res = quiche_conn_send(this->_quiche_con, reinterpret_cast<uint8_t *>(udp_payload->end()) + written, max_udp_payload_size,
                           &send_info);

#ifdef HAVE_SO_TXTIME
    if (written == 0) {
      memcpy(&send_at_hint, &send_info.at, sizeof(struct timespec));
    }
#endif

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
    this->_packet_handler->send_packet(this->_udp_con, this->con.addr, udp_payload, segment_size, &send_at_hint);
    net_activity(this, this_ethread());
  }
}

void
QUICNetVConnection::_handle_interval()
{
  quiche_conn_on_timeout(this->_quiche_con);

  if (quiche_conn_is_closed(this->_quiche_con)) {
    this->_schedule_closing_event();
  } else {
    // Just schedule timeout event again if the connection is still open
    this->_schedule_quiche_timeout();
  }
}

int
QUICNetVConnection::populate_protocol(std::string_view *results, int n) const
{
  int retval = 0;
  if (n > retval) {
    results[retval++] = IP_PROTO_TAG_QUIC;
    if (n > retval) {
      results[retval++] = IP_PROTO_TAG_TLS_1_3;
      if (n > retval) {
        retval += super::populate_protocol(results + retval, n - retval);
      }
    }
  }
  return retval;
}

const char *
QUICNetVConnection::protocol_contains(std::string_view prefix) const
{
  const char *retval = nullptr;
  if (prefix.size() <= IP_PROTO_TAG_QUIC.size() && strncmp(IP_PROTO_TAG_QUIC.data(), prefix.data(), prefix.size()) == 0) {
    retval = IP_PROTO_TAG_QUIC.data();
  } else if (prefix.size() <= IP_PROTO_TAG_TLS_1_3.size() &&
             strncmp(IP_PROTO_TAG_TLS_1_3.data(), prefix.data(), prefix.size()) == 0) {
    retval = IP_PROTO_TAG_TLS_1_3.data();
  } else {
    retval = super::protocol_contains(prefix);
  }
  return retval;
}

const char *
QUICNetVConnection::get_server_name() const
{
  return get_sni_server_name();
}

bool
QUICNetVConnection::support_sni() const
{
  return true;
}

QUICConnection *
QUICNetVConnection::get_quic_connection()
{
  return static_cast<QUICConnection *>(this);
}

void
QUICNetVConnection::reenable(int /* event ATS_UNUSED */)
{
}

Continuation *
QUICNetVConnection::getContinuationForTLSEvents()
{
  return this;
}

EThread *
QUICNetVConnection::getThreadForTLSEvents()
{
  return this->thread;
}

Ptr<ProxyMutex>
QUICNetVConnection::getMutexForTLSEvents()
{
  return this->nh->mutex;
}

SSL *
QUICNetVConnection::_get_ssl_object() const
{
  return this->_ssl;
}

ssl_curve_id
QUICNetVConnection::_get_tls_curve() const
{
  if (getSSLSessionCacheHit()) {
    return getSSLCurveNID();
  } else {
    return SSLGetCurveNID(this->_ssl);
  }
}

in_port_t
QUICNetVConnection::_get_local_port()
{
  return this->get_local_port();
}

const IpEndpoint &
QUICNetVConnection::_getLocalEndpoint()
{
  return this->local_addr;
}

bool
QUICNetVConnection::_isTryingRenegotiation() const
{
  // Renegotiation is not allowed on QUIC (TLS 1.3) connections.
  // If handshake is completed when this function is called, that should be unallowed attempt of renegotiation.
  return this->getSSLHandShakeComplete();
}

shared_SSL_CTX
QUICNetVConnection::_lookupContextByName(const std::string &servername, SSLCertContextType ctxType)
{
  shared_SSL_CTX                ctx = nullptr;
  QUICCertConfig::scoped_config lookup;
  SSLCertContext               *cc = lookup->find(servername, ctxType);

  if (cc && cc->getCtx()) {
    ctx = cc->getCtx();
  }

  return ctx;
}

shared_SSL_CTX
QUICNetVConnection::_lookupContextByIP()
{
  shared_SSL_CTX                ctx = nullptr;
  QUICCertConfig::scoped_config lookup;
  QUICFiveTuple                 five_tuple = this->five_tuple();
  IpEndpoint                    ip         = five_tuple.destination();
  SSLCertContext               *cc         = lookup->find(ip);

  if (cc && cc->getCtx()) {
    ctx = cc->getCtx();
  }

  return ctx;
}
