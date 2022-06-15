/** @file

  HKDF utility

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

#pragma once

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#include <openssl/cipher.h>
#else
#include <openssl/evp.h>
#endif

#ifdef OPENSSL_IS_OPENSSL3
#include <openssl/core.h>
#endif

class HKDF
{
public:
  HKDF(const char *digest);
  ~HKDF();
  int extract(uint8_t *dst, size_t *dst_len, const uint8_t *salt, size_t salt_len, const uint8_t *ikm, size_t ikm_len);
  int expand(uint8_t *dst, size_t *dst_len, const uint8_t *prk, size_t prk_len, const uint8_t *info, size_t info_len,
             uint16_t length);

protected:
#ifdef OPENSSL_IS_OPENSSL3
  EVP_KDF_CTX *_kctx = nullptr;
  OSSL_PARAM params[5];
#else
  EVP_PKEY_CTX *_pctx      = nullptr;
  const EVP_MD *_digest_md = nullptr;
#endif
};
