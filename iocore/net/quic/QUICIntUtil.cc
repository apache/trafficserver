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

#include "QUICIntUtil.h"
#include "tscore/ink_endian.h"
#include <memory>
#include <cstring>

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
  QUICIntUtil::write_uint_as_nbytes(src, len, dst, &dummy);
  dst[0] |= (flag << 6);

  return 0;
}

int
QUICVariableInt::decode(uint64_t &dst, size_t &len, const uint8_t *src, size_t src_len)
{
  if (src_len < 1) {
    return -1;
  }
  len = 1 << (src[0] >> 6);
  if (src_len < len) {
    return 1;
  }

  uint8_t buf[8] = {0};
  memcpy(buf, src, len);
  buf[0] &= 0x3f;

  dst = QUICIntUtil::read_nbytes_as_uint(buf, len);

  return 0;
}

uint64_t
QUICIntUtil::read_QUICVariableInt(const uint8_t *buf)
{
  uint64_t dst = 0;
  size_t len   = 0;
  QUICVariableInt::decode(dst, len, buf, 8);
  return dst;
}

void
QUICIntUtil::write_QUICVariableInt(uint64_t data, uint8_t *buf, size_t *len)
{
  QUICVariableInt::encode(buf, 8, *len, data);
}

uint64_t
QUICIntUtil::read_nbytes_as_uint(const uint8_t *buf, uint8_t n)
{
  uint64_t value = 0;
  memcpy(&value, buf, n);
  return be64toh(value << (64 - n * 8));
}

void
QUICIntUtil::write_uint_as_nbytes(uint64_t value, uint8_t n, uint8_t *buf, size_t *len)
{
  value = htobe64(value) >> (64 - n * 8);
  memcpy(buf, reinterpret_cast<uint8_t *>(&value), n);
  *len = n;
}
