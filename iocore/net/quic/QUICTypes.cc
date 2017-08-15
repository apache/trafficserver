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

const QUICStreamId STREAM_ID_FOR_HANDSHAKE = 0;

bool
QUICTypeUtil::hasLongHeader(const uint8_t *buf)
{
  return (buf[0] & 0x80) != 0;
}

QUICConnectionId
QUICTypeUtil::read_QUICConnectionId(const uint8_t *buf, uint8_t len)
{
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
QUICTypeUtil::read_QUICStreamId(const uint8_t *buf, uint8_t len)
{
  return static_cast<QUICStreamId>(read_nbytes_as_uint(buf, len));
}

QUICOffset
QUICTypeUtil::read_QUICOffset(const uint8_t *buf, uint8_t len)
{
  return static_cast<QUICOffset>(read_nbytes_as_uint(buf, len));
}

QUICErrorCode
QUICTypeUtil::read_QUICErrorCode(const uint8_t *buf)
{
  return static_cast<QUICErrorCode>(read_nbytes_as_uint(buf, 4));
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
QUICTypeUtil::write_QUICStreamId(QUICStreamId stream_id, uint8_t n, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(stream_id), n, buf, len);
}

void
QUICTypeUtil::write_QUICOffset(QUICOffset offset, uint8_t n, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(offset), n, buf, len);
}

void
QUICTypeUtil::write_QUICErrorCode(QUICErrorCode error_code, uint8_t *buf, size_t *len)
{
  write_uint_as_nbytes(static_cast<uint64_t>(error_code), 4, buf, len);
}

void
QUICTypeUtil::write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len)
{
  value = htobe64(value) >> (64 - n * 8);
  memcpy(buf, reinterpret_cast<uint8_t *>(&value), n);
  *len = n;
}

void
fnv1a(const uint8_t *data, size_t len, uint8_t *hash)
{
  uint64_t h     = 0xcbf29ce484222325ULL;
  uint64_t prime = 0x100000001b3ULL;
  size_t n;

  for (size_t i = 0; i < len; ++i) {
    h ^= data[i];
    h *= prime;
  }
  return QUICTypeUtil::write_uint_as_nbytes(h, 8, hash, &n);
}
