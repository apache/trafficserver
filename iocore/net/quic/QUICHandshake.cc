/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "QUICHandshake.h"

#include <utility>

#include "P_SSLNextProtocolSet.h"
#include "P_VConnection.h"

#include "QUICTLS.h"
#include "QUICEvents.h"
#include "QUICGlobals.h"
#include "QUICVersionNegotiator.h"
#include "QUICConfig.h"

static constexpr char dump_tag[] = "v_quic_handshake_dump_pkt";

#define QUICHSDebug(fmt, ...) Debug("quic_handshake", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

#define QUICVHSDebug(fmt, ...) Debug("v_quic_handshake", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

#define I_WANNA_DUMP_THIS_BUF(buf, len)                                                                                            \
  {                                                                                                                                \
    int i;                                                                                                                         \
    Debug(dump_tag, "len=%" PRId64 "\n", len);                                                                                     \
    for (i = 0; i < len / 8; i++) {                                                                                                \
      Debug(dump_tag, "%02x %02x %02x %02x %02x %02x %02x %02x ", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3],  \
            buf[i * 8 + 4], buf[i * 8 + 5], buf[i * 8 + 6], buf[i * 8 + 7]);                                                       \
    }                                                                                                                              \
    switch (len % 8) {                                                                                                             \
    case 1:                                                                                                                        \
      Debug(dump_tag, "%02x", buf[i * 8 + 0]);                                                                                     \
      break;                                                                                                                       \
    case 2:                                                                                                                        \
      Debug(dump_tag, "%02x %02x", buf[i * 8 + 0], buf[i * 8 + 1]);                                                                \
                                                                                                                                   \
      break;                                                                                                                       \
    case 3:                                                                                                                        \
      Debug(dump_tag, "%02x %02x %02x", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2]);                                           \
                                                                                                                                   \
      break;                                                                                                                       \
    case 4:                                                                                                                        \
      Debug(dump_tag, "%02x %02x %02x %02x", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3]);                      \
                                                                                                                                   \
      break;                                                                                                                       \
    case 5:                                                                                                                        \
      Debug(dump_tag, "%02x %02x %02x %02x %02x", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3], buf[i * 8 + 4]); \
                                                                                                                                   \
      break;                                                                                                                       \
    case 6:                                                                                                                        \
      Debug(dump_tag, "%02x %02x %02x %02x %02x %02x", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3],             \
            buf[i * 8 + 4], buf[i * 8 + 5]);                                                                                       \
                                                                                                                                   \
      break;                                                                                                                       \
    case 7:                                                                                                                        \
      Debug(dump_tag, "%02x %02x %02x %02x %02x %02x %02x", buf[i * 8 + 0], buf[i * 8 + 1], buf[i * 8 + 2], buf[i * 8 + 3],        \
            buf[i * 8 + 4], buf[i * 8 + 5], buf[i * 8 + 6]);                                                                       \
                                                                                                                                   \
      break;                                                                                                                       \
    default:                                                                                                                       \
      break;                                                                                                                       \
    }                                                                                                                              \
  }

static constexpr int UDP_MAXIMUM_PAYLOAD_SIZE = 65527;
// TODO: fix size
static constexpr int MAX_HANDSHAKE_MSG_LEN = 65527;

QUICHandshake::QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx) : QUICHandshake(qc, ssl_ctx, {}, false) {}

QUICHandshake::QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx, QUICStatelessResetToken token, bool stateless_retry)
  : QUICApplication(qc),
    _ssl(SSL_new(ssl_ctx)),
    _hs_protocol(new QUICTLS(this->_ssl, qc->direction(), stateless_retry)),
    _version_negotiator(new QUICVersionNegotiator()),
    _reset_token(token),
    _stateless_retry(stateless_retry)
{
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_qc_index, qc);
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_hs_index, this);
  this->_hs_protocol->initialize_key_materials(this->_qc->original_connection_id());

  if (this->_qc->direction() == NET_VCONNECTION_OUT) {
    this->_initial = true;
  }

  SET_HANDLER(&QUICHandshake::state_handshake);
}

QUICHandshake::~QUICHandshake()
{
  SSL_free(this->_ssl);
}

QUICErrorUPtr
QUICHandshake::start(QUICPacketFactory *packet_factory, bool vn_exercise_enabled)
{
  QUICVersion initital_version = QUIC_SUPPORTED_VERSIONS[0];
  if (vn_exercise_enabled) {
    initital_version = QUIC_EXERCISE_VERSIONS;
  }

  this->_load_local_client_transport_parameters(initital_version);
  packet_factory->set_version(initital_version);

  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICHandshake::start(const QUICPacket *initial_packet, QUICPacketFactory *packet_factory)
{
  // Negotiate version
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED) {
    if (initial_packet->type() != QUICPacketType::INITIAL) {
      return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::PROTOCOL_VIOLATION));
    }
    if (initial_packet->version()) {
      if (this->_version_negotiator->negotiate(initial_packet) == QUICVersionNegotiationStatus::NEGOTIATED) {
        QUICHSDebug("Version negotiation succeeded: %x", initial_packet->version());
        this->_load_local_server_transport_parameters(initial_packet->version());
        packet_factory->set_version(this->_version_negotiator->negotiated_version());
      } else {
        this->_qc->transmit_packet(packet_factory->create_version_negotiation_packet(initial_packet));
        QUICHSDebug("Version negotiation failed: %x", initial_packet->version());
      }
    } else {
      return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::PROTOCOL_VIOLATION));
    }
  }
  return QUICErrorUPtr(new QUICNoError());
}

QUICErrorUPtr
QUICHandshake::negotiate_version(const QUICPacket *vn, QUICPacketFactory *packet_factory)
{
  // Client side only
  ink_assert(this->_qc->direction() == NET_VCONNECTION_OUT);

  // If already negotiated, just ignore it
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NEGOTIATED ||
      this->_version_negotiator->status() == QUICVersionNegotiationStatus::VALIDATED) {
    QUICHSDebug("Ignore Version Negotiation packet");
    return QUICErrorUPtr(new QUICNoError());
  }

  if (vn->version() != 0x00) {
    QUICHSDebug("Version field must be 0x00000000");
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::PROTOCOL_VIOLATION));
  }

  if (this->_version_negotiator->negotiate(vn) == QUICVersionNegotiationStatus::NEGOTIATED) {
    QUICVersion version = this->_version_negotiator->negotiated_version();
    QUICHSDebug("Version negotiation succeeded: 0x%x", version);
    packet_factory->set_version(version);
  } else {
    QUICHSDebug("Version negotiation failed");
    return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR));
  }

  return QUICErrorUPtr(new QUICNoError());
}

bool
QUICHandshake::is_version_negotiated() const
{
  return (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NEGOTIATED ||
          this->_version_negotiator->status() == QUICVersionNegotiationStatus::VALIDATED);
}

bool
QUICHandshake::is_completed() const
{
  return this->handler == &QUICHandshake::state_complete;
}

bool
QUICHandshake::is_stateless_retry_enabled() const
{
  return this->_stateless_retry;
}

QUICHandshakeProtocol *
QUICHandshake::protocol()
{
  return this->_hs_protocol;
}

QUICVersion
QUICHandshake::negotiated_version()
{
  return this->_version_negotiator->negotiated_version();
}

// Similar to SSLNetVConnection::getSSLCipherSuite()
const char *
QUICHandshake::negotiated_cipher_suite()
{
  // FIXME Generalize and remove dynamic_cast
  QUICTLS *hs_tls = dynamic_cast<QUICTLS *>(this->_hs_protocol);
  if (hs_tls) {
    return SSL_get_cipher_name(hs_tls->ssl_handle());
  }

  return nullptr;
}

void
QUICHandshake::negotiated_application_name(const uint8_t **name, unsigned int *len)
{
  // FIXME Generalize and remove dynamic_cast
  QUICTLS *hs_tls = dynamic_cast<QUICTLS *>(this->_hs_protocol);
  if (hs_tls) {
    SSL_get0_alpn_selected(hs_tls->ssl_handle(), name, len);
  }
}

void
QUICHandshake::set_transport_parameters(std::shared_ptr<QUICTransportParametersInClientHello> tp)
{
  // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR.
  if (!tp->is_valid()) {
    QUICHSDebug("Transport parameter is not valid");
    this->_abort_handshake(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
    return;
  }

  this->_remote_transport_parameters = tp;

  // Version revalidation
  if (this->_version_negotiator->validate(tp.get()) != QUICVersionNegotiationStatus::VALIDATED) {
    QUICHSDebug("Version revalidation failed");
    this->_abort_handshake(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR);
    return;
  }

  QUICHSDebug("Version negotiation validated: %x", tp->initial_version());
  return;
}

void
QUICHandshake::set_transport_parameters(std::shared_ptr<QUICTransportParametersInEncryptedExtensions> tp)
{
  // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR.
  if (!tp->is_valid()) {
    QUICHSDebug("Transport parameter is not valid");
    this->_abort_handshake(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
    return;
  }

  this->_remote_transport_parameters = tp;

  // Version revalidation
  if (this->_version_negotiator->validate(tp.get()) != QUICVersionNegotiationStatus::VALIDATED) {
    QUICHSDebug("Version revalidation failed");
    this->_abort_handshake(QUICTransErrorCode::VERSION_NEGOTIATION_ERROR);
    return;
  }

  return;
}

void
QUICHandshake::set_transport_parameters(std::shared_ptr<QUICTransportParametersInNewSessionTicket> tp)
{
  // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR.
  if (!tp->is_valid()) {
    QUICHSDebug("Transport parameter is not valid");
    this->_abort_handshake(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
    return;
  }

  this->_remote_transport_parameters = tp;

  // TODO Add client side implementation
  ink_assert(false);
}

std::shared_ptr<const QUICTransportParameters>
QUICHandshake::local_transport_parameters()
{
  return this->_local_transport_parameters;
}

std::shared_ptr<const QUICTransportParameters>
QUICHandshake::remote_transport_parameters()
{
  return this->_remote_transport_parameters;
}

int
QUICHandshake::state_handshake(int event, Event *data)
{
  QUICVHSDebug("%s (%d)", get_vc_event_name(event), event);

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE: {
    if (this->_hs_protocol->is_handshake_finished()) {
      int res = this->_complete_handshake();
      if (!res) {
        this->_abort_handshake(QUICTransErrorCode::TLS_HANDSHAKE_FAILED);
      }
    }
    break;
  }
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE:
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    error = this->_process_handshake_msg();
    break;
  }
  default:
    break;
  }

  if (error->cls != QUICErrorClass::NONE) {
    QUICTransErrorCode code;
    if (dynamic_cast<QUICConnectionError *>(error.get()) != nullptr) {
      code = error->trans_error_code;
    } else {
      code = QUICTransErrorCode::PROTOCOL_VIOLATION;
    }
    this->_abort_handshake(code);
  }

  return EVENT_DONE;
}

int
QUICHandshake::state_complete(int event, void *data)
{
  QUICVHSDebug("%s (%d)", get_vc_event_name(event), event);
  QUICVHSDebug("Got an event on complete state. Ignoring it for now.");

  return EVENT_DONE;
}

int
QUICHandshake::state_closed(int event, void *data)
{
  return EVENT_DONE;
}

QUICHandshakeMsgType
QUICHandshake::msg_type() const
{
  if (this->_hs_protocol) {
    return this->_hs_protocol->msg_type();
  } else {
    return QUICHandshakeMsgType::NONE;
  }
}

/**
 * reset states for starting over
 */
void
QUICHandshake::reset()
{
  this->_initial = true;
  SSL_clear(this->_ssl);
}

void
QUICHandshake::_load_local_server_transport_parameters(QUICVersion negotiated_version)
{
  QUICConfig::scoped_config params;
  QUICTransportParametersInEncryptedExtensions *tp = new QUICTransportParametersInEncryptedExtensions(negotiated_version);

  // MUSTs
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, params->initial_max_stream_data());
  tp->set(QUICTransportParameterId::INITIAL_MAX_DATA, params->initial_max_data());
  tp->set(QUICTransportParameterId::IDLE_TIMEOUT, static_cast<uint16_t>(params->no_activity_timeout_in()));
  tp->set(QUICTransportParameterId::STATELESS_RESET_TOKEN, this->_reset_token.buf(), QUICStatelessResetToken::LEN);
  tp->add_version(QUIC_SUPPORTED_VERSIONS[0]);

  // MAYs
  tp->set(QUICTransportParameterId::INITIAL_MAX_BIDI_STREAMS, params->initial_max_bidi_streams_in());
  tp->set(QUICTransportParameterId::INITIAL_MAX_UNI_STREAMS, params->initial_max_uni_streams_in());
  // this->_local_transport_parameters.add(QUICTransportParameterId::MAX_PACKET_SIZE, {{0x00, 0x00}, 2});

  this->_local_transport_parameters = std::unique_ptr<QUICTransportParameters>(tp);
}

void
QUICHandshake::_load_local_client_transport_parameters(QUICVersion initial_version)
{
  QUICConfig::scoped_config params;

  QUICTransportParametersInClientHello *tp = new QUICTransportParametersInClientHello(initial_version);

  // MUSTs
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, params->initial_max_stream_data());
  tp->set(QUICTransportParameterId::INITIAL_MAX_DATA, params->initial_max_data());
  tp->set(QUICTransportParameterId::IDLE_TIMEOUT, static_cast<uint16_t>(params->no_activity_timeout_out()));

  // MAYs
  tp->set(QUICTransportParameterId::INITIAL_MAX_BIDI_STREAMS, params->initial_max_bidi_streams_out());
  tp->set(QUICTransportParameterId::INITIAL_MAX_UNI_STREAMS, params->initial_max_uni_streams_out());

  this->_local_transport_parameters = std::unique_ptr<QUICTransportParameters>(tp);
}

int
QUICHandshake::_do_handshake(size_t &out_len)
{
  // TODO: pass stream_io
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  uint8_t in[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t in_len                       = 0;

  if (this->_initial) {
    this->_initial = false;
  } else {
    // Complete message should fit in a packet and be able to read
    in_len = stream_io->read_avail();
    stream_io->read(in, in_len);

    if (in_len <= 0) {
      QUICVHSDebug("No message");
      return SSL_ERROR_NONE;
    }
    I_WANNA_DUMP_THIS_BUF(in, in_len);
  }

  uint8_t out[MAX_HANDSHAKE_MSG_LEN] = {0};
  int result                         = this->_hs_protocol->handshake(out, out_len, MAX_HANDSHAKE_MSG_LEN, in, in_len);

  if (out_len > 0) {
    I_WANNA_DUMP_THIS_BUF(out, static_cast<int64_t>(out_len));
    stream_io->write(out, out_len);
  }

  if (!this->_hs_protocol->is_key_derived(QUICKeyPhase::PHASE_0) && this->_hs_protocol->is_ready_to_derive()) {
    int res = this->_hs_protocol->update_key_materials();
    if (res) {
      QUICHSDebug("Keying Materials are exported");
    } else {
      QUICHSDebug("Failed to export Keying Materials");
    }
  }

  return result;
}

QUICErrorUPtr
QUICHandshake::_process_handshake_msg()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);
  size_t out_len          = 0;
  int result              = this->_do_handshake(out_len);
  QUICErrorUPtr error     = QUICErrorUPtr(new QUICNoError());

  switch (result) {
  case SSL_ERROR_NONE:
    if (this->_hs_protocol->is_handshake_finished()) {
      int res = this->_complete_handshake();
      if (!res) {
        error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_HANDSHAKE_FAILED));
      }
    }
  // Fall-through
  case SSL_ERROR_WANT_READ: {
    if (out_len > 0) {
      stream_io->write_reenable();
    }
    stream_io->read_reenable();

    break;
  }
  default:
    QUICHSDebug("Handshake failed: %d", result);
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_HANDSHAKE_FAILED));
  }

  return error;
}

int
QUICHandshake::_complete_handshake()
{
  QUICHSDebug("Enter state_complete");
  SET_HANDLER(&QUICHandshake::state_complete);
  QUICHSDebug("%s", this->negotiated_cipher_suite());

  int res = 1;
  if (!this->_hs_protocol->is_key_derived(QUICKeyPhase::PHASE_0)) {
    res = this->_hs_protocol->update_key_materials();
    if (res) {
      QUICHSDebug("Keying Materials are exported");
    } else {
      QUICHSDebug("Failed to export Keying Materials");
    }
  }

  return res;
}

void
QUICHandshake::_abort_handshake(QUICTransErrorCode code)
{
  this->_qc->close(QUICConnectionErrorUPtr(new QUICConnectionError(code)));

  QUICHSDebug("Enter state_closed");
  SET_HANDLER(&QUICHandshake::state_closed);
}
