/** @file

  @par License

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

void ssl_stapling_ex_init();
bool ssl_stapling_init_cert(SSL_CTX *ctx, X509 *cert, const char *certname, const char *rsp_file);
// Stops stapling for the certificates of @a ctx that a certificate just loaded onto it replaced; on a context that serves
// one certificate, that is all of them. Handshakes and the OCSP refresh read stapling state without a lock, so @a ctx must
// not be in use yet.
void ssl_stapling_remove_replaced_certs(SSL_CTX *ctx);

enum class OCSPStatus {
  OCSP_OK,
  OCSP_FETCHSM_NOT_INITIALIZED,
};
OCSPStatus ocsp_update();

int ssl_callback_ocsp_stapling(SSL *, void *);
