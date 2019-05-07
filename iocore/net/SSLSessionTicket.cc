/** @file

  SessionTicket TLS Extension

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

#include "SSLSessionTicket.h"

#if TS_HAVE_OPENSSL_SESSION_TICKETS

#include "tscore/MatcherUtils.h"

#include "P_Net.h"
#include "SSLStats.h"
#include "P_SSLConfig.h"

// Remove this when drop OpenSSL 1.0.2 support
#ifndef evp_md_func
#ifdef OPENSSL_NO_SHA256
#define evp_md_func EVP_sha1()
#else
#define evp_md_func EVP_sha256()
#endif
#endif

void
ssl_session_ticket_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  ticket_block_free((struct ssl_ticket_key_block *)ptr);
}

/*
 * RFC 5077. Create session ticket to resume SSL session without requiring session-specific state at the TLS server.
 * Specifically, it distributes the encrypted session-state information to the client in the form of a ticket and
 * a mechanism to present the ticket back to the server.
 * */
int
ssl_callback_session_ticket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx,
                            int enc)
{
  SSLCertificateConfig::scoped_config lookup;
  SSLTicketKeyConfig::scoped_config params;
  SSLNetVConnection &netvc = *SSLNetVCAccess(ssl);

  // Get the IP address to look up the keyblock
  IpEndpoint ip;
  int namelen        = sizeof(ip);
  SSLCertContext *cc = nullptr;
  if (0 == safe_getsockname(netvc.get_socket(), &ip.sa, &namelen)) {
    cc = lookup->find(ip);
  }
  ssl_ticket_key_block *keyblock = nullptr;
  if (cc == nullptr || cc->keyblock == nullptr) {
    // Try the default
    keyblock = params->default_global_keyblock;
  } else {
    keyblock = cc->keyblock;
  }
  ink_release_assert(keyblock != nullptr && keyblock->num_keys > 0);

  if (enc == 1) {
    const ssl_ticket_key_t &most_recent_key = keyblock->keys[0];
    memcpy(keyname, most_recent_key.key_name, sizeof(most_recent_key.key_name));
    RAND_bytes(iv, EVP_MAX_IV_LENGTH);
    EVP_EncryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, most_recent_key.aes_key, iv);
    HMAC_Init_ex(hctx, most_recent_key.hmac_secret, sizeof(most_recent_key.hmac_secret), evp_md_func, nullptr);

    Debug("ssl", "create ticket for a new session.");
    SSL_INCREMENT_DYN_STAT(ssl_total_tickets_created_stat);
    return 1;
  } else if (enc == 0) {
    for (unsigned i = 0; i < keyblock->num_keys; ++i) {
      if (memcmp(keyname, keyblock->keys[i].key_name, sizeof(keyblock->keys[i].key_name)) == 0) {
        EVP_DecryptInit_ex(cipher_ctx, EVP_aes_128_cbc(), nullptr, keyblock->keys[i].aes_key, iv);
        HMAC_Init_ex(hctx, keyblock->keys[i].hmac_secret, sizeof(keyblock->keys[i].hmac_secret), evp_md_func, nullptr);

        Debug("ssl", "verify the ticket for an existing session.");
        // Increase the total number of decrypted tickets.
        SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_stat);

        if (i != 0) { // The number of tickets decrypted with "older" keys.
          SSL_INCREMENT_DYN_STAT(ssl_total_tickets_verified_old_key_stat);
        }

        netvc.setSSLSessionCacheHit(true);
        // When we decrypt with an "older" key, encrypt the ticket again with the most recent key.
        return (i == 0) ? 1 : 2;
      }
    }

    Debug("ssl", "keyname is not consistent.");
    SSL_INCREMENT_DYN_STAT(ssl_total_tickets_not_found_stat);
    return 0;
  }

  return -1;
}

#endif /* TS_HAVE_OPENSSL_SESSION_TICKETS */
