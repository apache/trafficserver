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
#include "QUICDebugNames.h"

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/bio.h>

constexpr static char tag[] = "quic_tls";

static void
to_hex(uint8_t *out, uint8_t *in, int in_len)
{
  for (int i = 0; i < in_len; ++i) {
    int u4         = in[i] / 16;
    int l4         = in[i] % 16;
    out[i * 2]     = (u4 < 10) ? ('0' + u4) : ('A' + u4 - 10);
    out[i * 2 + 1] = (l4 < 10) ? ('0' + l4) : ('A' + l4 - 10);
  }
  out[in_len * 2] = 0;
}

QUICTLS::QUICTLS(SSL *ssl, NetVConnectionContext_t nvc_ctx, bool stateless)
  : QUICHandshakeProtocol(), _ssl(ssl), _netvc_context(nvc_ctx), _stateless(stateless)
{
  ink_assert(this->_netvc_context != NET_VCONNECTION_UNSET);

  this->_client_pp = new QUICPacketProtection();
  this->_server_pp = new QUICPacketProtection();
}

QUICTLS::QUICTLS(SSL *ssl, NetVConnectionContext_t nvc_ctx) : QUICTLS(ssl, nvc_ctx, false) {}

QUICTLS::~QUICTLS()
{
  delete this->_client_pp;
  delete this->_server_pp;
}

int
QUICTLS::handshake(uint8_t *out, size_t &out_len, size_t max_out_len, const uint8_t *in, size_t in_len)
{
  ink_assert(this->_ssl != nullptr);

  // TODO: directly read/write from VIO
  BIO *rbio = BIO_new(BIO_s_mem());
  BIO *wbio = BIO_new(BIO_s_mem());
  if (in != nullptr || in_len != 0) {
    BIO_write(rbio, in, in_len);
  }
  SSL_set_bio(this->_ssl, rbio, wbio);

  int err = SSL_ERROR_NONE;
  if (!SSL_is_init_finished(this->_ssl)) {
    ERR_clear_error();
    int ret = 0;
    if (this->_netvc_context == NET_VCONNECTION_IN) {
      // If stateless is enabled, TS should not allow early data, these are incompatible.
      // Details in https://github.com/openssl/openssl/issues/5235
      if (this->_stateless) {
        // process stateless retry
#ifndef OPENSSL_IS_BORINGSSL
        ret = SSL_stateless(this->_ssl);
#else
        ink_assert(!"stateless retry is not available with boringssl");
#endif
        if (ret > 0) {
          this->_stateless = false;
          this->_msg_type  = QUICHandshakeMsgType::HANDSHAKE;
        } else if (ret == 0) {
          this->_msg_type = QUICHandshakeMsgType::RETRY;
        }
      } else {
        // process early data
        if (!this->_early_data_processed) {
          if (this->_read_early_data()) {
            this->_early_data_processed = true;
          }

#ifndef OPENSSL_IS_BORINGSSL
          if (SSL_get_early_data_status(this->_ssl) == SSL_EARLY_DATA_ACCEPTED) {
#else
          if (SSL_early_data_accepted(this->_ssl)) {
#endif
            Debug(tag, "Early data processed");

            if (!this->_client_pp->get_key(QUICKeyPhase::ZERORTT)) {
              this->_generate_0rtt_key();
            }
          }
        }

        ret             = SSL_accept(this->_ssl);
        this->_msg_type = QUICHandshakeMsgType::HANDSHAKE;
      }
    } else {
      ret = SSL_connect(this->_ssl);

#ifndef OPENSSL_IS_BORINGSSL
      // FIXME: if SSL_get_state work well on server side, use this for distinction of HANDSHAKE and RERTY
      if (SSL_get_state(this->_ssl) == TLS_ST_CW_CLNT_HELLO) {
        this->_msg_type = QUICHandshakeMsgType::INITIAL;
      } else {
        this->_msg_type = QUICHandshakeMsgType::HANDSHAKE;
      }
#else
      // No stateless retry support
      this->_msg_type = QUICHandshakeMsgType::INITIAL;
#endif
    }

    if (ret < 0) {
      err = SSL_get_error(this->_ssl, ret);

      switch (err) {
      case SSL_ERROR_WANT_READ:
      case SSL_ERROR_WANT_WRITE:
        break;
      default:
        char err_buf[256] = {0};
        ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
        Debug(tag, "Handshake: %s", err_buf);
        return err;
      }
    }

    out_len = BIO_ctrl_pending(wbio);
    if (out_len > 0) {
      BIO_read(wbio, out, max_out_len);
    }
  }

  return err;
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
QUICTLS::is_key_derived(QUICKeyPhase key_phase) const
{
  if (key_phase == QUICKeyPhase::ZERORTT) {
    return this->_client_pp->get_key(QUICKeyPhase::ZERORTT);
  } else {
    return this->_client_pp->get_key(key_phase) && this->_server_pp->get_key(key_phase);
  }
}

int
QUICTLS::initialize_key_materials(QUICConnectionId cid)
{
  // Generate keys
  Debug(tag, "Generating %s keys", QUICDebugNames::key_phase(QUICKeyPhase::CLEARTEXT));
  uint8_t print_buf[512];
  std::unique_ptr<KeyMaterial> km;
  km = this->_keygen_for_client.generate(cid);
  if (is_debug_tag_set("vv_quic_crypto")) {
    to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "client key 0x%s", print_buf);
    to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "client iv 0x%s", print_buf);
  }
  this->_client_pp->set_key(std::move(km), QUICKeyPhase::CLEARTEXT);

  km = this->_keygen_for_server.generate(cid);
  if (is_debug_tag_set("vv_quic_crypto")) {
    to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "server key 0x%s", print_buf);
    to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "server iv 0x%s", print_buf);
  }
  this->_server_pp->set_key(std::move(km), QUICKeyPhase::CLEARTEXT);

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
  case QUICKeyPhase::CLEARTEXT:
    next_key_phase = QUICKeyPhase::PHASE_0;
    break;
  case QUICKeyPhase::ZERORTT:
    next_key_phase = QUICKeyPhase::PHASE_0;
    break;
  default:
    Error("QUICKeyPhase value is undefined");
    ink_assert(false);
    next_key_phase = QUICKeyPhase::PHASE_0;
  }

  // Generate keys
  Debug(tag, "Generating %s keys", QUICDebugNames::key_phase(next_key_phase));
  uint8_t print_buf[512];
  std::unique_ptr<KeyMaterial> km;
  km = this->_keygen_for_client.generate(this->_ssl);
  if (is_debug_tag_set("vv_quic_crypto")) {
    to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "client key 0x%s", print_buf);
    to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "client iv 0x%s", print_buf);
  }
  this->_client_pp->set_key(std::move(km), next_key_phase);
  km = this->_keygen_for_server.generate(this->_ssl);
  if (is_debug_tag_set("vv_quic_crypto")) {
    to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "server key 0x%s", print_buf);
    to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "server iv 0x%s", print_buf);
  }
  this->_server_pp->set_key(std::move(km), next_key_phase);

  return 1;
}

int
QUICTLS::_read_early_data()
{
  uint8_t early_data[8];
  size_t early_data_len = 0;
#ifndef OPENSSL_IS_BORINGSSL
  int ret = 0;

  do {
    ERR_clear_error();
    ret = SSL_read_early_data(this->_ssl, early_data, sizeof(early_data), &early_data_len);
  } while (ret == SSL_READ_EARLY_DATA_SUCCESS);

  return ret == SSL_READ_EARLY_DATA_FINISH ? 1 : 0;
#else
  do {
    ERR_clear_error();
    early_data_len = SSL_read(this->_ssl, early_data, sizeof(early_data));
  } while (SSL_in_early_data(this->_ssl));

  return 1;
#endif
}

void
QUICTLS::_generate_0rtt_key()
{
  // Generate key material for 0-RTT
  Debug(tag, "Generating %s keys", QUICDebugNames::key_phase(QUICKeyPhase::ZERORTT));
  std::unique_ptr<KeyMaterial> km;
  km = this->_keygen_for_client.generate_0rtt(this->_ssl);
  if (is_debug_tag_set("vv_quic_crypto")) {
    uint8_t print_buf[512];
    to_hex(print_buf, km->key, km->key_len);
    Debug("vv_quic_crypto", "0rtt key 0x%s", print_buf);
    to_hex(print_buf, km->iv, km->iv_len);
    Debug("vv_quic_crypto", "0rtt iv 0x%s", print_buf);
  }
  this->_client_pp->set_key(std::move(km), QUICKeyPhase::ZERORTT);
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
  QUICPacketProtection *pp = nullptr;
  Debug(tag, "Encrypting packet using %s key", QUICDebugNames::key_phase(phase));

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

  size_t tag_len        = this->_get_aead_tag_len(phase);
  const KeyMaterial *km = pp->get_key(phase);
  if (!km) {
    Debug(tag, "Failed to encrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead(phase);

  bool ret = _encrypt(cipher, cipher_len, max_cipher_len, plain, plain_len, pkt_num, ad, ad_len, *km, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to encrypt a packet: pkt_num=%" PRIu64, pkt_num);
  }
  return ret;
}

bool
QUICTLS::decrypt(uint8_t *plain, size_t &plain_len, size_t max_plain_len, const uint8_t *cipher, size_t cipher_len,
                 uint64_t pkt_num, const uint8_t *ad, size_t ad_len, QUICKeyPhase phase) const
{
  QUICPacketProtection *pp = nullptr;
  Debug(tag, "Decrypting packet using %s key", QUICDebugNames::key_phase(phase));

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

  size_t tag_len        = this->_get_aead_tag_len(phase);
  const KeyMaterial *km = pp->get_key(phase);
  if (!km) {
    Debug(tag, "Failed to decrypt a packet: keys for %s is not ready", QUICDebugNames::key_phase(phase));
    return false;
  }
  const QUIC_EVP_CIPHER *aead = this->_get_evp_aead(phase);
  bool ret = _decrypt(plain, plain_len, max_plain_len, cipher, cipher_len, pkt_num, ad, ad_len, *km, aead, tag_len);
  if (!ret) {
    Debug(tag, "Failed to decrypt a packet: pkt_num=%" PRIu64, pkt_num);
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
