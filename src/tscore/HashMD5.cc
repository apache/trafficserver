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

#include "tscore/ink_assert.h"
#include "tscore/ink_config.h"
#include "tscore/HashMD5.h"

namespace ts
{
HashMD5::HashMD5() : ctx(EVP_MD_CTX_new())
{
  int ret = EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
  ink_assert(ret == 1);
}

HashMD5 &
HashMD5::update(std::string_view const &data)
{
  if (!finalized) {
    int ret = EVP_DigestUpdate(ctx, data.data(), data.size());
    ink_assert(ret == 1);
  }
  return *this;
}

HashMD5 &
HashMD5::final()
{
  if (!finalized) {
    int ret = EVP_DigestFinal_ex(ctx, md_value, &md_len);
    ink_assert(ret == 1);
    finalized = true;
  }
  return *this;
}

bool
HashMD5::get(MemSpan dst) const
{
  bool zret = true;
  if (finalized && dst.size() > this->size()) {
    memcpy(dst.data(), md_value, this->size());
  } else {
    zret = false;
  }
  return zret;
}

size_t
HashMD5::size() const
{
  return EVP_MD_CTX_size(ctx);
}

HashMD5 &
HashMD5::clear()
{
  int ret = 1;
#ifndef OPENSSL_IS_BORINGSSL
  ret = EVP_MD_CTX_reset(ctx);
  ink_assert(ret == 1);
#else
  // OpenSSL's EVP_MD_CTX_reset always returns 1
  EVP_MD_CTX_reset(ctx);
#endif
  ret = EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
  ink_assert(ret == 1);
  md_len    = 0;
  finalized = false;
  return *this;
}

HashMD5::~HashMD5()
{
  EVP_MD_CTX_free(ctx);
}

} // namespace ts
