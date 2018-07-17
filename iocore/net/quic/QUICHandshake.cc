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
#include "QUICStream.h"

#define QUICHSDebug(fmt, ...) Debug("quic_handshake", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

#define QUICVHSDebug(fmt, ...) Debug("v_quic_handshake", "[%s] " fmt, this->_qc->cids().data(), ##__VA_ARGS__)

#define I_WANNA_DUMP_THIS_BUF(buf, len)                                                                                            \
  {                                                                                                                                \
    static constexpr char dump_tag[] = "v_quic_handshake_dump_pkt";                                                                \
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
  : _qc(qc),
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
    this->_client_initial = true;
  }
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
        ink_assert(!"Unsupported version initial packet should be droped QUICPakcetHandler");
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
  // TODO: check state with other way
  return SSL_is_init_finished(this->_ssl);
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

/**
 * reset states for starting over
 */
void
QUICHandshake::reset()
{
  this->_client_initial = true;
  SSL_clear(this->_ssl);

  for (auto level : QUIC_ENCRYPTION_LEVELS) {
    int index                = static_cast<int>(level);
    QUICCryptoStream *stream = &this->_crypto_streams[index];
    stream->reset_send_offset();
    stream->reset_recv_offset();
  }
}

std::vector<QUICFrameType>
QUICHandshake::interests()
{
  return {
    QUICFrameType::CRYPTO,
  };
}

QUICErrorUPtr
QUICHandshake::handle_frame(QUICEncryptionLevel level, std::shared_ptr<const QUICFrame> frame)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());

  switch (frame->type()) {
  case QUICFrameType::CRYPTO:
    error = this->_crypto_streams[static_cast<int>(level)].recv(std::static_pointer_cast<const QUICCryptoFrame>(frame));
    break;
  default:
    QUICHSDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame->type()));
    ink_assert(false);
    break;
  }

  this->do_handshake();

  return error;
}

bool
QUICHandshake::will_generate_frame(QUICEncryptionLevel level)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return this->_crypto_streams[static_cast<int>(level)].will_generate_frame(level);
}

QUICFrameUPtr
QUICHandshake::generate_frame(QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size)
{
  QUICFrameUPtr frame = QUICFrameFactory::create_null_frame();

  if (!this->_is_level_matched(level)) {
    return frame;
  }

  frame = this->_crypto_streams[static_cast<int>(level)].generate_frame(level, connection_credit, maximum_frame_size);

  return frame;
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
QUICHandshake::do_handshake()
{
  QUICHandshakeMsgs in;
  uint8_t in_buf[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  in.buf                                   = in_buf;
  in.max_buf_len                           = UDP_MAXIMUM_PAYLOAD_SIZE;

  if (this->_client_initial) {
    this->_client_initial = false;
  } else {
    for (auto level : QUIC_ENCRYPTION_LEVELS) {
      int index                = static_cast<int>(level);
      QUICCryptoStream *stream = &this->_crypto_streams[index];
      int64_t bytes_avail      = stream->read_avail();
      // TODO: check size
      if (bytes_avail > 0) {
        stream->read(in.buf + in.offsets[index], bytes_avail);
        in.offsets[index] = bytes_avail;
        in.offsets[4] += bytes_avail;
      }
    }
  }

  QUICHandshakeMsgs out;
  uint8_t out_buf[MAX_HANDSHAKE_MSG_LEN] = {0};
  out.buf                                = out_buf;
  out.max_buf_len                        = MAX_HANDSHAKE_MSG_LEN;

  int result = this->_hs_protocol->handshake(&out, &in);

  for (auto level : QUIC_ENCRYPTION_LEVELS) {
    int index                = static_cast<int>(level);
    QUICCryptoStream *stream = &this->_crypto_streams[index];
    size_t len               = out.offsets[index + 1] - out.offsets[index];
    // TODO: check size
    if (len > 0) {
      stream->write(out.buf + out.offsets[index], len);
    }
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

void
QUICHandshake::_abort_handshake(QUICTransErrorCode code)
{
  QUICHSDebug("Abort Handshake");

  this->_qc->close(QUICConnectionErrorUPtr(new QUICConnectionError(code)));
}
