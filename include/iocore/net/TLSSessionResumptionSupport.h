/** @file

  TLSSessionResumptionSupport implements common methods and members to
  support TLS Ssssion Resumption, either via server session caching or
  TLS session tickets.

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

#include <openssl/ssl.h>
#include <string_view>

#include "tscore/ink_inet.h"
#include "iocore/net/SSLTypes.h"

struct ssl_ticket_key_block;

class TLSSessionResumptionSupport
{
public:
  virtual ~TLSSessionResumptionSupport() = default;

  // ---------------------------------------------------------------------------
  // Binding of the TLSSessionResumptionSupport object to the SSL object
  // ---------------------------------------------------------------------------

  static void                         initialize();
  static TLSSessionResumptionSupport *getInstance(SSL *ssl);
  static void                         bind(SSL *ssl, TLSSessionResumptionSupport *srs);
  static void                         unbind(SSL *ssl);

  // ---------------------------------------------------------------------------
  // TLS Session Resumption Support Via Session Tickets
  // ---------------------------------------------------------------------------

  /** Handles TLS session ticket processing for session resumption.
   *
   * This function is called by OpenSSL to either encrypt (create) or decrypt (resume) a session ticket,
   * depending on the value of the @p enc parameter. It selects the appropriate ticket key block based on
   * the local endpoint and certificate context, and then either generates a new session ticket or attempts
   * to decrypt and validate an existing one.
   *
   * @param[in]  ssl         The SSL connection object.
   * @param[out] keyname     Buffer for the session ticket key name.
   * @param[out] iv          Buffer for the initialization vector.
   * @param[in,out] cipher_ctx  Cipher context for encryption/decryption.
   * @param[in,out] hctx        HMAC or MAC context for integrity protection.
   * @param[in]  enc         Indicates operation: 1 for encrypt (create ticket), 0 for decrypt (resume session).
   * @return            1 on success, 0 if key not found, negative value on error, or 2 if ticket should be renewed.
   */
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  int processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx,
                           int enc);
#else
  int processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx,
                           int enc);
#endif

  // ---------------------------------------------------------------------------
  // TLS Session Resumption Support Via Server Session Caching
  // ---------------------------------------------------------------------------

  /** Retrieves a cached SSL session from the session cache.
   *
   * This function is used to retrieve a cached SSL session from the session cache.
   *
   * @param[in]  ssl         The SSL connection object.
   * @param[in]  id          The session ID to lookup.
   * @param[in]  len         The length of the session ID.
   * @param[out] copy        Pointer to an integer indicating if the session ID should be copied.
   * @return                A pointer to the cached SSL session, or nullptr if not found.
   */
  SSL_SESSION *getSession(SSL *ssl, const unsigned char *id, int len, int *copy);

  /**
   * @brief Retrieves a cached SSL session from the origin session cache.
   *
   * This function is used to retrieve a cached SSL session from the origin session cache.
   *
   * @param[in]  lookup_key  The key to lookup the session in the cache.
   * @return                A pointer to the cached SSL session, or nullptr if not found.
   */
  std::shared_ptr<SSL_SESSION> getOriginSession(const std::string &lookup_key);

  // ---------------------------------------------------------------------------
  // Getters used for both ticket and session caching
  // ---------------------------------------------------------------------------

  bool             getIsResumedSSLSession() const;
  bool             getIsResumedOriginSSLSession() const;
  bool             getIsResumedFromSessionCache() const;
  bool             getIsResumedFromSessionTicket() const;
  ssl_curve_id     getSSLCurveNID() const;
  std::string_view getSSLGroupName() const;

protected:
  void                      clear();
  virtual const IpEndpoint &_getLocalEndpoint() = 0;

private:
  enum class ResumptionType {
    NOT_RESUMED,
    RESUMED_FROM_SESSION_CACHE,
    RESUMED_FROM_SESSION_TICKET,
  };

  static int _ex_data_index;

  ResumptionType _resumptionType         = ResumptionType::NOT_RESUMED;
  bool           _isResumedOriginSession = false;
  int            _sslCurveNID            = NID_undef;
  std::string    _sslGroupName;

private:
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  int _setSessionInformation(ssl_ticket_key_block *keyblock, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                             EVP_MAC_CTX *hctx);
  int _getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx);
#else
  int _setSessionInformation(ssl_ticket_key_block *keyblock, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx,
                             HMAC_CTX *hctx);
  int _getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx);
#endif

  constexpr static bool IS_RESUMED_ORIGIN_SESSION = true;
  void                  _setResumptionType(ResumptionType type, bool isOrigin);
  void                  _setSSLCurveNID(ssl_curve_id curve_nid);
  void                  _setSSLGroupName(std::string_view group_name);
};
