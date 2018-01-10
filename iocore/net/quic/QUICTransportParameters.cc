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
#include "ts/Diags.h"
#include "QUICGlobals.h"
#include "QUICTransportParameters.h"
#include "QUICConnection.h"
#include "QUICDebugNames.h"
#include "../P_QUICNetVConnection.h"

static constexpr int TRANSPORT_PARAMETERS_MAXIMUM_SIZE = 65535;
static constexpr char tag[]                            = "quic_handshake";

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
  const uint8_t *p = buf + this->_parameters_offset(buf);

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
  this->_valid = (this->_validate_parameters() == 0);
}

int
QUICTransportParameters::_validate_parameters() const
{
  decltype(this->_parameters)::const_iterator ite;

  // MUSTs
  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -1;
    }
  } else {
    return -1;
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_DATA)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -1;
    }
  } else {
    return -1;
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::IDLE_TIMEOUT)) != this->_parameters.end()) {
    if (ite->second->len() != 2) {
      return -1;
    }
    if (QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) > 600) {
      return -1;
    }
  } else {
    return -1;
  }

  // MAYs
  if ((ite = this->_parameters.find(QUICTransportParameterId::OMIT_CONNECTION_ID)) != this->_parameters.end()) {
    if (ite->second->len() != 0) {
      return -1;
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::MAX_PACKET_SIZE)) != this->_parameters.end()) {
    if (ite->second->len() != 2) {
      return -1;
    }
    if (QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) < 1200) {
      return -1;
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::ACK_DELAY_EXPONENT)) != this->_parameters.end()) {
    if (ite->second->len() != 1) {
      return -1;
    }
    if (QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) > 20) {
      return -1;
    }
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

uint8_t
QUICTransportParameters::getAsUInt8(QUICTransportParameterId tpid) const
{
  uint16_t len         = 0;
  const uint8_t *value = this->getAsBytes(tpid, len);
  if (value) {
    return QUICTypeUtil::read_nbytes_as_uint(value, 1);
  } else {
    return 0;
  }
}

uint16_t
QUICTransportParameters::getAsUInt16(QUICTransportParameterId tpid) const
{
  uint16_t len         = 0;
  const uint8_t *value = this->getAsBytes(tpid, len);
  if (value) {
    return QUICTypeUtil::read_nbytes_as_uint(value, 2);
  } else {
    return 0;
  }
}

uint32_t
QUICTransportParameters::getAsUInt32(QUICTransportParameterId tpid) const
{
  uint16_t len         = 0;
  const uint8_t *value = this->getAsBytes(tpid, len);
  if (value) {
    return QUICTypeUtil::read_nbytes_as_uint(value, 4);
  } else {
    return 0;
  }
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
QUICTransportParameters::set(QUICTransportParameterId id, uint16_t value)
{
  uint8_t v[2];
  size_t n;
  QUICTypeUtil::write_uint_as_nbytes(value, 2, v, &n);
  this->set(id, v, 2);
}

void
QUICTransportParameters::set(QUICTransportParameterId id, uint32_t value)
{
  uint8_t v[4];
  size_t n;
  QUICTypeUtil::write_uint_as_nbytes(value, 4, v, &n);
  this->set(id, v, 4);
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
      Debug(tag, "%s: 0x%" PRIx64 " (%" PRIu64 ")", QUICDebugNames::transport_parameter_id(p.first),
            QUICTypeUtil::read_nbytes_as_uint(p.second->data(), p.second->len()),
            QUICTypeUtil::read_nbytes_as_uint(p.second->data(), p.second->len()));
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
  this->_initial_version = QUICTypeUtil::read_QUICVersion(buf);
  this->_load(buf, len);
  this->_print();
}

void
QUICTransportParametersInClientHello::_store(uint8_t *buf, uint16_t *len) const
{
  size_t l;
  *len = 0;
  QUICTypeUtil::write_QUICVersion(this->_initial_version, buf, &l);
  *len += l;
}

std::ptrdiff_t
QUICTransportParametersInClientHello::_parameters_offset(const uint8_t *) const
{
  return 4; // sizeof(QUICVersion)
}

int
QUICTransportParametersInClientHello::_validate_parameters() const
{
  if (QUICTransportParameters::_validate_parameters() < 0) {
    return 0;
  }

  decltype(this->_parameters)::const_iterator ite;

  // MUST NOTs
  if ((ite = this->_parameters.find(QUICTransportParameterId::STATELESS_RESET_TOKEN)) != this->_parameters.end()) {
    return -1;
  }

  // MAYs
  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -2;
    }
    if ((QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) & 0x03) != 1) {
      return -3;
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -4;
    }
    if ((QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) & 0x03) != 3) {
      return -5;
    }
  }

  return 0;
}

QUICVersion
QUICTransportParametersInClientHello::initial_version() const
{
  return this->_initial_version;
}

//
// QUICTransportParametersInEncryptedExtensions
//

QUICTransportParametersInEncryptedExtensions::QUICTransportParametersInEncryptedExtensions(const uint8_t *buf, size_t len)
{
  this->_negotiated_version = QUICTypeUtil::read_QUICVersion(buf);
  this->_n_versions         = buf[4] / 4;
  for (int i = 0; i < this->_n_versions; ++i) {
    this->_versions[i] = QUICTypeUtil::read_QUICVersion(buf + 5 + (i * 4));
  }
  this->_load(buf, len);
  this->_print();
}

void
QUICTransportParametersInEncryptedExtensions::_store(uint8_t *buf, uint16_t *len) const
{
  uint8_t *p = buf;
  size_t l;

  // negotiated_version
  QUICTypeUtil::write_QUICVersion(this->_negotiated_version, buf, &l);
  p += l;

  // supported_versions
  p[0] = this->_n_versions * sizeof(uint32_t);
  ++p;
  for (int i = 0; i < this->_n_versions; ++i) {
    QUICTypeUtil::write_QUICVersion(this->_versions[i], p, &l);
    p += l;
  }
  *len = p - buf;
}

QUICVersion
QUICTransportParametersInEncryptedExtensions::negotiated_version() const
{
  return this->_negotiated_version;
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
  if (QUICTransportParameters::_validate_parameters() < 0) {
    return 0;
  }

  decltype(this->_parameters)::const_iterator ite;

  // MUSTs
  if ((ite = this->_parameters.find(QUICTransportParameterId::STATELESS_RESET_TOKEN)) != this->_parameters.end()) {
    if (ite->second->len() != 2) {
      return -1;
    }
  } else {
    return -2;
  }

  // MAYs
  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_BIDI)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -3;
    }
    if ((QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) & 0x03) != 1) {
      return -4;
    }
  }

  if ((ite = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_ID_UNI)) != this->_parameters.end()) {
    if (ite->second->len() != 4) {
      return -5;
    }
    if ((QUICTypeUtil::read_nbytes_as_uint(ite->second->data(), ite->second->len()) & 0x03) != 3) {
      return -6;
    }
  }

  return 0;
}

//
// QUICTransportParametersHandler
//

int
QUICTransportParametersHandler::add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out, size_t *outlen,
                                    X509 *x, size_t chainidx, int *al, void *add_arg)
{
  QUICHandshake *hs = static_cast<QUICHandshake *>(SSL_get_ex_data(s, QUIC::ssl_quic_hs_index));
  *out              = reinterpret_cast<const unsigned char *>(ats_malloc(TRANSPORT_PARAMETERS_MAXIMUM_SIZE));
  hs->local_transport_parameters()->store(const_cast<uint8_t *>(*out), reinterpret_cast<uint16_t *>(outlen));

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
  QUICHandshake *hs = static_cast<QUICHandshake *>(SSL_get_ex_data(s, QUIC::ssl_quic_hs_index));

  switch (context) {
  case SSL_EXT_CLIENT_HELLO: {
    hs->set_transport_parameters(std::make_shared<QUICTransportParametersInClientHello>(in, inlen));
    break;
  }
  case SSL_EXT_TLS1_3_ENCRYPTED_EXTENSIONS: {
    hs->set_transport_parameters(std::make_shared<QUICTransportParametersInEncryptedExtensions>(in, inlen));
    break;
  }
  default:
    // Do nothing
    break;
  }

  return 1;
}
