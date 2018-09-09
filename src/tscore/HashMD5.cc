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

ATSHashMD5::ATSHashMD5() : ctx(EVP_MD_CTX_new())
{
  int ret = EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
  ink_assert(ret == 1);
}

ATSHashMD5 &
ATSHashMD5::update(const void *data, size_t len)
{
  if (!finalized) {
    int ret = EVP_DigestUpdate(ctx, data, len);
    ink_assert(ret == 1);
  }
  return *this;
}

ATSHashMD5 &
ATSHashMD5::final()
{
  if (!finalized) {
    int ret = EVP_DigestFinal_ex(ctx, md_value, &md_len);
    ink_assert(ret == 1);
    finalized = true;
  }
  return *this;
}

const void *
ATSHashMD5::get() const
{
  return finalized ? md_value : nullptr;
}

size_t
ATSHashMD5::size() const
{
  return EVP_MD_CTX_size(ctx);
}

ATSHashMD5 &
ATSHashMD5::clear()
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

ATSHashMD5::~ATSHashMD5()
{
  EVP_MD_CTX_free(ctx);
}
