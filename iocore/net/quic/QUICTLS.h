/** @file
 *
 *  QUIC TLS
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

#pragma once

#include <openssl/ssl.h>

#ifdef OPENSSL_IS_BORINGSSL
#include <openssl/digest.h>
#include <openssl/cipher.h>
#else
#include <openssl/evp.h>
#endif

#include "I_EventSystem.h"
#include "I_NetVConnection.h"
#include "QUICHandshakeProtocol.h"

class QUICTLS : public QUICHandshakeProtocol
{
public:
  QUICTLS(SSL *ssl, NetVConnectionContext_t nvc_ctx);
  QUICTLS(SSL *ssl, NetVConnectionContext_t nvc_ctx, bool stateless);
  ~QUICTLS();

  static QUICEncryptionLevel get_encryption_level(int msg_type);

  int handshake(QUICHandshakeMsgs *out, const QUICHandshakeMsgs *in) override;
  bool is_handshake_finished() const override;
  bool is_ready_to_derive() const override;
  bool is_key_derived(QUICKeyPhase key_phase) const override;
  int initialize_key_materials(QUICConnectionId cid) override;
  int update_key_materials() override;
  void update_key_materials_on_key_cb(std::unique_ptr<KeyMaterial> km, int name);
  bool encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len, uint64_t pkt_num,
               const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const override;
  bool decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len, uint64_t pkt_num,
               const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const override;
  bool encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
                  const uint8_t *sample, QUICKeyPhase phase) const override;
  bool decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn, uint8_t protected_pn_len,
                  const uint8_t *sample, QUICKeyPhase phase) const override;

  // FIXME SSL handle should not be exported
  SSL *ssl_handle();

private:
  QUICKeyGenerator _keygen_for_client = QUICKeyGenerator(QUICKeyGenerator::Context::CLIENT);
  QUICKeyGenerator _keygen_for_server = QUICKeyGenerator(QUICKeyGenerator::Context::SERVER);
  void _gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const;
  const QUIC_EVP_CIPHER *_get_evp_aead(QUICKeyPhase phase) const;
  const QUIC_EVP_CIPHER *_get_evp_aead_for_pne(QUICKeyPhase phase) const;
  size_t _get_aead_tag_len(QUICKeyPhase phase) const;
  const KeyMaterial *_get_km(QUICKeyPhase phase, bool for_encryption) const;

  bool _encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                uint64_t pkt_num, const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const QUIC_EVP_CIPHER *aead,
                size_t tag_len) const;
  bool _decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len, uint64_t pkt_num,
                const uint8_t *ad, size_t ad_len, const KeyMaterial &km, const QUIC_EVP_CIPHER *aead, size_t tag_len) const;
  bool _encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
                   const uint8_t *sample, const KeyMaterial &km, const QUIC_EVP_CIPHER *aead) const;
  bool _decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn, uint8_t protected_pn_len,
                   const uint8_t *sample, const KeyMaterial &km, const QUIC_EVP_CIPHER *aead) const;
  SSL *_ssl                              = nullptr;
  QUICPacketProtection *_client_pp       = nullptr;
  QUICPacketProtection *_server_pp       = nullptr;
  NetVConnectionContext_t _netvc_context = NET_VCONNECTION_UNSET;
  bool _stateless                        = false;
  bool _early_data_processed             = false;
  bool _early_data                       = true;
  int _read_early_data();
  void _generate_0rtt_key();
};
