/** @file

  MD5 support class.

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

#include "tscore/ink_defs.h"
#include "tscore/CryptoHash.h"
#if HAVE_MD5_INIT
#include <openssl/md5.h>
#else
#include <openssl/evp.h>
#endif

class MD5Context : public ts::CryptoContext::Hasher
{
public:
  MD5Context()
  {
#if HAVE_MD5_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    MD5_Init(&_md5ctx);
#pragma GCC diagnostic pop
#else
    _ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(_ctx, EVP_md5(), nullptr);
#endif
  }
  ~MD5Context() override
  {
#if HAVE_MD5_INIT
    // _md5ctx does not need to be freed
#else
    EVP_MD_CTX_free(_ctx);
#endif
  }
  /// Update the hash with @a data of @a length bytes.
  bool
  update(void const *data, int length) override
  {
#if HAVE_MD5_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return MD5_Update(&_md5ctx, data, length);
#pragma GCC diagnostic pop
#else
    return EVP_DigestUpdate(_ctx, data, length);
#endif
  }
  /// Finalize and extract the @a hash.
  bool
  finalize(CryptoHash &hash) override
  {
#if HAVE_MD5_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    return MD5_Final(hash.u8, &_md5ctx);
#pragma GCC diagnostic pop
#else
    return EVP_DigestFinal_ex(_ctx, hash.u8, nullptr);
#endif
  }

#if HAVE_MD5_INIT
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
private:
  MD5_CTX _md5ctx;
#pragma GCC diagnostic pop
#endif
};

using INK_MD5 = CryptoHash;
