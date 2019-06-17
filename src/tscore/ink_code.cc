/** @file

  A brief file description

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

#include <cstring>
#include <cstdio>
#include "tscore/ink_code.h"
#include "tscore/ink_assert.h"
#include "tscore/CryptoHash.h"

ats::CryptoHash const ats::CRYPTO_HASH_ZERO; // default constructed is correct.
#if TS_ENABLE_FIPS == 0
#include "tscore/INK_MD5.h"

MD5Context::MD5Context()
{
  MD5_Init(&_ctx);
}

bool
MD5Context::update(void const *data, int length)
{
  return 0 != MD5_Update(&_ctx, data, length);
}

bool
MD5Context::finalize(CryptoHash &hash)
{
  return 0 != MD5_Final(hash.u8, &_ctx);
}

/**
  @brief Wrapper around MD5_Init
*/
int
ink_code_incr_md5_init(INK_DIGEST_CTX *context)
{
  return MD5_Init(context);
}

/**
  @brief Wrapper around MD5_Update
*/
int
ink_code_incr_md5_update(INK_DIGEST_CTX *context, const char *input, int input_length)
{
  return MD5_Update(context, input, input_length);
}

/**
  @brief Wrapper around MD5_Final
*/
int
ink_code_incr_md5_final(char *sixteen_byte_hash_pointer, INK_DIGEST_CTX *context)
{
  return MD5_Final((unsigned char *)sixteen_byte_hash_pointer, context);
}

/**
  @brief Helper that will init, update, and create a final MD5

  @return always returns 0, maybe some error checking should be done
*/
int
ink_code_md5(unsigned const char *input, int input_length, unsigned char *sixteen_byte_hash_pointer)
{
  MD5_CTX context;

  MD5_Init(&context);
  MD5_Update(&context, input, input_length);
  MD5_Final(sixteen_byte_hash_pointer, &context);

  return (0);
}
#endif
