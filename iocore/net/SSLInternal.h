/** @file

  Function prototypes that break the no internal pact with openssl.

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
#include <openssl/opensslv.h>

#if !TS_USE_SET_RBIO
// Defined in SSLInternal.c, should probably make a separate include
// file for this at some point
void SSL_set0_rbio(SSL *ssl, BIO *rbio);
#endif

#if OPENSSL_VERSION_NUMBER < 0x10100000L
int SSL_CTX_up_ref(SSL_CTX *ctx);
#endif
