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

#include <string.h>
#include <stdio.h>
#include "ts/ink_code.h"
#include "ts/INK_MD5.h"
#include "ts/ink_assert.h"
#include "ts/INK_MD5.h"

ats::CryptoHash const ats::CRYPTO_HASH_ZERO; // default constructed is correct.

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
ink_code_md5(unsigned char const *input, int input_length, unsigned char *sixteen_byte_hash_pointer)
{
  MD5_CTX context;

  MD5_Init(&context);
  MD5_Update(&context, input, input_length);
  MD5_Final(sixteen_byte_hash_pointer, &context);

  return (0);
}

/**
  @brief Converts a MD5 to a null-terminated string

  Externalizes an INK_MD5 as a null-terminated string into the first argument.
  Side Effects: none
  Reentrancy:     n/a.
  Thread Safety:  safe.
  Mem Management: stomps the passed dest char*.

  @return returns the passed destination string ptr.
*/
/* reentrant version */
char *
ink_code_md5_stringify(char *dest33, const size_t destSize, const char *md5)
{
  ink_assert(destSize >= 33);

  int i;
  for (i = 0; i < 16; i++) {
    // we check the size of the destination buffer above
    // coverity[secure_coding]
    sprintf(&(dest33[i * 2]), "%02X", md5[i]);
  }
  ink_assert(dest33[32] == '\0');
  return (dest33);
} /* End ink_code_stringify_md5(const char *md5) */

/**
  @brief Converts a MD5 to a null-terminated string

  Externalizes an INK_MD5 as a null-terminated string into the first argument.
  Does so without intenal procedure calls.
  Side Effects: none.
  Reentrancy:     n/a.
  Thread Safety:  safe.
  Mem Management: stomps the passed dest char*.

  @return returns the passed destination string ptr.
*/
/* reentrant version */
char *
ink_code_to_hex_str(char *dest33, uint8_t const *hash)
{
  int i;
  char *d;

  static char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  d = dest33;
  for (i = 0; i < 16; i += 4) {
    *(d + 0) = hex_digits[hash[i + 0] >> 4];
    *(d + 1) = hex_digits[hash[i + 0] & 15];
    *(d + 2) = hex_digits[hash[i + 1] >> 4];
    *(d + 3) = hex_digits[hash[i + 1] & 15];
    *(d + 4) = hex_digits[hash[i + 2] >> 4];
    *(d + 5) = hex_digits[hash[i + 2] & 15];
    *(d + 6) = hex_digits[hash[i + 3] >> 4];
    *(d + 7) = hex_digits[hash[i + 3] & 15];
    d += 8;
  }
  *d = '\0';
  return (dest33);
}
