/** @file
 *
 *  Callbacks for Stateless Retry
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

#include "QUICStatelessRetry.h"

#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "P_QUICNetVConnection.h"

#include "QUICGlobals.h"
#include "QUICConnection.h"

static constexpr size_t STATELESS_COOKIE_SECRET_LENGTH                 = 16;
static uint8_t stateless_cookie_secret[STATELESS_COOKIE_SECRET_LENGTH] = {0};

void
QUICStatelessRetry::init()
{
  // TODO: read cookie secret from file like SSLTicketKeyConfig
  RAND_bytes(stateless_cookie_secret, STATELESS_COOKIE_SECRET_LENGTH);
}

int
QUICStatelessRetry::generate_cookie(SSL *ssl, unsigned char *cookie, size_t *cookie_len)
{
  QUICConnection *qc = static_cast<QUICConnection *>(SSL_get_ex_data(ssl, QUIC::ssl_quic_qc_index));

  uint8_t key[INET6_ADDRPORTSTRLEN] = {0};
  size_t key_len                    = INET6_ADDRPORTSTRLEN;
  ats_ip_nptop(qc->five_tuple().source(), reinterpret_cast<char *>(key), key_len);

  unsigned int dst_len = 0;
  HMAC(EVP_sha1(), stateless_cookie_secret, STATELESS_COOKIE_SECRET_LENGTH, key, key_len, cookie, &dst_len);
  *cookie_len = dst_len;

  return 1;
}

int
QUICStatelessRetry::verify_cookie(SSL *ssl, const unsigned char *cookie, size_t cookie_len)
{
  uint8_t token[EVP_MAX_MD_SIZE];
  size_t token_len;

  if (QUICStatelessRetry::generate_cookie(ssl, token, &token_len) && cookie_len == token_len &&
      memcmp(token, cookie, cookie_len) == 0) {
    return 1;
  } else {
    return 0;
  }
}
