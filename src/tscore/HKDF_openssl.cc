/** @file
 *
 *  HKDF utility (OpenSSL version)
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
#include <openssl/kdf.h>

HKDF::HKDF(const char *digest)
{
  this->_digest_md = EVP_get_digestbyname(digest);
  // XXX We cannot reuse pctx now due to a bug in OpenSSL
  // this->_pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
}

HKDF::~HKDF()
{
  EVP_PKEY_CTX_free(this->_pctx);
  this->_pctx = nullptr;
}

int
HKDF::extract(uint8_t *dst, size_t *dst_len, const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len)
{
  // XXX See comments in the constructor
  if (this->_pctx) {
    EVP_PKEY_CTX_free(this->_pctx);
    this->_pctx = nullptr;
  }
  this->_pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);

  if (EVP_PKEY_derive_init(this->_pctx) != 1) {
    return -1;
  }
  if (EVP_PKEY_CTX_hkdf_mode(this->_pctx, EVP_PKEY_HKDEF_MODE_EXTRACT_ONLY) != 1) {
    return -2;
  }
  if (EVP_PKEY_CTX_set_hkdf_md(this->_pctx, this->_digest_md) != 1) {
    return -3;
  }
  if (EVP_PKEY_CTX_set1_hkdf_salt(this->_pctx, salt, salt_len) != 1) {
    return -4;
  }
  if (EVP_PKEY_CTX_set1_hkdf_key(this->_pctx, ikm, ikm_len) != 1) {
    return -5;
  }
  if (EVP_PKEY_derive(this->_pctx, dst, dst_len) != 1) {
    return -6;
  }

  return 1;
}

int
HKDF::expand(uint8_t *dst, size_t *dst_len, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
             uint16_t length)
{
  // XXX See comments in the constructor
  if (this->_pctx) {
    EVP_PKEY_CTX_free(this->_pctx);
    this->_pctx = nullptr;
  }
  this->_pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);

  if (EVP_PKEY_derive_init(this->_pctx) != 1) {
    return -1;
  }
  if (EVP_PKEY_CTX_hkdf_mode(this->_pctx, EVP_PKEY_HKDEF_MODE_EXPAND_ONLY) != 1) {
    return -2;
  }
  if (EVP_PKEY_CTX_set_hkdf_md(this->_pctx, this->_digest_md) != 1) {
    return -3;
  }
  if (EVP_PKEY_CTX_set1_hkdf_key(this->_pctx, prk, prk_len) != 1) {
    return -5;
  }
  if (EVP_PKEY_CTX_add1_hkdf_info(this->_pctx, info, info_len) != 1) {
    return -6;
  }
  *dst_len = length;
  if (EVP_PKEY_derive(this->_pctx, dst, dst_len) != 1) {
    return -7;
  }

  return 1;
}
