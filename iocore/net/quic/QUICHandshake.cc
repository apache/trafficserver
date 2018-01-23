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

#include "QUICCryptoTls.h"
#include "QUICEvents.h"
#include "QUICGlobals.h"
#include "QUICVersionNegotiator.h"
#include "QUICConfig.h"

static constexpr char dump_tag[] = "v_quic_handshake_dump_pkt";

#define QUICHSDebug(fmt, ...) \
  Debug("quic_handshake", "[%" PRIx64 "] " fmt, static_cast<uint64_t>(this->_client_qc->connection_id()), ##__VA_ARGS__)

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

QUICHandshake::QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx) : QUICHandshake(qc, ssl_ctx, {})
{
}

QUICHandshake::QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx, QUICStatelessResetToken token)
  : QUICApplication(qc),
    _ssl(SSL_new(ssl_ctx)),
    _crypto(new QUICCryptoTls(this->_ssl, qc->direction())),
    _version_negotiator(new QUICVersionNegotiator()),
    _netvc_context(qc->direction()),
    _reset_token(token)
{
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_qc_index, qc);
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_hs_index, this);
  this->_crypto->initialize_key_materials(this->_client_qc->original_connection_id());

  SET_HANDLER(&QUICHandshake::state_initial);
}

QUICHandshake::~QUICHandshake()
{
  SSL_free(this->_ssl);
}

QUICErrorUPtr
QUICHandshake::start(QUICPacketFactory *packet_factory)
{
  this->_load_local_client_transport_parameters(QUIC_SUPPORTED_VERSIONS[0]);
  packet_factory->set_version(QUIC_SUPPORTED_VERSIONS[0]);
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
        this->_client_qc->transmit_packet(
          packet_factory->create_version_negotiation_packet(initial_packet, _client_qc->largest_acked_packet_number()));
        QUICHSDebug("Version negotiation failed: %x", initial_packet->version());
      }
    } else {
      return QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::PROTOCOL_VIOLATION));
    }
  }
  return QUICErrorUPtr(new QUICNoError());
}

bool
QUICHandshake::is_version_negotiated()
{
  return (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NEGOTIATED);
}

bool
QUICHandshake::is_completed()
{
  return this->handler == &QUICHandshake::state_complete;
}

QUICCrypto *
QUICHandshake::crypto_module()
{
  return this->_crypto;
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
  QUICCryptoTls *crypto_tls = dynamic_cast<QUICCryptoTls *>(this->_crypto);
  if (crypto_tls) {
    return SSL_get_cipher_name(crypto_tls->ssl_handle());
  }

  return nullptr;
}

void
QUICHandshake::negotiated_application_name(const uint8_t **name, unsigned int *len)
{
  // FIXME Generalize and remove dynamic_cast
  QUICCryptoTls *crypto_tls = dynamic_cast<QUICCryptoTls *>(this->_crypto);
  if (crypto_tls) {
    SSL_get0_alpn_selected(crypto_tls->ssl_handle(), name, len);
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

  // TODO Add client side implementation

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
QUICHandshake::local_transport_parameters(bool with_version)
{
  if (with_version) {
    return this->_local_transport_parameters;
  } else {
    QUICConfig::scoped_config params;
    QUICTransportParametersInNewSessionTicket *tp = new QUICTransportParametersInNewSessionTicket();

    // MUSTs
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, params->initial_max_stream_data());
    tp->set(QUICTransportParameterId::INITIAL_MAX_DATA, params->initial_max_data());
    tp->set(QUICTransportParameterId::IDLE_TIMEOUT, static_cast<uint16_t>(params->no_activity_timeout_in()));
    tp->set(QUICTransportParameterId::STATELESS_RESET_TOKEN, this->_reset_token.buf(), QUICStatelessResetToken::LEN);

    // MAYs
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI, params->initial_max_stream_id_bidi_in());
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI, params->initial_max_stream_id_uni_in());
    // this->_local_transport_parameters.add(QUICTransportParameterId::OMIT_CONNECTION_ID, {});
    // this->_local_transport_parameters.add(QUICTransportParameterId::MAX_PACKET_SIZE, {{0x00, 0x00}, 2});

    return std::unique_ptr<QUICTransportParameters>(tp);
  }
}

std::shared_ptr<const QUICTransportParameters>
QUICHandshake::remote_transport_parameters()
{
  return this->_remote_transport_parameters;
}

int
QUICHandshake::state_initial(int event, Event *data)
{
  QUICHSDebug("%s (%d)", get_vc_event_name(event), event);

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE: {
    QUICHSDebug("Enter state_key_exchange");
    SET_HANDLER(&QUICHandshake::state_key_exchange);
    break;
  }
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    if (this->_netvc_context == NET_VCONNECTION_IN) {
      error = this->_process_client_hello();
    }
    break;
  }
  case VC_EVENT_WRITE_READY:
  case VC_EVENT_WRITE_COMPLETE: {
    if (this->_netvc_context == NET_VCONNECTION_OUT) {
      error = this->_process_initial();
    }
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
QUICHandshake::state_key_exchange(int event, Event *data)
{
  QUICHSDebug("%s (%d)", get_vc_event_name(event), event);

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case QUIC_EVENT_HANDSHAKE_PACKET_WRITE_COMPLETE: {
    QUICCryptoTls *crypto_tls = dynamic_cast<QUICCryptoTls *>(this->_crypto);
    if (crypto_tls && SSL_is_init_finished(crypto_tls->ssl_handle())) {
      int res = this->_complete_handshake();
      if (!res) {
        this->_abort_handshake(QUICTransErrorCode::TLS_HANDSHAKE_FAILED);
      }
    }

    break;
  }
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    ink_assert(this->_netvc_context == NET_VCONNECTION_OUT);
    error = this->_process_server_hello();
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
QUICHandshake::state_auth(int event, Event *data)
{
  QUICHSDebug("%s (%d)", get_vc_event_name(event), event);

  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    ink_assert(this->_netvc_context == NET_VCONNECTION_IN);

    error = this->_process_finished();
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

  return EVENT_CONT;
}

int
QUICHandshake::state_address_validation(int event, void *data)
{
  // TODO Address validation should be implemented for the 2nd implementation draft
  return EVENT_DONE;
}

int
QUICHandshake::state_complete(int event, void *data)
{
  QUICHSDebug("%s (%d)", get_vc_event_name(event), event);
  QUICHSDebug("Got an event on complete state. Ignoring it for now.");

  return EVENT_DONE;
}

int
QUICHandshake::state_closed(int event, void *data)
{
  return EVENT_DONE;
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
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI, params->initial_max_stream_id_bidi_in());
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI, params->initial_max_stream_id_uni_in());
  // this->_local_transport_parameters.add(QUICTransportParameterId::OMIT_CONNECTION_ID, {});
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
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI, params->initial_max_stream_id_bidi_out());
  tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI, params->initial_max_stream_id_uni_out());

  this->_local_transport_parameters = std::unique_ptr<QUICTransportParameters>(tp);
}

int
QUICHandshake::_do_handshake(bool initial)
{
  // TODO: pass stream_io
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  uint8_t in[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t in_len                       = 0;

  if (!initial) {
    // Complete message should fit in a packet and be able to read
    in_len = stream_io->read_avail();
    stream_io->read(in, in_len);

    if (in_len <= 0) {
      QUICHSDebug("No message");
      return SSL_ERROR_NONE;
    }
    I_WANNA_DUMP_THIS_BUF(in, in_len);
  }

  uint8_t out[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t out_len                     = 0;
  int result                         = this->_crypto->handshake(out, out_len, MAX_HANDSHAKE_MSG_LEN, in, in_len);

  if (out_len > 0) {
    I_WANNA_DUMP_THIS_BUF(out, static_cast<int64_t>(out_len));
    stream_io->write(out, out_len);
  }

  return result;
}

QUICErrorUPtr
QUICHandshake::_process_initial()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);
  int result              = _do_handshake(true);
  QUICErrorUPtr error     = QUICErrorUPtr(new QUICNoError());

  switch (result) {
  case SSL_ERROR_WANT_READ: {
    stream_io->write_reenable();
    stream_io->read_reenable();

    break;
  }
  default:
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_HANDSHAKE_FAILED));
  }

  return error;
}

QUICErrorUPtr
QUICHandshake::_process_client_hello()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);
  int result              = _do_handshake();
  QUICErrorUPtr error     = QUICErrorUPtr(new QUICNoError());

  switch (result) {
  case SSL_ERROR_WANT_READ: {
    QUICHSDebug("Enter state_auth");
    SET_HANDLER(&QUICHandshake::state_auth);

    stream_io->write_reenable();
    stream_io->read_reenable();

    break;
  }
  default:
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_HANDSHAKE_FAILED));
  }

  return error;
}

QUICErrorUPtr
QUICHandshake::_process_server_hello()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);
  int result              = _do_handshake();
  QUICErrorUPtr error     = QUICErrorUPtr(new QUICNoError());

  switch (result) {
  case SSL_ERROR_NONE: {
    stream_io->write_reenable();
    break;
  }
  case SSL_ERROR_WANT_READ: {
    // FIXME: check if write_reenable should be called or not
    stream_io->write_reenable();
    stream_io->read_reenable();
    break;
  }
  default:
    error = QUICErrorUPtr(new QUICConnectionError(QUICTransErrorCode::TLS_HANDSHAKE_FAILED));
  }

  return error;
}

QUICErrorUPtr
QUICHandshake::_process_finished()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);
  int result              = _do_handshake();
  QUICErrorUPtr error     = QUICErrorUPtr(new QUICNoError());

  switch (result) {
  case SSL_ERROR_NONE: {
    int res = this->_complete_handshake();
    if (res) {
      stream_io->write_reenable();
    } else {
      this->_abort_handshake(QUICTransErrorCode::TLS_HANDSHAKE_FAILED);
    }

    break;
  }
  case SSL_ERROR_WANT_READ: {
    stream_io->read_reenable();
    break;
  }
  default:
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

  int res = this->_crypto->update_key_materials();
  if (res) {
    QUICHSDebug("Keying Materials are exported");
  } else {
    QUICHSDebug("Failed to export Keying Materials");
  }

  return res;
}

void
QUICHandshake::_abort_handshake(QUICTransErrorCode code)
{
  this->_client_qc->close(QUICConnectionErrorUPtr(new QUICConnectionError(code)));

  QUICHSDebug("Enter state_closed");
  SET_HANDLER(&QUICHandshake::state_closed);
}
