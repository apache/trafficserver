/** @file

  Common functions for HPACK and QPACK

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "XPACK.h"
#include "HuffmanCodec.h"

#include "tscore/Arena.h"
#include "tscore/ink_memory.h"
#include "tscpp/util/LocalBuffer.h"

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
xpack_decode_integer(uint64_t &dst, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n)
{
  if (buf_start >= buf_end) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p = buf_start;

  dst = (*p & ((1 << n) - 1));
  if (dst == static_cast<uint64_t>(1 << n) - 1) {
    int m = 0;
    do {
      if (++p >= buf_end) {
        return XPACK_ERROR_COMPRESSION_ERROR;
      }

      uint64_t added_value = *p & 0x7f;
      if ((UINT64_MAX >> m) < added_value) {
        // Excessively large integer encodings - in value or octet
        // length - MUST be treated as a decoding error.
        return XPACK_ERROR_COMPRESSION_ERROR;
      }
      dst += added_value << m;
      m += 7;
    } while (*p & 0x80);
  }

  return p - buf_start + 1;
}

//
// [RFC 7541] 5.2. String Literal Representation
// return content from String Data (Length octets) with huffman decoding if it is encoded
//
int64_t
xpack_decode_string(Arena &arena, char **str, uint64_t &str_length, const uint8_t *buf_start, const uint8_t *buf_end, uint8_t n)
{
  if (buf_start >= buf_end) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  const uint8_t *p            = buf_start;
  bool isHuffman              = *p & (0x01 << n);
  uint64_t encoded_string_len = 0;
  int64_t len                 = 0;

  len = xpack_decode_integer(encoded_string_len, p, buf_end, n);
  if (len == XPACK_ERROR_COMPRESSION_ERROR) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }
  p += len;

  if (buf_end < p || static_cast<uint64_t>(buf_end - p) < encoded_string_len) {
    return XPACK_ERROR_COMPRESSION_ERROR;
  }

  if (isHuffman) {
    // Allocate temporary area twice the size of before decoded data
    *str = arena.str_alloc(encoded_string_len * 2);

    len = huffman_decode(*str, p, encoded_string_len);
    if (len < 0) {
      return XPACK_ERROR_COMPRESSION_ERROR;
    }
    str_length = len;
  } else {
    *str = arena.str_alloc(encoded_string_len);

    memcpy(*str, reinterpret_cast<const char *>(p), encoded_string_len);

    str_length = encoded_string_len;
  }

  return p + encoded_string_len - buf_start;
}

//
// [RFC 7541] 5.1. Integer representation
//
int64_t
xpack_encode_integer(uint8_t *buf_start, const uint8_t *buf_end, uint64_t value, uint8_t n)
{
  if (buf_start >= buf_end) {
    return -1;
  }

  uint8_t *p = buf_start;

  // Preserve the first n bits
  uint8_t prefix = *buf_start & (0xFF << n);

  if (value < (static_cast<uint64_t>(1 << n) - 1)) {
    *(p++) = value;
  } else {
    *(p++) = (1 << n) - 1;
    value -= (1 << n) - 1;
    while (value >= 128) {
      if (p >= buf_end) {
        return -1;
      }
      *(p++) = (value & 0x7F) | 0x80;
      value  = value >> 7;
    }
    if (p + 1 >= buf_end) {
      return -1;
    }
    *(p++) = value;
  }

  // Restore the prefix
  *buf_start |= prefix;

  return p - buf_start;
}

int64_t
xpack_encode_string(uint8_t *buf_start, const uint8_t *buf_end, const char *value, uint64_t value_len, uint8_t n)
{
  uint8_t *p       = buf_start;
  bool use_huffman = true;

  ts::LocalBuffer<uint8_t, 4096> local_buffer(value_len * 4);
  uint8_t *data    = local_buffer.data();
  int64_t data_len = 0;

  // TODO Choose whether to use Huffman encoding wisely
  // cppcheck-suppress knownConditionTrueFalse; leaving "use_huffman" for wise huffman usage in the future
  if (use_huffman && value_len) {
    data_len = huffman_encode(data, reinterpret_cast<const uint8_t *>(value), value_len);
  }

  // Length
  const int64_t len = xpack_encode_integer(p, buf_end, data_len, n);
  if (len == -1) {
    return -1;
  }

  if (use_huffman) {
    *p |= 0x01 << n;
  } else {
    *p &= ~(0x01 << n);
  }
  p += len;

  if (buf_end < p || buf_end - p < data_len) {
    return -1;
  }

  // Value
  if (data_len) {
    memcpy(p, data, data_len);
    p += data_len;
  }

  return p - buf_start;
}
