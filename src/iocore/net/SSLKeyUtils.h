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

#pragma once

#if OPENSSL_IS_OPENSSL3
#include <openssl/evp.h>
#else
#include <openssl/dh.h>
#endif
#include <openssl/ssl.h>

#ifdef OPENSSL_IS_OPENSSL3
using dh_key_t = EVP_PKEY;
#else
using dh_key_t = DH;
#endif

// Both gen_dh_2048_256_pkey and load_dhparams_file return owning pointers.
dh_key_t *gen_dh_2048_256_pkey();
dh_key_t *load_dhparams_file(char const *dhparams_file);

// Takes ownership of pkey.
bool set_ctx_dh(SSL_CTX *ctx, dh_key_t *pkey);
