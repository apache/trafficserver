/** @file
 *
 *  QUIC Handshake Protocol (OpenSSL specific code)
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

#include "QUICHandshakeProtocol.h"

bool
QUICPacketNumberProtector::_encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn,
                                       uint8_t unprotected_pn_len, const uint8_t *sample, const KeyMaterial &km,
                                       const EVP_CIPHER *aead) const
{
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len             = 0;

  if (!ctx || !EVP_EncryptInit_ex(ctx, aead, nullptr, km.pn, sample)) {
    return false;
  }
  if (!EVP_EncryptUpdate(ctx, protected_pn, &len, unprotected_pn, unprotected_pn_len)) {
    return false;
  }
  protected_pn_len = len;
  if (!EVP_EncryptFinal_ex(ctx, protected_pn + len, &len)) {
    return false;
  }
  protected_pn_len += len;
  EVP_CIPHER_CTX_free(ctx);

  return true;
}

bool
QUICPacketNumberProtector::_decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn,
                                       uint8_t protected_pn_len, const uint8_t *sample, const KeyMaterial &km,
                                       const EVP_CIPHER *aead) const
{
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  int len             = 0;

  if (!ctx || !EVP_DecryptInit_ex(ctx, aead, nullptr, km.pn, sample)) {
    return false;
  }
  if (!EVP_DecryptUpdate(ctx, unprotected_pn, &len, protected_pn, protected_pn_len)) {
    return false;
  }
  unprotected_pn_len = len;
  if (!EVP_DecryptFinal_ex(ctx, unprotected_pn, &len)) {
    return false;
  }
  unprotected_pn_len += len;
  EVP_CIPHER_CTX_free(ctx);

  return true;
}
