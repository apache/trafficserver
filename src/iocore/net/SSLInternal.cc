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
#include "tscore/ink_config.h"
#include <openssl/opensslv.h>

#if TS_USE_SET_RBIO && OPENSSL_VERSION_NUMBER >= 0x10100000L
// No need to do anything, this version of openssl provides the SSL_set0_rbio and SSL_CTX_up_ref.
#else

#ifdef OPENSSL_NO_SSL_INTERN
#undef OPENSSL_NO_SSL_INTERN
#endif

#include <openssl/ssl.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L
#include <atomic>

static_assert(sizeof(std::atomic_int) == sizeof(int));
static_assert(alignof(std::atomic_int) == alignof(int));

int
SSL_CTX_up_ref(SSL_CTX *ctx)
{
  int i;
  i = atomic_fetch_add_explicit(reinterpret_cast<std::atomic_int *>(&ctx->references), 1, std::memory_order::memory_order_relaxed) +
      1;
  return ((i > 1) ? 1 : 0);
}
#endif

#if !TS_USE_SET_RBIO
void
SSL_set0_rbio(SSL *ssl, BIO *rbio)
{
  if (ssl->rbio != nullptr) {
    BIO_free(ssl->rbio);
  }
  ssl->rbio = rbio;
}
#endif

#endif
