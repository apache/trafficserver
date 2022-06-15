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
#include <cstring>
#include <openssl/core_names.h>

HKDF::HKDF(const char *digest)
{
  EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
  this->_kctx  = EVP_KDF_CTX_new(kdf);
  EVP_KDF_free(kdf);
  *params = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char *)digest, strlen(digest));
}

HKDF::~HKDF()
{
  EVP_KDF_CTX_free(this->_kctx);
  this->_kctx = nullptr;
}

int
HKDF::extract(uint8_t *dst, size_t *dst_len, const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len)
{
  size_t keysize;
  int mode      = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
  OSSL_PARAM *p = params + 1;
  *p++          = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (uint8_t *)ikm, ikm_len);
  *p++          = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (uint8_t *)salt, salt_len);
  *p++          = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
  *p            = OSSL_PARAM_construct_end();

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

  return 1;
}

int
HKDF::expand(uint8_t *dst, size_t *dst_len, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
             uint16_t length)
{
  int mode      = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
  OSSL_PARAM *p = params + 1;
  *p++          = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (uint8_t *)prk, prk_len);
  *p++          = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (uint8_t *)info, info_len);
  *p++          = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
  *p            = OSSL_PARAM_construct_end();
  if (EVP_KDF_derive(_kctx, dst, length, params) <= 0) {
    return -1;
  }
  *dst_len = length;
  EVP_KDF_CTX_reset(this->_kctx);

  return 1;
}
