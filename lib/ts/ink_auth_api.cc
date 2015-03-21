/** @file

  PE/TE authentication definitions

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

#include <time.h>
#include <stdint.h>
#include "ink_rand.h"
#include "ink_code.h"
#include "ink_auth_api.h"

static int s_rand_seed = time(NULL); // + s_rand_seed;
static InkRand s_rand_gen(ink_rand_r((unsigned int *) & s_rand_seed) ^ (uintptr_t)&s_rand_seed);

inline uint32_t
ink_get_rand_intrn()
{
  return s_rand_gen.random();
}

inline void
ink_make_token_intrn(INK_AUTH_TOKEN *tok, const INK_AUTH_SEED *const *seeds, int slen)
{
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  while (slen-- > 0) {
    ink_code_incr_md5_update(&ctx, (const char *)seeds[slen]->data(), seeds[slen]->length());
  }
  ink_code_incr_md5_final((char *)&(tok->u8[0]), &ctx);
}

uint32_t
ink_get_rand()
{
  return ink_get_rand_intrn();
}

void
ink_make_token(INK_AUTH_TOKEN *tok, const INK_AUTH_TOKEN &mask, const INK_AUTH_SEED *const *seeds, int slen)
{
  ink_make_token_intrn(tok, seeds, slen);
  for (int i = 3; i >= 0; i--) // randomize masked bits
    tok->u32[i] ^= mask.u32[i] & ink_get_rand_intrn();
}

uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED *const *seeds, int slen)
{
  INK_AUTH_TOKEN tok;
  ink_make_token_intrn(&tok, seeds, slen);
  tok.u64[1] ^= tok.u64[0];
  tok.u32[3] ^= tok.u32[2];
  return tok.u32[3] ^ (mask & ink_get_rand_intrn());
}

uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED *const *seeds, int slen)
{
  INK_AUTH_TOKEN tok;
  ink_make_token_intrn(&tok, seeds, slen);
  tok.u64[1] ^= tok.u64[0];
  return tok.u64[1] ^ (mask & ((uint64_t)ink_get_rand_intrn() + (((uint64_t)ink_get_rand_intrn()) << 32)));
}
