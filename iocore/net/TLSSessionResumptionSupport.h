/** @file

  TLSSessionResumptionSupport implements common methods and members to
  support TLS Ssssion Resumption

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

#include "tscore/ink_inet.h"
#include "P_SSLCertLookup.h"
#include "P_SSLUtils.h"

class TLSSessionResumptionSupport
{
public:
  virtual ~TLSSessionResumptionSupport() = default;

  static void initialize();
  static TLSSessionResumptionSupport *getInstance(SSL *ssl);
  static void bind(SSL *ssl, TLSSessionResumptionSupport *srs);
  static void unbind(SSL *ssl);

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  int processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx,
                           int enc);
#else
  int processSessionTicket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx,
                           int enc);
#endif
  bool getSSLSessionCacheHit() const;
  bool getSSLOriginSessionCacheHit() const;
  ssl_curve_id getSSLCurveNID() const;

  SSL_SESSION *getSession(SSL *ssl, const unsigned char *id, int len, int *copy);
  std::shared_ptr<SSL_SESSION> getOriginSession(SSL *ssl, const std::string &lookup_key);

protected:
  void clear();
  virtual const IpEndpoint &_getLocalEndpoint() = 0;

private:
  static int _ex_data_index;

  bool _sslSessionCacheHit       = false;
  bool _sslOriginSessionCacheHit = false;
  int _sslCurveNID               = NID_undef;

#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
  int _setSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx);
  int _getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx);
#else
  int _setSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx);
  int _getSessionInformation(ssl_ticket_key_block *keyblock, SSL *ssl, unsigned char *keyname, unsigned char *iv,
                             EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx);
#endif

  void _setSSLSessionCacheHit(bool state);
  void _setSSLOriginSessionCacheHit(bool state);
  void _setSSLCurveNID(ssl_curve_id curve_nid);
};
