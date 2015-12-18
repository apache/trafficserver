/** @file

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

#ifndef __P_OCSPSTAPLING_H__
#define __P_OCSPSTAPLING_H__

#include <openssl/ssl.h>

// TODO: This should be moved to autoconf
#ifdef sk_OPENSSL_STRING_pop
#ifdef SSL_CTX_set_tlsext_status_cb
#define HAVE_OPENSSL_OCSP_STAPLING 1
void ssl_stapling_ex_init();
bool ssl_stapling_init_cert(SSL_CTX *ctx, X509 *cert, const char *certname);
void ocsp_update();
int ssl_callback_ocsp_stapling(SSL *);
#endif /* SSL_CTX_set_tlsext_status_cb */
#endif /* sk_OPENSSL_STRING_pop */

#endif /* __P_OCSPSTAPLING_H__ */
