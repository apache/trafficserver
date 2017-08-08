#include <cstdlib>
#include "QUICTransportParameters.h"

QUICTransportParameterValue
QUICTransportParameters::get(QUICTransportParameterId tpid) const
{
  QUICTransportParameterValue value;
  const uint8_t *p = this->_buf + this->_parameters_offset();

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
  return QUICTypeUtil::read_QUICVersion(this->_buf);
}

QUICVersion
QUICTransportParametersInClientHello::initial_version() const
{
  return QUICTypeUtil::read_QUICVersion(this->_buf + sizeof(QUICVersion));
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
  *n = (this->_buf[0] << 8) + this->_buf[1];
  return this->_buf + 2;
}

void
QUICTransportParametersInEncryptedExtensions::add_version(QUICVersion version)
{
  this->_versions[this->_n_versions++] = version;
}

std::ptrdiff_t
QUICTransportParametersInEncryptedExtensions::_parameters_offset() const
{
  return 2 + 4 * ((this->_buf[0] << 8) + this->_buf[1]);
}
