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

#include "QUICEvents.h"
#include "QUICGlobals.h"
#include "QUICHandshakeProtocol.h"
#include "QUICPacketFactory.h"
#include "QUICVersionNegotiator.h"
#include "QUICConfig.h"

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

QUICHandshake::QUICHandshake(QUICVersion version, QUICConnection *qc, QUICHandshakeProtocol *hsp)
  : QUICHandshake(version, qc, hsp, {}, false)
{
}

QUICHandshake::QUICHandshake(QUICVersion version, QUICConnection *qc, QUICHandshakeProtocol *hsp, QUICStatelessResetToken token,
                             bool stateless_retry)
  : _qc(qc),
    _hs_protocol(hsp),
    _version_negotiator(new QUICVersionNegotiator()),
    _reset_token(token),
    _stateless_retry(stateless_retry)
{
  this->_hs_protocol->initialize_key_materials(this->_qc->original_connection_id(), version);

  if (this->_qc->direction() == NET_VCONNECTION_OUT) {
    this->_client_initial = true;
  }
}

QUICHandshake::~QUICHandshake()
{
  delete this->_hs_protocol;
}

QUICConnectionErrorUPtr
QUICHandshake::start(const QUICTPConfig &tp_config, QUICPacketFactory *packet_factory, bool vn_exercise_enabled)
{
  QUICVersion initial_version = QUIC_SUPPORTED_VERSIONS[0];
  if (vn_exercise_enabled) {
    initial_version = QUIC_EXERCISE_VERSION1;
  }

  this->_load_local_client_transport_parameters(tp_config);
  packet_factory->set_version(initial_version);

  return nullptr;
}

QUICConnectionErrorUPtr
QUICHandshake::start(const QUICTPConfig &tp_config, const QUICInitialPacketR &initial_packet, QUICPacketFactory *packet_factory,
                     const QUICPreferredAddress *pref_addr)
{
  // Negotiate version
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED) {
    if (initial_packet.type() != QUICPacketType::INITIAL) {
      return std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
    }
    if (initial_packet.version()) {
      if (this->_version_negotiator->negotiate(initial_packet) == QUICVersionNegotiationStatus::NEGOTIATED) {
        QUICHSDebug("Version negotiation succeeded: %x", initial_packet.version());
        this->_load_local_server_transport_parameters(tp_config, pref_addr);
        packet_factory->set_version(this->_version_negotiator->negotiated_version());
      } else {
        ink_assert(!"Unsupported version initial packet should be dropped QUICPacketHandler");
      }
    } else {
      return std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
    }
    this->_initial_source_cid_received = initial_packet.source_cid();
  }
  return nullptr;
}

QUICConnectionErrorUPtr
QUICHandshake::negotiate_version(const QUICVersionNegotiationPacketR &vn, QUICPacketFactory *packet_factory)
{
  // Client side only
  ink_assert(this->_qc->direction() == NET_VCONNECTION_OUT);

  // If already negotiated, just ignore it
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NEGOTIATED ||
      this->_version_negotiator->status() == QUICVersionNegotiationStatus::VALIDATED) {
    QUICHSDebug("Ignore Version Negotiation packet");
    return nullptr;
  }

  if (vn.version() != 0x00) {
    QUICHSDebug("Version field must be 0x00000000");
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
  }

  if (this->_version_negotiator->negotiate(vn) == QUICVersionNegotiationStatus::NEGOTIATED) {
    QUICVersion version = this->_version_negotiator->negotiated_version();
    QUICHSDebug("Version negotiation succeeded: 0x%x", version);
    packet_factory->set_version(version);
  } else {
    QUICHSDebug("Version negotiation failed");
    return std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
  }

  return nullptr;
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
  return this->_hs_protocol->is_handshake_finished();
}

bool
QUICHandshake::is_confirmed() const
{
  if (this->_qc->direction() == NET_VCONNECTION_IN) {
    return this->is_completed();
  } else {
    return this->_is_handshake_done_received;
  }
}

bool
QUICHandshake::is_stateless_retry_enabled() const
{
  return this->_stateless_retry;
}

bool
QUICHandshake::has_remote_tp() const
{
  return this->_remote_transport_parameters != nullptr;
}

QUICVersion
QUICHandshake::negotiated_version() const
{
  return this->_version_negotiator->negotiated_version();
}

// Similar to SSLNetVConnection::getSSLCipherSuite()
const char *
QUICHandshake::negotiated_cipher_suite() const
{
  return this->_hs_protocol->negotiated_cipher_suite();
}

void
QUICHandshake::negotiated_application_name(const uint8_t **name, unsigned int *len) const
{
  this->_hs_protocol->negotiated_application_name(name, len);
}

bool
QUICHandshake::check_remote_transport_parameters()
{
  auto tp = this->_hs_protocol->remote_transport_parameters();

  if (tp == nullptr) {
    // nothing to check
    return true;
  }

  if (std::dynamic_pointer_cast<const QUICTransportParametersInClientHello>(tp)) {
    return this->_check_remote_transport_parameters(std::static_pointer_cast<const QUICTransportParametersInClientHello>(tp));
  } else {
    return this->_check_remote_transport_parameters(
      std::static_pointer_cast<const QUICTransportParametersInEncryptedExtensions>(tp));
  }
}

bool
QUICHandshake::_check_remote_transport_parameters(std::shared_ptr<const QUICTransportParametersInClientHello> tp)
{
  // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR.
  if (!tp->is_valid()) {
    QUICHSDebug("Transport parameter is not valid");
    this->_abort_handshake(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
    return false;
  }

  // Check if CIDs in TP match with the ones in packets
  if (this->negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]) { // draft-28
    uint16_t cid_buf_len;
    const uint8_t *cid_buf = tp->getAsBytes(QUICTransportParameterId::INITIAL_SOURCE_CONNECTION_ID, cid_buf_len);
    QUICConnectionId cid_in_tp(cid_buf, cid_buf_len);
    if (cid_in_tp != this->_initial_source_cid_received) {
      this->_abort_handshake(QUICTransErrorCode::PROTOCOL_VIOLATION);
      return false;
    }
  }

  this->_remote_transport_parameters = tp;

  return true;
}

bool
QUICHandshake::_check_remote_transport_parameters(std::shared_ptr<const QUICTransportParametersInEncryptedExtensions> tp)
{
  // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR.
  if (!tp->is_valid()) {
    QUICHSDebug("Transport parameter is not valid");
    this->_abort_handshake(QUICTransErrorCode::TRANSPORT_PARAMETER_ERROR);
    return false;
  }

  // Check if CIDs in TP match with the ones in packets
  if (this->negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]) { // draft-28
    uint16_t cid_buf_len;
    const uint8_t *cid_buf = tp->getAsBytes(QUICTransportParameterId::INITIAL_SOURCE_CONNECTION_ID, cid_buf_len);
    QUICConnectionId cid_in_tp(cid_buf, cid_buf_len);
    if (cid_in_tp != this->_initial_source_cid_received) {
      this->_abort_handshake(QUICTransErrorCode::PROTOCOL_VIOLATION);
      return false;
    }

    if (!this->_retry_source_cid_received.is_zero()) {
      cid_buf = tp->getAsBytes(QUICTransportParameterId::RETRY_SOURCE_CONNECTION_ID, cid_buf_len);
      QUICConnectionId cid_in_tp(cid_buf, cid_buf_len);
      if (cid_in_tp != this->_retry_source_cid_received) {
        this->_abort_handshake(QUICTransErrorCode::PROTOCOL_VIOLATION);
        return false;
      }
    }
  }

  this->_remote_transport_parameters = tp;

  return true;
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
  this->_hs_protocol->reset();

  for (auto level : QUIC_ENCRYPTION_LEVELS) {
    int index                = static_cast<int>(level);
    QUICCryptoStream *stream = &this->_crypto_streams[index];
    stream->reset_send_offset();
    stream->reset_recv_offset();
  }
}

void
QUICHandshake::update(const QUICInitialPacketR &packet)
{
  this->_initial_source_cid_received = packet.source_cid();
}

void
QUICHandshake::update(const QUICRetryPacketR &packet)
{
  this->_retry_source_cid_received = packet.source_cid();
}

std::vector<QUICFrameType>
QUICHandshake::interests()
{
  return {
    QUICFrameType::CRYPTO,
    QUICFrameType::HANDSHAKE_DONE,
  };
}

QUICConnectionErrorUPtr
QUICHandshake::handle_frame(QUICEncryptionLevel level, const QUICFrame &frame)
{
  QUICConnectionErrorUPtr error = nullptr;
  switch (frame.type()) {
  case QUICFrameType::CRYPTO:
    error = this->_crypto_streams[static_cast<int>(level)].recv(static_cast<const QUICCryptoFrame &>(frame));
    if (error == nullptr) {
      error = this->do_handshake();
    }
    break;
  case QUICFrameType::HANDSHAKE_DONE:
    if (this->_qc->direction() == NET_VCONNECTION_IN) {
      error = std::make_unique<QUICConnectionError>(QUICTransErrorCode::PROTOCOL_VIOLATION);
    } else {
      this->_is_handshake_done_received = true;
    }
    break;
  default:
    QUICHSDebug("Unexpected frame type: %02x", static_cast<unsigned int>(frame.type()));
    ink_assert(false);
    break;
  }

  return error;
}

bool
QUICHandshake::will_generate_frame(QUICEncryptionLevel level, size_t current_packet_size, bool ack_eliciting, uint32_t seq_num)
{
  if (!this->_is_level_matched(level)) {
    return false;
  }

  return (this->_qc->direction() == NET_VCONNECTION_IN && !this->_is_handshake_done_sent) ||
         this->_crypto_streams[static_cast<int>(level)].will_generate_frame(level, current_packet_size, ack_eliciting, seq_num);
}

QUICFrame *
QUICHandshake::generate_frame(uint8_t *buf, QUICEncryptionLevel level, uint64_t connection_credit, uint16_t maximum_frame_size,
                              size_t current_packet_size, uint32_t seq_num)
{
  QUICFrame *frame = nullptr;

  if (this->_is_level_matched(level)) {
    // CRYPTO
    frame = this->_crypto_streams[static_cast<int>(level)].generate_frame(buf, level, connection_credit, maximum_frame_size,
                                                                          current_packet_size, seq_num);
    if (frame) {
      return frame;
    }
  }

  if (level == QUICEncryptionLevel::ONE_RTT) {
    // HANDSHAKE_DONE
    if (!this->_is_handshake_done_sent && this->is_completed()) {
      frame = QUICFrameFactory::create_handshake_done_frame(buf, this->_issue_frame_id(), this);
    }
    if (frame) {
      this->_is_handshake_done_sent = true;
      return frame;
    }
  }

  return frame;
}

void
QUICHandshake::_load_local_server_transport_parameters(const QUICTPConfig &tp_config, const QUICPreferredAddress *pref_addr)
{
  QUICTransportParametersInEncryptedExtensions *tp = new QUICTransportParametersInEncryptedExtensions();

  // MUSTs
  tp->set(QUICTransportParameterId::MAX_IDLE_TIMEOUT, static_cast<uint16_t>(tp_config.no_activity_timeout()));
  if (this->_stateless_retry) {
    tp->set(QUICTransportParameterId::ORIGINAL_DESTINATION_CONNECTION_ID, this->_qc->first_connection_id(),
            this->_qc->first_connection_id().length());
    tp->set(QUICTransportParameterId::RETRY_SOURCE_CONNECTION_ID, this->_qc->retry_source_connection_id(),
            this->_qc->retry_source_connection_id().length());
  } else {
    if (this->negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]) { // draft-28
      tp->set(QUICTransportParameterId::ORIGINAL_DESTINATION_CONNECTION_ID, this->_qc->original_connection_id(),
              this->_qc->original_connection_id().length());
    }
  }
  if (this->negotiated_version() == QUIC_SUPPORTED_VERSIONS[0]) { // draft-28
    tp->set(QUICTransportParameterId::INITIAL_SOURCE_CONNECTION_ID, this->_qc->initial_source_connection_id(),
            this->_qc->initial_source_connection_id().length());
  }

  // MAYs
  if (tp_config.initial_max_data() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_DATA, tp_config.initial_max_data());
  }
  if (tp_config.initial_max_streams_bidi() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAMS_BIDI, tp_config.initial_max_streams_bidi());
  }
  if (tp_config.initial_max_streams_uni() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAMS_UNI, tp_config.initial_max_streams_uni());
  }
  if (tp_config.initial_max_stream_data_bidi_local() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL, tp_config.initial_max_stream_data_bidi_local());
  }
  if (tp_config.initial_max_stream_data_bidi_remote() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, tp_config.initial_max_stream_data_bidi_remote());
  }
  if (tp_config.initial_max_stream_data_uni() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI, tp_config.initial_max_stream_data_uni());
  }
  if (tp_config.disable_active_migration()) {
    tp->set(QUICTransportParameterId::DISABLE_ACTIVE_MIGRATION, nullptr, 0);
  }
  if (pref_addr != nullptr) {
    uint8_t pref_addr_buf[QUICPreferredAddress::MAX_LEN];
    uint16_t len;
    pref_addr->store(pref_addr_buf, len);
    tp->set(QUICTransportParameterId::PREFERRED_ADDRESS, pref_addr_buf, len);
  }
  if (tp_config.active_cid_limit() != 0) {
    tp->set(QUICTransportParameterId::ACTIVE_CONNECTION_ID_LIMIT, tp_config.active_cid_limit());
  }

  // MAYs (server)
  tp->set(QUICTransportParameterId::STATELESS_RESET_TOKEN, this->_reset_token.buf(), QUICStatelessResetToken::LEN);
  tp->set(QUICTransportParameterId::ACK_DELAY_EXPONENT, tp_config.ack_delay_exponent());

  // Additional parameters
  for (auto &&param : tp_config.additional_tp()) {
    tp->set(param.first, param.second.first, param.second.second);
  }

  // Additional parameters
  for (auto &&param : tp_config.additional_tp()) {
    tp->set(param.first, param.second.first, param.second.second);
  }

  this->_local_transport_parameters = std::shared_ptr<QUICTransportParameters>(tp);
  this->_hs_protocol->set_local_transport_parameters(this->_local_transport_parameters);
}

void
QUICHandshake::_load_local_client_transport_parameters(const QUICTPConfig &tp_config)
{
  QUICTransportParametersInClientHello *tp = new QUICTransportParametersInClientHello();

  // MUSTs
  tp->set(QUICTransportParameterId::MAX_IDLE_TIMEOUT, static_cast<uint16_t>(tp_config.no_activity_timeout()));
  tp->set(QUICTransportParameterId::INITIAL_SOURCE_CONNECTION_ID, this->_qc->initial_source_connection_id(),
          this->_qc->initial_source_connection_id().length());

  // MAYs
  if (tp_config.initial_max_data() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_DATA, tp_config.initial_max_data());
  }
  if (tp_config.initial_max_streams_bidi() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAMS_BIDI, tp_config.initial_max_streams_bidi());
  }
  if (tp_config.initial_max_streams_uni() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAMS_UNI, tp_config.initial_max_streams_uni());
  }
  if (tp_config.initial_max_stream_data_bidi_local() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL, tp_config.initial_max_stream_data_bidi_local());
  }
  if (tp_config.initial_max_stream_data_bidi_remote() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE, tp_config.initial_max_stream_data_bidi_remote());
  }
  if (tp_config.initial_max_stream_data_uni() != 0) {
    tp->set(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI, tp_config.initial_max_stream_data_uni());
  }
  tp->set(QUICTransportParameterId::ACK_DELAY_EXPONENT, tp_config.ack_delay_exponent());
  if (tp_config.active_cid_limit() != 0) {
    tp->set(QUICTransportParameterId::ACTIVE_CONNECTION_ID_LIMIT, tp_config.active_cid_limit());
  }

  // Additional parameters
  for (auto &&param : tp_config.additional_tp()) {
    tp->set(param.first, param.second.first, param.second.second);
  }

  this->_local_transport_parameters = std::shared_ptr<QUICTransportParameters>(tp);
  this->_hs_protocol->set_local_transport_parameters(std::unique_ptr<QUICTransportParameters>(tp));
}

QUICConnectionErrorUPtr
QUICHandshake::do_handshake()
{
  QUICConnectionErrorUPtr error = nullptr;

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
      }
      in.offsets[index + 1] = in.offsets[index] + bytes_avail;
    }
  }

  QUICHandshakeMsgs *out = nullptr;
  int result             = this->_hs_protocol->handshake(&out, &in);
  if (this->_remote_transport_parameters == nullptr) {
    if (!this->check_remote_transport_parameters()) {
      result = 0;
    }
  }

  if (result == 1) {
    if (out) {
      for (auto level : QUIC_ENCRYPTION_LEVELS) {
        int index                = static_cast<int>(level);
        QUICCryptoStream *stream = &this->_crypto_streams[index];
        size_t len               = out->offsets[index + 1] - out->offsets[index];
        // TODO: check size
        if (len > 0) {
          stream->write(out->buf + out->offsets[index], len);
        }
      }
    }
  } else {
    this->_hs_protocol->abort_handshake();
    if (this->_hs_protocol->has_crypto_error()) {
      error = std::make_unique<QUICConnectionError>(QUICErrorClass::TRANSPORT, this->_hs_protocol->crypto_error());
    } else {
      error = std::make_unique<QUICConnectionError>(QUICErrorClass::TRANSPORT,
                                                    static_cast<uint16_t>(QUICTransErrorCode::PROTOCOL_VIOLATION));
    }
  }

  return error;
}

void
QUICHandshake::_abort_handshake(QUICTransErrorCode code)
{
  QUICHSDebug("Abort Handshake");

  this->_hs_protocol->abort_handshake();

  this->_qc->close_quic_connection(QUICConnectionErrorUPtr(new QUICConnectionError(code)));
}

/*
   No limit of encryption level.
   ```
   std::array<QUICEncryptionLevel, 4> _encryption_level_filter = {
     QUICEncryptionLevel::INITIAL,
     QUICEncryptionLevel::ZERO_RTT,
     QUICEncryptionLevel::HANDSHAKE,
     QUICEncryptionLevel::ONE_RTT,
   };
   ```
*/
bool
QUICHandshake::_is_level_matched(QUICEncryptionLevel level)
{
  return true;
}

void
QUICHandshake::_on_frame_lost(QUICFrameInformationUPtr &info)
{
  ink_assert(info->type == QUICFrameType::HANDSHAKE_DONE);
  this->_is_handshake_done_sent = false;
}
