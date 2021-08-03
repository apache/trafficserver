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

#if TS_HAS_TLS_SESSION_TICKET

#include "P_SSLCertLookup.h"
#include "TLSSessionResumptionSupport.h"

void
ssl_session_ticket_free(void * /*parent*/, void *ptr, CRYPTO_EX_DATA * /*ad*/, int /*idx*/, long /*argl*/, void * /*argp*/)
{
  ticket_block_free(static_cast<struct ssl_ticket_key_block *>(ptr));
}

/*
 * RFC 5077. Create session ticket to resume SSL session without requiring session-specific state at the TLS server.
 * Specifically, it distributes the encrypted session-state information to the client in the form of a ticket and
 * a mechanism to present the ticket back to the server.
 * */
int
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
ssl_callback_session_ticket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, EVP_MAC_CTX *hctx,
                            int enc)
#else
ssl_callback_session_ticket(SSL *ssl, unsigned char *keyname, unsigned char *iv, EVP_CIPHER_CTX *cipher_ctx, HMAC_CTX *hctx,
                            int enc)
#endif
{
  TLSSessionResumptionSupport *srs = TLSSessionResumptionSupport::getInstance(ssl);

  if (srs) {
    return srs->processSessionTicket(ssl, keyname, iv, cipher_ctx, hctx, enc);
  } else {
    // We could implement a default behavior that would have been done if this callback was not registered, but it's not necessary
    // at the moment because TLSSessionResumptionSupport is alawys available when the callback is registerd.
    ink_assert(!"srs should be available");

    // For now, make it an error (this would cause handshake failure)
    return -1;
  }
}

#endif /* TS_HAS_TLS_SESSION_TICKET */
