/** @file

  OpenSSL native QUIC listener support.

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

#include "P_QUICPacketHandler.h"
#include "P_QUICNetProcessor.h"
#include "P_QUICNetVConnection.h"
#include "P_SSLCertLookup.h"
#include "P_UnixNet.h"
#include "iocore/net/QUICMultiCertConfigLoader.h"
#include "iocore/net/quic/QUICConfig.h"
#include "tscore/ink_atomic.h"
#include "tscore/ink_sock.h"

#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <cerrno>
#include <algorithm>

namespace
{
constexpr ink_hrtime OPENSSL_QUIC_EVENT_INTERVAL = HRTIME_MSECONDS(2);

DbgCtl dbg_ctl_openssl_quic{"openssl_quic"};

struct PeerCaptureBioState {
  BIO       *inner{nullptr};
  IpEndpoint last_peer;
  bool       have_last_peer{false};
};

thread_local PeerCaptureBioState *active_peer_capture = nullptr;

void
free_quic_peer_ex_data(void *, void *ptr, CRYPTO_EX_DATA *, int, long, void *)
{
  delete static_cast<IpEndpoint *>(ptr);
}

int
quic_peer_ex_data_index()
{
  static int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, free_quic_peer_ex_data);

  return index;
}

bool
bio_addr_to_ip_endpoint(IpEndpoint &endpoint, BIO_ADDR const *bio_addr)
{
  if (bio_addr == nullptr) {
    return false;
  }

  in_port_t const port = BIO_ADDR_rawport(bio_addr);
  if (BIO_ADDR_family(bio_addr) == AF_INET) {
    in_addr_t addr = 0;
    size_t    len  = sizeof(addr);

    if (BIO_ADDR_rawaddress(bio_addr, &addr, &len) != 1 || len != sizeof(addr)) {
      return false;
    }

    ats_ip4_set(&endpoint, addr, port);
    return true;
  }

  if (BIO_ADDR_family(bio_addr) == AF_INET6) {
    in6_addr addr;
    size_t   len = sizeof(addr);

    if (BIO_ADDR_rawaddress(bio_addr, &addr, &len) != 1 || len != sizeof(addr)) {
      return false;
    }

    ats_ip6_set(&endpoint, addr, port);
    return true;
  }

  return false;
}

PeerCaptureBioState *
peer_capture_state(BIO *bio)
{
  return static_cast<PeerCaptureBioState *>(BIO_get_data(bio));
}

BIO_MSG *
bio_msg_at(BIO_MSG *msg, size_t stride, size_t index)
{
  return reinterpret_cast<BIO_MSG *>(reinterpret_cast<char *>(msg) + (stride * index));
}

void
update_last_peer(PeerCaptureBioState &state, BIO_MSG const &msg)
{
  IpEndpoint peer;
  if (msg.peer != nullptr && bio_addr_to_ip_endpoint(peer, msg.peer)) {
    state.last_peer      = peer;
    state.have_last_peer = true;
    active_peer_capture  = &state;
  }
}

void
copy_retry_flags(BIO *bio, BIO *inner)
{
  BIO_clear_retry_flags(bio);
  BIO_set_flags(bio, BIO_get_retry_flags(inner));
  BIO_set_retry_reason(bio, BIO_get_retry_reason(inner));
}

int
peer_capture_bio_create(BIO *bio)
{
  BIO_set_init(bio, 0);
  BIO_set_data(bio, nullptr);
  BIO_set_shutdown(bio, 1);

  return 1;
}

int
peer_capture_bio_destroy(BIO *bio)
{
  if (bio == nullptr) {
    return 0;
  }

  auto *state = peer_capture_state(bio);
  if (state != nullptr) {
    if (active_peer_capture == state) {
      active_peer_capture = nullptr;
    }
    BIO_free(state->inner);
    delete state;
    BIO_set_data(bio, nullptr);
  }
  BIO_set_init(bio, 0);

  return 1;
}

int
peer_capture_bio_write(BIO *bio, char const *data, int len)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  int const result = BIO_write(state->inner, data, len);
  copy_retry_flags(bio, state->inner);

  return result;
}

int
peer_capture_bio_write_ex(BIO *bio, char const *data, size_t len, size_t *written)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  int const result = BIO_write_ex(state->inner, data, len, written);
  copy_retry_flags(bio, state->inner);

  return result;
}

int
peer_capture_bio_sendmmsg(BIO *bio, BIO_MSG *msg, size_t stride, size_t num_msg, uint64_t flags, size_t *num_processed)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  int const result = BIO_sendmmsg(state->inner, msg, stride, num_msg, flags, num_processed);
  copy_retry_flags(bio, state->inner);

  return result;
}

int
peer_capture_bio_read(BIO *bio, char *data, int len)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  int const result = BIO_read(state->inner, data, len);
  copy_retry_flags(bio, state->inner);

  return result;
}

int
peer_capture_bio_read_ex(BIO *bio, char *data, size_t len, size_t *read_bytes)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  int const result = BIO_read_ex(state->inner, data, len, read_bytes);
  copy_retry_flags(bio, state->inner);

  return result;
}

int
peer_capture_bio_recvmmsg(BIO *bio, BIO_MSG *msg, size_t stride, size_t num_msg, uint64_t flags, size_t *num_processed)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  size_t const read_limit = std::min<size_t>(num_msg, 1);

  int const result = BIO_recvmmsg(state->inner, msg, stride, read_limit, flags, num_processed);
  copy_retry_flags(bio, state->inner);

  if (result == 1 && num_processed != nullptr && *num_processed > 0) {
    update_last_peer(*state, *bio_msg_at(msg, stride, 0));
  }

  return result;
}

long
peer_capture_bio_ctrl(BIO *bio, int cmd, long larg, void *parg)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  return BIO_ctrl(state->inner, cmd, larg, parg);
}

long
peer_capture_bio_callback_ctrl(BIO *bio, int cmd, BIO_info_cb *cb)
{
  auto *state = peer_capture_state(bio);
  if (state == nullptr || state->inner == nullptr) {
    return 0;
  }

  return BIO_callback_ctrl(state->inner, cmd, cb);
}

BIO_METHOD *
peer_capture_bio_method()
{
  static BIO_METHOD *method = []() -> BIO_METHOD * {
    BIO_METHOD *m = BIO_meth_new(BIO_TYPE_SOURCE_SINK | BIO_get_new_index(), "ATS OpenSSL QUIC peer capture");
    if (m == nullptr) {
      return nullptr;
    }

    BIO_meth_set_create(m, peer_capture_bio_create);
    BIO_meth_set_destroy(m, peer_capture_bio_destroy);
    BIO_meth_set_write(m, peer_capture_bio_write);
    BIO_meth_set_write_ex(m, peer_capture_bio_write_ex);
    BIO_meth_set_sendmmsg(m, peer_capture_bio_sendmmsg);
    BIO_meth_set_read(m, peer_capture_bio_read);
    BIO_meth_set_read_ex(m, peer_capture_bio_read_ex);
    BIO_meth_set_recvmmsg(m, peer_capture_bio_recvmmsg);
    BIO_meth_set_ctrl(m, peer_capture_bio_ctrl);
    BIO_meth_set_callback_ctrl(m, peer_capture_bio_callback_ctrl);

    return m;
  }();

  return method;
}

BIO *
new_peer_capture_bio(int fd)
{
  BIO *inner = BIO_new_dgram(fd, BIO_NOCLOSE);
  if (inner == nullptr) {
    return nullptr;
  }

  BIO *bio = BIO_new(peer_capture_bio_method());
  if (bio == nullptr) {
    BIO_free(inner);
    return nullptr;
  }

  auto *state  = new PeerCaptureBioState;
  state->inner = inner;
  BIO_set_data(bio, state);
  BIO_set_init(bio, 1);

  return bio;
}

int
new_pending_quic_connection_cb(SSL_CTX *, SSL *new_ssl, void *)
{
  auto *capture = active_peer_capture;
  if (capture == nullptr || !capture->have_last_peer) {
    return 1;
  }

  int const index = quic_peer_ex_data_index();
  if (index < 0) {
    return 0;
  }

  delete static_cast<IpEndpoint *>(SSL_get_ex_data(new_ssl, index));
  auto *peer = new IpEndpoint(capture->last_peer);
  if (SSL_set_ex_data(new_ssl, index, peer) != 1) {
    delete peer;
    return 0;
  }

  return 1;
}

bool
get_bio_peer_addr(BIO *bio, IpEndpoint &remote_addr)
{
  if (bio == nullptr) {
    return false;
  }

  BIO_ADDR *peer_addr = BIO_ADDR_new();
  if (peer_addr == nullptr) {
    return false;
  }

  bool const success = BIO_dgram_get_peer(bio, peer_addr) == 1 && bio_addr_to_ip_endpoint(remote_addr, peer_addr);
  BIO_ADDR_free(peer_addr);

  return success;
}

bool
get_quic_peer_addr(SSL *ssl, IpEndpoint &remote_addr)
{
  int const index = quic_peer_ex_data_index();
  if (index >= 0) {
    auto *peer = static_cast<IpEndpoint *>(SSL_get_ex_data(ssl, index));
    if (peer != nullptr) {
      remote_addr = *peer;
      return true;
    }
  }

  return get_bio_peer_addr(SSL_get_rbio(ssl), remote_addr) || get_bio_peer_addr(SSL_get_wbio(ssl), remote_addr);
}

} // end anonymous namespace

QUICPacketHandler::QUICPacketHandler()
{
  this->_closed_con_collector        = std::make_unique<QUICClosedConCollector>();
  this->_closed_con_collector->mutex = new_ProxyMutex();
}

QUICPacketHandler::~QUICPacketHandler()
{
  if (this->_collector_event != nullptr) {
    this->_collector_event->cancel();
    this->_collector_event = nullptr;
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
QUICPacketHandler::send_packet(UDPConnection * /* udp_con ATS_UNUSED */, IpEndpoint & /* addr ATS_UNUSED */,
                               Ptr<IOBufferBlock> /* udp_payload ATS_UNUSED */, uint16_t /* segment_size ATS_UNUSED */,
                               struct timespec * /* send_at_hint ATS_UNUSED */)
{
}

QUICPacketHandlerIn::QUICPacketHandlerIn(NetProcessor::AcceptOptions const &opt) : NetAccept(opt), QUICPacketHandler()
{
  this->mutex = new_ProxyMutex();
}

QUICPacketHandlerIn::~QUICPacketHandlerIn()
{
  if (this->_event != nullptr) {
    this->_event->cancel();
    this->_event = nullptr;
  }
  if (this->_listener != nullptr) {
    SSL_free(this->_listener);
    this->_listener = nullptr;
  }
}

NetProcessor *
QUICPacketHandlerIn::getNetProcessor() const
{
  return &quic_NetProcessor;
}

NetAccept *
QUICPacketHandlerIn::clone() const
{
  return new QUICPacketHandlerIn(this->opt);
}

int
QUICPacketHandlerIn::acceptEvent(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
{
  this->setThreadAffinity(this_ethread());

  if (this->_collector_event == nullptr) {
    this->_collector_event = this_ethread()->schedule_every(this->_closed_con_collector.get(), HRTIME_MSECONDS(100));
  }

  if (this->_listener == nullptr && !this->_start_listener()) {
    return EVENT_DONE;
  }

  this->_event = nullptr;
  this->_service_listener();
  this->_schedule_event();

  return EVENT_CONT;
}

void
QUICPacketHandlerIn::init_accept(EThread *t)
{
  SET_HANDLER(&QUICPacketHandlerIn::acceptEvent);

  if (t == nullptr) {
    t = eventProcessor.assign_thread(ET_NET);
  }

  if (!this->action_->continuation->mutex) {
    this->action_->continuation->mutex = t->mutex;
    this->action_->mutex               = t->mutex;
  }

  this->mutex = get_NetHandler(t)->mutex;
  t->schedule_imm(this);
}

Continuation *
QUICPacketHandlerIn::_get_continuation()
{
  return this;
}

void
QUICPacketHandlerIn::_recv_packet(int /* event ATS_UNUSED */, UDPPacket * /* udpPacket ATS_UNUSED */)
{
}

bool
QUICPacketHandlerIn::_start_listener()
{
  if (!this->server.sock.is_ok()) {
    this->server.sock = UnixSocket{this->server.accept_addr.sa.sa_family, SOCK_DGRAM, 0};
    if (!this->server.sock.is_ok()) {
      Error("failed to create OpenSSL QUIC UDP socket: %s", strerror(errno));
      return false;
    }

    if (this->server.accept_addr.sa.sa_family == AF_INET6 && this->server.sock.enable_option(IPPROTO_IPV6, IPV6_V6ONLY) < 0) {
      Error("failed to set IPV6_V6ONLY on OpenSSL QUIC socket: %s", strerror(errno));
      return false;
    }
    if (this->server.sock.enable_option(SOL_SOCKET, SO_REUSEADDR) < 0) {
      Error("failed to set SO_REUSEADDR on OpenSSL QUIC socket: %s", strerror(errno));
      return false;
    }
    if (this->server.sock.bind(&this->server.accept_addr.sa, ats_ip_size(&this->server.accept_addr.sa)) < 0) {
      Error("failed to bind OpenSSL QUIC socket: %s", strerror(errno));
      return false;
    }
  }

  if (this->server.sock.set_nonblocking() < 0) {
    Error("failed to make OpenSSL QUIC socket nonblocking: %s", strerror(errno));
    return false;
  }
  if (this->opt.recv_bufsize > 0) {
    this->server.sock.set_rcvbuf_size(this->opt.recv_bufsize);
  }
  if (this->opt.send_bufsize > 0) {
    this->server.sock.set_sndbuf_size(this->opt.send_bufsize);
  }

  QUICCertConfig::scoped_config server_cert;
  QUICConfig::scoped_config     params;
  uint64_t                      listener_flags = 0;
  if (!params->stateless_retry()) {
    listener_flags |= SSL_LISTENER_FLAG_NO_VALIDATE;
  }

  SSL_CTX_set_new_pending_conn_cb(server_cert->defaultContext(), new_pending_quic_connection_cb, nullptr);

  this->_listener = SSL_new_listener(server_cert->defaultContext(), listener_flags);
  if (this->_listener == nullptr) {
    Error("failed to create OpenSSL QUIC listener");
    return false;
  }

  BIO *bio = new_peer_capture_bio(this->server.sock.get_fd());
  if (bio == nullptr) {
    Error("failed to create OpenSSL QUIC peer capture BIO");
    return false;
  }
  SSL_set_bio(this->_listener, bio, bio);
  if (SSL_get_rbio(this->_listener) == nullptr || SSL_get_wbio(this->_listener) == nullptr) {
    Error("failed to configure OpenSSL QUIC listener socket");
    return false;
  }
  SSL_set_blocking_mode(this->_listener, 0);
  SSL_set_event_handling_mode(this->_listener, SSL_VALUE_EVENT_HANDLING_MODE_EXPLICIT);

  SSL_set_feature_request_uint(this->_listener, SSL_VALUE_QUIC_IDLE_TIMEOUT, params->no_activity_timeout_in());
  SSL_set_feature_request_uint(this->_listener, SSL_VALUE_QUIC_STREAM_BIDI_LOCAL_AVAIL, params->initial_max_streams_bidi_in());
  SSL_set_feature_request_uint(this->_listener, SSL_VALUE_QUIC_STREAM_UNI_LOCAL_AVAIL, params->initial_max_streams_uni_in());
  SSL_set_incoming_stream_policy(this->_listener, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0);

  if (SSL_listen(this->_listener) != 1) {
    Error("failed to start OpenSSL QUIC listener");
    return false;
  }

  Dbg(dbg_ctl_openssl_quic, "OpenSSL QUIC listener started on fd %d", this->server.sock.get_fd());
  return true;
}

void
QUICPacketHandlerIn::_service_listener()
{
  SSL_handle_events(this->_listener);

  while (SSL *ssl = SSL_accept_connection(this->_listener, SSL_ACCEPT_CONNECTION_NO_BLOCK)) {
    SSL_set_blocking_mode(ssl, 0);
    SSL_set_event_handling_mode(ssl, SSL_VALUE_EVENT_HANDLING_MODE_IMPLICIT);
    SSL_set_incoming_stream_policy(ssl, SSL_INCOMING_STREAM_POLICY_ACCEPT, 0);
    SSL_set_default_stream_mode(ssl, SSL_DEFAULT_STREAM_MODE_NONE);

    auto *vc = static_cast<QUICNetVConnection *>(getNetProcessor()->allocate_vc(this_ethread()));
    if (vc == nullptr) {
      SSL_free(ssl);
      continue;
    }

    IpEndpoint remote_addr;
    if (!get_quic_peer_addr(ssl, remote_addr)) {
      remote_addr.setToAnyAddr(this->server.accept_addr.sa.sa_family);
    }

    vc->init(ssl, this);
    vc->id = net_next_connection_number();
    vc->set_quic_endpoints(this->server.accept_addr, remote_addr);
    vc->submit_time = ink_get_hrtime();
    vc->thread      = this_ethread();
    vc->mutex       = get_NetHandler(this_ethread())->mutex;
    vc->action_     = *this->action_;
    vc->set_is_transparent(this->opt.f_inbound_transparent);
    vc->set_context(NET_VCONNECTION_IN);
    vc->options.ip_proto  = NetVCOptions::USE_UDP;
    vc->options.ip_family = this->server.accept_addr.sa.sa_family;
    this_ethread()->schedule_imm(vc, EVENT_NONE, nullptr);
  }
}

void
QUICPacketHandlerIn::_schedule_event()
{
  if (this->_event == nullptr) {
    this->_event = this_ethread()->schedule_in(this, OPENSSL_QUIC_EVENT_INTERVAL);
  }
}

void
QUICPacketHandlerOut::init(QUICNetVConnection * /* vc ATS_UNUSED */)
{
}

Continuation *
QUICPacketHandlerOut::_get_continuation()
{
  return this;
}

void
QUICPacketHandlerOut::_recv_packet(int /* event ATS_UNUSED */, UDPPacket * /* udp_packet ATS_UNUSED */)
{
}
