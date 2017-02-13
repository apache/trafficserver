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

#include "ts/HashMD5.h"
#include "ts/ink_assert.h"

ATSHashMD5::ATSHashMD5(void)
{
  ctx = EVP_MD_CTX_create();
  EVP_DigestInit(ctx, EVP_md5());
  md_len    = 0;
  finalized = false;
}

void
ATSHashMD5::update(const void *data, size_t len)
{
  if (!finalized) {
    EVP_DigestUpdate(ctx, data, len);
  }
}

void
ATSHashMD5::final(void)
{
  if (!finalized) {
    EVP_DigestFinal_ex(ctx, md_value, &md_len);
    finalized = true;
  }
}

const void *
ATSHashMD5::get(void) const
{
  if (finalized) {
    return (void *)md_value;
  } else {
    return NULL;
  }
}

size_t
ATSHashMD5::size(void) const
{
  return EVP_MD_CTX_size(ctx);
}

void
ATSHashMD5::clear(void)
{
#if OPENSSL_VERSION_NUMBER < 0x10100000L
#define EVP_MD_CTX_reset(ctx) EVP_MD_CTX_cleanup((ctx))
#endif
  int ret = EVP_MD_CTX_reset(ctx);
  ink_assert(ret == 1);
  ret = EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
  ink_assert(ret == 1);
  md_len    = 0;
  finalized = false;
}

ATSHashMD5::~ATSHashMD5()
{
  EVP_MD_CTX_destroy(ctx);
}
