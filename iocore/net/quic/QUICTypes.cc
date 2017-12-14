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

#include "QUICTypes.h"

ats_unique_buf
ats_unique_malloc(size_t size)
{
  return ats_unique_buf(reinterpret_cast<uint8_t *>(ats_malloc(size)), [](void *p) { ats_free(p); });
}

bool
QUICTypeUtil::hasLongHeader(const uint8_t *buf)
{
  return (buf[0] & 0x80) != 0;
}

QUICStreamType
QUICTypeUtil::detect_stream_type(QUICStreamId id)
{
  uint8_t type = (id & 0x01);
  if (type == 0) {
    return QUICStreamType::HANDSHAKE;
  } else {
    return static_cast<QUICStreamType>(type);
  }
}

QUICConnectionId
QUICTypeUtil::read_QUICConnectionId(const uint8_t *buf, uint8_t len)
{
  // Should be QUICConnectionId(read_nbytes_as_uint(buf, len));
  return static_cast<QUICPacketNumber>(read_nbytes_as_uint(buf, len));
}

QUICPacketNumber
QUICTypeUtil::read_QUICPacketNumber(const uint8_t *buf, uint8_t len)
{
  return static_cast<QUICPacketNumber>(read_nbytes_as_uint(buf, len));
}

QUICVersion
QUICTypeUtil::read_QUICVersion(const uint8_t *buf)
{
  return static_cast<QUICVersion>(read_nbytes_as_uint(buf, 4));
}

QUICStreamId
QUICTypeUtil::read_QUICStreamId(const uint8_t *buf)
{
  uint64_t stream_id = 0;
  size_t len         = 0;
  QUICVariableInt::decode(stream_id, len, buf, 8);
  return static_cast<QUICStreamId>(stream_id);
}

QUICOffset
QUICTypeUtil::read_QUICOffset(const uint8_t *buf)
{
  uint64_t offset = 0;
  size_t len      = 0;
  QUICVariableInt::decode(offset, len, buf, 8);
  return static_cast<QUICOffset>(offset);
}

QUICTransErrorCode
QUICTypeUtil::read_QUICTransErrorCode(const uint8_t *buf)
{
  return static_cast<QUICTransErrorCode>(read_nbytes_as_uint(buf, 2));
}

QUICAppErrorCode
QUICTypeUtil::read_QUICAppErrorCode(const uint8_t *buf)
{
  return static_cast<QUICAppErrorCode>(read_nbytes_as_uint(buf, 2));
}

uint64_t
QUICTypeUtil::read_QUICVariableInt(const uint8_t *buf)
{
  uint64_t dst = 0;
  size_t len   = 0;
  QUICVariableInt::decode(dst, len, buf, 8);
  return dst;
}

uint64_t
QUICTypeUtil::read_nbytes_as_uint(const uint8_t *buf, uint8_t n)
{
  uint64_t value = 0;
  memcpy(&value, buf, n);
  return be64toh(value << (64 - n * 8));
}

void
QUICTypeUtil::write_QUICConnectionId(QUICConnectionId connection_id, uint8_t n, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(connection_id), n, buf, len);
}

void
QUICTypeUtil::write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(packet_number), n, buf, len);
}

void
QUICTypeUtil::write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(version), 4, buf, len);
}

void
QUICTypeUtil::write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len)
{
  QUICVariableInt::encode(buf, 8, *len, stream_id);
}

void
QUICTypeUtil::write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len)
{
  QUICVariableInt::encode(buf, 8, *len, offset);
}

void
QUICTypeUtil::write_QUICTransErrorCode(QUICTransErrorCode error_code, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(error_code), 2, buf, len);
}

void
QUICTypeUtil::write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(error_code), 2, buf, len);
}

void
QUICTypeUtil::write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len)
{
  QUICVariableInt::encode(buf, 8, *len, max_data);
}

void
QUICTypeUtil::write_QUICVariableInt(uint64_t data, uint8_t *buf, size_t *len)
{
  QUICVariableInt::encode(buf, 8, *len, data);
}

void
QUICTypeUtil::write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len)
{
  value = htobe64(value) >> (64 - n * 8);
  memcpy(buf, reinterpret_cast<uint8_t *>(&value), n);
  *len = n;
}

void
QUICStatelessResetToken::_gen_token(uint64_t data)
{
  INK_MD5 _md5;
  static constexpr char STATELESS_RESET_TOKEN_KEY[] = "stateless_token_reset_key";
  MD5Context ctx;
  ctx.update(STATELESS_RESET_TOKEN_KEY, strlen(STATELESS_RESET_TOKEN_KEY));
  ctx.update(reinterpret_cast<void *>(&data), 8);
  ctx.finalize(_md5);

  size_t dummy;
  QUICTypeUtil::write_uint_as_nbytes(_md5.u64[0], 8, _token, &dummy);
  QUICTypeUtil::write_uint_as_nbytes(_md5.u64[1], 8, _token + 8, &dummy);
}

uint16_t
QUICError::code()
{
  return static_cast<uint16_t>(this->trans_error_code);
}

size_t
QUICVariableInt::size(const uint8_t *src)
{
  return 1 << (src[0] >> 6);
}

size_t
QUICVariableInt::size(uint64_t src)
{
  uint8_t flag = 0;
  if (src > 4611686018427387903) {
    // max usable bits is 62
    return 0;
  } else if (src > 1073741823) {
    flag = 0x03;
  } else if (src > 16383) {
    flag = 0x02;
  } else if (src > 63) {
    flag = 0x01;
  } else {
    flag = 0x00;
  }

  return 1 << flag;
}

int
QUICVariableInt::encode(uint8_t *dst, size_t dst_len, size_t &len, uint64_t src)
{
  uint8_t flag = 0;
  if (src > 4611686018427387903) {
    // max usable bits is 62
    return 1;
  } else if (src > 1073741823) {
    flag = 0x03;
  } else if (src > 16383) {
    flag = 0x02;
  } else if (src > 63) {
    flag = 0x01;
  } else {
    flag = 0x00;
  }

  len = 1 << flag;
  if (len > dst_len) {
    return 1;
  }

  size_t dummy = 0;
  QUICTypeUtil::write_uint_as_nbytes(src, len, dst, &dummy);
  dst[0] |= (flag << 6);

  return 0;
}

int
QUICVariableInt::decode(uint64_t &dst, size_t &len, const uint8_t *src, size_t src_len)
{
  len = 1 << (src[0] >> 6);
  if (src_len < len) {
    return 1;
  }

  uint8_t buf[8] = {0};
  memcpy(buf, src, len);
  buf[0] &= 0x3f;

  dst = QUICTypeUtil::read_nbytes_as_uint(buf, len);

  return 0;
}
