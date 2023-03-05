/** @file

  SHA256 support class.

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

#include "tscore/ink_code.h"
#include "tscore/ink_defs.h"
#include "tscore/CryptoHash.h"
#if HAVE_SHA256_INIT
#include <openssl/sha.h>
#else
#include <openssl/evp.h>
#endif

class SHA256Context : public ats::CryptoContextBase
{
#ifndef HAVE_SHA256_INIT
protected:
  EVP_MD_CTX *ctx;
#endif

public:
  SHA256Context()
  {
#if HAVE_SHA256_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    SHA256_Init(&_sha256ctx);
#pragma GCC diagnostic pop
#else
    _ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(_ctx, EVP_sha256(), nullptr);
#endif
  }
  ~SHA256Context()
  {
#if HAVE_SHA256_INIT
    // _sha256ctx does not need to be freed
#else
    EVP_MD_CTX_free(_ctx);
#endif
  }
  /// Update the hash with @a data of @a length bytes.
  bool
  update(void const *data, int length) override
  {
#if HAVE_SHA256_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return SHA256_Update(&_sha256ctx, data, length);
#pragma GCC diagnostic pop
#else
    return EVP_DigestUpdate(_ctx, data, length);
#endif
  }
  /// Finalize and extract the @a hash.
  bool
  finalize(CryptoHash &hash) override
  {
#if HAVE_SHA256_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return SHA256_Final(hash.u8, &_sha256ctx);
#pragma GCC diagnostic pop
#else
    return EVP_DigestFinal_ex(_ctx, hash.u8, nullptr);
#endif
  }
#if HAVE_SHA256_INIT
private:
  SHA256_CTX _sha256ctx;
#endif
};
