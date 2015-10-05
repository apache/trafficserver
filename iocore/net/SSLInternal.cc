/** @file

  Functions that break the no internal pact with openssl.  We
  explicitly undefine OPENSSL_NO_SSL_INTERN in this file.

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
#include <ink_config.h>
#if TS_USE_SET_RBIO
// No need to do anything, this version of openssl provides the SSL_set_rbio function
#else

#ifdef OPENSSL_NO_SSL_INTERN
#undef OPENSSL_NO_SSL_INTERN
#endif

#include <openssl/ssl.h>
#include "P_Net.h"
#include "P_SSLNetVConnection.h"

void
SSL_set_rbio(SSL *ssl, BIO *rbio)
{
  if (ssl->rbio != NULL) {
    BIO_free(ssl->rbio);
  }
  ssl->rbio = rbio;
}

unsigned char *
SSL_get_session_ticket(SSLNetVConnection *sslvc)
{
  /* this routine will no longer have a reason-to-be when openssl implements and
   * releases SSL_SESSION_get_ticket() */
  /* https://www.openssl.org/docs/ssl/SSL_SESSION_get_ticket.htm */

  SSL_SESSION *sess;

  if (sslvc && sslvc->ssl) {
    if ((sess = SSL_get_session(sslvc->ssl))) {
      return (unsigned char *)sess->tlsext_tick;
    }
  }

  return (unsigned char *)0;
}

size_t
SSL_get_session_ticket_length(SSLNetVConnection *sslvc)
{
  SSL_SESSION *sess;

  if (sslvc && sslvc->ssl) {
    if ((sess = SSL_get_session(sslvc->ssl))) {
      return sess->tlsext_ticklen;
    }
  }

  return (size_t)0;
}

long
SSL_get_session_ticket_lifetime_hint(SSLNetVConnection *sslvc)
{
  /* this routine will no longer have a reason-to-be when openssl implements and
   * releases SSL_SESSION_get_ticket_lifetime_hint() */
  /* https://www.openssl.org/docs/ssl/SSL_SESSION_get_ticket_lifetime_hint.html */

  SSL_SESSION *sess;

  if (sslvc && sslvc->ssl) {
    if ((sess = SSL_get_session(sslvc->ssl))) {
      return sess->tlsext_tick_lifetime_hint;
    }
  }

  return (long)0;
}


#endif
