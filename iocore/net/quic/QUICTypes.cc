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

#include <algorithm>
#include "QUICTypes.h"
#include "QUICIntUtil.h"
#include "QUICConfig.h"

// TODO: move to somewhere in lib/ts/
static int
to_hex_str(char *dst, size_t dst_len, const uint8_t *src, size_t src_len)
{
  if (dst_len < src_len * 2 + 1) {
    return -1;
  }

  static char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  for (size_t i = 0; i < src_len; ++i) {
    *dst       = hex_digits[src[i] >> 4];
    *(dst + 1) = hex_digits[src[i] & 0xf];
    dst += 2;
  }
  *dst = '\0';

  return 0;
}

bool
QUICTypeUtil::is_supported_version(QUICVersion version)
{
  for (auto v : QUIC_SUPPORTED_VERSIONS) {
    if (v == version) {
      return true;
    }
  }
  return false;
}

QUICStreamType
QUICTypeUtil::detect_stream_type(QUICStreamId id)
{
  if (id == 0) {
    return QUICStreamType::HANDSHAKE;
  } else {
    uint8_t type = (id & 0x03);
    return static_cast<QUICStreamType>(type);
  }
}

QUICConnectionId
QUICTypeUtil::read_QUICConnectionId(const uint8_t *buf, uint8_t len)
{
  return {buf, len};
}

int
QUICTypeUtil::read_QUICPacketNumberLen(const uint8_t *buf)
{
  if (buf[0] & 0xC0) {
    return 4;
  } else if (buf[0] & 0x10) {
    return 2;
  } else {
    return 1;
  }
}

QUICPacketNumber
QUICTypeUtil::read_QUICPacketNumber(const uint8_t *buf)
{
  int encoded_length = QUICTypeUtil::read_QUICPacketNumberLen(buf);
  uint64_t pn        = QUICIntUtil::read_nbytes_as_uint(buf, encoded_length);

  // Remove length indicator
  if (encoded_length == 1) {
    pn &= 0x7F;
  } else {
    pn &= (0x3FFFFFFF >> ((4 - encoded_length) * 8));
  }

  return static_cast<QUICPacketNumber>(pn);
}

QUICVersion
QUICTypeUtil::read_QUICVersion(const uint8_t *buf)
{
  return static_cast<QUICVersion>(QUICIntUtil::read_nbytes_as_uint(buf, 4));
}

QUICStreamId
QUICTypeUtil::read_QUICStreamId(const uint8_t *buf)
{
  return static_cast<QUICStreamId>(QUICIntUtil::read_QUICVariableInt(buf));
}

QUICOffset
QUICTypeUtil::read_QUICOffset(const uint8_t *buf)
{
  return static_cast<QUICOffset>(QUICIntUtil::read_QUICVariableInt(buf));
}

QUICTransErrorCode
QUICTypeUtil::read_QUICTransErrorCode(const uint8_t *buf)
{
  return static_cast<QUICTransErrorCode>(QUICIntUtil::read_nbytes_as_uint(buf, 2));
}

QUICAppErrorCode
QUICTypeUtil::read_QUICAppErrorCode(const uint8_t *buf)
{
  return static_cast<QUICAppErrorCode>(QUICIntUtil::read_nbytes_as_uint(buf, 2));
}

uint64_t
QUICTypeUtil::read_QUICMaxData(const uint8_t *buf)
{
  return QUICIntUtil::read_QUICVariableInt(buf);
}

void
QUICTypeUtil::write_QUICConnectionId(QUICConnectionId connection_id, uint8_t *buf, size_t *len)
{
  memcpy(buf, connection_id, connection_id.length());
  *len = connection_id.length();
}

void
QUICTypeUtil::write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len)
{
  uint64_t pn = static_cast<uint64_t>(packet_number);
  if (n == 1) {
    // Do nothing
  } else if (n == 2) {
    pn |= 0x8000;
  } else if (n == 4) {
    pn |= 0xC0000000;
  } else {
    ink_assert(!"Encoded length must be 1, 2 or 4");
  }
  QUICIntUtil::write_uint_as_nbytes(static_cast<uint64_t>(pn), n, buf, len);
}

void
QUICTypeUtil::write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_uint_as_nbytes(static_cast<uint64_t>(version), 4, buf, len);
}

void
QUICTypeUtil::write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_QUICVariableInt(stream_id, buf, len);
}

void
QUICTypeUtil::write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_QUICVariableInt(offset, buf, len);
}

void
QUICTypeUtil::write_QUICTransErrorCode(QUICTransErrorCode error_code, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_uint_as_nbytes(static_cast<uint64_t>(error_code), 2, buf, len);
}

void
QUICTypeUtil::write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_uint_as_nbytes(static_cast<uint64_t>(error_code), 2, buf, len);
}

void
QUICTypeUtil::write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len)
{
  QUICIntUtil::write_QUICVariableInt(max_data, buf, len);
}

void
QUICStatelessResetToken::_gen_token(uint64_t data)
{
  CryptoHash _hash;
  static constexpr char STATELESS_RESET_TOKEN_KEY[] = "stateless_token_reset_key";
  CryptoContext ctx;
  ctx.update(STATELESS_RESET_TOKEN_KEY, strlen(STATELESS_RESET_TOKEN_KEY));
  ctx.update(reinterpret_cast<void *>(&data), 8);
  ctx.finalize(_hash);

  size_t dummy;
  QUICIntUtil::write_uint_as_nbytes(_hash.u64[0], 8, _token, &dummy);
  QUICIntUtil::write_uint_as_nbytes(_hash.u64[1], 8, _token + 8, &dummy);
}

uint16_t
QUICError::code()
{
  return static_cast<uint16_t>(this->trans_error_code);
}

//
// QUICFiveTuple
//
QUICFiveTuple::QUICFiveTuple(IpEndpoint src, IpEndpoint dst, int protocol) : _source(src), _destination(dst), _protocol(protocol)
{
  // FIXME Generate a hash code
  this->_hash_code = src.port() + dst.port() + protocol;
}
void
QUICFiveTuple::update(IpEndpoint src, IpEndpoint dst, int protocol)
{
  this->_source      = src;
  this->_destination = dst;
  this->_protocol    = protocol;

  // FIXME Generate a hash code
  this->_hash_code = src.port() + dst.port() + protocol;
}

IpEndpoint
QUICFiveTuple::source() const
{
  return this->_source;
}

IpEndpoint
QUICFiveTuple::destination() const
{
  return this->_destination;
}

int
QUICFiveTuple::protocol() const
{
  return this->_protocol;
}

//
// QUICConnectionId
//
QUICConnectionId
QUICConnectionId::ZERO()
{
  uint8_t zero[MAX_LENGTH] = {0};
  return QUICConnectionId(zero, sizeof(zero));
}

QUICConnectionId::QUICConnectionId()
{
  this->randomize();
}

QUICConnectionId::QUICConnectionId(const uint8_t *buf, uint8_t len) : _len(len)
{
  memcpy(this->_id, buf, len);
}

uint8_t
QUICConnectionId::length() const
{
  return this->_len;
}

bool
QUICConnectionId::is_zero() const
{
  for (int i = sizeof(this->_id) - 1; i >= 0; --i) {
    if (this->_id[i]) {
      return false;
    }
  }
  return true;
}

void
QUICConnectionId::randomize()
{
  std::random_device rnd;
  uint32_t x;
  for (int i = QUICConfigParams::scid_len(); i >= 0; --i) {
    if (i % 4 == 0) {
      x = rnd();
    }
    this->_id[i] = (x >> (8 * (i % 4))) & 0xFF;
  }
  this->_len = QUICConfigParams::scid_len();
}

uint64_t
QUICConnectionId::_hashcode() const
{
  return (static_cast<uint64_t>(this->_id[0]) << 56) + (static_cast<uint64_t>(this->_id[1]) << 48) +
         (static_cast<uint64_t>(this->_id[2]) << 40) + (static_cast<uint64_t>(this->_id[3]) << 32) + (this->_id[4] << 24) +
         (this->_id[5] << 16) + (this->_id[6] << 8) + this->_id[7];
}

uint32_t
QUICConnectionId::h32() const
{
  return static_cast<uint32_t>(QUICIntUtil::read_nbytes_as_uint(this->_id, 4));
}

int
QUICConnectionId::hex(char *buf, size_t len) const
{
  return to_hex_str(buf, len, this->_id, this->_len);
}

//
// QUICInvariants
//
bool
QUICInvariants::is_long_header(const uint8_t *buf)
{
  return (buf[0] & 0x80) != 0;
}

bool
QUICInvariants::is_version_negotiation(QUICVersion v)
{
  return v == 0x0;
}

bool
QUICInvariants::version(QUICVersion &dst, const uint8_t *buf, uint64_t buf_len)
{
  if (!QUICInvariants::is_long_header(buf) || buf_len < QUICInvariants::LH_CIL_OFFSET) {
    return false;
  }

  dst = QUICTypeUtil::read_QUICVersion(buf + QUICInvariants::LH_VERSION_OFFSET);

  return true;
}

bool
QUICInvariants::dcil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len)
{
  ink_assert(QUICInvariants::is_long_header(buf));

  if (buf_len < QUICInvariants::LH_CIL_OFFSET) {
    return false;
  }

  dst = buf[QUICInvariants::LH_CIL_OFFSET] >> 4;

  return true;
}

bool
QUICInvariants::scil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len)
{
  ink_assert(QUICInvariants::is_long_header(buf));

  if (buf_len < QUICInvariants::LH_CIL_OFFSET) {
    return false;
  }

  dst = buf[QUICInvariants::LH_CIL_OFFSET] & 0x0F;

  return true;
}

bool
QUICInvariants::dcid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len)
{
  uint8_t dcid_offset = 0;
  uint8_t dcid_len    = 0;

  if (QUICInvariants::is_long_header(buf)) {
    uint8_t dcil = 0;
    if (!QUICInvariants::dcil(dcil, buf, buf_len)) {
      return false;
    }

    if (dcil) {
      dcid_len = dcil + QUICInvariants::CIL_BASE;
    } else {
      dst = QUICConnectionId::ZERO();
      return true;
    }

    dcid_offset = QUICInvariants::LH_DCID_OFFSET;
  } else {
    // remote dcil is local scil
    dcid_len    = QUICConfigParams::scid_len();
    dcid_offset = QUICInvariants::SH_DCID_OFFSET;
  }

  if (dcid_offset + dcid_len > buf_len) {
    return false;
  }

  dst = QUICTypeUtil::read_QUICConnectionId(buf + dcid_offset, dcid_len);

  return true;
}

bool
QUICInvariants::scid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len)
{
  ink_assert(QUICInvariants::is_long_header(buf));

  if (buf_len < QUICInvariants::LH_CIL_OFFSET) {
    return false;
  }

  uint8_t scid_offset = QUICInvariants::LH_DCID_OFFSET;
  uint8_t scid_len    = 0;

  uint8_t dcil = 0;
  if (!QUICInvariants::dcil(dcil, buf, buf_len)) {
    return false;
  }

  if (dcil) {
    scid_offset += (dcil + QUICInvariants::CIL_BASE);
  }

  uint8_t scil = 0;
  if (!QUICInvariants::scil(scil, buf, buf_len)) {
    return false;
  }

  if (scil) {
    scid_len = scil + QUICInvariants::CIL_BASE;
  } else {
    dst = QUICConnectionId::ZERO();
    return true;
  }

  if (scid_offset + scid_len > buf_len) {
    return false;
  }

  dst = QUICTypeUtil::read_QUICConnectionId(buf + scid_offset, scid_len);

  return true;
}
