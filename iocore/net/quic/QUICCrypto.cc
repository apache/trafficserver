/** @file
 *
 *  QUIC Crypto (TLS to Secure QUIC)
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
#include "QUICGlobals.h"
#include "QUICCrypto.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

#include "ts/HKDF.h"
#include "ts/Diags.h"
#include "ts/string_view.h"
#include "QUICTypes.h"

constexpr static char tag[] = "quic_crypto";

//
// QUICPacketProtection
//

QUICPacketProtection::~QUICPacketProtection()
{
}

void
QUICPacketProtection::set_key(std::unique_ptr<KeyMaterial> km, QUICKeyPhase phase)
{
  this->_key_phase = phase;
  switch (phase) {
  case QUICKeyPhase::PHASE_0:
    this->_phase_0_key = std::move(km);
    break;
  case QUICKeyPhase::PHASE_1:
    this->_phase_1_key = std::move(km);
    break;
  case QUICKeyPhase::CLEARTEXT:
    this->_cleartext_key = std::move(km);
    break;
  }
}

const KeyMaterial &
QUICPacketProtection::get_key(QUICKeyPhase phase) const
{
  switch (phase) {
  case QUICKeyPhase::PHASE_0:
    return *this->_phase_0_key;
  case QUICKeyPhase::PHASE_1:
    return *this->_phase_1_key;
  case QUICKeyPhase::CLEARTEXT:
    return *this->_cleartext_key;
  }

  ink_release_assert(!"Bad phase");
  return *this->_cleartext_key;
}

QUICKeyPhase
QUICPacketProtection::key_phase() const
{
  return this->_key_phase;
}

//
// QUICCrypto
//
QUICCrypto::QUICCrypto(SSL *ssl, NetVConnectionContext_t nvc_ctx) : _ssl(ssl), _netvc_context(nvc_ctx)
{
  if (this->_netvc_context == NET_VCONNECTION_IN) {
    SSL_set_accept_state(this->_ssl);
  } else if (this->_netvc_context == NET_VCONNECTION_OUT) {
    SSL_set_connect_state(this->_ssl);
  } else {
    ink_assert(false);
  }

  this->_client_pp = new QUICPacketProtection();
  this->_server_pp = new QUICPacketProtection();
}

QUICCrypto::~QUICCrypto()
{
  delete this->_client_pp;
  delete this->_server_pp;
}

bool
QUICCrypto::handshake(uint8_t *out, size_t &out_len, size_t max_out_len, const uint8_t *in, size_t in_len)
{
  ink_assert(this->_ssl != nullptr);

  BIO *rbio = BIO_new(BIO_s_mem());
  BIO *wbio = BIO_new(BIO_s_mem());
  if (in != nullptr || in_len != 0) {
    BIO_write(rbio, in, in_len);
  }
  SSL_set_bio(this->_ssl, rbio, wbio);

  if (!SSL_is_init_finished(this->_ssl)) {
    ERR_clear_error();
    int ret = SSL_do_handshake(this->_ssl);
    if (ret <= 0) {
      int err = SSL_get_error(this->_ssl, ret);

      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        break;
      default:
        char err_buf[32] = {0};
        ERR_error_string_n(err, err_buf, sizeof(err_buf));
        Debug(tag, "Handshake error: %s (%d)", err_buf, err);
        return false;
      }
    }
  }

  // OpenSSL doesn't have BIO_mem_contents
  // const uint8_t *buf;
  // if (!BIO_mem_contents(wbio, &buf, &out_len)) {
  //   return false;
  // }
  // if (out_len <= 0) {
  //   return false;
  // }

  out_len = BIO_read(wbio, out, max_out_len);
  if (out_len <= 0) {
    return false;
  }

  return true;
}

bool
QUICCrypto::is_handshake_finished() const
{
  return SSL_is_init_finished(this->_ssl);
}

int
QUICCrypto::initialize_key_materials(QUICConnectionId cid)
{
  // Generate keys
  std::unique_ptr<KeyMaterial> km;
  km = this->_keygen_for_client.generate(cid);
  this->_client_pp->set_key(std::move(km), QUICKeyPhase::CLEARTEXT);
  km = this->_keygen_for_server.generate(cid);
  this->_server_pp->set_key(std::move(km), QUICKeyPhase::CLEARTEXT);

  // Update algorithm
  this->_aead = _get_evp_aead();

  return 1;
}

int
QUICCrypto::update_key_materials()
{
  ink_assert(this->is_handshake_finished());
  // Switch key phase
  QUICKeyPhase next_key_phase;
  switch (this->_client_pp->key_phase()) {
  case QUICKeyPhase::PHASE_0:
    next_key_phase = QUICKeyPhase::PHASE_1;
    break;
  case QUICKeyPhase::PHASE_1:
    next_key_phase = QUICKeyPhase::PHASE_0;
    break;
  case QUICKeyPhase::CLEARTEXT:
    next_key_phase = QUICKeyPhase::PHASE_0;
    break;
  }

  // Generate keys
  std::unique_ptr<KeyMaterial> km;
  km = this->_keygen_for_client.generate(this->_ssl);
  this->_client_pp->set_key(std::move(km), next_key_phase);
  km = this->_keygen_for_server.generate(this->_ssl);
  this->_server_pp->set_key(std::move(km), next_key_phase);

  // Update algorithm
  this->_aead = _get_evp_aead();

  return 1;
}

SSL *
QUICCrypto::ssl_handle()
{
  return this->_ssl;
}

bool
QUICCrypto::encrypt(uint8_t *cipher, size_t &cipher_len, size_t max_cipher_len, const uint8_t *plain, size_t plain_len,
                    uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  QUICPacketProtection *pp = nullptr;

  switch (this->_netvc_context) {
  case NET_VCONNECTION_IN: {
    pp = this->_server_pp;
    break;
  }
  case NET_VCONNECTION_OUT: {
    pp = this->_client_pp;
    break;
  }
  default:
    ink_assert(false);
    return false;
  }

  size_t tag_len = this->_get_aead_tag_len();
  return _encrypt(cipher, cipher_len, max_cipher_len, plain, plain_len, pkt_num, ad, ad_len, pp->get_key(phase), tag_len);
}

bool
QUICCrypto::decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                    uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  QUICPacketProtection *pp = nullptr;

  switch (this->_netvc_context) {
  case NET_VCONNECTION_IN: {
    pp = this->_client_pp;
    break;
  }
  case NET_VCONNECTION_OUT: {
    pp = this->_server_pp;
    break;
  }
  default:
    ink_assert(false);
    return false;
  }

  size_t tag_len = this->_get_aead_tag_len();
  return _decrypt(plain, plain_len, max_plain_len, cipher, cipher_len, pkt_num, ad, ad_len, pp->get_key(phase), tag_len);
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
QUICCrypto::_gen_nonce(uint8_t *nonce, size_t &nonce_len, uint64_t pkt_num, const uint8_t *iv, size_t iv_len) const
{
  nonce_len = iv_len;
  memcpy(nonce, iv, iv_len);

  pkt_num    = htobe64(pkt_num);
  uint8_t *p = reinterpret_cast<uint8_t *>(&pkt_num);

  for (size_t i = 0; i < 8; ++i) {
    nonce[iv_len - 8 + i] ^= p[i];
  }
}
