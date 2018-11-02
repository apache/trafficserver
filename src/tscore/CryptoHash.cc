/** @file

  Generic wrapper for cryptographic hashes.

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

#include <cstdlib>
#include <cstring>
#include <new>
#include "tscore/ink_assert.h"
#include "tscore/ink_platform.h"
#include "tscore/ink_code.h"
#include "tscore/CryptoHash.h"
#include "tscore/SHA256.h"

#if TS_ENABLE_FIPS == 1
CryptoContext::HashType CryptoContext::Setting = CryptoContext::SHA256;
#else
#include "tscore/INK_MD5.h"
#include "tscore/MMH.h"
CryptoContext::HashType CryptoContext::Setting = CryptoContext::MD5;
#endif

CryptoContext::CryptoContext()
{
  switch (Setting) {
  case UNSPECIFIED:
#if TS_ENABLE_FIPS == 0
  case MD5:
    new (_obj) MD5Context;
    break;
  case MMH:
    new (_obj) MMHContext;
    break;
#else
  case SHA256:
    new (_obj) SHA256Context;
    break;
#endif
  default:
    ink_release_assert("Invalid global URL hash context");
  };
#if TS_ENABLE_FIPS == 0
  static_assert(CryptoContext::OBJ_SIZE >= sizeof(MD5Context), "bad OBJ_SIZE");
  static_assert(CryptoContext::OBJ_SIZE >= sizeof(MMHContext), "bad OBJ_SIZE");
#else
  static_assert(CryptoContext::OBJ_SIZE >= sizeof(SHA256Context), "bad OBJ_SIZE");
#endif
}

/**
  @brief Converts a hash to a null-terminated string

  Externalizes an hash as a null-terminated string into the first argument.
  Does so without intenal procedure calls.
  Side Effects: none.
  Reentrancy:     n/a.
  Thread Safety:  safe.
  Mem Management: stomps the passed dest char*.

  @return returns the passed destination string ptr.
*/
/* reentrant version */
static char *
ink_code_to_hex_str(char *dest, uint8_t const *hash)
{
  int i;
  char *d;

  static char hex_digits[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

  d = dest;
  for (i = 0; i < CRYPTO_HASH_SIZE; i += 4) {
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
  return (dest);
}

char *
CryptoHash::toHexStr(char buffer[(CRYPTO_HASH_SIZE * 2) + 1]) const
{
  return ink_code_to_hex_str(buffer, u8);
}

namespace ats
{
ts::BufferWriter &
bwformat(ts::BufferWriter &w, ts::BWFSpec const &spec, ats::CryptoHash const &hash)
{
  ts::BWFSpec local_spec{spec};
  if ('X' != local_spec._type)
    local_spec._type = 'x';
  return bwformat(w, local_spec, std::string_view(reinterpret_cast<const char *>(hash.u8), CRYPTO_HASH_SIZE));
}
} // namespace ats
