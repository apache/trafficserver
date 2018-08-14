/** @file
 *
 *  QUIC Handshake Protocol (TLS to Secure QUIC)
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

#include "QUICTLS.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

#include "QUICDebugNames.h"

constexpr static char tag[] = "quic_tls";

QUICTLS::~QUICTLS()
{
  SSL_free(this->_ssl);

  delete this->_client_pp;
  delete this->_server_pp;
}

bool
QUICTLS::is_handshake_finished() const
{
  return SSL_is_init_finished(this->_ssl);
}

bool
QUICTLS::is_ready_to_derive() const
{
  if (this->_netvc_context == NET_VCONNECTION_IN) {
    return SSL_get_current_cipher(this->_ssl) != nullptr;
  } else {
    return this->is_handshake_finished();
  }
}

bool
QUICTLS::is_key_derived(QUICKeyPhase key_phase, bool for_encryption) const
{
  if (key_phase == QUICKeyPhase::ZERO_RTT) {
    return this->_client_pp->get_key(QUICKeyPhase::ZERO_RTT);
  } else {
    return this->_get_km(key_phase, for_encryption);
  }
}

int
QUICTLS::initialize_key_materials(QUICConnectionId cid)
{
  // Generate keys
  Debug(tag, "Generating %s keys", QUICDebugNames::key_phase(QUICKeyPhase::INITIAL));
  std::unique_ptr<KeyMaterial> km;

  km = this->_keygen_for_client.generate(cid);

  if (is_debug_tag_set("vv_quic_crypto")) {
    uint8_t print_buf[512];
    QUICDebug::to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "key=%s", print_buf);
    QUICDebug::to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "iv=%s", print_buf);
    QUICDebug::to_hex(print_buf, km->pn, km->pn_len);
    Debug("vv_quic_crypto", "pn=%s", print_buf);
  }
  this->_client_pp->set_key(std::move(km), QUICKeyPhase::INITIAL);

  km = this->_keygen_for_server.generate(cid);

  if (is_debug_tag_set("vv_quic_crypto")) {
    uint8_t print_buf[512];
    QUICDebug::to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "key=%s", print_buf);
    QUICDebug::to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "iv=%s", print_buf);
    QUICDebug::to_hex(print_buf, km->pn, km->pn_len);
    Debug("vv_quic_crypto", "pn=%s", print_buf);
  }
  this->_server_pp->set_key(std::move(km), QUICKeyPhase::INITIAL);

  return 1;
}

int
QUICTLS::update_key_materials()
{
  // Switch key phase
  QUICKeyPhase next_key_phase;
  switch (this->_client_pp->key_phase()) {
  case QUICKeyPhase::PHASE_0:
    next_key_phase = QUICKeyPhase::PHASE_1;
    break;
  case QUICKeyPhase::PHASE_1:
    next_key_phase = QUICKeyPhase::PHASE_0;
    break;
  default:
    Error("QUICKeyPhase value is undefined");
    ink_assert(false);
    next_key_phase = QUICKeyPhase::PHASE_0;
  }

  return 1;
}

QUICEncryptionLevel
QUICTLS::current_encryption_level() const
{
  return this->_current_level;
}

void
QUICTLS::abort_handshake()
{
  this->_state = HandshakeState::ABORTED;

  return;
}

void
QUICTLS::_update_encryption_level(QUICEncryptionLevel level)
{
  if (this->_current_level < level) {
    this->_current_level = level;
  }

  return;
}

SSL *
QUICTLS::ssl_handle()
{
  return this->_ssl;
}

bool
QUICTLS::encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                 uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  size_t tag_len        = this->_get_aead_tag_len(phase);
  const KeyMaterial *km = this->_get_km(phase, true);
  if (!km) {
    Debug(tag, "Failed to encrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead(phase);

  bool ret = _encrypt(cipher, cipher_len, max_cipher_len, plain, plain_len, pkt_num, ad, ad_len, *km, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to encrypt a packet #%" PRIu64, pkt_num);
  }
  return ret;
}

bool
QUICTLS::decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                 uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  size_t tag_len        = this->_get_aead_tag_len(phase);
  const KeyMaterial *km = this->_get_km(phase, false);
  if (!km) {
    Debug(tag, "Failed to decrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead(phase);
  bool ret = _decrypt(plain, plain_len, max_plain_len, cipher, cipher_len, pkt_num, ad, ad_len, *km, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to decrypt a packet #%" PRIu64, pkt_num);
  }
  return ret;
}

bool
QUICTLS::encrypt_pn(uint8_t *protected_pn, uint8_t &protected_pn_len, const uint8_t *unprotected_pn, uint8_t unprotected_pn_len,
                    const uint8_t *sample, QUICKeyPhase phase) const
{
  const KeyMaterial *km = this->_get_km(phase, true);
  if (!km) {
    Debug(tag, "Failed to encrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead_for_pne(phase);
  bool ret = this->_encrypt_pn(protected_pn, protected_pn_len, unprotected_pn, unprotected_pn_len, sample, *km, aead);
  if (!ret) {
    Debug(tag, "Failed to encrypt a packet number");
  }
  return ret;
}

bool
QUICTLS::decrypt_pn(uint8_t *unprotected_pn, uint8_t &unprotected_pn_len, const uint8_t *protected_pn, uint8_t protected_pn_len,
                    const uint8_t *sample, QUICKeyPhase phase) const
{
  const KeyMaterial *km = this->_get_km(phase, false);
  if (!km) {
    Debug(tag, "Failed to decrypt a packet number: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }

  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead_for_pne(phase);
  bool ret = this->_decrypt_pn(unprotected_pn, unprotected_pn_len, protected_pn, protected_pn_len, sample, *km, aead);
  if (!ret) {
    Debug(tag, "Failed to decrypt a packet number");
  }
  return ret;
}

/**
 * Example iv_len = 12
 *
 *   0                   1
 *   0 1 2 3 4 5 6 7 8 9 0 1 2  (byte)
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |           iv            |    // IV
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |0|0|0|0|    pkt num      |    // network byte order & left-padded with zeros
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *  |          nonce          |    // nonce = iv xor pkt_num
 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */
void
QUICTLS::_gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const
{
  nonce_len = iv_len;
  memcpy(nonce, iv, iv_len);

  pkt_num    = htobe64(pkt_num);
  uint8_t *p = reinterpret_cast<uint8_t *>(&pkt_num);

  for (size_t i = 0; i < 8; ++i) {
    nonce[iv_len - 8 + i] ^= p[i];
  }
}

const KeyMaterial *
QUICTLS::_get_km(QUICKeyPhase phase, bool for_encryption) const
{
  QUICPacketProtection *pp = nullptr;

  switch (this->_netvc_context) {
  case NET_VCONNECTION_IN:
    if (for_encryption) {
      pp = this->_server_pp;
    } else {
      pp = this->_client_pp;
    }
    break;
  case NET_VCONNECTION_OUT:
    if (for_encryption) {
      pp = this->_client_pp;
    } else {
      pp = this->_server_pp;
    }
    break;
  default:
    ink_assert(!"It should not happen");
    return nullptr;
  }

  return pp->get_key(phase);
}
