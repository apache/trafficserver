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
#include "HKDF.h"
#include <cstdio>
#include <cstring>

int
HKDF::expand_label(uint8_t *dst, size_t *dst_len, const uint8_t *secret, size_t secret_len, const char *label, size_t label_len,
                   const char *hash_value, size_t hash_value_len, uint16_t length)
{
  // Create HKDF label
  uint8_t hkdf_label[512]; // 2 + 255 + 255
  int hkdf_label_len = 0;
  // Length
  hkdf_label[0] = (length >> 8) & 0xFF;
  hkdf_label[1] = length & 0xFF;
  hkdf_label_len += 2;
  // "tls13 " + Label
  hkdf_label_len += sprintf(reinterpret_cast<char *>(hkdf_label + hkdf_label_len), "%ctls13 %.*s", static_cast<int>(6 + label_len), static_cast<int>(label_len), label);
  // Hash Value
  hkdf_label_len += sprintf(reinterpret_cast<char *>(hkdf_label + hkdf_label_len), "%c%.*s", static_cast<int>(hash_value_len), static_cast<int>(hash_value_len), hash_value);

  this->expand(dst, dst_len, secret, secret_len, hkdf_label, hkdf_label_len, length);
  return 1;
}
