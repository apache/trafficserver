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

#include <cstdlib>
#include "tscore/Diags.h"
#include "QUICGlobals.h"
#include "QUICIntUtil.h"
#include "QUICTransportParameters.h"
#include "QUICConnection.h"
#include "QUICHandshake.h"
#include "QUICDebugNames.h"
#include "QUICTLS.h"
#include "QUICTypes.h"

static constexpr char tag[] = "quic_handshake";

static constexpr uint32_t TP_ERROR_LENGTH = 0x010000;
static constexpr uint32_t TP_ERROR_VALUE  = 0x020000;
// static constexpr uint32_t TP_ERROR_MUST_EXIST     = 0x030000;
static constexpr uint32_t TP_ERROR_MUST_NOT_EXIST = 0x040000;

QUICTransportParameters::Value::Value(const uint8_t *data, uint16_t len) : _len(len)
{
  this->_data = static_cast<uint8_t *>(ats_malloc(len));
  memcpy(this->_data, data, len);
}

QUICTransportParameters::Value::~Value()
{
  ats_free(this->_data);
  this->_data = nullptr;
}

bool
QUICTransportParameters::is_valid() const
{
  return this->_valid;
}

const uint8_t *
QUICTransportParameters::Value::data() const
{
  return this->_data;
}

uint16_t
QUICTransportParameters::Value::len() const
{
  return this->_len;
}

QUICTransportParameters::~QUICTransportParameters()
{
  for (auto p : this->_parameters) {
    delete p.second;
  }
}

void
QUICTransportParameters::_load(const uint8_t *buf, size_t len)
{
  bool has_error   = false;
  const uint8_t *p = buf;

  // Read size of parameters field
  uint16_t nbytes = (p[0] << 8) + p[1];
  p += 2;

  // Read parameters
  const uint8_t *end = p + nbytes;
  while (p < end) {
    // Read ID
    uint16_t id = 0;
    if (end - p >= 2) {
      id = (p[0] << 8) + p[1];
      p += 2;
    } else {
      has_error = true;
      break;
    }

    // Check duplication
    // An endpoint MUST treat receipt of duplicate transport parameters as a connection error of type TRANSPORT_PARAMETER_ERROR
    if (this->_parameters.find(id) != this->_parameters.end()) {
      has_error = true;
      break;
    }

    // Read length of value
    uint16_t len = 0;
    if (end - p >= 2) {
      len = (p[0] << 8) + p[1];
      p += 2;
    } else {
      has_error = true;
      break;
    }

    // Store parameter
    if (end - p >= len) {
      this->_parameters.insert(std::make_pair(id, new Value(p, len)));
      p += len;
    } else {
      has_error = true;
      break;
    }
  }

  if (has_error) {
    this->_valid = false;
    return;
  }

  // Validate parameters
  int res = this->_validate_parameters();
  if (res < 0) {
    Debug(tag, "Transport parameter is not valid (err=%d)", res);
    this->_valid = false;
  } else {
    this->_valid = true;
  }
}

int
QUICTransportParameters::_validate_parameters() const
{
  decltype(this->_parameters)::const_iterator ite;

  // MUSTs

  // MAYs
  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_DATA)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAMS_BIDI)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAMS_UNI)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::IDLE_TIMEOUT)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::MAX_PACKET_SIZE)) != this->_parameters.end()) {
    if (QUICIntUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) < 1200) {
      return -(TP_ERROR_VALUE | QUICTransportParameterId::MAX_PACKET_SIZE);
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::ACK_DELAY_EXPONENT)) != this->_parameters.end()) {
    if (QUICIntUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) > 20) {
      return -(TP_ERROR_VALUE | QUICTransportParameterId::ACK_DELAY_EXPONENT);
    }
  }

  // MAYs (initial values for the flow control on each type of stream)
  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_LOCAL)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_BIDI_REMOTE)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA_UNI)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::DISABLE_MIGRATION)) != this->_parameters.end()) {
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::MAX_ACK_DELAY)) != this->_parameters.end()) {
  }

  return 0;
}

const uint8_t *
QUICTransportParameters::getAsBytes(QUICTransportParameterId tpid, uint16_t &len) const
{
  auto p = this->_parameters.find(tpid);
  if (p != this->_parameters.end()) {
    len = p->second->len();
    return p->second->data();
  }

  len = 0;
  return nullptr;
}

uint64_t
QUICTransportParameters::getAsUInt(QUICTransportParameterId tpid) const
{
  uint64_t int_value       = 0;
  size_t int_value_len     = 0;
  uint16_t raw_value_len   = 0;
  const uint8_t *raw_value = this->getAsBytes(tpid, raw_value_len);
  if (raw_value) {
    QUICVariableInt::decode(int_value, int_value_len, raw_value, raw_value_len);
    return int_value;
  } else {
    return 0;
  }
}

bool
QUICTransportParameters::contains(QUICTransportParameterId id) const
{
  // Use std::map::contains when C++20 is supported
  auto p = this->_parameters.find(id);
  return (p != this->_parameters.end());
}

void
QUICTransportParameters::set(QUICTransportParameterId id, const uint8_t *value, uint16_t value_len)
{
  if (this->_parameters.find(id) != this->_parameters.end()) {
    this->_parameters.erase(id);
  }
  this->_parameters.insert(std::make_pair(id, new Value(value, value_len)));
}

void
QUICTransportParameters::set(QUICTransportParameterId id, uint64_t value)
{
  uint8_t v[8];
  size_t n;
  QUICIntUtil::write_QUICVariableInt(value, v, &n);
  this->set(id, v, n);
}

void
QUICTransportParameters::store(uint8_t *buf, uint16_t *len) const
{
  uint8_t *p = buf;

  // Write QUIC versions
  this->_store(p, len);
  p += *len;

  // Write parameters
  // XXX parameters_size will be written later
  uint8_t *parameters_size = p;
  p += sizeof(uint16_t);

  for (auto &it : this->_parameters) {
    // TODO Skip non-MUST parameters that have their default values
    p[0] = (it.first & 0xff00) >> 8;
    p[1] = it.first & 0xff;
    p += 2;
    p[0] = (it.second->len() & 0xff00) >> 8;
    p[1] = it.second->len() & 0xff;
    p += 2;
    memcpy(p, it.second->data(), it.second->len());
    p += it.second->len();
  }

  ptrdiff_t n = p - parameters_size - sizeof(uint16_t);

  parameters_size[0] = (n & 0xff00) >> 8;
  parameters_size[1] = n & 0xff;

  *len = (p - buf);
}

void
QUICTransportParameters::_print() const
{
  for (auto &p : this->_parameters) {
    if (p.second->len() == 0) {
      Debug(tag, "%s: (no value)", QUICDebugNames::transport_parameter_id(p.first));
    } else if (p.second->len() <= 8) {
      uint64_t int_value;
      size_t int_value_len;
      QUICVariableInt::decode(int_value, int_value_len, p.second->data(), p.second->len());
      Debug(tag, "%s: 0x%" PRIx64 " (%" PRIu64 ")", QUICDebugNames::transport_parameter_id(p.first), int_value, int_value);
    } else if (p.second->len() <= 24) {
      char hex_str[65];
      to_hex_str(hex_str, sizeof(hex_str), p.second->data(), p.second->len());
      Debug(tag, "%s: %s", QUICDebugNames::transport_parameter_id(p.first), hex_str);
    } else if (QUICTransportParameterId::PREFERRED_ADDRESS == p.first) {
      QUICPreferredAddress pref_addr(p.second->data(), p.second->len());
      char cid_hex_str[QUICConnectionId::MAX_HEX_STR_LENGTH];
      char token_hex_str[QUICStatelessResetToken::LEN * 2 + 1];
      char ep_ipv4_hex_str[512];
      char ep_ipv6_hex_str[512];
      pref_addr.cid().hex(cid_hex_str, sizeof(cid_hex_str));
      to_hex_str(token_hex_str, sizeof(token_hex_str), pref_addr.token().buf(), QUICStatelessResetToken::LEN);
      ats_ip_nptop(pref_addr.endpoint_ipv4(), ep_ipv4_hex_str, sizeof(ep_ipv4_hex_str));
      ats_ip_nptop(pref_addr.endpoint_ipv6(), ep_ipv6_hex_str, sizeof(ep_ipv6_hex_str));
      Debug(tag, "%s: Endpoint(IPv4)=%s, Endpoint(IPv6)=%s, CID=%s, Token=%s", QUICDebugNames::transport_parameter_id(p.first),
            ep_ipv4_hex_str, ep_ipv6_hex_str, cid_hex_str, token_hex_str);
    } else {
      Debug(tag, "%s: (long data)", QUICDebugNames::transport_parameter_id(p.first));
    }
  }
}

//
// QUICTransportParametersInClientHello
//

QUICTransportParametersInClientHello::QUICTransportParametersInClientHello(const uint8_t *buf, size_t len)
{
  this->_load(buf, len);
  if (is_debug_tag_set(tag)) {
    this->_print();
  }
}

void
QUICTransportParametersInClientHello::_store(uint8_t *buf, uint16_t *len) const
{
  *len = 0;
}

std::ptrdiff_t
QUICTransportParametersInClientHello::_parameters_offset(const uint8_t *) const
{
  return 4; // sizeof(QUICVersion)
}

int
QUICTransportParametersInClientHello::_validate_parameters() const
{
  int res = QUICTransportParameters::_validate_parameters();
  if (res < 0) {
    return res;
  }

  decltype(this->_parameters)::const_iterator ite;

  // MUST NOTs
  if ((ite = this->_parameters.find(QUICTransportParameterId::STATELESS_RESET_TOKEN)) != this->_parameters.end()) {
    return -(TP_ERROR_MUST_NOT_EXIST | QUICTransportParameterId::STATELESS_RESET_TOKEN);
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::PREFERRED_ADDRESS)) != this->_parameters.end()) {
    return -(TP_ERROR_MUST_NOT_EXIST | QUICTransportParameterId::PREFERRED_ADDRESS);
  }

  return 0;
}

//
// QUICTransportParametersInEncryptedExtensions
//

QUICTransportParametersInEncryptedExtensions::QUICTransportParametersInEncryptedExtensions(const uint8_t *buf, size_t len)
{
  this->_load(buf, len);
  if (is_debug_tag_set(tag)) {
    this->_print();
  }
}

void
QUICTransportParametersInEncryptedExtensions::_store(uint8_t *buf, uint16_t *len) const
{
  *len = 0;
}

void
QUICTransportParametersInEncryptedExtensions::add_version(QUICVersion version)
{
  this->_versions[this->_n_versions++] = version;
}

std::ptrdiff_t
QUICTransportParametersInEncryptedExtensions::_parameters_offset(const uint8_t *buf) const
{
  return 4 + 1 + buf[4];
}

int
QUICTransportParametersInEncryptedExtensions::_validate_parameters() const
{
  int res = QUICTransportParameters::_validate_parameters();
  if (res < 0) {
    return res;
  }

  decltype(this->_parameters)::const_iterator ite;

  // MUSTs if the server sent a Retry packet
  if ((ite = this->_parameters.find(QUICTransportParameterId::ORIGINAL_CONNECTION_ID)) != this->_parameters.end()) {
    // We cannot check the length because it's not a fixed length.
  } else {
    // TODO Need a way that checks if we received a Retry from the server
    // return -(TP_ERROR_MUST_EXIST | QUICTransportParameterId::ORIGINAL_CONNECTION_ID);
  }

  // MAYs
  if ((ite = this->_parameters.find(QUICTransportParameterId::STATELESS_RESET_TOKEN)) != this->_parameters.end()) {
    if (ite->second->len() != 16) {
      return -(TP_ERROR_LENGTH | QUICTransportParameterId::STATELESS_RESET_TOKEN);
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::PREFERRED_ADDRESS)) != this->_parameters.end()) {
    if (ite->second->len() < QUICPreferredAddress::MIN_LEN || QUICPreferredAddress::MAX_LEN < ite->second->len()) {
      return -(TP_ERROR_LENGTH | QUICTransportParameterId::PREFERRED_ADDRESS);
    }
  }

  return 0;
}

#ifndef OPENSSL_IS_BORINGSSL

static constexpr int TRANSPORT_PARAMETERS_MAXIMUM_SIZE = 65535;

//
// QUICTransportParametersHandler
//

int
QUICTransportParametersHandler::add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out, size_t *outlen,
                                    X509 *x, size_t chainidx, int *al, void *add_arg)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(s, QUIC::ssl_quic_tls_index));
  *out          = reinterpret_cast<const unsigned char *>(ats_malloc(TRANSPORT_PARAMETERS_MAXIMUM_SIZE));
  qtls->local_transport_parameters()->store(const_cast<uint8_t *>(*out), reinterpret_cast<uint16_t *>(outlen));

  return 1;
}

void
QUICTransportParametersHandler::free(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *out, void *add_arg)
{
  ats_free(const_cast<unsigned char *>(out));
}

int
QUICTransportParametersHandler::parse(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char *in, size_t inlen,
                                      X509 *x, size_t chainidx, int *al, void *parse_arg)
{
  QUICTLS *qtls = static_cast<QUICTLS *>(SSL_get_ex_data(s, QUIC::ssl_quic_tls_index));

  switch (context) {
  case SSL_EXT_CLIENT_HELLO:
    qtls->set_remote_transport_parameters(std::make_shared<QUICTransportParametersInClientHello>(in, inlen));
    break;
  case SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS:
    qtls->set_remote_transport_parameters(std::make_shared<QUICTransportParametersInEncryptedExtensions>(in, inlen));
    break;
  default:
    // Do nothing
    break;
  }

  return 1;
}
#endif
