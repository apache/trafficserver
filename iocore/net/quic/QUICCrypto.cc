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

// constexpr static ts::StringView _exporter_label_0_rtt("EXPORTER-QUIC 0-RTT Secret", ts::StringView::literal);
constexpr static ts::string_view exporter_label_client_1_rtt("EXPORTER-QUIC client 1-RTT Secret"_sv);
constexpr static ts::string_view exporter_label_server_1_rtt("EXPORTER-QUIC server 1-RTT Secret"_sv);

// [quic-tls draft-05] "tls13 " + Label
// constexpr static ts::StringView expand_label_client_1_rtt("tls13 QUIC client 1-RTT secret", ts::StringView::literal);
// constexpr static ts::StringView expand_label_server_1_rtt("tls13 QUIC server 1-RTT secret", ts::StringView::literal);
constexpr static ts::string_view expand_label_key("tls13 key"_sv);
constexpr static ts::string_view expand_label_iv("tls13 iv"_sv);

//
// QUICPacketProtection
//

QUICPacketProtection::~QUICPacketProtection()
{
  delete this->_phase_0_key;
  delete this->_phase_1_key;
}

void
QUICPacketProtection::set_key(KeyMaterial *km, QUICKeyPhase phase)
{
  this->_key_phase = phase;
  if (phase == QUICKeyPhase::PHASE_0) {
    this->_phase_0_key = km;
  } else {
    this->_phase_1_key = km;
  }
}

const KeyMaterial *
QUICPacketProtection::get_key(QUICKeyPhase phase) const
{
  if (phase == QUICKeyPhase::PHASE_0) {
    return this->_phase_0_key;
  } else {
    return this->_phase_1_key;
  }
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
  delete this->_hkdf;
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
QUICCrypto::setup_session()
{
  const SSL_CIPHER *cipher = SSL_get_current_cipher(this->_ssl);
  this->_digest            = _get_handshake_digest(cipher);
  this->_aead              = _get_evp_aead(cipher);
  this->_hkdf              = new HKDF(this->_digest);

  size_t secret_len = EVP_MD_size(this->_digest);
  size_t key_len    = _get_aead_key_len(this->_aead);
  size_t iv_len     = std::max(static_cast<size_t>(8), _get_aead_nonce_len(this->_aead));

  int r = 0;

  r = _export_client_keymaterial(secret_len, key_len, iv_len);
  if (r != 1) {
    return r;
  }

  r = _export_server_keymaterial(secret_len, key_len, iv_len);
  if (r != 1) {
    return r;
  }

  Debug(tag, "Negotiated ciper: %s, secret_len: %zu, key_len: %zu, iv_len: %zu", SSL_CIPHER_get_name(cipher), secret_len, key_len,
        iv_len);
  return 1;
}

/**
 * update client_pp_secret_<N+1> and keying material
 */
int
QUICCrypto::update_client_keymaterial()
{
  return 0;
  // KeyMaterial *km_n   = nullptr;
  // KeyMaterial *km_n_1 = new KeyMaterial(km_n->secret_len, km_n->key_len, km_n->iv_len);
  // uint8_t secret[256] = {0};
  // int r               = 0;

  // r = _hkdf_expand_label(secret, km_n->secret_len, km_n->secret, km_n->secret_len, _expand_label_client_1_rtt,
  //                       sizeof(_expand_label_client_1_rtt), this->_digest);
  // if (r != 1) {
  //   return r;
  // }

  // r = km_n_1->init(this->_aead, this->_digest, secret);
  // if (r != 1) {
  //   return r;
  // }
  // this->_server_pp->set_key(km_n_1, new_key_phase);

  // return 1;
}

/**
 * update server_pp_secret_<N+1> and keying material
 */
int
QUICCrypto::update_server_keymaterial()
{
  return 0;
  // KeyMaterial *km_n   = nullptr;
  // KeyMaterial *km_n_1 = new KeyMaterial(km_n->secret_len, km_n->key_len, km_n->iv_len);
  // uint8_t secret[256] = {0};
  // int r               = 0;

  // r = _hkdf_expand_label(secret, km_n->secret_len, km_n->secret, km_n->secret_len, _expand_label_server_1_rtt,
  //                       sizeof(_expand_label_server_1_rtt), this->_digest);
  // if (r != 1) {
  //   return r;
  // }

  // r = km_n_1->init(this->_aead, this->_digest, secret);
  // if (r != 1) {
  //   return r;
  // }
  // this->_server_pp->set_key(km_n_1, new_key_phase);

  // return 1;
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
  const KeyMaterial *km = nullptr;

  switch (this->_netvc_context) {
  case NET_VCONNECTION_IN: {
    km = this->_server_pp->get_key(phase);
    break;
  }
  case NET_VCONNECTION_OUT: {
    km = this->_client_pp->get_key(phase);
    break;
  }
  default:
    ink_assert(false);
    return false;
  }

  size_t tag_len = _get_aead_tag_len(SSL_get_current_cipher(this->_ssl));
  return _encrypt(cipher, cipher_len, max_cipher_len, plain, plain_len, pkt_num, ad, ad_len, km->key, km->key_len, km->iv,
                  km->iv_len, tag_len);
}

bool
QUICCrypto::decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                    uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  const KeyMaterial *km = nullptr;

  switch (this->_netvc_context) {
  case NET_VCONNECTION_IN: {
    km = this->_client_pp->get_key(phase);
    break;
  }
  case NET_VCONNECTION_OUT: {
    km = this->_server_pp->get_key(phase);
    break;
  }
  default:
    ink_assert(false);
    return false;
  }

  size_t tag_len = _get_aead_tag_len(SSL_get_current_cipher(this->_ssl));
  return _decrypt(plain, plain_len, max_plain_len, cipher, cipher_len, pkt_num, ad, ad_len, km->key, km->key_len, km->iv,
                  km->iv_len, tag_len);
}

int
QUICCrypto::_export_secret(uint8_t *dst, size_t dst_len, const char *label, size_t label_len) const
{
  return SSL_export_keying_material(this->_ssl, dst, dst_len, label, label_len, reinterpret_cast<const uint8_t *>(""), 0, 1);
}

/**
 * export client_pp_secret_0 and keying material
 */
int
QUICCrypto::_export_client_keymaterial(size_t secret_len, size_t key_len, size_t iv_len)
{
  KeyMaterial *km = new KeyMaterial(secret_len, key_len, iv_len);
  int r           = 0;

  r = _export_secret(km->secret, secret_len, exporter_label_client_1_rtt.data(), exporter_label_client_1_rtt.size());
  if (r != 1) {
    Debug(tag, "Failed to export secret");
    return r;
  }

  r = this->_hkdf->expand_label(km->key, &key_len, km->secret, secret_len, expand_label_key.data(), expand_label_key.size(),
                                EVP_MD_size(this->_digest));
  if (r != 1) {
    Debug(tag, "Failed to expand label for key");
    return r;
  }

  r = this->_hkdf->expand_label(km->iv, &iv_len, km->secret, secret_len, expand_label_iv.data(), expand_label_iv.size(),
                                EVP_MD_size(this->_digest));
  if (r != 1) {
    Debug(tag, "Failed to expand label for iv");
    return r;
  }

  this->_client_pp->set_key(km, QUICKeyPhase::PHASE_0);

  return 1;
}

/**
 * export server_pp_secret_0 and keying material
 */
int
QUICCrypto::_export_server_keymaterial(size_t secret_len, size_t key_len, size_t iv_len)
{
  KeyMaterial *km = new KeyMaterial(secret_len, key_len, iv_len);
  int r           = 0;

  r = _export_secret(km->secret, secret_len, exporter_label_server_1_rtt.data(), exporter_label_server_1_rtt.size());
  if (r != 1) {
    return r;
  }

  r = this->_hkdf->expand_label(km->key, &key_len, km->secret, secret_len, expand_label_key.data(), expand_label_key.size(),
                                EVP_MD_size(this->_digest));
  if (r != 1) {
    Debug(tag, "Failed to expand label for key");
    return r;
  }

  r = this->_hkdf->expand_label(km->iv, &iv_len, km->secret, secret_len, expand_label_iv.data(), expand_label_iv.size(),
                                EVP_MD_size(this->_digest));
  if (r != 1) {
    Debug(tag, "Failed to expand label for iv");
    return r;
  }

  this->_server_pp->set_key(km, QUICKeyPhase::PHASE_0);

  return 1;
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
