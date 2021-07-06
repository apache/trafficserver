/** @file
 *
 *  HKDF utility (BoringSSL version)
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
#include "tscore/HKDF.h"
#include <openssl/hkdf.h>

HKDF::HKDF(const EVP_MD *digest) : _digest(digest) {}
HKDF::~HKDF() {}

int
HKDF::extract(uint8_t *dst, size_t *dst_len, const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len)
{
  return HKDF_extract(dst, dst_len, this->_digest, ikm, ikm_len, salt, salt_len);
}

int
HKDF::expand(uint8_t *dst, size_t *dst_len, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
             uint16_t length)
{
  *dst_len = length;
  return HKDF_expand(dst, length, this->_digest, prk, prk_len, info, info_len);
}
