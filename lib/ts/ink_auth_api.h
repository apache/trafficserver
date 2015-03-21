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

#ifndef __INK_AUTH_API_H_INCLUDDED__
#define __INK_AUTH_API_H_INCLUDDED__

#include <sys/types.h>
#include <string.h>

#include "ink_defs.h"

typedef union {
  uint64_t u64[2];
  uint32_t u32[4];
  uint16_t u16[8];
  uint8_t u8[16];
} INK_AUTH_TOKEN;

class INK_AUTH_SEED
{
public:
  const void *
  data() const
  {
    return m_data;
  }
  unsigned
  length() const
  {
    return m_length;
  }

  inline INK_AUTH_SEED(const void *x, unsigned ln) { init(x, ln); }

  inline INK_AUTH_SEED(const uint8_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const uint16_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const uint32_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const uint64_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const int8_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const int16_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const int32_t &x) { init((const void *)&x, sizeof(x)); }
  inline INK_AUTH_SEED(const int64_t &x) { init((const void *)&x, sizeof(x)); }

  inline INK_AUTH_SEED(const INK_AUTH_TOKEN &x) { init((const void *)&(x.u8[0]), sizeof(x.u8)); }

  inline INK_AUTH_SEED(const char *str) { init((const void *)str, strlen((const char *)str)); }

  inline INK_AUTH_SEED(const char *str, unsigned ln) { init((const void *)str, ln); }

  inline INK_AUTH_SEED(const unsigned char *str) { init((const void *)str, strlen((const char *)str)); }

  inline INK_AUTH_SEED(const unsigned char *str, unsigned ln) { init((const void *)str, ln); }

protected:
  void
  init(const void *d, unsigned l)
  {
    m_data = d;
    m_length = l;
  }

  const void *m_data;
  unsigned m_length;
};


void ink_make_token(INK_AUTH_TOKEN *tok, const INK_AUTH_TOKEN &mask, const INK_AUTH_SEED *const *seeds, int slen);

uint32_t ink_make_token32(uint32_t mask, const INK_AUTH_SEED *const *seeds, int slen);
uint64_t ink_make_token64(uint64_t mask, const INK_AUTH_SEED *const *seeds, int slen);

uint32_t ink_get_rand();

#define INK_TOKENS_EQUAL(m, t1, t2) ((((t1) ^ (t2)) & (~(m))) == 0)

//
// Helper functions - wiil create INK_AUTH_SEEDs from base types on fly
//
inline uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED &s1)
{
  const INK_AUTH_SEED *s[] = {&s1};
  return ink_make_token32(mask, s, sizeof(s) / sizeof(*s));
}

inline uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2};
  return ink_make_token32(mask, s, sizeof(s) / sizeof(*s));
}

inline uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3};
  return ink_make_token32(mask, s, sizeof(s) / sizeof(*s));
}

inline uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3, const INK_AUTH_SEED &s4)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3, &s4};
  return ink_make_token32(mask, s, sizeof(s) / sizeof(*s));
}

inline uint32_t
ink_make_token32(uint32_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3, const INK_AUTH_SEED &s4,
                 const INK_AUTH_SEED &s5)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3, &s4, &s5};
  return ink_make_token32(mask, s, sizeof(s) / sizeof(*s));
}

inline uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED &s1)
{
  const INK_AUTH_SEED *s[] = {&s1};
  return ink_make_token64(mask, s, sizeof(s) / sizeof(*s));
}

inline uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2};
  return ink_make_token64(mask, s, sizeof(s) / sizeof(*s));
}

inline uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3};
  return ink_make_token64(mask, s, sizeof(s) / sizeof(*s));
}

inline uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3, const INK_AUTH_SEED &s4)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3, &s4};
  return ink_make_token64(mask, s, sizeof(s) / sizeof(*s));
}

inline uint64_t
ink_make_token64(uint64_t mask, const INK_AUTH_SEED &s1, const INK_AUTH_SEED &s2, const INK_AUTH_SEED &s3, const INK_AUTH_SEED &s4,
                 const INK_AUTH_SEED &s5)
{
  const INK_AUTH_SEED *s[] = {&s1, &s2, &s3, &s4, &s5};
  return ink_make_token64(mask, s, sizeof(s) / sizeof(*s));
}

inline int64_t
INK_AUTH_MAKE_INT_64(uint32_t h, uint32_t l)
{
  return int64_t((((uint64_t)h) << 32) + (uint32_t)l);
}

inline int64_t
INK_AUTH_MAKE_INT_64(uint32_t u)
{
  return INK_AUTH_MAKE_INT_64(u, u);
}

#endif // #ifndef __INK_AUTH_API_H_INCLUDDED__
