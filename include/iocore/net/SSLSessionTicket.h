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

#include "tscore/ink_config.h"
#include <openssl/safestack.h>
#include <openssl/tls1.h>
#include <openssl/ssl.h>

#if TS_HAS_TLS_SESSION_TICKET

#include <openssl/crypto.h>
#include <openssl/hmac.h>

void ssl_session_ticket_free(void *, void *, CRYPTO_EX_DATA *, int, long, void *);
#ifdef HAVE_SSL_CTX_SET_TLSEXT_TICKET_KEY_EVP_CB
int ssl_callback_session_ticket(SSL *, unsigned char *, unsigned char *, EVP_CIPHER_CTX *, EVP_MAC_CTX *, int);
#else
int ssl_callback_session_ticket(SSL *, unsigned char *, unsigned char *, EVP_CIPHER_CTX *, HMAC_CTX *, int);
#endif

#endif /* TS_HAS_TLS_SESSION_TICKET */
