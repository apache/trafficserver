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

#include "QUICGlobals.h"
#include "QUICHandshake.h"

#include <utility>
#include "QUICVersionNegotiator.h"
#include "QUICConfig.h"
#include "P_SSLNextProtocolSet.h"

static constexpr char dump_tag[] = "v_quic_handshake_dump_pkt";

#define DebugQHS(fmt, ...) \
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

QUICHandshake::QUICHandshake(QUICConnection *qc, SSL_CTX *ssl_ctx, QUICStatelessToken token) : QUICApplication(qc), _token(token)
{
  this->_ssl = SSL_new(ssl_ctx);
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_qc_index, qc);
  SSL_set_ex_data(this->_ssl, QUIC::ssl_quic_hs_index, this);
  this->_crypto             = new QUICCrypto(this->_ssl, qc->direction());
  this->_version_negotiator = new QUICVersionNegotiator();

  this->_load_local_transport_parameters();

  SET_HANDLER(&QUICHandshake::state_read_client_hello);
}

QUICHandshake::~QUICHandshake()
{
  SSL_free(this->_ssl);
}

QUICErrorUPtr
QUICHandshake::start(const QUICPacket *initial_packet, QUICPacketFactory *packet_factory)
{
  // Negotiate version
  if (this->_version_negotiator->status() == QUICVersionNegotiationStatus::NOT_NEGOTIATED) {
    if (initial_packet->type() != QUICPacketType::CLIENT_INITIAL) {
      return QUICErrorUPtr(new QUICConnectionError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::PROTOCOL_VIOLATION));
    }
    if (initial_packet->version()) {
      if (this->_version_negotiator->negotiate(initial_packet) == QUICVersionNegotiationStatus::NEGOTIATED) {
        DebugQHS("Version negotiation succeeded: %x", initial_packet->version());
        packet_factory->set_version(this->_version_negotiator->negotiated_version());
      } else {
        this->_client_qc->transmit_packet(
          packet_factory->create_version_negotiation_packet(initial_packet, _client_qc->largest_acked_packet_number()));
        DebugQHS("Version negotiation failed: %x", initial_packet->version());
      }
    } else {
      return QUICErrorUPtr(new QUICConnectionError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::PROTOCOL_VIOLATION));
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
  return this->_crypto->is_handshake_finished();
}

QUICVersion
QUICHandshake::negotiated_version()
{
  return this->_version_negotiator->negotiated_version();
}

QUICCrypto *
QUICHandshake::crypto_module()
{
  return this->_crypto;
}

void
QUICHandshake::negotiated_application_name(const uint8_t **name, unsigned int *len)
{
  SSL_get0_alpn_selected(this->_crypto->ssl_handle(), name, len);
}

void
QUICHandshake::set_transport_parameters(std::shared_ptr<QUICTransportParameters> tp)
{
  this->_remote_transport_parameters = std::move(tp);

  const QUICTransportParametersInClientHello *tp_in_ch =
    dynamic_cast<const QUICTransportParametersInClientHello *>(this->_remote_transport_parameters.get());
  if (tp_in_ch) {
    // Version revalidation
    if (this->_version_negotiator->revalidate(tp_in_ch) != QUICVersionNegotiationStatus::REVALIDATED) {
      this->_client_qc->close(
        QUICConnectionErrorUPtr(new QUICConnectionError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::VERSION_NEGOTIATION_ERROR)));
      DebugQHS("Enter state_closed");
      SET_HANDLER(&QUICHandshake::state_closed);
      return;
    }
    DebugQHS("Version negotiation revalidated: %x", tp_in_ch->negotiated_version());
    return;
  }

  const QUICTransportParametersInEncryptedExtensions *tp_in_ee =
    dynamic_cast<const QUICTransportParametersInEncryptedExtensions *>(this->_remote_transport_parameters.get());
  if (tp_in_ee) {
    // TODO Add client side implementation
    return;
  }
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
QUICHandshake::state_read_client_hello(int event, Event *data)
{
  QUICErrorUPtr error;
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    error = this->_process_client_hello();
    break;
  }
  default:
    DebugQHS("event: %d", event);
    break;
  }

  if (error->cls != QUICErrorClass::NONE) {
    if (dynamic_cast<QUICConnectionError *>(error.get()) != nullptr) {
      this->_client_qc->close(QUICConnectionErrorUPtr(static_cast<QUICConnectionError *>(error.release())));
    } else {
      this->_client_qc->close(
        QUICConnectionErrorUPtr(new QUICConnectionError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::PROTOCOL_VIOLATION)));
    }
    DebugQHS("Enter state_closed");
    SET_HANDLER(&QUICHandshake::state_closed);
  }

  return EVENT_CONT;
}

int
QUICHandshake::state_read_client_finished(int event, Event *data)
{
  QUICErrorUPtr error = QUICErrorUPtr(new QUICNoError());
  switch (event) {
  case VC_EVENT_READ_READY:
  case VC_EVENT_READ_COMPLETE: {
    error = this->_process_client_finished();
    break;
  }
  default:
    DebugQHS("event: %d", event);
    break;
  }

  if (error->cls != QUICErrorClass::NONE) {
    if (dynamic_cast<QUICConnectionError *>(error.get()) != nullptr) {
      this->_client_qc->close(QUICConnectionErrorUPtr(static_cast<QUICConnectionError *>(error.release())));
    } else {
      this->_client_qc->close(
        QUICConnectionErrorUPtr(new QUICConnectionError(QUICErrorClass::QUIC_TRANSPORT, QUICErrorCode::PROTOCOL_VIOLATION)));
    }
    DebugQHS("Enter state_closed");
    SET_HANDLER(&QUICHandshake::state_closed);
  }

  return EVENT_CONT;
}

int
QUICHandshake::state_address_validation(int event, void *data)
{
  // TODO Address validation should be implemented for the 2nd implementation draft
  return EVENT_CONT;
}

int
QUICHandshake::state_complete(int event, void *data)
{
  DebugQHS("event: %d", event);
  DebugQHS("Got an event on complete state. Ignoring it for now.");

  return EVENT_CONT;
}

int
QUICHandshake::state_closed(int event, void *data)
{
  return EVENT_CONT;
}

void
QUICHandshake::_load_local_transport_parameters()
{
  QUICConfig::scoped_config params;

  // MUSTs
  QUICTransportParametersInEncryptedExtensions *tp = new QUICTransportParametersInEncryptedExtensions();

  tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA,
          std::unique_ptr<QUICTransportParameterValue>(
            new QUICTransportParameterValue(params->initial_max_stream_data(), sizeof(params->initial_max_stream_data()))));

  tp->add(QUICTransportParameterId::INITIAL_MAX_DATA, std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(
                                                        params->initial_max_data(), sizeof(params->initial_max_data()))));

  tp->add(QUICTransportParameterId::INITIAL_MAX_STREAM_ID,
          std::unique_ptr<QUICTransportParameterValue>(
            new QUICTransportParameterValue(params->initial_max_stream_id(), sizeof(params->initial_max_stream_id()))));

  tp->add(QUICTransportParameterId::IDLE_TIMEOUT, std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(
                                                    params->no_activity_timeout_in(), sizeof(uint16_t))));

  tp->add(QUICTransportParameterId::STATELESS_RETRY_TOKEN,
          std::unique_ptr<QUICTransportParameterValue>(new QUICTransportParameterValue(this->_token.get_u64(), 16)));

  tp->add_version(QUIC_SUPPORTED_VERSIONS[0]);
  // MAYs
  // this->_local_transport_parameters.add(QUICTransportParameterId::OMIT_CONNECTION_ID, {});
  // this->_local_transport_parameters.add(QUICTransportParameterId::MAX_PACKET_SIZE, {{0x00, 0x00}, 2});
  this->_local_transport_parameters = std::unique_ptr<QUICTransportParameters>(tp);
}

QUICErrorUPtr
QUICHandshake::_process_client_hello()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  // Complete message should fit in a packet and be able to read
  uint8_t msg[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t msg_len                       = stream_io->read_avail();
  stream_io->read(msg, msg_len);

  if (msg_len <= 0) {
    DebugQHS("No message");
    return QUICErrorUPtr(new QUICNoError());
  }

  // ----- DEBUG ----->
  I_WANNA_DUMP_THIS_BUF(msg, msg_len);
  // <----- DEBUG -----

  QUICCrypto *crypto = this->_crypto;

  uint8_t server_hello[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t server_hello_len                     = 0;
  bool result                                 = false;
  result = crypto->handshake(server_hello, server_hello_len, MAX_HANDSHAKE_MSG_LEN, msg, msg_len);

  if (result) {
    // ----- DEBUG ----->
    I_WANNA_DUMP_THIS_BUF(server_hello, static_cast<int64_t>(server_hello_len));
    // <----- DEBUG -----

    DebugQHS("Enter state_read_client_finished");
    SET_HANDLER(&QUICHandshake::state_read_client_finished);

    stream_io->write(server_hello, server_hello_len);
    stream_io->write_reenable();
    stream_io->read_reenable();

    return QUICErrorUPtr(new QUICNoError());
  } else {
    return QUICErrorUPtr(new QUICConnectionError(QUICErrorClass::CRYPTOGRAPHIC, QUICErrorCode::TLS_HANDSHAKE_FAILED));
  }
}

QUICErrorUPtr
QUICHandshake::_process_client_finished()
{
  QUICStreamIO *stream_io = this->_find_stream_io(STREAM_ID_FOR_HANDSHAKE);

  // Complete message should fit in a packet and be able to read
  uint8_t msg[UDP_MAXIMUM_PAYLOAD_SIZE] = {0};
  int64_t msg_len                       = stream_io->read_avail();
  stream_io->read(msg, msg_len);

  if (msg_len <= 0) {
    DebugQHS("No message");
    return QUICErrorUPtr(new QUICNoError());
  }

  // ----- DEBUG ----->
  I_WANNA_DUMP_THIS_BUF(msg, msg_len);
  // <----- DEBUG -----

  QUICCrypto *crypto = this->_crypto;

  uint8_t out[MAX_HANDSHAKE_MSG_LEN] = {0};
  size_t out_len                     = 0;
  bool result                        = false;
  result                             = crypto->handshake(out, out_len, MAX_HANDSHAKE_MSG_LEN, msg, msg_len);

  if (result) {
    // ----- DEBUG ----->
    I_WANNA_DUMP_THIS_BUF(out, static_cast<int64_t>(out_len));
    // <----- DEBUG -----

    ink_assert(this->is_completed());
    DebugQHS("Handshake is completed");

    DebugQHS("Enter state_complete");
    SET_HANDLER(&QUICHandshake::state_complete);
    _process_handshake_complete();

    stream_io->write(out, out_len);
    stream_io->write_reenable();
    stream_io->read_reenable();

    return QUICErrorUPtr(new QUICNoError());
  } else {
    return QUICErrorUPtr(new QUICConnectionError(QUICErrorClass::CRYPTOGRAPHIC, QUICErrorCode::TLS_HANDSHAKE_FAILED));
  }
}

QUICErrorUPtr
QUICHandshake::_process_handshake_complete()
{
  QUICCrypto *crypto = this->_crypto;
  int r              = crypto->setup_session();

  if (r) {
    DebugQHS("Keying Materials are exported");
  } else {
    DebugQHS("Failed to export Keying Materials");
  }

  return QUICErrorUPtr(new QUICNoError());
}
