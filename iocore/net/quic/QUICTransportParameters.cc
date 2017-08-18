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

const static int TRANSPORT_PARAMETERS_MAXIMUM_SIZE = 65535;

QUICTransportParameters::QUICTransportParameters(const uint8_t *buf, size_t len)
{
  this->_buf = ats_unique_malloc(len);
  memcpy(this->_buf.get(), buf, len);
}

QUICTransportParameterValue
QUICTransportParameters::get(QUICTransportParameterId tpid) const
{
  QUICTransportParameterValue value;
  const uint8_t *p = this->_buf.get() + this->_parameters_offset();

  uint16_t n = (p[0] << 8) + p[1];
  p += 2;
  for (; n > 0; --n) {
    uint16_t _id = (p[0] << 8) + p[1];
    p += 2;
    uint16_t _value_len = (p[0] << 8) + p[1];
    p += 2;
    if (tpid == _id) {
      value.data = p;
      value.len  = _value_len;
      return value;
    }
    p += _value_len;
  }
  value.data = nullptr;
  value.len  = 0;
  return value;
}

void
QUICTransportParameters::add(QUICTransportParameterId id, QUICTransportParameterValue value)
{
  _parameters.put(id, value);
}

void
QUICTransportParameters::store(uint8_t *buf, uint16_t *len) const
{
  uint8_t *p = buf;

  // Why Map::get() doesn't have const??
  QUICTransportParameters *me = const_cast<QUICTransportParameters *>(this);

  // Write QUIC versions
  this->_store(p, len);
  p += *len;

  // Write parameters
  Vec<QUICTransportParameterId> keys;
  me->_parameters.get_keys(keys);
  unsigned int n = keys.length();
  p[0]           = (n & 0xff00) >> 8;
  p[1]           = n & 0xff;
  p += 2;
  for (unsigned int i = 0; i < n; ++i) {
    QUICTransportParameterValue value;
    p[0] = (keys[i] & 0xff00) >> 8;
    p[1] = keys[i] & 0xff;
    p += 2;
    value = me->_parameters.get(keys[i]);
    p[0]  = (value.len & 0xff00) >> 8;
    p[1]  = value.len & 0xff;
    p += 2;
    memcpy(p, value.data, value.len);
    p += value.len;
  }
  *len = (p - buf);
}

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
  return QUICTypeUtil::read_QUICVersion(this->_buf.get());
}

QUICVersion
QUICTransportParametersInClientHello::initial_version() const
{
  return QUICTypeUtil::read_QUICVersion(this->_buf.get() + sizeof(QUICVersion));
}

void
QUICTransportParametersInEncryptedExtensions::_store(uint8_t *buf, uint16_t *len) const
{
  uint8_t *p = buf;
  size_t l;

  p[0] = (this->_n_versions & 0xff00) >> 8;
  p[1] = this->_n_versions & 0xff;
  p += 2;
  for (int i = 0; i < this->_n_versions; ++i) {
    QUICTypeUtil::write_QUICVersion(this->_versions[i], p, &l);
    p += l;
  }
  *len = p - buf;
}

const uint8_t *
QUICTransportParametersInEncryptedExtensions::supported_versions(uint16_t *n) const
{
  uint8_t *b = this->_buf.get();
  *n         = (b[0] << 8) + b[1];
  return b + 2;
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
  return 2 + 4 * ((b[0] << 8) + b[1]);
}

//
// QUICTransportParametersHandler
//

int
QUICTransportParametersHandler::add(SSL *s, unsigned int ext_type, unsigned int context, const unsigned char **out, size_t *outlen,
                                    X509 *x, size_t chainidx, int *al, void *add_arg)
{
  QUICConnection *qc =
    static_cast<QUICConnection *>(static_cast<QUICNetVConnection *>(SSL_get_ex_data(s, QUIC::ssl_quic_vc_index)));
  *out = reinterpret_cast<const unsigned char *>(ats_malloc(TRANSPORT_PARAMETERS_MAXIMUM_SIZE));
  qc->local_transport_parameters().store(const_cast<uint8_t *>(*out), reinterpret_cast<uint16_t *>(outlen));

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
  QUICConnection *qc =
    static_cast<QUICConnection *>(static_cast<QUICNetVConnection *>(SSL_get_ex_data(s, QUIC::ssl_quic_vc_index)));
  QUICTransportParametersInClientHello *tp     = new QUICTransportParametersInClientHello(in, inlen);
  std::unique_ptr<QUICTransportParameters> utp = std::unique_ptr<QUICTransportParameters>(tp);
  qc->set_transport_parameters(std::move(utp));

  return 1;
}
