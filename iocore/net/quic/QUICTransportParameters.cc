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
#include "QUICGlobals.h"
#include "QUICTransportParameters.h"
#include "QUICConnection.h"
#include "../P_QUICNetVConnection.h"

static constexpr int TRANSPORT_PARAMETERS_MAXIMUM_SIZE = 65535;

//
// QUICTransportParameterValue
//
QUICTransportParameterValue::QUICTransportParameterValue(ats_unique_buf d, uint16_t l) : _data(std::move(d)), _len(l){};

QUICTransportParameterValue::QUICTransportParameterValue(uint64_t raw_data, uint16_t l)
{
  this->_data = ats_unique_malloc(l);
  size_t len  = 0;
  QUICTypeUtil::write_uint_as_nbytes(raw_data, l, this->_data.get(), &len);
  this->_len = len;
};

const uint8_t *
QUICTransportParameterValue::data() const
{
  return this->_data.get();
}

uint16_t
QUICTransportParameterValue::len() const
{
  return this->_len;
}

//
// QUICTransportParameters
//

QUICTransportParameters::QUICTransportParameters(const uint8_t *buf, size_t len)
{
  this->_buf = ats_unique_malloc(len);
  memcpy(this->_buf.get(), buf, len);
}

const uint8_t *
QUICTransportParameters::get(QUICTransportParameterId tpid, uint16_t &len) const
{
  if (this->_buf) {
    const uint8_t *p = this->_buf.get() + this->_parameters_offset();

    uint16_t n = (p[0] << 8) + p[1];
    p += 2;
    for (; n > 0; --n) {
      uint16_t _id = (p[0] << 8) + p[1];
      p += 2;
      uint16_t _value_len = (p[0] << 8) + p[1];
      p += 2;
      if (tpid == _id) {
        len = _value_len;
        return p;
      }
      p += _value_len;
    }
  } else {
    auto p = this->_parameters.find(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA);
    if (p != this->_parameters.end()) {
      len = p->second->len();
      return p->second->data();
    }
  }

  len = 0;
  return nullptr;
}

uint32_t
QUICTransportParameters::initial_max_stream_data() const
{
  uint16_t len        = 0;
  const uint8_t *data = this->get(QUICTransportParameterId::INITIAL_MAX_STREAM_DATA, len);

  return static_cast<uint32_t>(QUICTypeUtil::read_nbytes_as_uint(data, len));
}

uint32_t
QUICTransportParameters::initial_max_data() const
{
  uint16_t len        = 0;
  const uint8_t *data = this->get(QUICTransportParameterId::INITIAL_MAX_DATA, len);

  return static_cast<uint32_t>(QUICTypeUtil::read_nbytes_as_uint(data, len));
}

void
QUICTransportParameters::add(QUICTransportParameterId id, std::unique_ptr<QUICTransportParameterValue> value)
{
  this->_parameters.insert(std::pair<QUICTransportParameterId, std::unique_ptr<QUICTransportParameterValue>>(id, std::move(value)));
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
    const QUICTransportParameterValue *value = it.second.get();
    p[0]                                     = (value->len() & 0xff00) >> 8;
    p[1]                                     = value->len() & 0xff;
    p += 2;
    memcpy(p, value->data(), value->len());
    p += value->len();
  }

  ptrdiff_t n = p - parameters_size - sizeof(uint16_t);

  parameters_size[0] = (n & 0xff00) >> 8;
  parameters_size[1] = n & 0xff;

  *len = (p - buf);
}

//
// QUICTransportParametersInClientHello
//

void
QUICTransportParametersInClientHello::_store(uint8_t *buf, uint16_t *len) const
{
  size_t l;
  *len = 0;
  QUICTypeUtil::write_QUICVersion(this->_negotiated_version, buf, &l);
  buf += l;
  *len += l;
  QUICTypeUtil::write_QUICVersion(this->_initial_version, buf, &l);
  *len += l;
}

std::ptrdiff_t
QUICTransportParametersInClientHello::_parameters_offset() const
{
  return 8; // sizeof(QUICVersion) + sizeof(QUICVersion)
}

QUICVersion
QUICTransportParametersInClientHello::negotiated_version() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVersion(this->_buf.get());
  } else {
    return this->_negotiated_version;
  }
}

QUICVersion
QUICTransportParametersInClientHello::initial_version() const
{
  if (this->_buf) {
    return QUICTypeUtil::read_QUICVersion(this->_buf.get() + sizeof(QUICVersion));
  } else {
    return this->_initial_version;
  }
}

//
// QUICTransportParametersInEncryptedExtensions
//

void
QUICTransportParametersInEncryptedExtensions::_store(uint8_t *buf, uint16_t *len) const
{
  uint8_t *p = buf;
  size_t l;

  p[0] = this->_n_versions * sizeof(uint32_t);
  ++p;
  for (int i = 0; i < this->_n_versions; ++i) {
    QUICTypeUtil::write_QUICVersion(this->_versions[i], p, &l);
    p += l;
  }
  *len = p - buf;
}

const uint8_t *
QUICTransportParametersInEncryptedExtensions::supported_versions_len(uint16_t *n) const
{
  uint8_t *b = this->_buf.get();
  *n         = b[0];
  return b + 1;
}

void
QUICTransportParametersInEncryptedExtensions::add_version(QUICVersion version)
{
  this->_versions[this->_n_versions++] = version;
}

std::ptrdiff_t
QUICTransportParametersInEncryptedExtensions::_parameters_offset() const
{
  const uint8_t *b = this->_buf.get();
  return sizeof(uint8_t) + b[0];
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
  hs->set_transport_parameters(std::make_shared<QUICTransportParametersInClientHello>(in, inlen));

  return 1;
}
