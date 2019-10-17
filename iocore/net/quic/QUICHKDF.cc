/** @file
 *
 *  HKDF utility (common part)
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
#include "QUICHKDF.h"

#include <cstdio>
#include <cstring>

#include <string_view>

/**
 * HKDF-Expand-Label function of QUIC
 * The HKDF-Expand-Label function and HkdfLabel structure is defined in TLS 1.3 Section 7.1.
 */
int
QUICHKDF::expand(uint8_t *dst, size_t *dst_len, const uint8_t *secret, size_t secret_len, const char *label, size_t label_len,
                 uint16_t length)
{
  // Create HkdfLabel
  uint8_t hkdf_label[512]; // 2 + 255 + 255
  int hkdf_label_len = 0;

  // length field
  hkdf_label[0] = (length >> 8) & 0xFF;
  hkdf_label[1] = length & 0xFF;
  hkdf_label_len += 2;

  // label (prefix + Label) field
  hkdf_label_len += sprintf(reinterpret_cast<char *>(hkdf_label + hkdf_label_len), "%ctls13 %.*s", static_cast<int>(6 + label_len),
                            static_cast<int>(label_len), label);

  // context field
  // XXX: Assuming Context is zero-length character (indicated by "")
  // HkdfLabel requires "0" (length) in context field, because `context<0..255>` is valiable-integer.
  hkdf_label[hkdf_label_len] = 0;
  ++hkdf_label_len;

  HKDF::expand(dst, dst_len, secret, secret_len, hkdf_label, hkdf_label_len, length);

  return 1;
}
