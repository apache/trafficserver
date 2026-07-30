/** @file

  OpenSSL native QUIC NetVConnection support.

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
#include "P_UnixNet.h"
#include "api/APIHook.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/net/QUICMultiCertConfigLoader.h"
#include "iocore/net/SSLAPIHooks.h"
#include "iocore/net/quic/QUICApplicationMap.h"
#include "iocore/net/quic/QUICEvents.h"
#include "iocore/net/quic/QUICGlobals.h"
#include "iocore/net/quic/QUICStream.h"
#include "iocore/net/quic/QUICStreamManager.h"
#include "tscore/ink_config.h"

#include <openssl/err.h>
#include <openssl/quic.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <vector>

namespace
{
constexpr ink_hrtime OPENSSL_QUIC_EVENT_INTERVAL = HRTIME_MSECONDS(2);

DbgCtl dbg_ctl_quic_net{"quic_net"};
DbgCtl dbg_ctl_v_quic_net{"v_quic_net"};

class OpenSSLQUICStreamManager : public QUICStreamManager
{
public:
  OpenSSLQUICStreamManager(QUICContext *context, QUICApplicationMap *app_map, QUICNetVConnection *vc)
    : QUICStreamManager(context, app_map), _vc(vc)
  {
  }

  QUICConnectionErrorUPtr
  create_uni_stream(QUICStreamId &new_stream_id) override
  {
    return this->_vc->create_openssl_stream(SSL_STREAM_FLAG_UNI | SSL_STREAM_FLAG_NO_BLOCK, new_stream_id);
  }

  QUICConnectionErrorUPtr
  create_bidi_stream(QUICStreamId &new_stream_id) override
  {
    return this->_vc->create_openssl_stream(SSL_STREAM_FLAG_NO_BLOCK, new_stream_id);
  }

private:
  QUICNetVConnection *_vc = nullptr;
};

} // end anonymous namespace

#define QUICConDebug(fmt, ...)  Dbg(dbg_ctl_quic_net, "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)
#define QUICConVDebug(fmt, ...) Dbg(dbg_ctl_v_quic_net, "[%s] " fmt, this->cids().data(), ##__VA_ARGS__)

ClassAllocator<QUICNetVConnection, false> quicNetVCAllocator("quicNetVCAllocator");

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
QUICNetVConnection::init(SSL *ssl, QUICPacketHandler *packet_handler)
{
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::acceptEvent);

  this->_ssl            = ssl;
  this->_packet_handler = packet_handler;
  this->_quic_connection_id.randomize();
  this->_initial_source_connection_id = this->_quic_connection_id;
  this->_cid_text                     = this->_quic_connection_id.hex();

  SSL_set_ex_data(ssl, QUIC::ssl_quic_qc_index, static_cast<QUICConnection *>(this));
  SSL_set_blocking_mode(ssl, 0);
  SSL_set_event_handling_mode(ssl, SSL_VALUE_EVENT_HANDLING_MODE_IMPLICIT);
  SSL_set_incoming_stream_policy(ssl, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0);
  SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_NONE);

  QUICConfig::scoped_config params;
  SSL_set_feature_request_uint(ssl, SSL_VALUE_QUIC_IDLE_TIMEOUT, params->no_activity_timeout_in());
  SSL_set_feature_request_uint(ssl, SSL_VALUE_QUIC_STREAM_BIDI_LOCAL_AVAIL, params->initial_max_streams_bidi_in());
  SSL_set_feature_request_uint(ssl, SSL_VALUE_QUIC_STREAM_UNI_LOCAL_AVAIL, params->initial_max_streams_uni_in());

  this->_bindSSLObject();
  if (auto const servername = quic_sni_server_name(ssl); !servername.empty()) {
    this->set_sni_server_name(servername);
  }
}

void
QUICNetVConnection::set_quic_endpoints(IpEndpoint const &local, IpEndpoint const &remote)
{
  this->local_addr      = local;
  this->remote_addr     = remote;
  this->got_local_addr  = true;
  this->got_remote_addr = true;
}

QUICConnectionErrorUPtr
QUICNetVConnection::create_openssl_stream(uint64_t flags, QUICStreamId &new_stream_id)
{
  if (this->_ssl == nullptr) {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::INTERNAL_ERROR, "QUIC connection is not initialized");
  }

  SSL *stream_ssl = SSL_new_stream(this->_ssl, flags);
  if (stream_ssl == nullptr) {
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::INTERNAL_ERROR, "Failed to create QUIC stream");
  }

  SSL_set_blocking_mode(stream_ssl, 0);
  SSL_set_event_handling_mode(stream_ssl, SSL_VALUE_EVENT_HANDLING_MODE_IMPLICIT);
  SSL_set_mode(stream_ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);

  new_stream_id = SSL_get_stream_id(stream_ssl);
  this->_openssl_streams.emplace(new_stream_id, stream_ssl);

  [[maybe_unused]] QUICConnectionError err;
  this->_stream_manager->create_stream(new_stream_id, err);

  return nullptr;
}

void
QUICNetVConnection::free()
{
  this->free_thread(this_ethread());
}

void
QUICNetVConnection::remove_connection_ids()
{
}

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

  this->_unschedule_openssl_event();

  for (auto &[stream_id, stream_ssl] : this->_openssl_streams) {
    SSL_free(stream_ssl);
  }
  this->_openssl_streams.clear();

  if (this->_ssl != nullptr) {
    this->_unbindSSLObject();
    SSL_free(this->_ssl);
    this->_ssl = nullptr;
  }

  this->_application_map.reset();
  this->_stream_manager.reset();

  super::clear();
  ALPNSupport::clear();
  TLSBasicSupport::clear();
  TLSEventSupport::clear();
  TLSCertSwitchSupport::_clear();

  if (this->_packet_handler != nullptr) {
    this->_packet_handler->close_connection(this);
    this->_packet_handler = nullptr;
  }
}

void
QUICNetVConnection::reenable(VIO * /* vio ATS_UNUSED */)
{
}

int
QUICNetVConnection::state_handshake(int event, Event *data)
{
  if (data == this->_packet_write_ready) {
    this->_packet_write_ready = nullptr;
  }

  switch (event) {
  case EVENT_IMMEDIATE:
  case EVENT_INTERVAL:
  case QUIC_EVENT_PACKET_READ_READY:
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_handle_openssl_events();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    this->_unschedule_openssl_event();
    this->_propagate_event(event);
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandled event: %d", event);
    break;
  }

  if (this->closed != 1 && SSL_is_init_finished(this->_ssl)) {
    this->_switch_to_established_state();
    this->_handle_openssl_events();
  }

  if (this->closed != 1 && this->_openssl_connection_closed()) {
    this->_schedule_closing_event();
  } else if (this->closed != 1) {
    this->_schedule_openssl_event();
  }

  return EVENT_DONE;
}

int
QUICNetVConnection::state_established(int event, Event *data)
{
  if (this->_ssl == nullptr) {
    return EVENT_DONE;
  }

  if (data == this->_packet_write_ready) {
    this->_packet_write_ready = nullptr;
  }

  switch (event) {
  case EVENT_IMMEDIATE:
  case EVENT_INTERVAL:
  case QUIC_EVENT_PACKET_READ_READY:
  case QUIC_EVENT_PACKET_WRITE_READY:
    this->_handle_openssl_events();
    break;
  case VC_EVENT_EOS:
  case VC_EVENT_ERROR:
  case VC_EVENT_ACTIVE_TIMEOUT:
  case VC_EVENT_INACTIVITY_TIMEOUT:
    this->_unschedule_openssl_event();
    this->_propagate_event(event);
    this->closed = 1;
    break;
  default:
    QUICConDebug("Unhandled event: %d", event);
    break;
  }

  if (this->closed != 1 && this->_openssl_connection_closed()) {
    this->_schedule_closing_event();
  } else if (this->closed != 1) {
    this->_schedule_openssl_event();
  }

  return EVENT_DONE;
}

void
QUICNetVConnection::_switch_to_established_state()
{
  QUICConDebug("Enter state_connection_established");
  this->_record_tls_handshake_end_time();
  this->_update_end_of_handshake_stats();
  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_established);
  this->_handshake_completed = true;
  this->_start_application();
}

void
QUICNetVConnection::_start_application()
{
  if (this->_application_started) {
    return;
  }

  this->_application_started = true;

  unsigned char const *app_name     = nullptr;
  unsigned int         app_name_len = 0;
  SSL_get0_alpn_selected(this->_ssl, &app_name, &app_name_len);

  if (app_name == nullptr || app_name_len == 0) {
    app_name     = reinterpret_cast<unsigned char const *>(IP_PROTO_TAG_HTTP_QUIC.data());
    app_name_len = IP_PROTO_TAG_HTTP_QUIC.size();
  }

  this->_negotiated_alpn.assign(reinterpret_cast<char const *>(app_name), app_name_len);
  this->set_negotiated_protocol_id(this->_negotiated_alpn);

  if (netvc_context == NET_VCONNECTION_IN) {
    if (this->setSelectedProtocol(app_name, app_name_len)) {
      this->endpoint()->handleEvent(NET_EVENT_ACCEPT, this);
    }
  } else {
    this->action_.continuation->handleEvent(NET_EVENT_OPEN, this);
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
  this->_schedule_openssl_event(false);
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
  this->_application_map = std::make_unique<QUICApplicationMap>();
  this->_stream_manager  = std::make_unique<OpenSSLQUICStreamManager>(this->_context.get(), this->_application_map.get(), this);

  ink_assert(this->thread == this_ethread());

  if (h->startIO(this) < 0) {
    this->free_thread(t);
    return EVENT_DONE;
  }

  this->read.enabled  = 1;
  this->write.enabled = 1;

  SET_HANDLER((NetVConnHandler)&QUICNetVConnection::state_handshake);

  nh->startCop(this);
  this->set_default_inactivity_timeout(0);

  if (inactivity_timeout_in) {
    this->set_inactivity_timeout(inactivity_timeout_in);
  }

  if (active_timeout_in) {
    set_active_timeout(active_timeout_in);
  }

  action_.continuation->handleEvent(NET_EVENT_ACCEPT, this);
  this->_schedule_openssl_event(false);

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
  return this->_stream_manager.get();
}

void
QUICNetVConnection::close_quic_connection(QUICConnectionErrorUPtr error)
{
  if (this->_ssl != nullptr) {
    SSL_SHUTDOWN_EX_ARGS args = {};
    if (error != nullptr) {
      args.quic_error_code = error->code;
      args.quic_reason     = error->msg;
    }
    SSL_shutdown_ex(this->_ssl, SSL_SHUTDOWN_FLAG_NO_BLOCK, &args, sizeof(args));
  }
}

void
QUICNetVConnection::reset_quic_connection()
{
  if (this->_ssl != nullptr) {
    SSL_shutdown_ex(this->_ssl, SSL_SHUTDOWN_FLAG_RAPID | SSL_SHUTDOWN_FLAG_NO_BLOCK, nullptr, 0);
  }
}

void
QUICNetVConnection::handle_received_packet(UDPPacket * /* packet ATS_UNUSED */)
{
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
  return this->_initial_source_connection_id;
}

QUICConnectionId
QUICNetVConnection::connection_id() const
{
  return this->_quic_connection_id;
}

std::string_view
QUICNetVConnection::cids() const
{
  return this->_cid_text;
}

QUICFiveTuple const
QUICNetVConnection::five_tuple() const
{
  return QUICFiveTuple(this->remote_addr, this->local_addr, IPPROTO_UDP);
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
  return QUIC_SUPPORTED_VERSIONS[0];
}

std::string_view
QUICNetVConnection::negotiated_application_name() const
{
  return this->_negotiated_alpn;
}

void
QUICNetVConnection::on_stream_updated()
{
  this->_schedule_openssl_event(false);
}

bool
QUICNetVConnection::is_closed() const
{
  return this->_openssl_connection_closed();
}

bool
QUICNetVConnection::is_at_anti_amplification_limit() const
{
  return false;
}

bool
QUICNetVConnection::is_address_validation_completed() const
{
  return true;
}

bool
QUICNetVConnection::is_handshake_completed() const
{
  return this->_handshake_completed;
}

void
QUICNetVConnection::net_read_io(NetHandler * /* nh ATS_UNUSED */)
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
  this->_schedule_openssl_event(delay);
}

void
QUICNetVConnection::_unschedule_packet_write_ready()
{
  this->_unschedule_openssl_event();
}

void
QUICNetVConnection::_close_packet_write_ready(Event *data)
{
  if (this->_packet_write_ready == data) {
    this->_packet_write_ready = nullptr;
  }
}

void
QUICNetVConnection::_schedule_quiche_timeout()
{
}

void
QUICNetVConnection::_unschedule_quiche_timeout()
{
}

void
QUICNetVConnection::_close_quiche_timeout(Event * /* data ATS_UNUSED */)
{
}

void
QUICNetVConnection::_schedule_closing_event()
{
  QUICConDebug("Scheduling closing event");
  SSL_CONN_CLOSE_INFO info = {};
  if (this->_ssl != nullptr && SSL_get_conn_close_info(this->_ssl, &info, sizeof(info)) == 1) {
    QUICConDebug("QUIC close info: error_code=%" PRIu64 " frame_type=%" PRIu64 " flags=0x%x reason=\"%.*s\"", info.error_code,
                 info.frame_type, info.flags, static_cast<int>(info.reason_len), info.reason == nullptr ? "" : info.reason);
    if (info.error_code == OSSL_QUIC_LOCAL_ERR_IDLE_TIMEOUT) {
      QUICConDebug("QUIC Idle timeout detected");
      this->thread->schedule_imm(this, VC_EVENT_INACTIVITY_TIMEOUT);
      return;
    }
  }

  this->thread->schedule_imm(this, VC_EVENT_EOS);
}

void
QUICNetVConnection::_handle_read_ready()
{
  this->_handle_openssl_events();
}

void
QUICNetVConnection::_handle_write_ready()
{
  this->_handle_openssl_events();
}

void
QUICNetVConnection::_handle_interval()
{
  this->_handle_openssl_events();
}

void
QUICNetVConnection::_handle_openssl_events()
{
  if (this->_ssl == nullptr) {
    return;
  }

  if (SSL_handle_events(this->_ssl) != 1) {
    QUICConDebug("SSL_handle_events failed: %s", ERR_error_string(ERR_peek_error(), nullptr));
  }

  if (this->_stream_manager != nullptr && this->_application_started) {
    this->_accept_openssl_streams();
    this->_process_openssl_streams();
  }

  this->netActivity();
}

void
QUICNetVConnection::_accept_openssl_streams()
{
  while (SSL *stream_ssl = SSL_accept_stream(this->_ssl, SSL_ACCEPT_STREAM_NO_BLOCK)) {
    SSL_set_blocking_mode(stream_ssl, 0);
    SSL_set_event_handling_mode(stream_ssl, SSL_VALUE_EVENT_HANDLING_MODE_IMPLICIT);
    SSL_set_mode(stream_ssl, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER | SSL_MODE_ENABLE_PARTIAL_WRITE);

    QUICStreamId stream_id = SSL_get_stream_id(stream_ssl);
    this->_openssl_streams.emplace(stream_id, stream_ssl);

    if (this->_stream_manager->find_stream(stream_id) == nullptr) {
      [[maybe_unused]] QUICConnectionError err;
      this->_stream_manager->create_stream(stream_id, err);
    }
  }
}

void
QUICNetVConnection::_process_openssl_streams()
{
  std::vector<QUICStreamId> stream_ids;
  stream_ids.reserve(this->_openssl_streams.size());
  for (auto const &entry : this->_openssl_streams) {
    stream_ids.push_back(entry.first);
  }

  for (QUICStreamId stream_id : stream_ids) {
    auto stream_it = this->_openssl_streams.find(stream_id);
    if (stream_it == this->_openssl_streams.end()) {
      continue;
    }

    SSL        *stream_ssl = stream_it->second;
    QUICStream *stream     = static_cast<QUICStream *>(this->_stream_manager->find_stream(stream_id));
    if (stream == nullptr) {
      continue;
    }

    int stream_type = SSL_get_stream_type(stream_ssl);
    if ((stream_type & SSL_STREAM_TYPE_READ) != 0) {
      stream->receive_data(*this);
    }
    if ((stream_type & SSL_STREAM_TYPE_WRITE) != 0 || stream->has_data_to_send()) {
      if (stream->has_data_to_send()) {
        while (stream->has_data_to_send() && stream->send_data(*this) > 0) {}
      } else {
        stream->send_data(*this);
      }
    }
  }
}

void
QUICNetVConnection::_schedule_openssl_event(bool delay)
{
  if (!delay && this->_packet_write_ready != nullptr) {
    this->_packet_write_ready->cancel();
    this->_packet_write_ready = nullptr;
  }

  if (this->_packet_write_ready == nullptr && this->thread != nullptr) {
    if (delay) {
      this->_packet_write_ready = this->thread->schedule_in(this, OPENSSL_QUIC_EVENT_INTERVAL);
    } else {
      this->_packet_write_ready = this->thread->schedule_imm(this, QUIC_EVENT_PACKET_WRITE_READY);
    }
  }
}

void
QUICNetVConnection::_unschedule_openssl_event()
{
  if (this->_packet_write_ready != nullptr) {
    this->_packet_write_ready->cancel();
    this->_packet_write_ready = nullptr;
  }
}

bool
QUICNetVConnection::_openssl_connection_closed() const
{
  if (this->_ssl == nullptr) {
    return true;
  }

  SSL_CONN_CLOSE_INFO info = {};
  return SSL_get_conn_close_info(this->_ssl, &info, sizeof(info)) == 1;
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

char const *
QUICNetVConnection::protocol_contains(std::string_view prefix) const
{
  char const *retval = nullptr;
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

QUICConnection *
QUICNetVConnection::get_quic_connection()
{
  return static_cast<QUICConnection *>(this);
}

int64_t
QUICNetVConnection::read_stream(QUICStreamId stream_id, uint8_t *buf, size_t len, bool &fin, QUICStreamIO::ErrorCode &error_code)
{
  auto it = this->_openssl_streams.find(stream_id);
  if (it == this->_openssl_streams.end()) {
    error_code = ENOENT;
    return -1;
  }

  SSL_handle_events(it->second);

  size_t read_len = 0;
  fin             = false;
  if (SSL_read_ex(it->second, buf, len, &read_len) == 1) {
    fin = this->stream_read_finished(stream_id);
    return read_len;
  }

  int ssl_error = SSL_get_error(it->second, 0);
  if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
    return 0;
  }
  if (ssl_error == SSL_ERROR_ZERO_RETURN) {
    fin = true;
    return 0;
  }

  error_code = ssl_error;
  return -1;
}

bool
QUICNetVConnection::stream_read_finished(QUICStreamId stream_id)
{
  auto it = this->_openssl_streams.find(stream_id);
  if (it == this->_openssl_streams.end()) {
    return true;
  }

  int state = SSL_get_stream_read_state(it->second);
  return state == SSL_STREAM_STATE_FINISHED || state == SSL_STREAM_STATE_RESET_REMOTE || state == SSL_STREAM_STATE_CONN_CLOSED;
}

int64_t
QUICNetVConnection::stream_write_capacity(QUICStreamId stream_id)
{
  auto it = this->_openssl_streams.find(stream_id);
  if (it == this->_openssl_streams.end()) {
    return -1;
  }

  uint64_t avail = 0;
  if (SSL_get_stream_write_buf_avail(it->second, &avail) == 1) {
    return std::min<uint64_t>(avail, 16 * 1024);
  }

  return 0;
}

int64_t
QUICNetVConnection::write_stream(QUICStreamId stream_id, uint8_t const *buf, size_t len, bool fin,
                                 QUICStreamIO::ErrorCode &error_code)
{
  auto it = this->_openssl_streams.find(stream_id);
  if (it == this->_openssl_streams.end()) {
    error_code = ENOENT;
    return -1;
  }

  SSL_handle_events(it->second);

  if (len == 0 && fin) {
    SSL_stream_conclude(it->second, 0);
    return 0;
  }

  size_t         written_len = 0;
  const uint64_t flags       = fin ? SSL_WRITE_FLAG_CONCLUDE : 0;
  if (SSL_write_ex2(it->second, buf, len, flags, &written_len) == 1) {
    return written_len;
  }

  int ssl_error = SSL_get_error(it->second, 0);
  if (ssl_error == SSL_ERROR_WANT_READ || ssl_error == SSL_ERROR_WANT_WRITE) {
    return 0;
  }

  error_code = ssl_error;
  QUICConDebug("Failed to write %zu bytes on QUIC stream %" PRIu64 ": ssl_error=%d, openssl_error=%s", len, stream_id, ssl_error,
               ERR_error_string(ERR_peek_error(), nullptr));
  return -1;
}

void
QUICNetVConnection::reenable(int event)
{
  this->_is_verifying_cert = false;

  if (event == TS_EVENT_ERROR) {
    this->_is_cert_verified = false;
  }
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

bool
QUICNetVConnection::_isReadyToTransferData() const
{
  return this->_handshake_completed;
}

SSL *
QUICNetVConnection::_get_ssl_object() const
{
  return this->_ssl;
}

ssl_curve_id
QUICNetVConnection::_get_tls_curve() const
{
  if (getIsResumedFromSessionCache()) {
    return getSSLCurveNID();
  } else {
    return SSLGetCurveNID(this->_ssl);
  }
}

std::string_view
QUICNetVConnection::_get_tls_group() const
{
  if (getIsResumedFromSessionCache()) {
    return getSSLGroupName();
  } else {
    return SSLGetGroupName(this->_ssl);
  }
}

int
QUICNetVConnection::_verify_certificate(X509_STORE_CTX * /* ctx ATS_UNUSED */)
{
  TSEvent      eventId;
  TSHttpHookID hookId;
  APIHook     *hook = nullptr;

  if (get_context() == NET_VCONNECTION_IN) {
    eventId = TS_EVENT_SSL_VERIFY_CLIENT;
    hookId  = TS_SSL_VERIFY_CLIENT_HOOK;
  } else {
    eventId = TS_EVENT_SSL_VERIFY_SERVER;
    hookId  = TS_SSL_VERIFY_SERVER_HOOK;
  }
  hook = SSLAPIHooks::instance()->get(TSSslHookInternalID(hookId));
  if (hook != nullptr) {
    this->_is_verifying_cert = true;
    WEAK_SCOPED_MUTEX_LOCK(lock, hook->m_cont->mutex, this_ethread());
    hook->invoke(eventId, this);
  }

  ink_assert(this->_is_verifying_cert == false);
  if (this->_is_cert_verified) {
    return 1;
  }

  return 0;
}

in_port_t
QUICNetVConnection::_get_local_port()
{
  return this->get_local_port();
}

IpEndpoint const &
QUICNetVConnection::_getLocalEndpoint()
{
  return this->local_addr;
}

bool
QUICNetVConnection::_isTryingRenegotiation() const
{
  return this->_handshake_completed;
}

shared_SSL_CTX
QUICNetVConnection::_lookupContextByName(std::string const &servername, SSLCertContextType ctxType)
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
