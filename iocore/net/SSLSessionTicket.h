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

#pragma once

#include <openssl/safestack.h>
#include <openssl/tls1.h>
#include <openssl/ssl.h>

// Check if the ticket_key callback #define is available, and if so, enable session tickets.
#ifdef SSL_CTX_set_tlsext_ticket_key_cb
#define TS_HAVE_OPENSSL_SESSION_TICKETS 1
#endif

#ifdef TS_HAVE_OPENSSL_SESSION_TICKETS

#include <openssl/crypto.h>
#include <openssl/hmac.h>

void ssl_session_ticket_free(void *, void *, CRYPTO_EX_DATA *, int, long, void *);
int ssl_callback_session_ticket(SSL *, unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int);

#endif /* TS_HAVE_OPENSSL_SESSION_TICKETS */
