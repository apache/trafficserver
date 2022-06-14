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
#ifdef OPENSSL_IS_OPENSSL3
#include <cstring>
#include <openssl/core_names.h>
#endif

HKDF::HKDF(const char *digest) : _digest(digest)
{
#ifdef OPENSSL_IS_OPENSSL3
  EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
  this->_kctx = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  *params = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char *)_digest, strlen(_digest));
#else
  this->_digest_md = EVP_get_digestbyname(_digest);
  // XXX We cannot reuse pctx now due to a bug in OpenSSL
  // this->_pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr);
#endif
}

HKDF::~HKDF()
{
#ifdef OPENSSL_IS_OPENSSL3
  EVP_KDF_CTX_free(this->_kctx);
  this->_kctx = nullptr;
#else
  EVP_PKEY_CTX_free(this->_pctx);
  this->_pctx = nullptr;
#endif
}

int
HKDF::extract(uint8_t *dst, size_t *dst_len, const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len)
{
#ifdef OPENSSL_IS_OPENSSL3
  size_t keysize;
  int mode = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
  OSSL_PARAM *p = params+1;
  *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (uint8_t *)ikm, ikm_len);
  *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (uint8_t *)salt, salt_len);
  *p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
  *p = OSSL_PARAM_construct_end();

  EVP_KDF_CTX_set_params(_kctx, params);
  keysize = EVP_KDF_CTX_get_kdf_size(this->_kctx);
  if (*dst_len < keysize) {
    return -1;
  }
  if (EVP_KDF_derive(_kctx, dst, keysize, params) <= 0) {
    EVP_KDF_CTX_reset(this->_kctx);
    return -2;
  }
  *dst_len = keysize;
  EVP_KDF_CTX_reset(this->_kctx);
#else
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
#endif

  return 1;
}

int
HKDF::expand(uint8_t *dst, size_t *dst_len, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
             uint16_t length)
{
#ifdef OPENSSL_IS_OPENSSL3
  int mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
  OSSL_PARAM *p = params+1;
  *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (uint8_t *)prk, prk_len);
  *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (uint8_t *)info, info_len);
  *p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
  *p = OSSL_PARAM_construct_end();
  if (EVP_KDF_derive(_kctx, dst, length, params) <= 0) {
    return -1;
  }
  *dst_len = length;
  EVP_KDF_CTX_reset(this->_kctx);
#else
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
#endif

  return 1;
}
